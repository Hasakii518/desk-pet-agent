---
name: esp-idf-workflow
description: >
  ESP32/ESP-IDF 固件构建、烧录和串口调试的完整工作流。当用户提到以下任何内容时使用此 skill：
  "ESP32"、"ESP-IDF"、"idf.py"、"烧录"、"刷机"、"flash ESP"、"build firmware"、
  "串口监控"、"serial monitor"、"构建固件"、"下载到开发板"、"ESP 编译"、
  "esp32 build"、硬件部署、固件下发调试、"set-target"、"menuconfig"。
  即使只提到"编译"或"烧录"也应该主动使用此 skill。
compatibility: Windows (PowerShell) + WSL, ESP-IDF v5.x/v6.x
---

# ESP-IDF 构建、烧录 & 调试工作流

## 概述

此 skill 自动化 ESP-IDF 开发的完整流程，遵循 [官方文档工作流](https://docs.espressif.com/projects/esp-idf/zh_CN/v6.0.2/esp32s3/get-started/windows-start-project.html)：

**激活环境 → 设置目标芯片 → 配置项目 → 构建烧录 → 串口监控**

---

## 环境感知

首先检测当前运行环境：

- **WSL/Linux 终端**：所有 ESP-IDF 命令必须通过 `powershell.exe` 执行
- **Windows PowerShell（原生）**：直接执行 PowerShell 命令

### WSL 检测

```bash
grep -qi microsoft /proc/version 2>/dev/null && echo "WSL" || echo "Native"
```

**WSL → PowerShell 桥接**：
```bash
powershell.exe -Command "<PowerShell 命令>"
```

> **重要**：每次 `powershell.exe` 调用是独立的 shell 会话。激活环境的命令和 IDF 操作命令必须放在**同一个 `-Command` 块**中，用分号 `;` 分隔。

---

## 工作流

### 第一步：定位项目

1. 如果用户指定了项目路径 → 使用该路径
2. 否则使用当前工作目录
3. 验证项目根目录存在 `CMakeLists.txt`（ESP-IDF 项目标志）

```bash
test -f CMakeLists.txt && echo "Valid ESP-IDF project" || echo "Not an ESP-IDF project"
```

如果找不到 `CMakeLists.txt`，向上搜索 3 层父目录。找到后，所有后续命令都从该目录执行。

> **约束**：ESP-IDF 构建系统不支持路径中包含**空格**，且路径长度建议不超过 **90 个字符**。

### 第二步：激活 ESP-IDF 环境

ESP-IDF 命令需要特定的环境变量。v6.0 提供两种激活方式：

#### 方式 A：桌面快捷方式（推荐，v5.x/v6.x 均有）

安装 ESP-IDF 后，桌面会生成快捷方式（如 `IDF_v6.0_Powershell`、`ESP-IDF PowerShell`）。双击打开即可获得已激活环境的终端，无需手动执行任何脚本。

如果用户已在这样的终端中运行，可以跳过此步骤，直接执行 IDF 命令。

#### 方式 B：手动激活 `export.ps1`（原生 PowerShell）

**Windows PowerShell（原生）**：
```powershell
. C:\esp\v5.5.4\esp-idf\export.ps1
```

#### 方式 C：EIM Profile 脚本（WSL 推荐）

在 WSL 中通过 `powershell.exe` 调用 IDF 时，**必须使用 EIM 生成的 profile 脚本**而非原始 `export.ps1`。EIM 脚本直接构造正确的 PATH（含工具版本号），避免了 `idf_tools.py export` 在 WSL 环境中失败的问题：

```bash
powershell.exe -Command '
  . "C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1"
  cd "<项目Windows路径>"
  idf.py --version
'
```

> **关键**：使用**单引号**包裹 PowerShell 命令，防止 bash 吃掉 `$` 符号。profile 脚本路径格式为 `Microsoft.v<版本号>.PowerShell_profile.ps1`。

v6.0 引入的 GUI 管理工具，支持多版本共存。从 EIM Dashboard → 选择版本 → **Open IDF Terminal** 打开已激活的终端。

> 根据用户实际安装路径调整 `export.ps1` 的位置。常见路径包括：
> - `C:\esp\v5.5.4\esp-idf\export.ps1`（手动安装）
> - `C:\Espressif\frameworks\esp-idf-v5.2.2\export.ps1`（安装器安装）
> - `%userprofile%\Desktop\esp-idf\export.ps1`

### 第三步：设置目标芯片

这是**必须**的步骤 — 每个项目都需要指定目标芯片型号：

```bash
# WSL 执行方式
powershell.exe -Command "
  . C:\esp\v5.5.4\esp-idf\export.ps1
  cd '<项目Windows路径>'
  idf.py set-target esp32s3
"
```

**常用目标芯片**：

| 芯片 | 架构 | 说明 |
|------|------|------|
| `esp32` | Xtensa 双核 | 经典 ESP32 |
| `esp32s3` | Xtensa 双核 | 带 AI 加速、USB OTG |
| `esp32c3` | RISC-V 单核 | 低功耗、低成本 |
| `esp32c6` | RISC-V 单核 | WiFi 6 + BLE 5 |
| `esp32p4` | RISC-V 双核 | 高性能（无无线） |

> `set-target` 会：清除旧 build 目录 → 切换交叉编译器 → 加载芯片寄存器映射 → 生成适配的 `sdkconfig`

### 第四步：项目配置（按需）

```bash
idf.py menuconfig
```

常用配置项：
- **Serial flasher config** → Flash 大小和烧录波特率
- **Component config → Hardware Settings → Main XTAL Config** → 晶振频率（若监控乱码，切换 40MHz / 26MHz）
- **Component config → ESP System Settings** → CPU 频率
- **Component config** → 启用 WiFi / BLE / BluFi

### 第五步：构建项目

> **超时设置**：ESP-IDF 构建可能耗时 3-10 分钟（取决于项目大小和增量/全量）。在 Claude Code 中使用 **600000ms (10分钟)** 超时，防止命令被提前杀死。

```bash
# WSL 执行方式（使用 EIM profile 脚本 + 单引号防转义）
powershell.exe -Command '
  . "C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1"
  cd "<项目Windows路径>"
  idf.py build
'
```

> **注意**：如果下一步直接执行 `flash`，可以**跳过**此步骤 — `idf.py flash` 会自动先完成构建。

**构建失败处理**：

| 错误信息 | 原因 | 建议 |
|----------|------|------|
| `fatal error: xxx.h: No such file or directory` | 缺少组件依赖 | `idf.py fullclean && idf.py build` |
| `partition table not found` | 缺少分区表 | 检查 `partitions.csv` 是否存在 |
| `CONFIG_xxx` 相关错误 | sdkconfig 配置问题 | 运行 `idf.py menuconfig` 检查 |
| 链接错误 (undefined reference) | CMakeLists.txt 配置问题 | 检查 `SRCS` 和 `REQUIRES` |

**不要自动执行 `fullclean`** — 它会删除整个 build 目录，导致下次全量编译耗时较长。先分析错误原因。

**构建成功标志**：
```
Project build complete. To flash, run:
 idf.py flash
```

### 第六步：检测串口 & 烧录固件

#### 自动检测（推荐）

`idf.py` **自带串口自动检测**能力 — 省略 `-p` 参数时，它会自动扫描可用的 USB 串口设备。对于只连接了一个 ESP32 的情况非常方便：

```bash
# 自动检测端口烧录
idf.py flash
```

#### 手动指定端口

```bash
# WSL 执行方式
powershell.exe -Command "
  . C:\esp\v5.5.4\esp-idf\export.ps1
  cd '<项目Windows路径>'
  idf.py -p COM3 flash
"
```

> `flash` 命令**会自动先构建**再烧录 — 无需单独执行 `idf.py build`。

**指定烧录波特率**（加速烧录）：
```bash
idf.py -p COM3 -b 460800 flash
```

#### 使用捆绑脚本检测端口

如果自动检测失败或需要手动选择，使用 `scripts/detect-port.ps1`：

```bash
# WSL 中
powershell.exe -File "<skill目录>/scripts/detect-port.ps1"

# 列出所有端口详情
powershell.exe -File "<skill目录>/scripts/detect-port.ps1" -ListAll
```

**串口检测逻辑**：
1. 只有一个串口 → 直接使用
2. 多个串口 → 优先匹配 CP210x / CH340 / CH341 / FTDI 芯片
3. 无法确定 → 列出所有端口让用户选择
4. 没有串口 → 提示检查 USB 连接和驱动

#### 烧录失败排查

| 错误信息 | 原因 | 解决方案 |
|----------|------|----------|
| `Could not open COMx` | 端口被占用 | 关闭串口监视器、Arduino IDE、PuTTY 等占用端口的程序 |
| `Timed out waiting for packet header` | 设备未进入下载模式 | **按住 BOOT 按钮 → 按一下 RST/EN → 松开 BOOT** |
| `Wrong boot mode detected` | GPIO0 未拉低 | 检查 GPIO0 是否接地（进入下载模式的条件） |
| `Invalid head of packet` | 烧录波特率过高 | 降低波特率：`-b 115200` |
| 烧录后设备无响应 | 晶振频率不匹配 | `menuconfig` → Main XTAL Config → 切换 40MHz/26MHz |

### 第七步：串口监控

烧录成功后，**询问用户**是否需要启动串口监视器：

```bash
# WSL 执行方式
powershell.exe -Command "
  . C:\esp\v5.5.4\esp-idf\export.ps1
  cd '<项目Windows路径>'
  idf.py -p COM3 monitor
"
```

#### IDF Monitor 快捷键

| 快捷键 | 功能 |
|--------|------|
| **`Ctrl+]`** | 退出监视器 |
| **`Ctrl+T` → `Ctrl+R`** | 复位目标设备 |
| **`Ctrl+T` → `Ctrl+F`** | 编译并烧录（监控中直接更新固件） |
| **`Ctrl+T` → `Ctrl+A`** | 仅烧录应用程序 |
| **`Ctrl+T` → `Ctrl+Y`** | 暂停/恢复日志输出 |
| **`Ctrl+T` → `Ctrl+L`** | 开始/停止日志写入文件 |
| **`Ctrl+T` → `Ctrl+I`** | 切换时间戳显示 |
| **`Ctrl+T` → `Ctrl+P`** | 暂停应用程序（RTS 线复位到 Bootloader） |
| **`Ctrl+T` → `Ctrl+H`** | 显示所有快捷键帮助 |
| **`Ctrl+C`** | 中断运行中程序（触发 GDBStub 调试） |

#### 日志过滤

使用 `--print-filter` 按标签和级别过滤输出：

```bash
# 语法：idf.py monitor --print-filter="<tag>:<level>"
# 级别：N=None, E=Error, W=Warning, I=Info, D=Debug, V=Verbose, *=全部

# 示例：仅显示 esp_image 的 Error 和 wifi 的全部输出
idf.py monitor --print-filter="wifi esp_image:E light_driver:I"

# 示例：抑制启动时的冗长日志，只看应用输出
idf.py monitor --print-filter="light_driver:D esp_image:N boot:N cpu_start:N vfs:N wifi:N *:V"
```

#### 自动地址解码

当 ESP32 发生 crash/panic 时，IDF Monitor 会**自动**将十六进制地址解码为源代码位置（黄色高亮显示）。无需手动运行 `addr2line`。

如需禁用：`idf.py monitor --disable-address-decoding`

#### 不复位连接

如果不想在连接时复位设备（保留当前运行状态）：
```bash
idf.py monitor --no-reset
```

---

## 一键完整流程

### 最简方式（自动检测端口）

```bash
powershell.exe -Command "
  . C:\esp\v5.5.4\esp-idf\export.ps1
  cd '<项目Windows路径>'
  idf.py flash monitor
"
```

> `flash` 自动完成 **构建 → 烧录**，`monitor` 接着启动串口监控。这是官方推荐的日常开发工作流。

### 使用捆绑脚本

```bash
# 完整流程（自动检测端口）
powershell.exe -File "<skill目录>/scripts/flash-all.ps1"

# 指定端口
powershell.exe -File "<skill目录>/scripts/flash-all.ps1" -Port COM3

# 仅构建
powershell.exe -File "<skill目录>/scripts/flash-all.ps1" -BuildOnly

# 构建+烧录（不启动监控）
powershell.exe -File "<skill目录>/scripts/flash-all.ps1" -NoMonitor

# 仅监控（不构建不烧录）
powershell.exe -File "<skill目录>/scripts/flash-all.ps1" -MonitorOnly -Port COM3

# 清理后全量重建
powershell.exe -File "<skill目录>/scripts/flash-all.ps1" -Clean
```

---

## 快速命令参考

以下命令假设 IDF 环境已激活（`export.ps1` 已执行）：

| 操作 | 命令 | 说明 |
|------|------|------|
| 设置目标芯片 | `idf.py set-target <芯片>` | 如 `esp32`, `esp32s3` |
| 图形化配置 | `idf.py menuconfig` | 配置 Flash、WiFi、晶振等 |
| 构建 | `idf.py build` | 增量编译 |
| 烧录 | `idf.py -p COMx flash` | 自动先构建再烧录 |
| 监控 | `idf.py -p COMx monitor` | 启动串口监视器 |
| **一键完成** | **`idf.py -p COMx flash monitor`** | 构建+烧录+监控 |
| 清理构建 | `idf.py fullclean` | 删除整个 build 目录 |
| 清理中间文件 | `idf.py clean` | 保留配置，仅清理 .o |
| 查看固件大小 | `idf.py size` | 分析各组件内存占用 |
| 查看分区表 | `idf.py partition-table` | v6.0 用连字符 |
| 擦除 Flash | `idf.py erase-flash` | v6.0 用连字符 |
| 查看芯片信息 | `esptool.py -p COMx flash_id` | 识别 Flash 型号和大小 |
| 烧录指定文件 | `esptool.py -p COMx write_flash 0x0 firmware.bin` | 烧录到指定地址 |

> **v5.x → v6.0 命令变化**：`erase_flash` → `erase-flash`，`partition_table` → `partition-table`。实际上两种写法在过渡期均可使用。

---

## 诊断与排查

### 监控输出乱码

最常见原因是晶振频率不匹配：
1. 运行 `idf.py menuconfig`
2. 进入 **Component config → Hardware Settings → Main XTAL Config**
3. 在 **40 MHz** 和 **26 MHz** 之间切换（大多数 ESP32 开发板使用 40MHz，部分使用 26MHz）
4. 保存后重新 `idf.py flash monitor`

### 烧录后不断重启

检查是否有看门狗触发或电压检测问题：
1. `menuconfig` → **Component config → ESP System Settings**
2. 关闭电压检测（使用 USB 供电时常见）
3. 检查启动日志中的 `rst:` 原因码

### 串口无法打开

1. 检查设备管理器中的端口号
2. 确认驱动已安装（CP210x / CH340 / FTDI）
3. 关闭所有可能占用端口的程序
4. 尝试重新插拔 USB 线

### 固件运行但功能异常

1. 检查 `sdkconfig` 中相关功能是否使能
2. 检查分区表是否与代码中使用的分区匹配
3. 使用 `idf.py monitor --print-filter="*:V"` 查看详细日志

---

## 关键约束

1. **路径转换**：WSL 中 `/mnt/c/Users/...` → Windows `C:\Users\...`
2. **环境隔离**：激活命令和 IDF 命令必须在同一个 `powershell.exe -Command` 块中
3. **WSL 转义**：bash 中的 `$`、`"` 在 PowerShell 字符串中需转义（`\$`、`\"`）
4. **路径不含空格**：ESP-IDF 构建系统不支持
5. **路径长度 ≤ 90 字符**：Windows 路径过长会导致编译失败
6. **不要自动执行破坏性操作**：`fullclean`、`erase-flash` 等需先确认
7. **首次烧录新设备前确认**：烧录会覆盖现有固件
8. **`set-target` 是必须步骤**：每个项目首次设置必须指定芯片型号

---

## 参考资料

- [ESP-IDF 编程指南 v6.0.2](https://docs.espressif.com/projects/esp-idf/zh_CN/v6.0.2/esp32s3/get-started/index.html)
- [Windows 命令行构建项目](https://docs.espressif.com/projects/esp-idf/zh_CN/v6.0.2/esp32s3/get-started/windows-start-project.html)
- [IDF Monitor 完整参考](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/api-guides/tools/idf-monitor.html)
- [idf.py 前端工具参考](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-guides/tools/idf-py.html)
