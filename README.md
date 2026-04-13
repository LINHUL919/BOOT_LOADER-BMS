# BOOT_LOADER-BMS

> 一个功能完整的小型锂电池包管理固件，保护策略齐全，带 SOC 估算和 Flash 掉电保存，附带 Bootloader 远程升级版本。

---

## 📖 项目简介

本项目是基于 **STM32** 系列微控制器开发的锂电池包管理系统（BMS）固件，集成了完整的电池保护策略、SOC（荷电状态）估算算法，以及通过 Flash 实现的掉电数据保存机制。同时附带一套独立的 **Bootloader**，支持通过串口（UART/USART）对 BMS 主程序进行 OTA 固件远程升级。

---

## ✨ 主要功能

### BMS 主程序（STM32F103C8T6）
| 功能模块 | 说明 |
|----------|------|
| 🔋 电芯管理 | 基于 **TI BQ76940** AFE 芯片，支持多节锂电池串联检测 |
| ⚡ 过压保护（OVP） | 单节电芯电压超限自动断路 |
| ⚡ 欠压保护（UVP） | 单节电芯电压过低自动断路 |
| 🌡️ 过温保护（OTP） | 温度超限自动保护 |
| 💥 过流保护（OCP） | 充放电电流异常时自动断路 |
| 📊 SOC 估算 | 实现电量百分比估算（安时积分 / OCV 查表法）|
| 💾 掉电保存 | 关键数据写入 Flash，上电后自动恢复 |
| 🖥️ 上位机通信 | 通过 UART 与 PC 端 Python 上位机实时通信 |
| 🔌 I²C 通信 | 软件模拟 I²C（sw_i2c）与 BQ76940 通信 |
| 🔄 FreeRTOS | 基于 FreeRTOS 实现多任务调度 |

### Bootloader（STM32F407ZGT6）
| 功能模块 | 说明 |
|----------|------|
| 🚀 固件升级 | 通过 UART 接收新固件并写入 Flash |
| 💾 W25Q128 | SPI Flash 用于暂存升级固件包 |
| 🗂️ AT24C02/W24C02 | I²C EEPROM 用于保存升级标志位 |
| 🔁 跳转逻辑 | 升级完成后自动跳转至 App 区执行 |
| 🔄 CAN 预留 | 预留 CAN 总线升级接口（CAN_reserve）|

---

## 🗂️ 项目结构

```
BOOT_LOADER-BMS/
│
├── BMS_program _stm32F103c8t6/        # BMS 主程序（STM32F103C8T6）
│   ├── BMS_HAL/                       # STM32 HAL 工程
│   │   ├── BSP/                       # 板级支持包（核心驱动）
│   │   │   ├── bq76940.c / .h         # TI BQ76940 AFE 驱动
│   │   │   ├── bms_soc.c / .h         # SOC 估算模块
│   │   │   └── sw_i2c.c / .h          # 软件 I²C 驱动
│   │   ├── Core/                      # STM32 HAL 核心（main, 中断, 外设初始化）
│   │   ├── Drivers/                   # STM32 HAL 库
│   │   ├── FreeRTOS/                  # FreeRTOS 源码
│   │   ├── MDK-ARM/                   # Keil MDK 工程文件
│   │   └── USART_test.ioc             # STM32CubeMX 配置文件
│   ├── bms_upper_computer.py          # Python 上位机程序
│   └── BMS上位机.bat                  # 上位机启动脚本
│
└── BMS_Bootloader/                    # Bootloader 工程
    ├── BMS_program_stm32F407ZGT6/     # BMS App（F407 版本）
    ├── BOOT_LOADER/                   # Bootloader 主工程（STM32F407ZGT6）
    │   ├── Core/                      # 核心代码（main, gpio, usart, spi, i2c）
    │   ├── Drivers/                   # STM32 HAL 库
    │   ├── MDK-ARM/                   # Keil MDK 工程文件
    │   ├── interface/                 # 外设接口封装
    │   │   ├── int_bootloader.c / .h  # Bootloader 核心逻辑
    │   │   ├── int_w25q128.c / .h     # W25Q128 SPI Flash 驱动
    │   │   └── int_w24c02.c / .h      # AT24C02 I²C EEPROM 驱动
    │   └── P02_boot_loader.ioc        # STM32CubeMX 配置文件
    ├── CAN_reserve/                   # CAN 总线升级预留工程
    ├── Host_computer/                 # Bootloader 上位机工具
    └── Reset/                         # 复位相关工程
```

---

## 🛠️ 硬件平台

| 模块 | 型号 |
|------|------|
| BMS 主控（应用） | STM32F103C8T6 |
| Bootloader 主控 | STM32F407ZGT6 |
| 电池管理 AFE | TI BQ76940 |
| SPI NOR Flash | W25Q128（16MB） |
| I²C EEPROM | AT24C02 / W24C02（2Kbit） |

---

## 💻 开发环境

| 工具 | 版本要求 |
|------|----------|
| IDE | Keil MDK-ARM（uVision5 或以上）|
| 代码生成 | STM32CubeMX |
| 编译工具链 | ARM GCC / AC5 / AC6 |
| 上位机 | Python 3.x（依赖 `pyserial`）|
| 调试器 | ST-Link V2 / J-Link |

---

## 🚀 快速开始

### 1. 克隆仓库

```bash
git clone https://github.com/LINHUL919/BOOT_LOADER-BMS.git
```

### 2. 编译 BMS 主程序

1. 使用 Keil MDK 打开 `BMS_program _stm32F103c8t6/BMS_HAL/MDK-ARM/` 下的 `.uvprojx` 工程文件。
2. 确认目标芯片为 **STM32F103C8T6**。
3. 点击 **Build** 编译，生成 `.hex` / `.bin` 文件。
4. 通过 ST-Link 烧录至目标板。

### 3. 编译 Bootloader

1. 使用 Keil MDK 打开 `BMS_Bootloader/BOOT_LOADER/MDK-ARM/` 下的 `.uvprojx` 工程文件。
2. 确认目标芯片为 **STM32F407ZGT6**。
3. 编译后烧录至目标板 Bootloader 区域（通常为 `0x08000000`）。

### 4. Flash 分区规划（参考）

| 区域 | 起始地址 | 说明 |
|------|----------|------|
| Bootloader | `0x08000000` | 系统启动后首先运行 |
| App 区 | `0x08020000` | BMS 主程序存放区 |
| 参数存储 | Flash 尾部 | SOC、保护参数掉电保存 |

> ⚠️ 实际地址以工程 Scatter / Linker 文件配置为准。

### 5. 运行上位机

```bash
cd "BMS_program _stm32F103c8t6"
pip install pyserial
python bms_upper_computer.py
# 或直接双击运行 BMS上位机.bat
```

---

## 🔄 Bootloader 升级流程

```
上位机工具
    │
    │  ① 发送升级指令 + 固件数据（UART）
    ▼
Bootloader（STM32F407ZGT6）
    │
    │  ② 接收固件 → 写入 W25Q128 SPI Flash
    │  ③ 在 AT24C02 EEPROM 中写入升级标志
    │  ④ 系统复位
    ▼
Bootloader 重启
    │
    │  ⑤ 检测到升级标志 → 从 W25Q128 读出固件
    │  ⑥ 写入内部 Flash App 区
    │  ⑦ 清除升级标志
    ▼
跳转执行新 App
```

---

## 📡 通信协议（UART）

BMS 主程序与上位机通过 UART 进行通信，数据帧格式（参考）：

| 字段 | 帧头 | 命令字 | 数据长度 | 数据域 | 校验 | 帧尾 |
|------|------|--------|----------|--------|------|------|
| 说明 | `0xAA` | 1 Byte | 1 Byte | N Bytes | CRC/SUM | `0x55` |

> 📝 具体协议以源码 `bq76940.c` 及 `bms_upper_computer.py` 中的实现为准。

---

## 📊 SOC 估算说明

本项目 SOC 估算模块（`bms_soc.c`）支持以下方式：

- **安时积分法（Coulomb Counting）**：实时积分电流，适合放电过程动态跟踪。
- **OCV 查表法（Open Circuit Voltage）**：上电或静置后通过开路电压反查 SOC 表格进行校正。
- **掉电保存**：当前 SOC 值在关机时写入 Flash，下次开机直接恢复，避免重新估算。

---

## ⚙️ 保护策略一览

| 保护类型 | 触发条件 | 恢复方式 |
|----------|----------|----------|
| 单节过压（OVP） | 单节电压 > 阈值 | 电压降至恢复值 |
| 单节欠压（UVP） | 单节电压 < 阈值 | 电压升至恢复值 |
| 整组过压 | 组端电压 > 阈值 | 自动 / 手动复位 |
| 过流放电（OCD） | 放电电流 > 阈值 | 自动延时恢复 |
| 过流充电（OCC） | 充电电流 > 阈值 | 自动延时恢复 |
| 短路保护（SCP） | 电流突变超限 | 手动复位 |
| 过温保护（OTP） | NTC 温度 > 阈值 | 温度降至恢复值 |

> 各阈值可在 `bq76940.h` 宏定义中修改。

---

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

1. Fork 本仓库
2. 创建特性分支：`git checkout -b feature/your-feature`
3. 提交修改：`git commit -m 'Add some feature'`
4. 推送分支：`git push origin feature/your-feature`
5. 提交 Pull Request

---

## 📄 许可证

本项目暂未添加开源许可证，如需使用请联系仓库作者。

---

## 📬 联系作者

- GitHub：[@LINHUL919](https://github.com/LINHUL919)

---

<p align="center">⭐ 如果本项目对你有帮助，欢迎 Star 支持！⭐</p>
