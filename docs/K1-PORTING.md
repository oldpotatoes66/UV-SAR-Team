# UV-K1 SAR-TEAM 移植与使用记录

UV-K1 使用 `PY32F071`，现已完成独立的 SAR-TEAM 移植、构建、USB 刷写和真机功能回归。它不能使用 K5/K6 的 `DP32G030` 固件；两个 MCU 系列的二进制禁止交叉刷写。

## 已验证测试机与版本

- 机型/MCU：Quansheng UV-K1 / PY32F071；
- USB 设备：`QUANSHENG K1 CPS`（VID `0x36B7`，PID `0x0001`）；
- 已测试 Bootloader：`7.02.02`；
- 固件：`k1-firmware/build/SarTeam/sar-team-k1.bin`，大小 68,560 字节；
- SHA-256：`3b092e25e5b21047afea07f5e942cdf5a0815c90c1d40c0a631707d3ae28c20a`；
- 构建占用：RAM 62.70%，Flash 56.74%；
- 移植基线：`armel/uv-k1-k5v3-firmware-custom`，上游审计基线 `v5.7.0`。

上述哈希只对应本次已验证构建。源码变化后必须重新记录哈希并重新回归。

## 已验证功能

- `F + 5` 进入 SAR；长按数字 `6` 进入 TEAM/ARTS；`F + 6` 仅切换发射功率；
- DCS 023N ARTS 接收、低功率自动轮询和正常语音收发可同时工作；
- 失联重复三连低音、恢复双高音、长按 `F` 锁定配置键；
- 长按 `3` 开启 AUTO TX，默认关闭；自动轮询仅允许 FM、单工、合法发射频率及 L1–L5 低功率；
- 15/25 秒间隔、提示音、功率档、CW、60 秒人工 PTT 超时和低电量发射保护；
- 呼号可由脚本独立写入并复读验证，公开固件不写死队员呼号。

## USB 连接与 DFU

- 优先使用 USB-A→USB-C 数据线；实测部分 K1 使用 USB-C→USB-C 不会枚举串口。
- 正常开机串口可能类似 `/dev/cu.usbmodem20221234561`，用于读取或配置呼号。
- 关机并连接 USB，按住 PTT 开机进入黑屏 DFU。只按 PTT，不要同时按侧键；错误组合会进入 `AIR COPY`。
- DFU 串口可能类似 `/dev/cu.usbmodem20250808011`；端口编号会变化，以当前实际枚举为准。
- 双针音频编程线继续用于 K5/K6；当前测试中不能可靠用于 K1 备份或刷写。

## 构建

```sh
cd k1-firmware
./compile-with-docker.sh SarTeam
```

输出为 `k1-firmware/build/SarTeam/sar-team-k1.bin`。

## 呼号和 ARTS 默认配置

配置时让 K1 正常开机，不要进入 DFU：

```sh
python3 tools/team_config.py \
  --port /dev/cu.usbmodem20221234561 --model k1 show

python3 tools/team_config.py \
  --port /dev/cu.usbmodem20221234561 --model k1 configure \
  --callsign BH1ABC --interval 25 --power 3 --alerts on --cw off
```

K1 配置使用虚拟 EEPROM `0xD000–0xD007`。兼容层将它映射到独立物理 Flash 扇区 `0x012000`，不覆盖校准数据。脚本写入前自动备份，写后复读并验证 CRC-8；只有显示 `Verified` 才算成功。

## EEPROM 与备份说明

原厂通信协议可读取 8,192 字节 EEPROM，并把 `0x1E00–0x1FFF` 作为独立校准备份范围。SAR-TEAM 自定义固件内部的兼容 EEPROM 映射与原厂协议地址不是同一概念，不得用通用写入工具猜测物理地址。

单机 EEPROM、校准文件和呼号备份保存在本地 `backups/` 或 `team-config-backups/`，设备私有数据不应提交到公开仓库。

## 刷写与安全要求

1. 第一次写入前保存并复读验证原厂固件、完整 EEPROM 和校准数据；
2. 在 DFU 模式先识别 Bootloader，确认目标为 K1 / `7.02.02`；
3. 只刷 `sar-team-k1.bin`，绝不刷 K5/K6 packed 固件；
4. 刷写期间不得断电、拔线或让计算机休眠；
5. 刷后检查语音、SAR、ARTS、功率快捷键、锁键、PTT 超时、低电保护和 `EXIT`；
6. 固件不是生命安全设备，现场必须保留独立通信方案与原厂恢复能力。
