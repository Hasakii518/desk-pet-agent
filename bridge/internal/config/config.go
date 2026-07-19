// Package config 提供 bridge agent / probe 的跨平台配置目录与配置文件加载。
//
// 配置目录:
//   Windows:        %APPDATA%\ClaudeWatch
//   macOS / Linux:  $HOME/.config/claudewatch
//
// 解析优先级: 命令行 flag(显式) > 环境变量 > 配置文件 > 内置默认。
// token 若最终仍为空，agent 会自动生成并持久化到 TokenPath()（供 probe 共享）。
package config

import (
	"encoding/json"
	"os"
	"path/filepath"
	"runtime"
)

// Config 是 agent 的配置文件结构（JSON）。所有字段可选。
type Config struct {
	Addr       string `json:"addr"`       // 监听地址，如 ":7777" 或 "0.0.0.0:7777"
	Token      string `json:"token"`      // 鉴权 token；为空表示自动生成
	DB         string `json:"db"`         // SQLite 路径；为空使用默认（配置目录/events.db）
	SerialPort string `json:"serialPort"` // 串口路径或 "auto"
	WSLDistro  string `json:"wslDistro"`  // WSL 发行版名（doctor UNC 访问用）
	BLE        bool   `json:"ble"`        // 是否启用 BLE 传输
	BLEName    string `json:"bleName"`    // BLE 设备名前缀（默认 "ClawdPet-"）
}

// Dir 返回跨平台配置目录。
func Dir() string {
	if runtime.GOOS == "windows" {
		if app := os.Getenv("APPDATA"); app != "" {
			return filepath.Join(app, "ClaudeWatch")
		}
	}
	home, err := os.UserHomeDir()
	if err != nil || home == "" {
		home = "."
	}
	return filepath.Join(home, ".config", "claudewatch")
}

// DefaultPath 返回默认配置文件路径（Dir()/config.json）。
func DefaultPath() string {
	return filepath.Join(Dir(), "config.json")
}

// TokenPath 返回 token 文件路径（Dir()/token）。
func TokenPath() string {
	return filepath.Join(Dir(), "token")
}

// Load 读取并解析配置文件。
// 文件不存在时返回零值 Config 且 exists=false（不报错），调用方回落到默认/自动生成。
func Load(path string) (Config, bool, error) {
	var cfg Config
	data, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return cfg, false, nil
		}
		return cfg, false, err
	}
	if err := json.Unmarshal(data, &cfg); err != nil {
		return cfg, true, err
	}
	return cfg, true, nil
}

// Save 写入配置文件（供 installer 落盘或手动编辑后保存）。
func Save(path string, cfg Config) error {
	if dir := filepath.Dir(path); dir != "" {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return err
		}
	}
	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, data, 0o644)
}
