//go:build windows

package update

import (
	"fmt"
	"os/exec"
	"strings"
	"syscall"
)

// buildRestarter 构造 Windows .bat restarter 脚本。
// 返回: 脚本路径, 脚本内容, 已配置为 detached 的 *exec.Cmd。
func buildRestarter(selfExe, newExe string, args []string) (string, string, *exec.Cmd) {
	batPath := selfExe + ".update.bat"
	argsStr := strings.Join(args, " ")
	if argsStr != "" {
		argsStr = " " + argsStr
	}
	script := fmt.Sprintf(`@echo off
timeout /t 1 /nobreak >nul
move /y "%s" "%s.old" >nul 2>&1
move /y "%s" "%s" >nul 2>&1
start "" "%s"%s
del "%s.old" >nul 2>&1
del "%s" >nul 2>&1
`, selfExe, selfExe, newExe, selfExe, selfExe, argsStr, selfExe, batPath)

	// cmd /c start /b "title" cmd /c bat — 用 start /b 让 cmd 不开新窗口且脱离父进程
	cmd := exec.Command("cmd", "/c", "start", "/b", "", "cmd", "/c", batPath)
	cmd.SysProcAttr = &syscall.SysProcAttr{
		HideWindow:    true,
		CreationFlags: syscall.CREATE_NEW_PROCESS_GROUP,
	}
	return batPath, script, cmd
}
