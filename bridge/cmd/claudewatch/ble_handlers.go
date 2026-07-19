// ble_handlers.go —— BLE / WiFi 配网的 HTTP 端点。
//
// bleW 为 nil（未启用 --ble）时统一返回 503，前端据此提示「以串口模式运行」。
package main

import (
	"encoding/json"
	"net/http"
	"strings"

	"claudewatch/internal/logbuf"
	"claudewatch/internal/transport"
)

func bleUnavailable(w http.ResponseWriter) {
	http.Error(w, `{"error":"BLE not enabled (start with --ble)"}`, http.StatusServiceUnavailable)
}

// handleBLEStatus GET：链路诊断 + 最近一次配网状态。
func handleBLEStatus(bw *transport.BLEWriter) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if bw == nil {
			bleUnavailable(w)
			return
		}
		writeJSON(w, bw.Status())
	}
}

// handleBLEScan GET：4s 主动扫描，返回候选设备列表。
func handleBLEScan(bw *transport.BLEWriter, lb *logbuf.Buffer) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if bw == nil {
			bleUnavailable(w)
			return
		}
		devs, err := bw.ScanOnce()
		if err != nil {
			lb.Log(logbuf.LevelWarn, "ble scan: "+err.Error())
			http.Error(w, err.Error(), http.StatusBadGateway)
			return
		}
		writeJSON(w, map[string]any{"devices": devs})
	}
}

// handleBLEConnect POST {"address":"AA:BB:.."}：address 为空则恢复自动重连。
func handleBLEConnect(bw *transport.BLEWriter, lb *logbuf.Buffer) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if bw == nil {
			bleUnavailable(w)
			return
		}
		if r.Method != http.MethodPost {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		var body struct {
			Address string `json:"address"`
		}
		_ = json.NewDecoder(r.Body).Decode(&body) // 空 body = 自动模式
		bw.Connect(strings.TrimSpace(body.Address))
		lb.Log(logbuf.LevelInfo, "ble: connect requested (addr="+body.Address+")")
		writeJSON(w, map[string]string{"ok": "1"})
	}
}

// handleBLEDisconnect POST：断开并暂停自动重连。
func handleBLEDisconnect(bw *transport.BLEWriter, lb *logbuf.Buffer) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if bw == nil {
			bleUnavailable(w)
			return
		}
		if r.Method != http.MethodPost {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		bw.Disconnect()
		lb.Log(logbuf.LevelInfo, "ble: user disconnected (auto-reconnect paused)")
		writeJSON(w, map[string]string{"ok": "1"})
	}
}

// handleWiFiProvision POST {"ssid":"…","password":"…"}：经 BLE 配网特征下发。
func handleWiFiProvision(bw *transport.BLEWriter, lb *logbuf.Buffer) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if bw == nil {
			bleUnavailable(w)
			return
		}
		if r.Method != http.MethodPost {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		var body struct {
			SSID     string `json:"ssid"`
			Password string `json:"password"`
		}
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
			http.Error(w, "bad request: "+err.Error(), http.StatusBadRequest)
			return
		}
		if err := bw.Provision(body.SSID, body.Password); err != nil {
			lb.Log(logbuf.LevelWarn, "wifi provision: "+err.Error())
			http.Error(w, err.Error(), http.StatusBadGateway)
			return
		}
		lb.Log(logbuf.LevelInfo, "wifi provision sent (ssid="+body.SSID+")")
		writeJSON(w, map[string]string{"ok": "1"})
	}
}
