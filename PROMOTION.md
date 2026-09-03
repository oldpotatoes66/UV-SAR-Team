# UV-K5 SAR-TEAM 宣传文案 / Promotion Copy

## 中文长文案

UV-K5 SAR-TEAM 是面向泉盛 UV-K5、UV-K6（DP32G030）和 UV-K1（PY32F071）电台的开源搜救固件，两类芯片使用各自独立的固件。

它把 SAR 无线电测向、Yaesu ARTS 兼容团队链路和日常语音通信整合在同一台电台中：`F + 5` 进入 SAR；长按数字 `6` 进入 TEAM；`F + 6` 切换实际发射功率。TEAM 通过 DCS 023N 与 VX-6R、VX-7R、VX-8R 进行低功率轮询，同时保留正常语音收发。失联约 60 秒后发出三次低音告警，并在持续失联时每约 60 秒重复，恢复连接时发出双高音提示。K1 还会在 A/B 两个频段持续显示各自功率，并在调整时给出大字确认。

公开固件不包含任何队员呼号。救援队可以使用 `team_config.py` 为每台设备单独设置 3–6 位呼号、15/25 秒轮询间隔、功率字段、提示音和 CW 默认状态，也可以使用 CSV 名册批量部署。K5/K6 使用 TEAM 的 P1/P2/P3；K1 使用当前频道 L1–L5。配置脚本会自动备份原值、写后复读，并进行 CRC-8 校验。

项目免费开源，欢迎无线电爱好者、志愿救援组织和开发者学习、测试与共同改进：

https://github.com/oldpotatoes66/UV-SAR-Team

安全提示：本项目是实验性业余无线电固件，不是生命安全设备。请遵守当地频率、呼号、功率和发射规定，不要把它作为救援通信的唯一手段。刷机前确认 MCU：K5/K6 的 DP32G030 固件与 K1 的 PY32F071 固件禁止交叉刷写，PY32F030 不受支持。

## 中文短文案

一台 UV-K5/K6/K1，同时拥有 SAR 测向、Yaesu ARTS 兼容团队链路和正常语音收发。对应机型固件不写死队员呼号，支持脚本与 CSV 批量配置。免费开源，欢迎测试与改进：

https://github.com/oldpotatoes66/UV-SAR-Team

## English

UV-K5 SAR-TEAM is open-source search-and-rescue firmware for Quansheng UV-K5/UV-K6 (DP32G030) and UV-K1 (PY32F071), with separate images for each MCU family.

It combines radio direction finding, a Yaesu ARTS-compatible team link, and normal voice communication in one handheld. Press `F + 5` for SAR, hold digit `6` for TEAM, and use `F + 6` to change actual transmit power. TEAM provides DCS 023N low-power polling compatible with tested VX-6R, VX-7R, and VX-8R radios while retaining normal voice RX and PTT voice TX. A three-tone alarm repeats about once per minute while a link remains lost, and two high tones announce recovery. K1 permanently displays separate A/B power levels and shows a large confirmation when power changes.

The public firmware contains no team-member callsign. Rescue teams can configure each radio individually or deploy a CSV roster with `team_config.py`. K5/K6 use TEAM P1/P2/P3, while K1 uses the selected channel's L1–L5 power. The tool backs up the previous value, writes only the dedicated eight-byte SAR-TEAM EEPROM block, reads it back, and verifies CRC-8.

Free and open source—learn, test, and contribute:

https://github.com/oldpotatoes66/UV-SAR-Team

Safety notice: experimental amateur-radio firmware is not certified life-safety equipment. Follow local frequency, callsign, power, and transmission rules, and never rely on this project as the only rescue communication system. Never cross-flash the K5/K6 DP32G030 and K1 PY32F071 images; PY32F030 is unsupported.

## 推荐发布顺序 / Suggested order

1. `images/promo/01-cover.png`
2. `images/promo/02-sar-direction-finding.png`
3. `images/promo/03-team-arts.png`
4. `images/promo/04-flash-and-configure.png`
5. `images/promo/05-safety-open-source.png`
