//go:build !windows

package transport

import "os"

// 非 Windows：Linux 串口 Read/Write 在驱动层不互锁，阻塞 Read 不影响并发
// Write，无需 COMMTIMEOUTS。readLoop 用阻塞 Read 事件驱动排空 RX。
func setNonBlockingReads(f *os.File) error { return nil }
