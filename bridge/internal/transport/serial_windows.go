//go:build windows

package transport

import (
	"os"
	"syscall"
	"unsafe"
)

// COMMTIMEOUTS 结构，对应 Windows API。
type commTimeouts struct {
	ReadIntervalTimeout         uint32
	ReadTotalTimeoutMultiplier  uint32
	ReadTotalTimeoutConstant    uint32
	WriteTotalTimeoutMultiplier uint32
	WriteTotalTimeoutConstant   uint32
}

var (
	modKernel32          = syscall.NewLazyDLL("kernel32.dll")
	procSetCommTimeouts  = modKernel32.NewProc("SetCommTimeouts")
)

// setNonBlockingReads 配置 COM 口的 COMMTIMEOUTS，使 ReadFile 立即返回
// （有数据返回数据，无数据返回 0/nil）。
//
// 必要性：raw os.File 的 ReadFile 在 COM 口上默认阻塞等待数据；而 Windows
// 非 overlapped COM 句柄的 ReadFile/WriteFile 在驱动层串行化，阻塞 Read 会
// 卡住并发 Write（心跳写不出去、frames_sent 卡 0）。设为非阻塞后 readLoop
// 可排空 RX（防设备 ESP_LOG 反压冻 LVGL），且不阻塞 Write。
func setNonBlockingReads(f *os.File) error {
	// ReadIntervalTimeout=MAXDWORD + multiplier/constant=0 → ReadFile 立即返回
	// 已收到的字节，无字节时返回 0/nil（真·非阻塞，MSDN 明确语义）。
	t := commTimeouts{
		ReadIntervalTimeout:        0xFFFFFFFF, // MAXDWORD
		ReadTotalTimeoutMultiplier: 0,
		ReadTotalTimeoutConstant:   0,
	}
	r1, _, _ := procSetCommTimeouts.Call(f.Fd(), uintptr(unsafe.Pointer(&t)))
	if r1 == 0 {
		return syscall.EINVAL
	}
	return nil
}
