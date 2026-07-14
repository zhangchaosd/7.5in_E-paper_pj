# 7.5 英寸墨水屏 ESP32 P0 硬件自检

这是新固件的第一阶段工程。它只验证 ESP32、三个用户键、SHT30、电池 ADC 和屏幕基本驱动，不包含 Wi-Fi、Bridge 或产品 UI。

## 安全约束

- 在备份完整 8MB 原机 Flash 前，不执行上传。
- 固件启动时不会初始化或刷新墨水屏。
- 屏幕测试必须从串口依次输入 `EPD TEST` 和 `CONFIRM`。
- 每次启动最多执行一次屏幕测试；异常后锁定到下次重启。
- 只使用 `GxEPD2_750c_Z08`、4MHz SPI 和全屏刷新，不启用 GDEY 快刷或自定义 LUT。
- 刷屏期间板上会产生墨水屏正负高压，不要带电短接或触碰电荷泵区域。

## 已核对的引脚

| 功能 | GPIO | 说明 |
|---|---:|---|
| EPD SCK / MOSI | 18 / 23 | MISO 不使用 |
| EPD CS / DC | 5 / 19 | GPIO5 是启动绑带脚，启动后尽早拉高 CS |
| EPD RST / BUSY | 16 / 17 | 不启用 PSRAM |
| I2C SDA / SCL | 21 / 22 | SHT30 地址 0x44 |
| 外设使能 | 25 | 高电平接通 SHT30 地和电池采样，平时保持低 |
| 电池 ADC | 36 | 10k/10k 分压，初始换算约为 ADC 电压的 2 倍 |
| KEY1 / KEY2 / KEY3 | 34 / 35 / 39 | 外部 10k 上拉，按下为低；只能设为 `INPUT` |

第四个白色按键接 ESP32 `EN`，是硬件复位键，软件无法读取。GPIO0 由 CH340C 自动下载电路控制。

最终用户视角下接口和按键均在右侧，从上到下为 USB-C、电源开关、`EN`、`GPIO34`、`GPIO35`、`GPIO39`。

## 1. 安装工具链

P0 与产品固件使用同一套 Arduino ESP32 Core 3.3.10、GxEPD2 1.6.9、Adafruit GFX 和 BusIO。先按 [`../epaper75-app/tools/setup_arduino.ps1`](../epaper75-app/tools/setup_arduino.ps1) 或 macOS 的 `setup_arduino.sh` 安装固定版本。

P0 目录中不再提供 PlatformIO 环境。旧 PlatformIO 平台使用 Arduino core 2.0.17，与实机验证的 3.3.10 不等价，而且通用 upload 会破坏 app0-only 安全边界。

## 2. 连接设备并备份

先确认设备管理器出现 CH340 串口。Windows 可使用 Arduino ESP32 Core 自带的 esptool 读取完整 8 MB：

```powershell
.\tools\backup_flash_local.ps1 -Port <COM端口>
```

macOS/Linux：

```bash
./tools/backup_flash.sh --port /dev/cu.<device>
```

脚本会生成 8,388,608 字节备份，并把 SHA-256 追加到私密目录的 `SHA256SUMS.txt`。至少独立读取两次并保存到私密离线位置；备份含 NVS，禁止提交 Git。可用 `EPAPER75_BACKUP_DIR` 或脚本参数把备份放到仓库外。

## 3. 编译

Arduino CLI：

```powershell
.\tools\build_arduino.ps1
```

脚本把临时构建目录放在纯英文的 `%TEMP%` 下；ESP32 的 Windows 汇编器无法可靠地在当前中文路径中创建对象文件。

## 4. 串口自检

P0 是历史诊断固件，不提供通用上传快捷命令。确需重新写入时，必须像产品固件一样经过两份备份校验、镜像检查，并且只写 app0 `0x10000`；禁止 Arduino IDE/PlatformIO Upload。

串口命令：

- `STATUS`：芯片、按键、温湿度和电池快照
- `KEYS`：读取三个用户键
- `SENSOR`：给 SHT30 临时上电并校验 CRC
- `BATTERY`：临时接通分压并读取 GPIO36
- `EPD TEST`：预备屏幕测试
- `CONFIRM`：15 秒内确认，执行白底、黑块、红块三次保守全刷

电池换算尚未标定，应与万用表读数比较后再修改 `kBatteryDividerRatio`。

## 当前实机状态

- CH340 和自动下载电路工作正常。
- 已确认 ESP32-D0WD-V3 revision 3.1、8MB Flash、0 PSRAM。
- 两次独立 8 MB 备份已完成并按分区比较通过；公开摘要见 [`P0_RESULTS.md`](P0_RESULTS.md)，私密备份和 manifest 不进入 Git。
- P0 应用只写入活动 app0；bootloader、分区、NVS、备用 app1 与 SPIFFS 未动。
- 三次保守全刷均约 21 秒，BUSY 正常，结果见 `P0_RESULTS.md`。
