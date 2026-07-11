// Package web 嵌入前端构建产物，供 agent 直接 serve。
// 构建前先 cd frontend && npm run build (outDir 指向 ../web/dist)。
package web

import "embed"

//go:embed all:dist
var Files embed.FS
