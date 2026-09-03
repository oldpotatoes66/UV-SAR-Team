# UV-K5 / UV-K6 / UV-K1 SAR-TEAM

[中文](#中文说明) | [English](#english)

Open-source SAR direction-finding and Yaesu ARTS-compatible team-safety firmware for Quansheng UV-K5, UV-K6, and UV-K1 radios.

> **Safety notice / 安全提示:** This is experimental amateur-radio firmware, not certified life-safety equipment. Never use it as the only means of communication, navigation, or rescue coordination. 本项目是实验性业余无线电固件，不是经过认证的生命安全设备，不能作为通信、导航或救援协调的唯一手段。

## 中文说明

### 项目简介

SAR-TEAM 为泉盛 UV-K5、UV-K6（DP32G030）和 UV-K1（PY32F071）提供两套现场功能。两类 MCU 使用不同固件，禁止交叉刷写：

- `F + 5`：SAR 无线电测向，显示相对信号强度、dBm、趋势、峰值和自动增益；
- 长按数字 `6`：进入 TEAM LINK；`F + 6` 保留为发射功率切换。TEAM 与 Yaesu VX-6R、VX-7R、VX-8R 的 ARTS/DCS 023N 轮询兼容，同时保留正常语音收发。

公开固件不会把作者呼号当作队员呼号。版本署名为 `BH1JID`，项目联系邮箱为 `oldpotatoes66@gmail.com`；每台电台用于 CW 识别的呼号和 ARTS 默认参数仍由配置脚本独立写入 EEPROM。

### 已实现功能

完整的双语机型差异和按键矩阵见 [`docs/FEATURES.md`](docs/FEATURES.md)。

- SAR 实时强度、趋势、峰值、`CAL/TARGET/WAIT/LOST/TOO CLOSE` 状态；
- HI/LOW/MIN 自动接收增益；
- 可变音调强度提示和智能语音监听；
- SAR 中锁定 PTT，避免测向时误发射；
- Yaesu ARTS 兼容的 DCS 023N 接收和低功率自动轮询；
- `LINK OK / LINK WEAK / LINK LOST`、RSSI 和最后接收时间；
- 失联三连低音和恢复双高音；
- 失联状态持续存在时每 60 秒重复三连低音告警；
- TEAM 内正常接收语音和 PTT 语音发射；
- 屏幕显示电量百分比，低电量时禁止人工、自动轮询和 CW 发射；
- 人工 PTT 最长 60 秒，超时后必须松开 PTT 才能再次发射；
- 数字配置键显示约 1 秒的大字确认，长按 `3` 才能开启 AUTO TX；
- 可选 CW 呼号，每 10 分钟识别一次；
- TEAM 内长按 `F` 锁定配置键，PTT 和 `EXIT` 始终可用；
- 独立 EEPROM 配置、CRC-8 校验、自动备份和写后复读验证；
- CSV 名册逐台批量配置。
- K1 `F+6` 实际调整并保存当前发送 VFO 功率，显示约 1.5 秒大字确认；待机画面的 A/B 两行持续显示各自 `L1–L5 / M / H`；
- 通用频道工具支持 K6/K1 导出、导入、写后验证和备份后裁剪历史频道。

### 支持范围

- K5/K6 固件：Quansheng UV-K5、UV-K6 和兼容 DP32G030 机型；
- K1 固件：Quansheng UV-K1 / PY32F071；
- 已测试 Bootloader：UV-K6 `3.00.02`、UV-K1 `7.02.02`；
- 已完成互通测试：Yaesu VX-6R、VX-7R、VX-8R；
- ARTS 使用 DCS 023N；
- K5/K6 与 K1 二进制绝不能交叉刷写；PY32F030 仍不受支持。

### 构建

### 直接下载固件

每个发布提交都在 [`release/latest`](release/latest) 中附带两种机型可直接刷入的固件和 `SHA256SUMS`：

- `SAR-TEAM-K5-K6-DP32G030.packed.bin`：UV-K5 / UV-K6；
- `SAR-TEAM-K1-PY32F071.bin`：UV-K1。

两种固件不可交叉刷写。发布前使用 `tools/package-release.sh` 从本次源码构建结果更新发布包，然后将固件、校验值和源码改动放在同一提交中。

### 网页在线刷机

可以打开 **[SAR-TEAM Web Flasher](https://sar.trailspud.com/flash)**，直接在网页中选择机型并刷入固件，无需安装桌面刷机软件。

频道、呼号和 ARTS 参数统一使用 **[SAR-TEAM Web Config](https://sar.trailspud.com/config)** 管理。网站源码集中维护在 `Website-SAR` 项目中，本固件仓库不再重复实现网页工具。

- 使用桌面版 Chrome、Edge 或 Opera；网页刷机依赖 Web Serial，Safari、Firefox 和手机浏览器不支持；
- K5/K6 选择 `DP32G030` 固件，K1 选择 `PY32F071` 固件，严禁交叉刷写；
- 固件文件与串口数据只在本机浏览器中处理；
- 刷机前确认机型和 Bootloader，保留原厂固件及 EEPROM 备份；
- 刷写期间不要断电、拔线或让计算机休眠。

网站首页及完整使用说明：[`https://sar.trailspud.com/`](https://sar.trailspud.com/)

需要 Docker Desktop。构建 K5/K6 DP32G030 固件：

```sh
./compile-with-docker.sh sarteam
```

输出文件：

```text
compiled-firmware/f4hwn.sar-team.packed.bin
```

构建 K1 PY32F071 固件：

```sh
cd k1-firmware
./compile-with-docker.sh SarTeam
```

输出文件：`k1-firmware/build/SarTeam/sar-team-k1.bin`。

版本画面显示作者署名 `BH1JID`。它不是设备的 CW/队员呼号，不会替代配置脚本写入的每机呼号。

### 刷机

K5/K6（双针编程线）：

1. 关闭电台并插紧编程线；
2. 按住 PTT 开机，进入 Bootloader；
3. 使用支持 Protocol V2 的 K5TOOL 写入 packed 固件；
4. 只有 CRC 通过、所有区块为 `OK` 且最终显示 `Done` 才视为成功；
5. 关闭电台，退出 Bootloader 后正常开机。

刷写期间不要断电、拔线或让计算机休眠。务必保留原厂固件和 EEPROM 备份。

K1（USB）：使用 USB-A→USB-C 数据线；部分机器不能通过 USB-C→USB-C 建立串口。关机并连接 USB，按住 PTT 开机进入黑屏 DFU；不要按侧键组合，否则可能进入 `AIR COPY`。确认 Bootloader 为 `7.02.02` 后只刷入 `sar-team-k1.bin`。正常开机时 K1 会显示 USB 串口，可用于配置呼号。

### 配置呼号与 ARTS

配置时电台必须正常开机，不能处于 Bootloader：

```sh
python3 -m pip install pyserial

python3 tools/team_config.py \
  --port /dev/cu.usbserial-0001 --model k5 configure \
  --callsign BH1ABC \
  --interval 25 \
  --power 3 \
  --alerts on \
  --cw off
```

Linux 串口通常是 `/dev/ttyUSB0`。查看当前配置：

```sh
python3 tools/team_config.py --port /dev/ttyUSB0 --model k5 show
```

K1 示例（串口名以本机实际枚举结果为准）：

```sh
python3 tools/team_config.py \
  --port /dev/cu.usbmodem20221234561 --model k1 show
```

配置项：呼号（3–6 个大写字母或数字）、15/25 秒间隔、功率字段、提示音和 CW 默认状态。K5/K6 使用配置中的 P1/P2/P3；K1 为保持 CSV 兼容会保存该字段，但运行时使用当前频道 `L1–L5` 功率。自动发射不能配置为默认开启，操作员每次进入 TEAM 后仍必须长按 `3` 约 1 秒明确启用。

### 批量部署

复制并编辑 `tools/team-roster.example.csv`：

```csv
Label,Callsign,Interval,Power,Alerts,CW
Team-01,BH1AAA,25,3,on,off
Team-02,BH1AAB,25,3,on,off
```

逐台正常开机并连接：

```sh
python3 tools/team_config.py \
  --port /dev/ttyUSB0 --model k5 batch \
  --csv tools/team-roster.example.csv
```

脚本只写对应机型的 8 字节 SAR-TEAM 配置区：K5/K6 为 `0x1FF8–0x1FFF`；K1 为虚拟 EEPROM `0xD000–0xD007`，固件映射到独立物理 Flash 扇区 `0x012000`，不占用校准区。每台设备写入前保存旧值，写入后必须复读一致才显示 `Verified`。批量配置同一批次必须使用相同 `--model`，混合机型应分批执行。

### K6/K1 批量频道

`tools/channel_config.py` 使用一份通用 CSV 在 K6 与 K1 之间迁移前 200 个存储频道。电台必须正常开机；K6 必须运行包含“频道区串口支持”的新版 SAR-TEAM 固件。

先从 K6 导出已使用的频道：

```sh
python3 tools/channel_config.py \
  --port /dev/cu.usbserial-0001 --model k6 export \
  --csv team-channels.csv
```

再将同一文件写入 K1：

```sh
python3 tools/channel_config.py \
  --port /dev/cu.usbmodem20221234561 --model k1 import \
  --csv team-channels.csv
```

工具会转换两种机型不同的频道名称和扫描列表属性，保留频道号、频率、收发偏移、模拟/DCS 亚音、模式、功率、步进及锁定标志。写入前在 `channel-backups/` 备份目标机全部频道区，逐频道写后复读校验；CSV 未列出的频道保持不变。串口权限只覆盖频道记录、名称和属性，不允许修改普通设置或校准区。

备份后删除编号高于 14 的历史频道：

```sh
python3 tools/channel_config.py \
  --port /dev/cu.usbmodem20221234561 --model k1 prune --keep 14
```

`--keep` 可按任务需要更改。K1 会同时清理完整 1024 槽属性区，避免已删除记录仍被菜单计为有效频道。

### TEAM 按键

- `1`：BEEP/MUTE；
- `2`：15/25 秒；
- 长按 `3` 约 1 秒：开启/关闭 AUTO TX（短按无效）；
- `4`：仅 K5/K6 使用，切换 P1/P2/P3；K1 的 AUTO TX 使用当前频道 `L1–L5` 功率；
- `5`：开启/关闭 CW；
- 长按 `F`：锁定/解锁数字配置键；
- PTT：正常语音发射并携带 DCS 023N；
- `EXIT`：立即停止并退出。

K1 若处于 `M/H`，AUTO TX 会被安全阻止。退出 TEAM，用 `F+6` 选择 `L1–L5` 后重新进入。K5/K6 则使用 TEAM 内独立的 P1/P2/P3。

## English

### Overview

SAR-TEAM adds two field-oriented modes to Quansheng UV-K5/UV-K6 (DP32G030) and UV-K1 (PY32F071) radios. The MCU families require separate binaries and must never be cross-flashed:

- `F + 5`: SAR radio direction finding with relative strength, dBm, trend, peak hold, and automatic gain control;
- Hold digit `6`: enter TEAM LINK; `F + 6` remains the transmit-power shortcut. TEAM is compatible with Yaesu VX-6R/VX-7R/VX-8R ARTS polling using DCS 023N while retaining normal voice receive and PTT transmit.

The public firmware never uses the author's callsign as a team member's identity. Release metadata credits `BH1JID` and lists `oldpotatoes66@gmail.com`; each radio's CW callsign and ARTS defaults remain independently stored in EEPROM by the configuration tool.

### Features

See [`docs/FEATURES.md`](docs/FEATURES.md) for the canonical bilingual model and control matrix.

- Live SAR strength, trend, peak, and `CAL/TARGET/WAIT/LOST/TOO CLOSE` states;
- automatic HI/LOW/MIN receiver gain;
- variable-pitch strength cues and timed voice monitoring;
- PTT lockout in SAR mode to prevent accidental transmission;
- Yaesu ARTS-compatible DCS 023N receive and low-power polling;
- `LINK OK / LINK WEAK / LINK LOST`, RSSI, and last-heard timer;
- three-tone lost-link alarm and two-tone recovery alert;
- repeated three-tone lost-link alarm every 60 seconds while the link remains lost;
- normal voice RX and manual PTT voice TX inside TEAM mode;
- battery percentage display and low-battery lockout for manual, automatic-poll, and CW transmission;
- 60-second manual PTT timeout; PTT must be released before transmitting again;
- one-second key confirmations and long-press `3` protection for AUTO TX;
- optional CW callsign identification every ten minutes;
- long-press `F` keypad lock in TEAM mode, with PTT and `EXIT` always available;
- CRC-8 protected EEPROM configuration with automatic backup and read-back verification;
- CSV roster mode for configuring multiple radios.
- K1 `F+6` changes and saves actual TX power, shows a 1.5-second confirmation, and permanently displays independent A/B `L1–L5 / M / H` values;
- common K6/K1 channel export, import, verified write, backup, and history-pruning support.

### Compatibility

- K5/K6 image: Quansheng UV-K5, UV-K6, and compatible DP32G030 radios;
- K1 image: Quansheng UV-K1 with PY32F071;
- tested bootloaders: UV-K6 `3.00.02`, UV-K1 `7.02.02`;
- tested peer radios: Yaesu VX-6R, VX-7R, and VX-8R;
- ARTS signaling: DCS 023N;
- never cross-flash K5/K6 and K1 images; PY32F030 remains unsupported.

### Build

### Ready-to-flash downloads

Every release commit includes both ready-to-flash images and `SHA256SUMS` in [`release/latest`](release/latest):

- `SAR-TEAM-K5-K6-DP32G030.packed.bin`: UV-K5 / UV-K6;
- `SAR-TEAM-K1-PY32F071.bin`: UV-K1.

Never cross-flash these images. Before a release, run `tools/package-release.sh` after both builds and commit the images, checksums, and matching source changes together. `BUILD-INFO.txt` records the exact source commit used for the binaries.

### Web flashing

Open the **[SAR-TEAM Web Flasher](https://sar.trailspud.com/flash)** to select the radio model and flash the firmware directly from a web browser without installing a desktop flashing application.

Use **[SAR-TEAM Web Config](https://sar.trailspud.com/config)** for channel, callsign, and ARTS settings. The website source is maintained centrally in the `Website-SAR` project; this firmware repository does not duplicate the web tools.

- Use desktop Chrome, Edge, or Opera. Web Serial is not supported by Safari, Firefox, or mobile browsers;
- select the `DP32G030` image for K5/K6 and the `PY32F071` image for K1—never cross-flash them;
- firmware files and serial data are processed locally in the browser;
- verify the radio model and bootloader, and keep the factory firmware and an EEPROM backup;
- do not disconnect power or the cable, or let the computer sleep, while flashing.

Website and full instructions: [`https://sar.trailspud.com/`](https://sar.trailspud.com/)

Docker Desktop is required. Build the K5/K6 DP32G030 image with:

```sh
./compile-with-docker.sh sarteam
```

Output:

```text
compiled-firmware/f4hwn.sar-team.packed.bin
```

Build the K1 PY32F071 image with:

```sh
cd k1-firmware
./compile-with-docker.sh SarTeam
```

Output: `k1-firmware/build/SarTeam/sar-team-k1.bin`.

The version screen credits `BH1JID`. This is release attribution, not the radio's CW/team callsign, and does not replace the per-radio identity written by the configuration tool.

### Flashing

K5/K6 (two-pin programming cable):

1. Power off the radio and fully insert the programming cable.
2. Hold PTT while powering on to enter the bootloader.
3. Flash the packed image with a Protocol V2-compatible K5TOOL.
4. Treat the write as successful only when CRC passes, every block reports `OK`, and the tool ends with `Done`.
5. Power off, leave bootloader mode, and start the radio normally.

Do not remove power or the cable during flashing. Keep verified stock-firmware and EEPROM backups.

K1 (USB): use a USB-A-to-USB-C data cable; some units do not enumerate with USB-C-to-USB-C. With the radio off and USB connected, hold PTT while powering on to enter the black-screen DFU mode. Do not use a side-key combination, which may enter `AIR COPY`. Confirm bootloader `7.02.02` and flash only `sar-team-k1.bin`. In normal operation, the K1 USB serial port can configure the callsign.

### Configure callsign and ARTS defaults

The radio must be running normally, not in bootloader mode:

```sh
python3 -m pip install pyserial

python3 tools/team_config.py \
  --port /dev/ttyUSB0 --model k5 configure \
  --callsign BH1ABC \
  --interval 25 \
  --power 3 \
  --alerts on \
  --cw off
```

On macOS, the port is commonly `/dev/cu.usbserial-0001`. Read the current configuration with:

```sh
python3 tools/team_config.py --port /dev/cu.usbserial-0001 --model k5 show
```

K1 example (use the port actually reported by your system):

```sh
python3 tools/team_config.py \
  --port /dev/cu.usbmodem20221234561 --model k1 show
```

Configurable values are a 3–6 character callsign, 15/25-second interval, power field, alerts, and initial CW state. K5/K6 use the configured P1/P2/P3 value. K1 stores the field for roster portability but uses the selected channel's `L1–L5` power at runtime. Automatic transmission cannot be a persistent default; the operator must explicitly hold `3` for about one second after entering TEAM mode.

### Batch deployment

Copy and edit `tools/team-roster.example.csv`, then connect each normally powered radio when prompted:

```sh
python3 tools/team_config.py \
  --port /dev/ttyUSB0 --model k5 batch \
  --csv tools/team-roster.example.csv
```

The tool writes only the model-specific eight-byte SAR-TEAM block: `0x1FF8–0x1FFF` on K5/K6, or virtual EEPROM `0xD000–0xD007` on K1. K1 firmware maps this to the dedicated physical Flash sector at `0x012000`, outside calibration data. It backs up the previous value and reports `Verified` only after an exact read-back match. Run mixed K5/K6 and K1 fleets as separate batches with the correct `--model`.

### K6/K1 batch channels

`tools/channel_config.py` uses one portable CSV to migrate the first 200 memory channels between K6 and K1 radios. Radios must be running normally, and the K6 must have the newer SAR-TEAM image with channel-only serial access.

Export occupied channels from the K6:

```sh
python3 tools/channel_config.py \
  --port /dev/cu.usbserial-0001 --model k6 export \
  --csv team-channels.csv
```

Import the same file into the K1:

```sh
python3 tools/channel_config.py \
  --port /dev/cu.usbmodem20221234561 --model k1 import \
  --csv team-channels.csv
```

The tool converts the different name and scan-list attributes while preserving slot number, frequency, duplex offset, analog/DCS tones, mode, power, tuning step, and lock flags. Before an import it saves the target radio's complete channel areas under `channel-backups/`, then reads back and verifies every written channel. Slots absent from the CSV remain unchanged. Serial access is restricted to channel records, names, and attributes; general settings and calibration are not writable.

To back up first and remove occupied history above channel 14:

```sh
python3 tools/channel_config.py \
  --port /dev/cu.usbmodem20221234561 --model k1 prune --keep 14
```

Choose a different `--keep` boundary as needed. On K1, pruning also clears the full 1024-slot attribute table so erased records cannot remain visible as valid channels.

### TEAM controls

- `1`: BEEP/MUTE;
- `2`: 15/25-second interval;
- hold `3` for about one second: enable/disable AUTO TX (a short press does nothing);
- `4`: K5/K6 only, P1/P2/P3; K1 AUTO TX uses the current channel's `L1–L5` power;
- `5`: enable/disable CW;
- long-press `F`: lock/unlock configuration keys;
- PTT: normal voice transmission with DCS 023N;
- `EXIT`: stop immediately and leave TEAM mode.

K1 blocks AUTO TX at `M/H`. Exit TEAM, select `L1–L5` with `F+6`, then re-enter. K5/K6 instead use the separate TEAM P1/P2/P3 setting.

## License and acknowledgements / 许可证与致谢

Maintainer / 维护者: **BH1JID** — `oldpotatoes66@gmail.com`

Licensed under the Apache License 2.0. This project builds on work by the Quansheng open-source community, including F4HWN, Egzumer, Fagci, OneOfEleven, and DualTachyon. Preserve upstream copyright and license notices when redistributing.

本项目采用 Apache License 2.0，并基于 F4HWN、Egzumer、Fagci、OneOfEleven、DualTachyon 等泉盛开源社区贡献。再发布时必须保留上游版权和许可证声明。
