// Package update 实现 agent 自更新。
//
// 流程:
//  1. 调用方把新 exe 字节写到 <exe>.new
//  2. Trigger 写一个 restarter 脚本并 detached 启动
//  3. 调用方 os.Exit(0)
//  4. Restarter 等 1s → rename <exe> → <exe>.old → move <exe>.new → <exe> → start <exe> → del <exe>.old
//
// Windows: restarter 是 .bat，用 cmd /c start /b 脱离
// Unix:    restarter 是 .sh，用 setsid 脱离（仅用于本地开发测试）
package update

import (
	"fmt"
	"os"
)

// Trigger 写 restarter 脚本并 detached 启动。
// newExePath 是已写好的新 exe 临时文件路径。
// restartArgs 是重启时传给新 exe 的参数（通常 = os.Args[1:]）。
func Trigger(newExePath, selfExePath string, restartArgs []string) error {
	scriptPath, scriptContent, cmd := buildRestarter(selfExePath, newExePath, restartArgs)
	if err := os.WriteFile(scriptPath, []byte(scriptContent), 0o644); err != nil {
		return fmt.Errorf("write restarter script: %w", err)
	}

	// detached 启动 restarter
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("start restarter: %w", err)
	}
	// 不等 restarter 完成 — 它会等我们自己退出
	_ = cmd.Process.Release()
	return nil
}

// tempPath 返回 <exe>.new 的路径，用于写新 exe 字节。
func TempPath(exePath string) string {
	return exePath + ".new"
}
