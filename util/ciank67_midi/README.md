# ciank67 Audio / MIDI（R16）

默认固件增加 QMK Audio 和标准 USB MIDI 1.0 / BLE MIDI 1.0 收发。

## 本地发声

原理图 `67nhf_3741.sch` 中 LS1 的 `buzzer1` 接 E73 模块第 9 脚（P0.31），`buzzer2` 接第 10 脚（P0.30）；已与同目录 PCB、缓存符号库交叉核对。封装为 CPT-9019S 无源压电蜂鸣器。PWM1 两通道反相驱动，静音时停止 PWM、两脚拉低；底灯仍占用 PWM0。

音频默认开启，按键音默认关闭（已有 EEPROM 设置优先）。开机旋律延迟到主循环启动后播放。USB、蓝牙、未连接电脑时均可本地发声；普通打字保持原状，按需开启按键音或音乐模式。

进入原有灯光层 `TO(_RGBST)` 后，以下位置按标准 QWERTY 键帽标注，与当前字母布局无关：

| 物理键 | 功能 |
| --- | --- |
| E | `AU_TOGG`：本地音频总开关 |
| R | `CK_TOGG`：按键音开关 |
| T | `MU_TOGG`：本地音乐模式开关 |
| Y | `MU_NEXT`：切换 QMK 音阶/音乐模式 |
| U | `MI_ON`：开启键盘向电脑发送音符 |
| I | `MI_OFF`：关闭 MIDI 音乐模式并发送停音 |
| O | `AU_OFF`：关闭本地音频 |

音乐模式会将普通按键用于演奏，不再输入普通字符；退出 `MU_TOGG` / `MI_OFF` 后恢复。可同时开启 MU 和 MI，获得本地音符和电脑 MIDI 输出。回到原布局仍使用灯光层原有退出键。Tab / Q / W 仍分别为灯光状态、底灯自检、频谱效果。

蜂鸣器为单声部方波输出，最近音符优先，保留 QMK 音符栈。收到标准 MIDI Note On/Off 自动发声，无须开启 MI_ON；需保持 AU 开启。A4（音符 69）为 440 Hz。支持力度 0 停音、CC64 延音、CC120 All Sound Off、CC123 All Notes Off、CC121 Reset Controllers、Stop/Reset 停音；同音高的不同通道分别记账。蜂鸣器不模拟钢琴音色、力度音量和 Pitch Bend，不能代替音箱。

## USB / BLE MIDI

USB 增加一组 AudioControl + MIDIStreaming 接口，单 cable、64 字节 bulk IN/OUT，端点 5；不是 USB PCM 声卡。现有 HID 键盘、鼠标、灯光律动的接口和端点保持原编号。电脑选择 `ciank67` 的 MIDI 输入/输出即可；输入表示键盘→电脑，输出表示电脑→键盘。

BLE 使用标准服务 `03B80E5A-EDE8-4B33-A751-6CE34EC4C700` 和读/写/通知特征 `7772E5DB-3868-4112-A1A9-F2669D106BF3`，服务 UUID 放在扫描响应。需配对加密；支持空数据读取握手、CCCD 订阅、已配对重连的订阅恢复、13 位发送时间戳、接收 running status、实时消息穿插及跨包 SysEx。接收音符按到达时间处理，不实现主机时间戳调度。

**首次从旧固件升级后，建议在 Windows 删除旧的蓝牙配对并重新配对一次**，让系统重新发现新增 GATT 服务。BLE MIDI 需要支持 Windows BLE MIDI API 的软件；旧的仅 WinMM MIDI 软件可能不直接列出 BLE 端口。

发送通道跟随当前键盘输出（`OU_USB` / `OU_BT`）。切换连接前先 `MI_OFF`、松开演奏键；断开会清理本地音符与数据队列。接收队列溢出会清空并停音，发送队列溢出改为各通道 All Sound Off；USB 恢复时也发送各通道 All Sound Off。没有订阅者时丢弃 BLE 输出，不在下次连接时重放旧音符。

SDK 仍为 nRF5 SDK 15 / S140 6.1.1。本功能和 Windows 音乐律动客户端相互独立，原客户端无需更新；律动 HID/GATT 数据协议未改变。

## Windows 检测工具

`Ciank67.MidiCheck` 使用 Windows.Devices.Midi，可列出 USB 和 BLE MIDI 端口、向指定端口发送测试旋律，以及监视键盘发送的音符。不自动选择设备，不安装驱动，不捆绑 .NET 运行时；需要 Windows 10/11 x64 和 .NET 10 Runtime x64。

在工具目录打开终端：

```powershell
.\Ciank67.MidiCheck.exe list
.\Ciank67.MidiCheck.exe play 0
.\Ciank67.MidiCheck.exe monitor 0
```

把 `0` 替换为列表中 **ciank67 对应方向** 的编号；不要把 Windows 自带的 Microsoft GS 合成器当成键盘。`play` 依次播放 C5 / E5 / G5 / A4，结束或 Ctrl+C 时发送停音；`monitor` 后在键盘开启 MI_ON、按键，应该看到 NoteOn / NoteOff。一次只让一个测试/MIDI 程序占用端口；WinUI 灯光客户端使用其他接口，可以继续使用。

从 Windows 编译（WSL 内不能运行该 WinRT 工具）：

```powershell
dotnet publish -c Release -r win-x64 --self-contained false -o publish
```

运行目录须保留 DLL、deps.json、runtimeconfig.json；PDB 可省略。工具依赖的 Windows API 投影 DLL 是编译依赖，不是捆绑的 .NET 运行时。

## 验证

```sh
make ciank67:default
python3 -m unittest discover -s platforms/nrf5/tests -v
```

原生测试运行真实驱动/传输/编码逻辑并启用 ASan/UBSan，覆盖频率和双脚极性、PWM0 隔离、USB 描述符跨 EP0 分包、USB 收发缓冲区、暂停恢复、BLE 服务/订阅/握手、运行状态、SysEx、畸形包、延音、通道隔离、队列溢出和原有灯光功能。实机还需验证蜂鸣器响度、Windows USB/BLE 枚举及长时间演奏。
