# ciank67 Windows 音乐律动

> Windows 图形客户端已提供 **[WinUI 3 版本](WinUI/README.md)**，支持设备选择、频谱预览、演示模式和 USB/蓝牙连接，不需要 Python。以下保留 Python 命令行版本的使用说明。

将 Windows 正在播放的声音转换为 16 段频谱，通过 USB 或蓝牙发送给键盘。矩阵灯显示频谱柱，底灯显示各频段的彩色强度。无需麦克风，不录音、不保存音频文件。nRF5 SDK 保持 15.3.0。

R13 固件的矩阵频谱为独立灯效 `MUSIC_SPECTRUM`，按实际矩阵显示 7 组宽柱，使用纵向配色、即时上升与短尾回落。音乐输入自动临时启用，停止后恢复之前的灯效；不改写保存的模式。灯光层物理 W 键也可手动选择，未播放时回落至全黑。矩阵灯的色相、饱和度、亮度和速度按键分别控制配色、亮度和回落速度。现有 WinUI 客户端可直接使用，USB 产品名为 `ciank67`。

## 安装与 USB 使用

1. 将这次生成的 `ciank67-music-r15-color-picker.zip` 通过原有 nrfutil 流程刷入。它是应用升级包，不更新 SoftDevice 或 bootloader。
2. Windows 安装 **Python 3.12（64 位）**，保留 Python Launcher（`py`）。
3. 在本目录双击 `install.cmd`，或在 PowerShell 执行：

```powershell
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

4. 连接 USB，确保普通灯光开关已打开。双击 `start-usb.cmd`，播放音乐；也可以直接运行：

```powershell
.\.venv\Scripts\python.exe music.py --transport usb
```

程序默认采集当前默认播放设备的声音，25 帧/秒，同时控制两组灯。Ctrl+C 退出并恢复原灯效。播放设备切换后重启程序。

先验证 USB 通道而不采集声音，可运行以下命令，观察两组灯随演示频谱变化：

```powershell
.\.venv\Scripts\python.exe music.py --transport usb --demo --seconds 10
```

## 蓝牙使用

先在 Windows 设置中配对键盘，拔掉 USB，让键盘进入蓝牙工作状态。该键盘固件插入 USB 时会切换到 USB 并断开蓝牙。先列出已配对设备（键盘已连接时可能不再广播，因此优先使用此命令）：

```powershell
.\.venv\Scripts\python.exe music.py --list-paired-ble
```

找到键盘地址后连接；先加 `--demo --seconds 10` 验证通道，再测试系统声音：

```powershell
.\.venv\Scripts\python.exe music.py --transport ble --ble-address "AA:BB:CC:DD:EE:FF" --demo --seconds 10
.\.venv\Scripts\python.exe music.py --transport ble --ble-address "AA:BB:CC:DD:EE:FF"
```

未配对且正在广播的设备可以用 `--scan-ble` 查找。程序优先连接 Windows 已配对设备，不要求键盘重新广播。也可用 `--ble-name "设备名"` 精确匹配设备名称。新 GATT 特征不单独广播服务 UUID，因此扫描列表也会包含其他设备；选择实际键盘的地址。写入特征要求配对加密。Windows 如果缓存了旧固件的服务而报找不到音乐服务，可在系统设置移除键盘，重新让键盘广播并配对；不要先清空键盘的其他配置。

## 控制与排查

```powershell
# 只控制矩阵灯或底灯
.\.venv\Scripts\python.exe music.py --target matrix
.\.venv\Scripts\python.exe music.py --target underglow

# 声音较小时提高灵敏度；蓝牙不流畅时降低帧率
.\.venv\Scripts\python.exe music.py --gain-db 10
.\.venv\Scripts\python.exe music.py --transport ble --ble-address "AA:BB:CC:DD:EE:FF" --fps 20

# 列出 USB 音乐接口和 WASAPI 播放设备的回环输入
.\.venv\Scripts\python.exe music.py --list-usb
.\.venv\Scripts\python.exe music.py --list-audio
.\.venv\Scripts\python.exe music.py --audio-device 23
```

- `RM_TOGG`、`UG_TOGG`、`RGBG_TOG` 仍能分别或同时关灯；音乐程序不会重新打开手动关闭的灯光。
- Caps Lock 保留原有覆盖优先级：底灯 9～16 为白色；矩阵灯保留原有 Caps 指示行为。
- 亮度使用键盘现有亮度设置；频谱效果期间调节普通灯效，会在退出律动后显示所选灯效。
- 关闭程序时发送停止指令；掉线、程序崩溃或超过 750 ms 无数据时恢复普通灯效。静音时显示黑色频谱，Caps 指示仍有效。
- 灯光层 Q 的底灯自检优先于音乐，结束后可继续律动。
- 多块相同键盘同时连接时，USB 程序会拒绝自动选择；使用 `--list-usb` 查看序列号，再通过 `--serial` 指定一块。一次只运行一个音乐控制程序。
- `--demo` 正常而实际音乐不动时，检查选择的播放设备、系统音量和 `--gain-db`。部分独占播放或受保护内容可能无法回环采集。
- `--list-usb` 无结果时，确认刷入的是音乐版本，重新拔插 USB。此程序使用专用厂商 HID 接口，不能向普通键盘接口发送频谱。

## 固件与协议

默认 `ciank67:default` 启用 `MUSIC_STREAM_ENABLE=yes`。可用 `make ciank67:default MUSIC_STREAM_ENABLE=no` 编译不带音乐通道的版本。音乐实时数据保存在 RAM，不写入 EEPROM；有效蓝牙音乐数据会重置键盘空闲计时，停止数据后恢复原有休眠策略；设备端最多每 33 ms 提交一次音乐底灯帧，电脑端帧率限制为 1～30。

| 字节 | 内容 |
|---|---|
| 0 | 魔数 `0xC7` |
| 1 | 协议版本 `1` |
| 2 | 序号，0～255 循环 |
| 3 | bit 0 矩阵灯，bit 1 底灯；bit 4 效果（0 频谱柱，1 双心共舞）；bit 5 纯光（0 彩光，1 纯光）；其他位必须为 0。目标位均为 0 表示停止，客户端停止包固定为 `0` |
| 4～19 | 从低频到高频的 16 个强度值，范围 0～255 |

USB：R7 使用 VID `0xFEED`、PID `0xC475`；客户端兼容 R6 的 Nordic 默认身份 `0x1915:0x520B`，名称不参与筛选。Usage Page `0xFF67`、Usage `0x67`，报告 ID `4`；发送 21 字节（ID + 20 字节协议包）。接收独立于键盘的 Caps Lock 报告，使用接口 4、IN1/OUT1 端点。

BLE：服务 `c1a00001-7d9b-4c7d-a568-7294e2166700`，写入特征 `c1a00002-7d9b-4c7d-a568-7294e2166700`；每次 Write Without Response 发送完整 20 字节，适配默认 ATT MTU，不要求提高 MTU。

中断/接收回调仅更新最新频谱快照，不执行渲染和存储；USB 和 BLE 共用解析器，最后收到的有效包生效。序号用于诊断，不因回绕或重启拒收。FFT 使用 2048 点 Hann 窗、40 Hz～16 kHz 对数频段和快速上升与短尾回落平滑；左右声道按能量合成，避免反相抵消。

## 验证范围

已在开发机验证固件构建、数据解析、USB/BLE 回调、灯光恢复、DSP 和程序退出逻辑。已通过 WSL 调用 Windows 构建和验证 WinUI 客户端，包括界面、图标、效果选择和 WASAPI 采集。新的双中心环带灯效仍需刷入实物键盘验证。

```sh
python3 -m unittest discover -s platforms/nrf5/tests -v
python -m unittest discover -s util/ciank67_music/tests -v
```

参考：[PyAudioWPatch](https://github.com/s0d3s/PyAudioWPatch)、[Bleak](https://bleak.readthedocs.io/en/stable/)、[HIDAPI](https://github.com/libusb/hidapi)。

R10 的 WinUI 客户端可在“矩阵律动效果”下拉框实时切换频谱柱和双心共舞，底灯律动不变。Python 命令行对应 `--effect spectrum` / `--effect dual-dance`，例如：

```powershell
.\.venv\Scripts\python.exe music.py --transport usb --effect dual-dance --demo --seconds 10
```

双心共舞需要 R10 固件；默认频谱柱包保持与旧固件兼容。效果选择使用原包的 bit 4，长度仍是 20 字节；USB Report ID、BLE UUID 和 ATT MTU 均无需改变。

R10 使用两组同心彩色环带，音量控制范围，颜色按时间连续流动；平滑上升与回落替换 R9 的短脉冲发射。协议 ID 1 不变；Python 仍兼容旧参数 `--effect dual-rays`。形态参考：[HFD SDK 音乐律动 V2 模式 22](https://docs.driveall.cn/kit/music-v2.html)。

R11 只调整键盘端频谱显示：16 个输入频段合为 7 组，每组覆盖实际矩阵的相邻两列，取消白色峰值并改用纵向绿黄红配色，上升与回落均平滑。客户端、20 字节协议、底灯和双心共舞均不变，无需更新客户端。

R12：两种模式客户端均使用快速攻击 / 回落，固件即时跟随上升、限制短尾下降，避免重复平滑。配套更新 Windows 客户端，或使用本目录最新版 Python 客户端。双心共舞同步缩短固件包络的上升和回落时间，仍保留连续环带和渐隐边缘。

R13 修正矩阵灯幅度映射：降低对数强度转换为柱高/半径时的弱音基线，并让柱体底部和双心中心亮度随音乐变化，不再提前满亮。FFT、20 字节协议和底灯算法不变，R12 客户端无需更新。

R14 新增配色选项，Python 对应 `--color-mode colorful` / `--color-mode solid`，默认彩光。纯光使用矩阵灯、底灯各自当前保存的颜色，保留 R13 的亮度和形状映射，需要 R14 固件；停止包仍固定为目标/效果字节全零。

R15 支持自选纯光颜色。Python 例如 `music.py --transport usb --solid-color "#FF8000"`，指定颜色会自动使用纯光；WinUI 使用原生调色器。颜色命令同样为 20 字节：`C7 02 R G B` 后跟 15 个零，通过原 USB / BLE 通道在首个纯光音乐帧前发送。保留原音乐包和 16 频段，不扩大 BLE MTU。颜色改变不刷新音乐超时，不写 EEPROM，停止/断开后清除本次覆盖。
