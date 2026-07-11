//go:build !windows

package update

import (
	"fmt"
	"os/exec"
	"strings"
	"syscall"
)

// buildRestarter 构造 Unix .sh restarter 脚本（仅用于本地开发测试）。
func buildRestarter(selfExe, newExe string, args []string) (string, string, *exec.Cmd) {
	shPath := selfExe + ".update.sh"
	argsStr := strings.Join(args, `" "`)
	if argsStr != "" {
		argsStr = `"` + argsStr + `"`
	}
	script := fmt.Sprintf(`#!/bin/sh
sleep 1
mv -f "%s" "%s.old" 2>/dev/null
mv -f "%s" "%s" 2>/dev/null
chmod +x "%s" 2>/dev/null
nohup "%s" %s </dev/null >/dev/null 2>&1 &
rm -f "%s.old" "%s" 2>/dev/null
`, selfExe, selfExe, newExe, selfExe, selfExe, selfExe, argsStr, selfExe, shPath)

	cmd := exec.Command("sh", shPath)
	cmd.SysProcAttr = &syscall.SysProcAttr{Setsid: true}
	return shPath, script, cmd
}
