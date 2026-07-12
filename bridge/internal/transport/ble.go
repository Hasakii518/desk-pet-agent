// ble.go —— 蓝牙下行通道（桩）。
//
// 真实 BLE GATT UART 跨平台实现重（Linux 需 BlueZ D-Bus，Windows 需 WinRT），
// 本轮仅占位：构造时记一行日志，Write 静默丢弃帧。接口已就位，后续接入时
// 替换 Write 内部即可，上层 Multi / 编码逻辑无需改动。
package transport

import "log"

// BLEWriter 是蓝牙下行 Writer 的占位实现。
type BLEWriter struct {
	warned bool
}

// NewBLE 构造蓝牙 Writer，启动时记一行提示。
func NewBLE() *BLEWriter {
	log.Printf("transport: BLE writer loaded (stub, frames will be dropped)")
	return &BLEWriter{}
}

// Write 静默丢弃。返回 nil 以避免 Multi 反复打错误日志。
func (b *BLEWriter) Write(frame []byte) error {
	_ = frame
	return nil
}

// Close 空操作。
func (b *BLEWriter) Close() error { return nil }
