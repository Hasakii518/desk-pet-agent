# Clawd 桌宠资源（ESP32 固件）

本目录包含来自 [clawd-on-desk](https://github.com/rullerzhou-afk/clawd-on-desk) 的 Clawd 角色素材。

## 授权

- Clawd 角色形象版权归 **Anthropic** 所有。
- 使用需遵守原项目 `LICENSE`（All Rights Reserved）。
- 本项目已获授权使用，**仅限非商业用途**。

## 目录结构

```
assets/clawd/
├── gif/          # 原始动态参考（27 个）
├── svg/          # 原始矢量素材（44 个）
├── png/          # 已转换的 150×150 透明 PNG（47 个）
├── c/            # 已生成的 LVGL Canvas ARGB8888 数据头文件
├── LICENSE       # 原项目美术资源授权声明
├── NOTICE.md     # 第三方素材声明
└── README.md     # 本文件
```

## 当前实现

`ui_pet.c` 已经通过 `lv_canvas` + `c/clawd_image_data.h` 直接显示真实 Clawd 图片。

转换流程：

1. `tools/svg-to-png.js`：将 `svg/` 渲染为 `png/`（默认 150×150，透明背景）。
2. `tools/png-to-canvas.js`：将 12 个关键状态的 PNG 转为 ARGB8888 字节数组，生成 `c/clawd_image_data.h`。

重新生成：

```bash
cd firmware/tools
node svg-to-png.js
node png-to-canvas.js
```

## 状态映射

| PetState | 使用的 PNG / C 数组 |
|---|---|
| idle | `clawd-idle-living` |
| thinking | `clawd-working-thinking` |
| typing | `clawd-working-typing` |
| building | `clawd-working-building` |
| notification | `clawd-notification` |
| waiting | `clawd-idle-bubble` |
| permission | `clawd-mini-alert` |
| speaking | `clawd-working-typing-boss` |
| happy | `clawd-happy` |
| error | `clawd-error` |
| sleeping | `clawd-sleeping` |
| disconnected | `clawd-idle-low-battery`（建议灰度处理） |

## 优化建议

- 当前为 150×150 ARGB8888，单图约 90KB Flash/RAM。若 Flash 紧张，可：
  - 减小 `SIZE` 到 120 或 100；
  - 仅保留最常用的 6-8 个状态；
  - 改用 `LV_COLOR_FORMAT_RGB565`（需修改 `png-to-canvas.js` 去掉 Alpha）。
