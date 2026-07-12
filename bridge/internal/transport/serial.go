// serial.go —— 串口下行通道。
//
// ESP32-S3 走 USB-CDC 虚拟串口，插上即用、无需波特率配置，故直接 os.OpenFile
// 读写，不引第三方串口库（零依赖、跨平台）。真实 UART 桥接场景预留 baud 字段。
//
// 端口解析优先级：显式 port > auto-scan：
//   Linux   /dev/ttyACM*、/dev/ttyUSB*
//   Windows COM3..COM20
//   macOS   /dev/cu.usbmodem*、/dev/cu.usbserial*
//
// 后台 connectLoop 持续保活：断开后自动重扫重连，Write 在未连接时返回错误
// （调用方丢弃该帧，绝不阻塞 ingest）。
package transport

import (
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"sync"
	"sync/atomic"
	"time"
)

// SerialWriter 是串口下行 Writer。
type SerialWriter struct {
	port string // 显式端口路径或 "auto"

	mu        sync.Mutex
	writeMu   sync.Mutex  // 串行化所有 Write 调用（防并发 WriteFile 粘连帧）
	f         *os.File
	onLine    func(string) // 每收到一行回调：Command 或设备日志
	onTx      func([]byte) // 每成功写出一帧回调（用于串口日志）
	sent      int64        // 累计成功写入帧数（atomic）
	recv      int64        // 累计收到的上行行数（atomic）
	suspended bool         // Disconnect 置位，阻止重连
	stop      chan struct{}
	done      chan struct{}
}

// NewSerial 启动串口 Writer + RX 读取循环。port 为空或 "auto" 触发自动扫描。
// onLine 为每收到一行回调（设备日志 / 上行 Command）；可为 nil（仅写）。
// onTx 为每成功写出一帧回调（用于串口日志记录）；可为 nil。
func NewSerial(port string, onLine func(string), onTx func([]byte)) *SerialWriter {
	if port == "" {
		port = "auto"
	}
	s := &SerialWriter{port: port, onLine: onLine, onTx: onTx, stop: make(chan struct{}), done: make(chan struct{})}
	go s.connectLoop()
	return s
}

// Disconnect 断开当前连接并阻止重连（供 web UI 释放 COM 口刷固件用）。
func (s *SerialWriter) Disconnect() {
	s.mu.Lock()
	s.suspended = true
	if s.f != nil {
		s.f.Close()
		s.f = nil
	}
	s.mu.Unlock()
}

// Connect 清除暂停标志，允许重连。
func (s *SerialWriter) Connect() {
	s.mu.Lock()
	s.suspended = false
	s.mu.Unlock()
}

// Suspended 报告是否已暂停（用户主动断开）。
func (s *SerialWriter) Suspended() bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.suspended
}

// Write 写一条已成型帧行（含 \n）。未连接返回错误。
// 由 s.writeMu 串行化：Windows 非 overlapped COM 句柄，多 goroutine 并发
// WriteFile 不保证按调用边界原子化，会产出两帧粘连的字节流（缺中间的 \n），
// 导致设备 cJSON 解析失败。串行化后每帧原子写入，杜绝粘连。
func (s *SerialWriter) Write(frame []byte) error {
	s.writeMu.Lock()
	defer s.writeMu.Unlock()

	s.mu.Lock()
	f := s.f
	s.mu.Unlock()
	if f == nil {
		return fmt.Errorf("serial: not connected")
	}
	if _, err := f.Write(frame); err != nil {
		s.mu.Lock()
		if s.f == f {
			s.f.Close()
			s.f = nil
		}
		s.mu.Unlock()
		return err
	}
	atomic.AddInt64(&s.sent, 1)
	if s.onTx != nil {
		s.onTx(frame)
	}
	return nil
}

// Status 返回串口链路诊断信息。
func (s *SerialWriter) Status() (port string, connected bool, sent int64, recv int64, suspended bool) {
	s.mu.Lock()
	connected = s.f != nil
	suspended = s.suspended
	s.mu.Unlock()
	return s.port, connected, atomic.LoadInt64(&s.sent), atomic.LoadInt64(&s.recv), suspended
}

// Close 停止重连循环并关闭端口。
func (s *SerialWriter) Close() error {
	select {
	case <-s.stop:
	default:
		close(s.stop)
	}
	<-s.done
	s.mu.Lock()
	if s.f != nil {
		s.f.Close()
		s.f = nil
	}
	s.mu.Unlock()
	return nil
}

// connectLoop 维护连接：未连上就尝试 openAny，已连上就空转等待断开/停止。
func (s *SerialWriter) connectLoop() {
	defer close(s.done)
	for {
		select {
		case <-s.stop:
			return
		default:
		}

		s.mu.Lock()
		connected := s.f != nil
		suspended := s.suspended
		s.mu.Unlock()

		// 用户主动断开（web UI）→ 不做重连
		if suspended {
			select {
			case <-s.stop:
				return
			case <-time.After(1 * time.Second):
			}
			continue
		}

		if !connected {
			if f, p, err := s.openAny(); err == nil {
				s.mu.Lock()
				s.f = f
				s.mu.Unlock()
				log.Printf("transport: serial connected %s", p)
				// 设 COMMTIMEOUTS 让 Read 非阻塞（Windows），否则阻塞 Read 会
				// 与 Write 在驱动层互锁。失败则降级只写（不启 readLoop）。
				if rbErr := setNonBlockingReads(f); rbErr != nil {
					log.Printf("transport: setNonBlockingReads failed: %v (RX disabled)", rbErr)
				} else {
					// 排空 RX：解析上行 Command + 转发设备日志，并防设备
					// ESP_LOG 反压冻 LVGL。
					go s.readLoop(f)
				}
			} else {
				select {
				case <-s.stop:
					return
				case <-time.After(2 * time.Second):
				}
				continue
			}
		}

		select {
		case <-s.stop:
			return
		case <-time.After(1 * time.Second):
		}
	}
}

// readLoop 阻塞读指定连接，按 \n 切行回调 onLine。
// Windows 非阻塞 COMMTIMEOUTS 下 0 字节 → Go 返回 io.EOF（非真断连），
// 此处只短睡不回连；真断连由 Write 路径监测（写失败→nil→重连）。
func (s *SerialWriter) readLoop(f *os.File) {
	buf := make([]byte, 64)
	var line []byte
	for {
		n, err := f.Read(buf)
		if n > 0 && s.onLine != nil {
			for i := 0; i < n; i++ {
				c := buf[i]
				if c == '\n' {
					if len(line) > 0 {
						s.onLine(string(line))
						atomic.AddInt64(&s.recv, 1)
					}
					line = line[:0]
				} else if c != '\r' {
					if len(line) < 1024 {
						line = append(line, c)
					}
				}
			}
		}
		if err != nil {
			// Windows 非阻塞串口：0 字节 → Go 返回 io.EOF ≠ 真断连。
			if errors.Is(err, io.EOF) {
				select {
				case <-s.stop:
					return
				case <-time.After(10 * time.Millisecond):
				}
				continue
			}
			// 真错误（端口消失 / 关闭）→ 触发重连
			s.mu.Lock()
			if s.f == f {
				s.f.Close()
				s.f = nil
			}
			s.mu.Unlock()
			return
		}
		if n == 0 {
			select {
			case <-s.stop:
				return
			case <-time.After(10 * time.Millisecond):
			}
		}
	}
}

// openAny 按优先级尝试候选端口，返回第一个能打开的。
func (s *SerialWriter) openAny() (*os.File, string, error) {
	for _, p := range s.candidates() {
		if f, err := os.OpenFile(p, os.O_RDWR, 0666); err == nil {
			return f, p, nil
		}
	}
	return nil, "", fmt.Errorf("serial: no port available")
}

// candidates 产出本次应尝试的端口列表。
func (s *SerialWriter) candidates() []string {
	if s.port != "auto" {
		return []string{s.port}
	}
	var out []string
	switch runtime.GOOS {
	case "windows":
		for i := 3; i <= 20; i++ {
			out = append(out, fmt.Sprintf("COM%d", i))
		}
	case "darwin":
		for _, pat := range []string{"/dev/cu.usbmodem*", "/dev/cu.usbserial*", "/dev/tty.usbmodem*"} {
			if ms, _ := filepath.Glob(pat); ms != nil {
				out = append(out, ms...)
			}
		}
	default: // linux 等
		for _, pat := range []string{"/dev/ttyACM*", "/dev/ttyUSB*", "/dev/tty.SLAB_USBtoUART*"} {
			if ms, _ := filepath.Glob(pat); ms != nil {
				out = append(out, ms...)
			}
		}
	}
	return out
}
