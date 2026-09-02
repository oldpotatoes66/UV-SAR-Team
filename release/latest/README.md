# SAR-TEAM ready-to-flash firmware / 可直接刷入固件

The exact source commit and build time are recorded in `BUILD-INFO.txt`.

准确的源码提交和编译时间记录在 `BUILD-INFO.txt` 中。

| Radio / 电台 | MCU | Firmware / 固件 |
|---|---|---|
| UV-K5 / UV-K6 | DP32G030 | `SAR-TEAM-K5-K6-DP32G030.packed.bin` |
| UV-K1 | PY32F071 | `SAR-TEAM-K1-PY32F071.bin` |

**Never flash one model's binary onto the other model.** Verify the SHA-256 value in `SHA256SUMS` before flashing.

**严禁将两种机型的固件交叉刷写。** 刷机前请使用 `SHA256SUMS` 核对文件完整性。

- K5/K6: power off, connect the two-pin programming cable, hold PTT while powering on, and flash the `.packed.bin` file.
- K1: use a USB-A to USB-C data cable, power off, hold PTT while powering on to enter black-screen DFU, and flash the K1 `.bin` file.
- Do not disconnect power or the cable during flashing. Keep the factory firmware and an EEPROM backup.

- K5/K6：关机并连接双针编程线，按住 PTT 开机，然后刷入 `.packed.bin` 文件。
- K1：使用 USB-A 转 USB-C 数据线，关机后按住 PTT 开机进入黑屏 DFU，然后刷入 K1 `.bin` 文件。
- 刷机过程中不要断电或拔线，并保留原厂固件及 EEPROM 备份。
