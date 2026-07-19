// ble.go —— BLE 下行通道（真实实现，tinygo-org/bluetooth）。
//
// 设备端是 ESP32-S3 NimBLE GATT server（Nordic UART Service + 配网 Service），
// 本端作中心：扫描设备名前缀 → 连接 → 发现特征 → 分片写 NUS RX。
//
// 与串口 Writer 语义一致：
//   - 后台 connectLoop 持续保活：断开后自动重扫重连；suspended 时不重连
//   - Write 未连接返回错误（调用方丢帧，绝不阻塞 ingest）
//   - 上行经 NUS TX notify 按 \n 重组行，回调 onLine（Command / 日志）
//
// 分片：帧可能超过 ATT 负载，按 (MTU-3) 切片（ble_chunk.go）。MTU 连接后
// 经 GetMTU 获取，取不到按 185 起步，写失败降级 23 重试。
//
// 平台限制：WSL2 无蓝牙适配器，需跑原生 Windows/macOS/Linux。
package transport

import (
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"tinygo.org/x/bluetooth"
)

// BLE GATT UUID（与固件 bt_stack.c 保持一致）。
var (
	// Nordic UART Service
	uuidNUSService = bluetooth.NewUUID([16]byte{0x6E, 0x40, 0x00, 0x01, 0xB5, 0xA3, 0xF3, 0x93, 0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E})
	uuidNUSRX      = bluetooth.NewUUID([16]byte{0x6E, 0x40, 0x00, 0x02, 0xB5, 0xA3, 0xF3, 0x93, 0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E}) // bridge→设备
	uuidNUSTX      = bluetooth.NewUUID([16]byte{0x6E, 0x40, 0x00, 0x03, 0xB5, 0xA3, 0xF3, 0x93, 0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E}) // 设备→bridge
	// WiFi 配网 Service（ClawdPet 自定义 base）
	uuidProvService = bluetooth.NewUUID([16]byte{0x4A, 0x1A, 0x00, 0x00, 0xB5, 0xA3, 0xF3, 0x93, 0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E})
	uuidProvSSID    = bluetooth.NewUUID([16]byte{0x4A, 0x1A, 0x00, 0x01, 0xB5, 0xA3, 0xF3, 0x93, 0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E})
	uuidProvPass    = bluetooth.NewUUID([16]byte{0x4A, 0x1A, 0x00, 0x02, 0xB5, 0xA3, 0xF3, 0x93, 0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E})
	uuidProvCommit  = bluetooth.NewUUID([16]byte{0x4A, 0x1A, 0x00, 0x03, 0xB5, 0xA3, 0xF3, 0x93, 0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E})
	uuidProvStatus  = bluetooth.NewUUID([16]byte{0x4A, 0x1A, 0x00, 0x04, 0xB5, 0xA3, 0xF3, 0x93, 0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E})
)

// WiFiProvStatus 是设备配网 Status 特征上报的 JSON。
type WiFiProvStatus struct {
	S    string `json:"s"`              // idle / connecting / ok / fail
	IP   string `json:"ip,omitempty"`   // ok 时的 IP
	RSSI int    `json:"rssi,omitempty"` // ok 时的 WiFi 信号
	Err  int    `json:"err,omitempty"`  // fail 时的 esp_wifi 原因码
	TS   int64  `json:"-"`              // 本地收到时间（毫秒）
}

// BLEDeviceInfo 是扫描到的一台候选设备。
type BLEDeviceInfo struct {
	Name    string `json:"name"`
	Address string `json:"address"`
	RSSI    int16  `json:"rssi"`
}

// BLEStatus 是 /api/ble/status 的返回结构。
type BLEStatus struct {
	AdapterOK bool            `json:"adapter_ok"`
	Connected bool            `json:"connected"`
	Device    string          `json:"device,omitempty"`
	Address   string          `json:"address,omitempty"`
	RSSI      int16           `json:"rssi,omitempty"`
	MTU       int             `json:"mtu,omitempty"`
	Suspended bool            `json:"suspended"`
	LastError string          `json:"last_error,omitempty"`
	TxFrames  int64           `json:"tx_frames"`
	RxLines   int64           `json:"rx_lines"`
	WiFi      *WiFiProvStatus `json:"wifi,omitempty"`
}

// BLEWriter 是蓝牙下行 Writer。
type BLEWriter struct {
	namePrefix string
	adapter    *bluetooth.Adapter

	writeMu sync.Mutex // 串行化 Write（同 serial 防粘连）

	mu          sync.Mutex
	dev         *bluetooth.Device
	rxChar      *bluetooth.DeviceCharacteristic // NUS RX（下行写）
	ssidChar    *bluetooth.DeviceCharacteristic
	passChar    *bluetooth.DeviceCharacteristic
	commitChar  *bluetooth.DeviceCharacteristic
	mtu         int    // 协商后 ATT MTU，默认 185
	targetAddr  string // UI 手动指定地址（空 = 按名字前缀自动连）
	deviceName  string
	deviceAddr  string
	rssi        int16
	suspended   bool
	adapterOK   bool
	lastError   string
	wifi        *WiFiProvStatus
	found       map[string]BLEDeviceInfo // 扫描缓存（地址 → 信息）

	scanMu sync.Mutex // connectLoop 与 ScanOnce 互斥扫描

	onLine func(string)
	onTx   func([]byte)
	reasm  *LineReassembler

	sent int64
	recv int64

	stop chan struct{}
	done chan struct{}
}

// NewBLE 启动 BLE Writer + 保活循环。namePrefix 为设备广播名前缀（如 "ClawdPet-"）。
// onLine 为上行行回调（可为 nil）；onTx 为每成功写一帧回调（可为 nil）。
func NewBLE(namePrefix string, onLine func(string), onTx func([]byte)) *BLEWriter {
	if namePrefix == "" {
		namePrefix = "ClawdPet-"
	}
	b := &BLEWriter{
		namePrefix: namePrefix,
		adapter:    bluetooth.DefaultAdapter,
		mtu:        185,
		found:      map[string]BLEDeviceInfo{},
		onLine:     onLine,
		onTx:       onTx,
		reasm:      NewLineReassembler(1024),
		stop:       make(chan struct{}),
		done:       make(chan struct{}),
	}
	go b.connectLoop()
	return b
}

// ---- transport.Writer 接口 ----

// Write 写一条已成型帧行（含 \n），按 (MTU-3) 分片。未连接返回错误。
func (b *BLEWriter) Write(frame []byte) error {
	b.writeMu.Lock()
	defer b.writeMu.Unlock()

	b.mu.Lock()
	ch := b.rxChar
	mtu := b.mtu
	b.mu.Unlock()
	if ch == nil {
		return fmt.Errorf("ble: not connected")
	}
	if err := b.writeChunked(ch, frame, mtu); err != nil {
		// MTU 协商未生效（部分平台 GetMTU 返回乐观值）：降级 23 重试一次
		if mtu > 23 {
			b.mu.Lock()
			b.mtu = 23
			b.mu.Unlock()
			log.Printf("transport: BLE write failed at mtu=%d, retry with 23: %v", mtu, err)
			if err2 := b.writeChunked(ch, frame, 23); err2 == nil {
				atomic.AddInt64(&b.sent, 1)
				if b.onTx != nil {
					b.onTx(frame)
				}
				return nil
			}
		}
		b.markDisconnected()
		return err
	}
	atomic.AddInt64(&b.sent, 1)
	if b.onTx != nil {
		b.onTx(frame)
	}
	return nil
}

// writeChunked 按 (mtu-3) 分片写，任一片失败即返回。
func (b *BLEWriter) writeChunked(ch *bluetooth.DeviceCharacteristic, frame []byte, mtu int) error {
	chunkSize := mtu - 3
	for _, c := range ChunkFrame(frame, chunkSize) {
		if _, err := ch.WriteWithoutResponse(c); err != nil {
			return err
		}
	}
	return nil
}

// Close 停止保活循环并断开连接。
func (b *BLEWriter) Close() error {
	select {
	case <-b.stop:
	default:
		close(b.stop)
	}
	<-b.done
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.dev != nil {
		_ = b.dev.Disconnect()
		b.dev = nil
	}
	return nil
}

// ---- 控制与状态（供 HTTP API）----

// Connect 允许重连；addr 非空时固定连该地址（UI 手动选设备）。
func (b *BLEWriter) Connect(addr string) {
	b.mu.Lock()
	b.targetAddr = addr
	b.suspended = false
	b.mu.Unlock()
}

// Disconnect 断开并阻止自动重连。
func (b *BLEWriter) Disconnect() {
	b.mu.Lock()
	b.suspended = true
	dev := b.dev
	b.mu.Unlock()
	if dev != nil {
		_ = dev.Disconnect()
	}
}

// Status 返回 BLE 链路诊断信息。
func (b *BLEWriter) Status() BLEStatus {
	b.mu.Lock()
	defer b.mu.Unlock()
	return BLEStatus{
		AdapterOK: b.adapterOK,
		Connected: b.rxChar != nil,
		Device:    b.deviceName,
		Address:   b.deviceAddr,
		RSSI:      b.rssi,
		MTU:       b.mtu,
		Suspended: b.suspended,
		LastError: b.lastError,
		TxFrames:  atomic.LoadInt64(&b.sent),
		RxLines:   atomic.LoadInt64(&b.recv),
		WiFi:      b.wifi,
	}
}

// Provision 经配网特征下发 WiFi 凭据：先 SSID、再密码、最后 commit 触发连接。
// 结果经 Status 特征 notify 回来，体现在 Status().WiFi。
func (b *BLEWriter) Provision(ssid, password string) error {
	b.mu.Lock()
	ssidCh, passCh, commitCh := b.ssidChar, b.passChar, b.commitChar
	b.mu.Unlock()
	if ssidCh == nil || passCh == nil || commitCh == nil {
		return fmt.Errorf("ble: provisioning service unavailable (not connected)")
	}
	if len(ssid) == 0 || len(ssid) > 32 {
		return fmt.Errorf("ssid length must be 1..32 bytes")
	}
	if len(password) > 64 {
		return fmt.Errorf("password too long (max 64 bytes)")
	}
	if _, err := ssidCh.Write([]byte(ssid)); err != nil {
		return fmt.Errorf("write ssid: %w", err)
	}
	if _, err := passCh.Write([]byte(password)); err != nil {
		return fmt.Errorf("write password: %w", err)
	}
	if _, err := commitCh.Write([]byte{1}); err != nil {
		return fmt.Errorf("write commit: %w", err)
	}
	log.Printf("transport: BLE wifi provision sent (ssid=%q)", ssid)
	return nil
}

// ScanOnce 做一次 4s 主动扫描，返回扫描到的候选设备（含历史缓存）。
// 与 connectLoop 的扫描互斥；扫描期间自动重连暂停一轮。
func (b *BLEWriter) ScanOnce() ([]BLEDeviceInfo, error) {
	b.mu.Lock()
	if !b.adapterOK {
		b.mu.Unlock()
		return nil, errors.New("ble: adapter not available")
	}
	b.mu.Unlock()

	b.scanMu.Lock()
	defer b.scanMu.Unlock()
	if err := b.scan(4 * time.Second, nil); err != nil {
		return nil, err
	}
	b.mu.Lock()
	defer b.mu.Unlock()
	out := make([]BLEDeviceInfo, 0, len(b.found))
	for _, d := range b.found {
		out = append(out, d)
	}
	return out, nil
}

// ---- 内部：保活循环 ----

func (b *BLEWriter) connectLoop() {
	defer close(b.done)
	for {
		select {
		case <-b.stop:
			return
		default:
		}

		b.mu.Lock()
		adapterOK := b.adapterOK
		suspended := b.suspended
		connected := b.rxChar != nil
		b.mu.Unlock()

		if !adapterOK {
			if err := b.adapter.Enable(); err != nil {
				b.setError("adapter enable: " + err.Error())
				b.sleepOrStop(10 * time.Second)
				continue
			}
			b.mu.Lock()
			b.adapterOK = true
			b.lastError = ""
			b.mu.Unlock()
			log.Printf("transport: BLE adapter enabled")
		}

		if suspended {
			b.sleepOrStop(1 * time.Second)
			continue
		}

		if !connected {
			b.tryConnectOnce()
			continue
		}

		b.sleepOrStop(1 * time.Second)
	}
}

// tryConnectOnce 扫描一轮 → 连接 → 发现服务 → 订阅 notify。失败记错误返回，
// 由 connectLoop 决定节奏重试。
func (b *BLEWriter) tryConnectOnce() {
	b.scanMu.Lock()
	target, addr, err := b.scanForTarget(8 * time.Second)
	b.scanMu.Unlock()
	if err != nil {
		b.setError(err.Error())
		b.sleepOrStop(3 * time.Second)
		return
	}

	dev, err := b.adapter.Connect(addr, bluetooth.ConnectionParams{})
	if err != nil {
		b.setError("connect: " + err.Error())
		b.sleepOrStop(3 * time.Second)
		return
	}
	log.Printf("transport: BLE connected %s (%s rssi=%d)", target.Name, target.Address, target.RSSI)

	if err := b.setupCharacteristics(&dev, target); err != nil {
		b.setError("discover: " + err.Error())
		_ = dev.Disconnect()
		b.sleepOrStop(3 * time.Second)
		return
	}
}

// scanForTarget 扫描 timeout，返回目标设备及其地址：优先手动指定地址，
// 其次名字前缀匹配中信号最强者。扫描期间持续刷新 found 缓存。
func (b *BLEWriter) scanForTarget(timeout time.Duration) (BLEDeviceInfo, bluetooth.Address, error) {
	b.mu.Lock()
	wantAddr := b.targetAddr
	prefix := b.namePrefix
	b.mu.Unlock()

	var best *BLEDeviceInfo
	var bestAddr bluetooth.Address
	err := b.scan(timeout, func(d BLEDeviceInfo, addr bluetooth.Address) bool {
		if wantAddr != "" {
			if strings.EqualFold(d.Address, wantAddr) {
				best = &d
				bestAddr = addr
				return true // 命中即停
			}
			return false
		}
		if strings.HasPrefix(d.Name, prefix) {
			if best == nil || d.RSSI > best.RSSI {
				best = &d
				bestAddr = addr
			}
		}
		return false
	})
	if err != nil {
		return BLEDeviceInfo{}, bestAddr, err
	}
	if best == nil {
		if wantAddr != "" {
			return BLEDeviceInfo{}, bestAddr, fmt.Errorf("device %s not found", wantAddr)
		}
		return BLEDeviceInfo{}, bestAddr, fmt.Errorf("no %s* device found", prefix)
	}
	return *best, bestAddr, nil
}

// scan 执行一次限时主动扫描。match 返回 true 时提前结束。
// StopScan 后 Scan 在各平台均返回 nil，故错误即真实错误。
func (b *BLEWriter) scan(timeout time.Duration, match func(BLEDeviceInfo, bluetooth.Address) bool) error {
	stopTimer := time.AfterFunc(timeout, func() {
		_ = b.adapter.StopScan()
	})
	defer stopTimer.Stop()

	err := b.adapter.Scan(func(_ *bluetooth.Adapter, r bluetooth.ScanResult) {
		name := r.LocalName()
		if name == "" {
			return
		}
		d := BLEDeviceInfo{
			Name:    name,
			Address: r.Address.String(),
			RSSI:    r.RSSI,
		}
		b.mu.Lock()
		b.found[d.Address] = d
		b.mu.Unlock()
		if match != nil && match(d, r.Address) {
			_ = b.adapter.StopScan()
		}
	})
	if err != nil {
		return fmt.Errorf("scan: %w", err)
	}
	return nil
}

// setupCharacteristics 发现 NUS + 配网 Service 的特征并订阅 notify。
func (b *BLEWriter) setupCharacteristics(dev *bluetooth.Device, target BLEDeviceInfo) error {
	svcs, err := dev.DiscoverServices([]bluetooth.UUID{uuidNUSService, uuidProvService})
	if err != nil {
		return err
	}
	var nusSvc, provSvc *bluetooth.DeviceService
	for i := range svcs {
		switch svcs[i].UUID() {
		case uuidNUSService:
			nusSvc = &svcs[i]
		case uuidProvService:
			provSvc = &svcs[i]
		}
	}
	if nusSvc == nil {
		return fmt.Errorf("NUS service not found")
	}

	chars, err := nusSvc.DiscoverCharacteristics([]bluetooth.UUID{uuidNUSRX, uuidNUSTX})
	if err != nil {
		return err
	}
	var rxCh, txCh *bluetooth.DeviceCharacteristic
	for i := range chars {
		switch chars[i].UUID() {
		case uuidNUSRX:
			rxCh = &chars[i]
		case uuidNUSTX:
			txCh = &chars[i]
		}
	}
	if rxCh == nil {
		return fmt.Errorf("NUS RX characteristic not found")
	}

	// 上行：TX notify → 行重组 → onLine（Command / 日志）
	if txCh != nil {
		if err := txCh.EnableNotifications(func(buf []byte) {
			for _, line := range b.reasm.Feed(buf) {
				atomic.AddInt64(&b.recv, 1)
				if b.onLine != nil {
					b.onLine(line)
				}
			}
		}); err != nil {
			log.Printf("transport: BLE TX notify subscribe failed: %v (uplink disabled)", err)
		}
	}

	// MTU：拿不到就保持默认 185（写失败时 Write 会自动降级 23）
	mtu := b.mtu
	if m, err := rxCh.GetMTU(); err == nil && m >= 23 {
		mtu = int(m)
	}

	// 配网 Service（可选——老固件没有也能跑数据通道）
	var ssidCh, passCh, commitCh *bluetooth.DeviceCharacteristic
	if provSvc != nil {
		pchars, err := provSvc.DiscoverCharacteristics([]bluetooth.UUID{
			uuidProvSSID, uuidProvPass, uuidProvCommit, uuidProvStatus,
		})
		if err == nil {
			for i := range pchars {
				switch pchars[i].UUID() {
				case uuidProvSSID:
					ssidCh = &pchars[i]
				case uuidProvPass:
					passCh = &pchars[i]
				case uuidProvCommit:
					commitCh = &pchars[i]
				case uuidProvStatus:
					st := pchars[i]
					if err := st.EnableNotifications(func(buf []byte) {
						b.onProvStatus(buf)
					}); err != nil {
						log.Printf("transport: prov status notify subscribe failed: %v", err)
					}
				}
			}
		}
	}

	b.mu.Lock()
	b.dev = dev
	b.rxChar = rxCh
	b.ssidChar, b.passChar, b.commitChar = ssidCh, passCh, commitCh
	b.mtu = mtu
	b.deviceName = target.Name
	b.deviceAddr = target.Address
	b.rssi = target.RSSI
	b.lastError = ""
	b.reasm.Reset()
	b.mu.Unlock()
	log.Printf("transport: BLE ready %s mtu=%d prov=%v", target.Name, mtu, ssidCh != nil)
	return nil
}

// onProvStatus 处理配网 Status 特征 notify：JSON 存入 wifi，供 Status API 展示。
func (b *BLEWriter) onProvStatus(buf []byte) {
	var ws WiFiProvStatus
	if err := json.Unmarshal(buf, &ws); err != nil {
		log.Printf("transport: bad prov status %q: %v", buf, err)
		return
	}
	ws.TS = time.Now().UnixMilli()
	b.mu.Lock()
	b.wifi = &ws
	b.mu.Unlock()
	log.Printf("transport: wifi prov status s=%s ip=%s err=%d", ws.S, ws.IP, ws.Err)
}

// markDisconnected 写失败后清理连接状态，connectLoop 下轮自动重连。
func (b *BLEWriter) markDisconnected() {
	b.mu.Lock()
	dev := b.dev
	b.dev = nil
	b.rxChar = nil
	b.ssidChar, b.passChar, b.commitChar = nil, nil, nil
	b.mu.Unlock()
	if dev != nil {
		_ = dev.Disconnect()
	}
	log.Printf("transport: BLE disconnected (write failed), will reconnect")
}

func (b *BLEWriter) setError(msg string) {
	b.mu.Lock()
	b.lastError = msg
	b.mu.Unlock()
}

// sleepOrStop 睡眠或响应停止。返回 false 表示收到停止信号。
func (b *BLEWriter) sleepOrStop(d time.Duration) bool {
	select {
	case <-b.stop:
		return false
	case <-time.After(d):
		return true
	}
}
