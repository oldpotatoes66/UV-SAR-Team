# Quansheng SAR Tools

面向 Quansheng UV-K6（DP32G030）的简化无线电信号定位固件，基于 [F4HWN UV-K5/K6 固件](https://github.com/armel/uv-k5-firmware-custom) 开发。

项目目标是让不具备专业无线电知识的救援人员，也能按照一个已知频率，或先扫描发现信号后，使用信号强度和定向天线进行快速搜索。

> 当前实机验证版本：SAR v3.6a
> 验证设备：Quansheng UV-K6、MCU DP32G030、Bootloader 3.00.02

完整开发状态、后续功能和每版固定验收项目见 [SAR 救援定位功能清单](docs/SAR-ROADMAP.md)。

## 主要功能

- `F + 5` 从主界面直接进入 SAR 定位模式；
- 实时 RSSI/dBm、0–100% 相对强度与大型强度条；
- IIR 平滑及 `UP / DOWN / HOLD` 信号趋势；
- `HI / LOW / MIN` 三级自动接收增益；
- 切换增益后根据实机响应自动校准显示连续性；
- SAR 模式锁定 PTT，避免误发射；
- SAR 内按 `MENU` 进入频谱扫描；
- 扫描画面按 `PTT` 锁定最强信号并返回 SAR；
- `EXIT` 可靠退出并恢复进入前的射频状态；
- 大字户外显示和目标频率状态栏。
- 随信号增强而变高、变密的声音提示；
- 数字键 `1` 或任一侧键切换 `BEEP / MUTE`。

## 使用方法

### 已知目标频率

1. 正常开机。
2. 在主界面切换到 VFO 模式并输入目标频率。
3. 按 `F`，再按 `5 NOAA`，直接进入 SAR。
4. 缓慢旋转定向天线，观察百分比、强度条和趋势。
5. 朝信号增强方向移动，并从不同位置重复测量。
6. 按 `EXIT` 返回主界面。

### 未知目标频率

1. 按 `F + 5` 进入 SAR。
2. 按 `MENU` 进入 Bandscope 频谱扫描。
3. 调整扫描范围，等待目标发射。
4. 出现目标峰值后按 `PTT`。
5. 固件锁定最强信号并返回 SAR 定位画面。

### SAR 画面

```text
状态栏        目标频率
SAR HI        当前自动增益档
百分比        相对信号强度
UP            信号增强
DOWN          信号减弱
HOLD          信号基本稳定
大号 dBm      平滑并校正后的信号读数
强度条        便于快速观察方向变化
```

自动增益含义：

- `HI`：远距离或弱信号；
- `LOW`：中近距离强信号；
- `MIN`：非常靠近目标，降低增益以避免满表。

使用者无需调整 LNAs、LNA、PGA 或 IF 参数。

## 推荐测向流程

1. 先确认目标频率和信号确实存在。
2. 使用八木、环形或其他定向天线。
3. 保持天线高度和姿态一致，缓慢旋转一周。
4. 记录最强方向；换一个位置再次测量。
5. 接近目标后缩短移动距离，注意建筑、车辆和山体反射。
6. 若所有方向都很强，可换短天线或增加外置衰减器。

屏幕趋势只能表示近期强度变化，不能自动给出地理方向。方向仍由人员旋转定向天线并比较读数获得。

## 编译

推荐使用 ARM GNU Embedded Toolchain 10.3.1，并安装 Python 3 与 `crcmod`。

```sh
make clean
make \
  ENABLE_SPECTRUM=1 \
  ENABLE_FOX_MODE=1 \
  ENABLE_FMRADIO=0 \
  ENABLE_AIRCOPY=0 \
  ENABLE_VOX=0 \
  ENABLE_UART=0 \
  ENABLE_AUDIO_BAR=0 \
  ENABLE_FEAT_F4HWN_RESUME_STATE=0 \
  EDITION_STRING=SAR33 \
  VERSION_STRING=v3.3 \
  VERSION_STRING_2=v3.3
```

输出文件：

- `f4hwn.bin`：原始固件镜像；
- `f4hwn.packed.bin`：供兼容刷机工具写入的打包固件。

最近一次 v3.3 构建结果：

```text
text  59484 bytes
data     60 bytes
bss    2784 bytes
```

## 刷机

1. 关闭对讲机并连接可靠的编程线。
2. 按住 PTT，同时开机进入 Bootloader。
3. 使用支持 Protocol V2 的 K5TOOL 写入 `f4hwn.packed.bin`。
4. 写入期间不要断电、拔线或让计算机休眠。

示例：

```sh
mono K5TOOL/build/k5tool.exe \
  -port /dev/ttyUSB0 \
  -wrflash f4hwn.packed.bin
```

只有工具报告 CRC 通过、所有区块为 `OK` 且最终显示 `Done`，才视为写入成功。

## 恢复

测试新版本前应始终保留一个已验证可启动的恢复固件。本项目实机验证过 F4HWN Basic v4.3 和 Bandscope v4.3，可在异常时重新进入刷机模式恢复。

不要使用早期 FOX v1.0/v1.1 测试固件。

## 源码结构

- `Makefile`：`ENABLE_FOX_MODE` 构建开关；
- `app/main.c`：`F + 5` 直接进入 SAR；
- `app/spectrum.c`：SAR 状态、显示、IIR、趋势、自动增益、扫描切换和退出；
- `app/spectrum.h`：SAR/FOX 入口声明；
- `docs/F4HWN-Bandscope-v4.3-中文使用说明.md`：Bandscope 中文说明；
- `docs/UPSTREAM_README.md`：原始 F4HWN README。

## 已验证状态

- 正常启动：通过；
- `F + 5` 直接进入 SAR：通过；
- 实时 dBm/百分比/趋势：通过；
- 自动切换 HI/LOW 及切档校准：通过；
- SAR 内进入频谱扫描：通过；
- 扫描峰值返回 SAR：通过；
- PTT 防误发射：通过；
- EXIT 退出：通过；
- 字体与界面布局：通过。

开发中：`TARGET / WAIT / LOST` 间歇信号状态已编译，等待真机验证。其他待开发内容以 [SAR 救援定位功能清单](docs/SAR-ROADMAP.md) 为准。

## 安全与限制

- 本项目不是认证的生命安全设备，不能作为搜救行动的唯一定位手段。
- UV-K6 接收前端动态范围和抗强信号能力有限，复杂射频环境可能产生假峰或过载。
- dBm 和百分比用于同一频率、天线和测量条件下的相对比较，不应视为校准仪表读数。
- 建筑物、车辆、山体和金属设施会造成多径反射，必须从多个位置交叉判断。
- 使用者必须遵守当地无线电发射、频率和设备改装法规。

## 上游与许可证

本项目基于 Armel F4HWN、Egzumer、Fagci、OneOfEleven 和 DualTachyon 等人的开源工作。上游说明及完整致谢见 [docs/UPSTREAM_README.md](docs/UPSTREAM_README.md)。

源码沿用上游 Apache License 2.0；详见 [LICENSE](LICENSE)。
