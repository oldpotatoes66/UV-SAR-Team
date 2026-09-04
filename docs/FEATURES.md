# SAR-TEAM 功能与操作矩阵 / Feature and Control Matrix

本文档是当前已验证功能的统一说明。K5/K6 使用 DP32G030 固件，K1 使用 PY32F071 固件；二进制绝不能交叉刷写。

This is the canonical list of currently verified behavior. K5/K6 use the DP32G030 image and K1 uses the PY32F071 image. Never cross-flash these binaries.

## 公共功能 / Common features

| 功能 / Feature | K5/K6 | K1 |
|---|---:|---:|
| `F+5` SAR 测向 / direction finding | ✓ | ✓ |
| 长按裸键 `6` 进入 TEAM / hold bare `6` for TEAM | ✓ | ✓ |
| `F+6` 调整当前发送 VFO 功率 / TX power | ✓ | ✓ |
| TEAM 中正常语音接收和 PTT 语音发射 | ✓ | ✓ |
| Yaesu ARTS、DCS 023N、15/25 秒轮询 | ✓ | ✓ |
| VX-6R / VX-7R / VX-8R 互通 | ✓ | ✓ |
| `LINK OK / WEAK / LOST`、RSSI、最后接收时间 | ✓ | ✓ |
| 失联三连低音、每 60 秒重复、恢复双高音 | ✓ | ✓ |
| 长按 `3` 约 1 秒开启 AUTO TX，进入时默认关闭 | ✓ | ✓ |
| 60 秒人工 PTT 超时，松开后才能再次发射 | ✓ | ✓ |
| 低电量显示及人工/AUTO/CW 发射保护 | ✓ | ✓ |
| 长按 `F` 锁定 TEAM 配置键，PTT/EXIT 保留 | ✓ | ✓ |
| EEPROM 呼号、CRC-8、备份和写后复读 | ✓ | ✓ |
| CSV 呼号名册和跨机型频道 CSV | ✓ | ✓ |

## 主界面快捷键 / Main-screen shortcuts

- `F+5`：进入 SAR 测向；SAR 内 PTT 不发射，只重置峰值。
- 长按裸键 `6`：进入 TEAM。不要先按 `F`。
- `F+6`：调整当前发送 VFO 的普通发射功率，不进入 TEAM。
- K1 会显示约 1.5 秒的 `TX POWER` 大字确认，并在 A、B 两个 VFO 行持续显示各自的 `L1–L5 / M / H`。

- `F+5`: enter SAR direction finding. PTT is transmit-inhibited in SAR and resets the peak only.
- Hold bare `6`: enter TEAM. Do not press `F` first.
- `F+6`: change normal transmit power for the selected TX VFO; it does not enter TEAM.
- K1 shows a roughly 1.5-second `TX POWER` confirmation and permanently shows each A/B VFO power as `L1–L5 / M / H`.

## TEAM 按键 / TEAM controls

| 按键 / Key | 行为 / Action |
|---|---|
| `1` | BEEP/MUTE |
| `2` | 15/25 秒轮询间隔 / poll interval |
| 长按 `3` / hold `3` | AUTO TX ON/OFF；短按无效 / short press is ignored |
| `4` | K5/K6：P1/P2/P3；K1：L1–L5/M/H / K5/K6 P1–P3; K1 L1–L5/M/H |
| `5` | CW ON/OFF；未配置呼号时拒绝开启 / requires configured callsign |
| 长按 `F` / hold `F` | 锁定/解锁数字配置键 / lock configuration keys |
| PTT | 带 DCS 023N 的正常语音发射 / normal voice TX with DCS 023N |
| `EXIT` | 立即停止并退出 / stop and exit immediately |

K1 的语音、AUTO TX 和 CW 均使用当前频道的实际功率。TEAM 内数字 `4` 与主界面 `F+6` 一样循环 `L1–L5/M/H`，并保存到当前频道。K5/K6 的 AUTO TX 使用 TEAM 内独立的 P1/P2/P3 功率。

K1 voice, AUTO TX, and CW use the channel's actual power. Key `4` in TEAM cycles `L1–L5/M/H`, like `F+6` on the main screen, and saves the setting to the current channel. K5/K6 use separate TEAM P1/P2/P3 power.

K1 v0.2 also matches the K5/K6 field-safety behavior: CW ID transmits only while AUTO TX is armed; `EXIT` aborts a poll, CW ID, voice TX, or alert; recent carrier activity postpones an automatic transmission; and the TEAM screen continuously shows interval, alert mode, power, battery, and `RF NO DCS` diagnostics.

K1 v0.5 keeps `PWR L1–L5/M/H` in a dedicated fixed position on the TEAM screen. The field reserves enough LCD width for the complete label, so the actual transmit power remains visible during RX ONLY, AUTO TX, link-state changes, countdowns, and key feedback.

K1 v0.6 uses the same non-overlapping TEAM information layout as K5/K6: callsign/battery, DCS/RSSI, persistent `PWR L1–L5/M/H`/poll settings, and link age/exit state.

K5/K6 v1.0RC2 reorganizes the TEAM screen into fixed, non-overlapping fields: callsign/battery, DCS/RSSI, persistent `PWR P1–P3`/poll settings, and link age/exit state.

## 配置与频道 / Configuration and channels

- `tools/team_config.py --model k5`：K5/K6 呼号与 TEAM 默认参数。
- `tools/team_config.py --model k1`：K1 呼号与适用的 TEAM 默认参数。
- `tools/channel_config.py --model k6|k1 export/import`：通过通用 CSV 导出或写入前 200 个频道。
- `tools/channel_config.py --model k6|k1 prune --keep N`：备份后删除编号高于 N 的已占用频道；K1 会同时清理完整 1024 槽属性区。
- 所有配置和频道操作都必须在正常开机状态下执行，不能在 Bootloader/DFU 中执行。

- `team_config.py`: callsign and TEAM defaults, selected with `--model k5` or `--model k1`.
- `channel_config.py export/import`: portable CSV for the first 200 channels on K6 or K1.
- `channel_config.py prune --keep N`: back up first, then remove occupied slots above N; K1 also clears the full 1024-slot attribute table.
- Run configuration and channel operations with the radio normally powered, never in bootloader/DFU mode.

## 安全边界 / Safety boundaries

- 固件不是认证的生命安全设备，不能作为唯一通信、定位或救援协调手段。
- 遵守当地频率、呼号、功率和发射规定；AUTO TX 只在低功率、可控环境先行测试。
- 刷机前备份原厂固件、EEPROM 和校准数据；刷写过程中不得断电或拔线。
- K5/K6 packed 固件、K1 `.bin` 和 PY32F030 机型不可混用。

- This firmware is not certified life-safety equipment and must never be the sole communication, location, or rescue-coordination system.
- Follow local frequency, callsign, power, and transmission rules. Test AUTO TX at low power in a controlled environment first.
- Back up stock firmware, EEPROM, and calibration before flashing. Never remove power or the cable during a write.
- Do not mix the K5/K6 packed image, K1 `.bin`, or unsupported PY32F030 hardware.
