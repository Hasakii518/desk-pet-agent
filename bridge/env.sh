# 项目内激活 Go 1.23.12，不影响全局
# 用法: source ./env.sh
export GOROOT="$HOME/.g/versions/1.23.12"
export GOPATH="$HOME/go"
export PATH="$GOROOT/bin:$GOPATH/bin:$PATH"
# proxy.golang.org 不可达时走国内镜像
export GOPROXY="https://goproxy.cn,direct"
