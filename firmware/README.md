# DeskPet Agent 固件 Demo

适用于 ESP32-S3 + 466×466 圆形 AMOLED + LVGL 9 的可交互原型。

## BLE + WiFi（2026-07-19 新增）

- **BLE 数据通道**：NimBLE GATT server，广播名 `ClawdPet-XXXX`（MAC 后 4 位）。
  NUS（Nordic UART Service）收发协议帧，与串口共用同一套行 JSON 解析（`frame_parse.c`），
  两通道可同时工作。bridge 侧 `claudewatch --ble` 自动扫描连接、断线重连。
- **WiFi 配网**：自定义 GATT Service（SSID/Password/Commit/Status），从 bridge
  Web UI「device」页下发凭据；连接成功存 NVS，下次上电自动重连。
- **控制中心**：WiFi/蓝牙 tile 显示真实链路状态，点按打开「连接详情」浮窗。
- 构建注意：本次新增了 BLE/WiFi 配置，**先删 `firmware/sdkconfig`**（或
  `idf.py reconfigure` 后按提示确认）再构建，否则 sdkconfig.defaults 里的
  `CONFIG_BT_NIMBLE_*` 不会生效。

## 功能

- 四屏完整 UI：主页桌宠、负一屏、会话页、控制中心。
- 使用 **mock 数据**，无任何通信。
- 点击桌宠切换其状态（idle → thinking → typing → … → disconnected，循环），状态图片使用真实 Clawd 素材。
- 滑动手势导航：
  - 主页左滑 → 负一屏
  - 主页右滑 → 会话页
  - 主页下滑 → 控制中心
  - 会话页右滑 → 下一个会话
  - 会话页左滑 → 上一个会话 / 回主页
  - 底边上滑 → 回主页
- Clawd 素材已预转换为 `LV_COLOR_FORMAT_ARGB8888` 数组（`assets/clawd/c/clawd_image_data.h`），
  由 `clawd_assets.c` 包装成 `lv_image_dsc_t` 并在主页用 `lv_image` 直接显示。

## 文件结构

```
firmware/
├── CMakeLists.txt          # 顶层 CMake
├── sdkconfig.defaults      # ESP32-S3 / PSRAM / LVGL 配置
├── partitions.csv          # 8MB 单 app 分区（为 Clawd 素材留足空间）
├── README.md               # 本文件
└── main/
    ├── CMakeLists.txt      # 组件源文件列表
    ├── idf_component.yml   # 依赖：lvgl / esp_lvgl_port / sh8601 / ft5x06
    ├── app_main.c          # 入口 + 视图切换 + 手势分发
    ├── bsp.c/h             # 板级：SH8601 QSPI 屏 + FT5x06 触摸 + LVGL 移植
    ├── ui_common.c/h       # 屏幕尺寸、颜色/字号/间距 Token、卡片/Halo 工具
    ├── clawd_assets.c/h    # 原始 ARGB8888 数组 → lv_image_dsc_t + 状态查表
    ├── mock_data.c/h       # mock 会话 / 硬件状态 / 通知数据（无通信）
    ├── pet_states.c/h      # 表情状态机（12 态 + 点击循环 + 断连覆盖）
    ├── gesture.c/h         # 滑动方向识别（左/右/上/下 + 底边上滑回家）
    ├── ui_pet.c/h          # ① 主页桌宠（Clawd 图片 + Halo + 通知气泡）
    ├── ui_negative.c/h     # ② 负一屏（语音按钮 + 硬件卡 + slot 占位）
    ├── ui_session.c/h      # ③ 会话页（单会话 + 右滑切会话 + 上下滑翻内容）
    ├── ui_control.c/h      # ④ 控制中心（3×2 tile 网格）
    └── assets/clawd/       # Clawd 素材
        ├── c/clawd_image_data.h  # 12 个状态的 150×150 ARGB8888 数组
        ├── gif/ svg/ png/        # 原始 / 中间素材
        ├── LICENSE NOTICE.md README.md
```

## 硬件目标

- 开发板：Waveshare **ESP32-S3-Touch-AMOLED-1.43C**（466×466 圆形 AMOLED）。
- 屏：SH8601（QSPI，RGB565）。触摸：FT5x06（I2C）。
- 引脚定义在 `main/bsp.c` 顶部，如换板请对照修改。

## 构建

1. 安装 ESP-IDF v5.3+ 并设置环境：
   ```bash
   . $IDF_PATH/export.sh
   ```

2. 设置目标并构建烧录（首次会自动拉取 `idf_component.yml` 里的依赖组件）：
   ```bash
   cd firmware
   idf.py set-target esp32s3
   idf.py build flash monitor
   ```

> 依赖组件（LVGL 9.2 / esp_lvgl_port / esp_lcd_sh8601 / esp_lcd_touch_ft5x06）
> 由 ESP-IDF 组件管理器按 `main/idf_component.yml` 自动下载，无需手动放置。

## 快速体验（无硬件）

本 demo 是纯 LVGL 代码，可在支持 SDL2 的 PC 模拟器中运行：

1. 安装 LVGL PC 模拟器依赖（Ubuntu/WSL 示例）：
   ```bash
   sudo apt-get install libsdl2-dev
   ```

2. 使用 LVGL 官方 `lv_port_pc_vscode` 或 `lv_port_pc_eclipse` 模板，把 `main/` 下的源文件复制进去。

3. 修改显示分辨率：
   ```c
   #define SCREEN_SIZE 466
   ```

4. 运行模拟器，用鼠标点击/拖动模拟触摸。

## 素材说明

12 个桌宠状态的图片已预生成在 `main/assets/clawd/c/clawd_image_data.h`
（150×150 ARGB8888，每个状态一个 `static const uint8_t clawd_<state>_data[90000]`）。
`clawd_assets.c` 把它们包装成 `lv_image_dsc_t` 并按 `pet_state_t` 查表，主页用 `lv_image` 直接显示。

状态 → 素材映射见 `main/assets/clawd/README.md`。

> 注意：150×150 ARGB8888 单张约 90KB Flash（C 数组）。12 个状态总计约 1.1MB Flash。
> 若 Flash 紧张，可减小尺寸、减少状态数量，或改用 `LV_COLOR_FORMAT_RGB565`（需去掉透明通道）。

## 后期加回中文

UI 当前为英文标签（避免无字库时显示方框）。恢复中文的步骤：

1. 用 `lv_font_conv` 生成 `font_cjk_14.c`（详见 `main/assets/clawd/README.md`）。
2. 加入 `CMakeLists.txt` 的 SRCS。
3. 在 `ui_common.h` 声明 `extern const lv_font_t font_cjk_14;`。
4. 把各文件英文 label 改回中文，字体设为 `&font_cjk_14`。

> 后续有字库支持时，再将 mock 数据和 UI 标签改回中文。
