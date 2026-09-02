# UV-K1 SAR-TEAM 移植记录

UV-K1 使用 `PY32F071`，不能刷入本项目现有的 K5/K6 `DP32G030` 固件。K1 版本必须基于独立的 PY32F071 上游移植、编译和真机验收，并使用明确带 `k1` 的文件名。

## 已识别测试机

- USB 设备：`QUANSHENG K1 CPS`（VID `0x36B7`，PID `0x0001`）；
- 原厂固件：`7.02.02`；
- EEPROM：8,192 字节；
- 校准区：`0x1E00–0x1FFF`，512 字节；
- MCU/目标：UV-K1 / PY32F071；
- USB-C 完整 EEPROM 连续读取结果逐字节一致。

单机 EEPROM 和校准文件保存在本地 `backups/`，该目录被 Git 忽略，严禁将设备私有数据提交到公开或局域网仓库。

## 连接方式

- UV-K1：使用 USB-A→USB-C 数据线。已验证 macOS 设备名类似 `/dev/cu.usbmodem20250808011`；部分 K1 不兼容 USB-C→USB-C。
- UV-K6：使用 Kenwood/泉盛双针编程线，设备名类似 `/dev/cu.usbserial-0001`。
- 当前 CP2102 双针线连接 K1 时仅出现发送回显、收不到电台回复，因此不能用于 K1 备份或刷写。

## 安全要求

1. 第一次写入前必须保存并复读验证完整 EEPROM 和独立校准区；
2. 必须在 DFU 模式只读识别 Bootloader，并确认目标为 K1；
3. K1 与 K5/K6 二进制禁止交叉刷写；
4. 先移植接收和 SAR，再启用低功率 ARTS 发射；
5. 语音、DCS 023N、低电保护、60 秒 PTT、EXIT 和恢复原厂能力全部通过后才能发布。

K1 移植基线采用 `armel/uv-k1-k5v3-firmware-custom` 的 PY32F071 代码；当前审计基线为上游 `v5.7.0`。
