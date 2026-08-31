# UV-K5 SAR-TEAM

[中文](#中文说明) | [English](#english)

Open-source SAR direction-finding and Yaesu ARTS-compatible team-safety firmware for Quansheng UV-K5/UV-K6 radios.

> **Safety notice / 安全提示:** This is experimental amateur-radio firmware, not certified life-safety equipment. Never use it as the only means of communication, navigation, or rescue coordination. 本项目是实验性业余无线电固件，不是经过认证的生命安全设备，不能作为通信、导航或救援协调的唯一手段。

## 中文说明

### 项目简介

UV-K5 SAR-TEAM 为泉盛 UV-K5、UV-K6 及兼容 DP32G030 机型提供两套现场功能：

- `F + 5`：SAR 无线电测向，显示相对信号强度、dBm、趋势、峰值和自动增益；
- `F + 6`：TEAM LINK，与 Yaesu VX-6R、VX-7R、VX-8R 的 ARTS/DCS 023N 轮询兼容，同时保留正常语音收发。

公开固件不包含任何个人呼号。每台电台的呼号和 ARTS 默认参数通过配置脚本写入 EEPROM，适合救援队批量部署。

### 已实现功能

- SAR 实时强度、趋势、峰值、`CAL/TARGET/WAIT/LOST/TOO CLOSE` 状态；
- HI/LOW/MIN 自动接收增益；
- 可变音调强度提示和智能语音监听；
- SAR 中锁定 PTT，避免测向时误发射；
- Yaesu ARTS 兼容的 DCS 023N 接收和低功率自动轮询；
- `LINK OK / LINK WEAK / LINK LOST`、RSSI 和最后接收时间；
- 失联三连低音和恢复双高音；
- TEAM 内正常接收语音和 PTT 语音发射；
- 可选 CW 呼号，每 10 分钟识别一次；
- TEAM 内长按 `F` 锁定配置键，PTT 和 `EXIT` 始终可用；
- 独立 EEPROM 配置、CRC-8 校验、自动备份和写后复读验证；
- CSV 名册逐台批量配置。

### 支持范围

- 目标硬件：Quansheng UV-K5、UV-K6 和兼容 DP32G030 机型；
- 已测试 Bootloader：UV-K6 `3.00.02`；
- 已完成互通测试：Yaesu VX-6R、VX-7R、VX-8R；
- ARTS 使用 DCS 023N；
- 不要刷入 PY32F030/PY32F071 等不同 MCU 的设备。

### 构建

需要 Docker Desktop：

```sh
./compile-with-docker.sh sarteam
```

输出文件：

```text
compiled-firmware/f4hwn.sar-team.packed.bin
```

公开构建不接受编译时呼号，避免批量设备误用开发者呼号。

### 刷机

1. 关闭电台并插紧编程线；
2. 按住 PTT 开机，进入 Bootloader；
3. 使用支持 Protocol V2 的 K5TOOL 写入 packed 固件；
4. 只有 CRC 通过、所有区块为 `OK` 且最终显示 `Done` 才视为成功；
5. 关闭电台，退出 Bootloader 后正常开机。

刷写期间不要断电、拔线或让计算机休眠。务必保留原厂固件和 EEPROM 备份。

### 配置呼号与 ARTS

配置时电台必须正常开机，不能处于 Bootloader：

```sh
python3 -m pip install pyserial

python3 tools/team_config.py \
  --port /dev/cu.usbserial-0001 configure \
  --callsign BH1ABC \
  --interval 25 \
  --power 3 \
  --alerts on \
  --cw off
```

Linux 串口通常是 `/dev/ttyUSB0`。查看当前配置：

```sh
python3 tools/team_config.py --port /dev/ttyUSB0 show
```

配置项：呼号（3–6 个大写字母或数字）、15/25 秒间隔、P1/P2/P3、提示音和 CW 默认状态。自动发射不能配置为默认开启，操作员每次进入 TEAM 后仍必须按 `3` 明确启用。

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
  --port /dev/ttyUSB0 batch \
  --csv tools/team-roster.example.csv
```

脚本仅写 EEPROM `0x1FF8–0x1FFF` 的 8 字节 SAR-TEAM 配置区。每台设备写入前保存旧值，写入后必须复读一致才显示 `Verified`。

### TEAM 按键

- `1`：BEEP/MUTE；
- `2`：15/25 秒；
- `3`：开启/关闭 AUTO TX；
- `4`：P1/P2/P3；
- `5`：开启/关闭 CW；
- 长按 `F`：锁定/解锁数字配置键；
- PTT：正常语音发射并携带 DCS 023N；
- `EXIT`：立即停止并退出。

## English

### Overview

UV-K5 SAR-TEAM adds two field-oriented modes to Quansheng UV-K5, UV-K6, and compatible DP32G030 radios:

- `F + 5`: SAR radio direction finding with relative strength, dBm, trend, peak hold, and automatic gain control;
- `F + 6`: TEAM LINK, compatible with Yaesu VX-6R/VX-7R/VX-8R ARTS polling using DCS 023N while retaining normal voice receive and PTT transmit.

The public firmware contains no personal callsign. Callsigns and ARTS defaults are stored per radio in EEPROM with a standalone configuration tool, making the project suitable for team deployments.

### Features

- Live SAR strength, trend, peak, and `CAL/TARGET/WAIT/LOST/TOO CLOSE` states;
- automatic HI/LOW/MIN receiver gain;
- variable-pitch strength cues and timed voice monitoring;
- PTT lockout in SAR mode to prevent accidental transmission;
- Yaesu ARTS-compatible DCS 023N receive and low-power polling;
- `LINK OK / LINK WEAK / LINK LOST`, RSSI, and last-heard timer;
- three-tone lost-link alarm and two-tone recovery alert;
- normal voice RX and manual PTT voice TX inside TEAM mode;
- optional CW callsign identification every ten minutes;
- long-press `F` keypad lock in TEAM mode, with PTT and `EXIT` always available;
- CRC-8 protected EEPROM configuration with automatic backup and read-back verification;
- CSV roster mode for configuring multiple radios.

### Compatibility

- Target: Quansheng UV-K5, UV-K6, and compatible DP32G030 radios;
- tested bootloader: UV-K6 `3.00.02`;
- tested peer radios: Yaesu VX-6R, VX-7R, and VX-8R;
- ARTS signaling: DCS 023N;
- do not flash devices based on a different MCU such as PY32F030/PY32F071.

### Build

Docker Desktop is required:

```sh
./compile-with-docker.sh sarteam
```

Output:

```text
compiled-firmware/f4hwn.sar-team.packed.bin
```

Public builds do not accept a compile-time callsign, preventing a developer identity from being copied to every radio.

### Flashing

1. Power off the radio and fully insert the programming cable.
2. Hold PTT while powering on to enter the bootloader.
3. Flash the packed image with a Protocol V2-compatible K5TOOL.
4. Treat the write as successful only when CRC passes, every block reports `OK`, and the tool ends with `Done`.
5. Power off, leave bootloader mode, and start the radio normally.

Do not remove power or the cable during flashing. Keep verified stock-firmware and EEPROM backups.

### Configure callsign and ARTS defaults

The radio must be running normally, not in bootloader mode:

```sh
python3 -m pip install pyserial

python3 tools/team_config.py \
  --port /dev/ttyUSB0 configure \
  --callsign BH1ABC \
  --interval 25 \
  --power 3 \
  --alerts on \
  --cw off
```

On macOS, the port is commonly `/dev/cu.usbserial-0001`. Read the current configuration with:

```sh
python3 tools/team_config.py --port /dev/cu.usbserial-0001 show
```

Configurable values are a 3–6 character callsign, 15/25-second interval, P1/P2/P3, alerts, and initial CW state. Automatic transmission cannot be a persistent default; the operator must explicitly press `3` after entering TEAM mode.

### Batch deployment

Copy and edit `tools/team-roster.example.csv`, then connect each normally powered radio when prompted:

```sh
python3 tools/team_config.py \
  --port /dev/ttyUSB0 batch \
  --csv tools/team-roster.example.csv
```

The tool writes only the eight-byte SAR-TEAM block at EEPROM `0x1FF8–0x1FFF`. It backs up the previous value and reports `Verified` only after an exact read-back match.

### TEAM controls

- `1`: BEEP/MUTE;
- `2`: 15/25-second interval;
- `3`: enable/disable AUTO TX;
- `4`: P1/P2/P3;
- `5`: enable/disable CW;
- long-press `F`: lock/unlock configuration keys;
- PTT: normal voice transmission with DCS 023N;
- `EXIT`: stop immediately and leave TEAM mode.

## License and acknowledgements / 许可证与致谢

Licensed under the Apache License 2.0. This project builds on work by the Quansheng open-source community, including F4HWN, Egzumer, Fagci, OneOfEleven, and DualTachyon. Preserve upstream copyright and license notices when redistributing.

本项目采用 Apache License 2.0，并基于 F4HWN、Egzumer、Fagci、OneOfEleven、DualTachyon 等泉盛开源社区贡献。再发布时必须保留上游版权和许可证声明。
