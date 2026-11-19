# ciank67 音乐律动 · WinUI 3

C# / WinUI 3 Windows 客户端，使用原生 Fluent 控件、系统主题与 Mica 背景。沿用 R6 固件的 20 字节频谱协议；不需要升级 nRF5 SDK，不需要 Python。

## 使用

1. 此发布包不包含系统运行时。首次使用需安装 **.NET 10 Runtime（x64）** 和 **Windows App Runtime 2.7（x64，或兼容的更高 2.x 版本）**。已安装则无需重复安装。面向 Windows x64，最低 Windows 10 2004；Windows 11 可显示 Mica 效果。
   - [.NET 10 Runtime 官方下载](https://dotnet.microsoft.com/zh-cn/download/dotnet/10.0/runtime)
   - [Windows App Runtime 官方下载](https://learn.microsoft.com/windows/apps/windows-app-sdk/downloads)（选择与 2.7 SDK 匹配的 x64 安装程序）
2. 保留发布包的完整目录，双击 `Ciank67Music.exe`。安装运行时后可供多份程序共用，分发 ZIP 不附带安装程序。
3. 首次可以选择 **仅预览**，打开 **演示模式**，点击 **开始律动**，在不连接键盘时检查界面和频谱。
4. 键盘建议刷入 `ciank67-music-r8-spectrum.zip`。R8 包含独立矩阵频谱灯效，USB 名称为 `ciank67`，仍沿用 R6 音乐协议。刷入后重新拔插 USB。
5. USB：选择 **USB** → **刷新设备** → 选择键盘。蓝牙：先在 Windows 设置配对键盘并拔掉 USB，再选择 **蓝牙（已配对）** 并刷新。蓝牙列表包含其他已配对设备，请选择键盘；已连接的键盘不需要重新广播。
6. 选择声音的 **播放设备**。程序采集该设备正在播放的声音，不采集麦克风，也不保存录音。
7. 关闭演示模式，选择矩阵灯 / 底灯，点击 **开始律动**。灵敏度、帧率和两组灯的选择可在运行中调节。
8. **停止**或关闭窗口会发送恢复指令。连接中断时，固件也会在约 750 ms 无数据后恢复原灯效。

键盘上的 `RM_TOGG` / `UG_TOGG` / `RGBG_TOG` 仍可关灯，客户端不会强制打开手动关闭的灯。Caps Lock 保留原有覆盖优先级。更换系统默认播放设备后，请停止、刷新并重新开始。

蓝牙若报找不到音乐服务，先确认刷入 R6、键盘当前使用蓝牙，再尝试在 Windows 设置移除并重新配对以清理旧服务缓存。演示模式正常而实际音乐没有频谱时，检查所选播放设备、音量和灵敏度；部分独占播放或受保护音频可能无法回环采集。

## 从源码构建

Windows 安装 .NET 10 SDK，双击 `build.cmd`。也可在当前目录执行：

```powershell
dotnet run --project Ciank67.Music.Tests -c Release
dotnet publish Ciank67.Music.App -c Release -r win-x64 --self-contained false -o publish\win-x64
```

输出为 `publish\win-x64\Ciank67Music.exe` 及其依赖。发布采用依赖系统运行时的轻量方式，不包含 .NET 或 Windows App SDK 运行时；分发时保留整个目录。请发布到空目录，避免混入旧版自包含发布的 DLL。源码提供 `Ciank67Music.sln`，可用支持 .NET 10 的 Visual Studio 打开。构建需要联网恢复 NuGet 包。

- `Ciank67.Music.App`：WinUI 3 界面、WASAPI 回环采集、USB HID、Windows BLE GATT。
- `Ciank67.Music.Core`：协议、PCM 解码、2048 点 FFT、16 段频谱、音频环形缓冲、发送/停止流程。
- `Ciank67.Music.Tests`：不依赖硬件的核心回归测试，可在 Linux 或 Windows 运行。

界面使用 C# 创建原生 WinUI 控件，App.xaml / MainWindow.xaml 负责标准资源和窗口初始化；没有 WinForms、WebView 或 Python 运行时。采集和数据发送不在 UI 线程执行；绘图只读取最新频谱，不排队补发旧帧。USB 和 BLE 与原 Python 客户端共用固件协议，请不要同时运行多个控制程序。

NuGet：引用 Microsoft.WindowsAppSDK.WinUI / Runtime 2.7.0 及其必需依赖、NAudio.Wasapi 2.2.1、HidSharp 2.6.4。不引用 Windows App SDK 总包，避免带入 AI、ML、搜索、Widgets 等未使用组件。第三方组件许可证随发布包附带。

## 体积与依赖

.NET / WinUI 运行时由系统安装，本 ZIP 不再捆绑。目录中的 `Microsoft.Windows.SDK.NET.dll`、`Microsoft.WinUI.dll`、`WinRT.Runtime.dll` 等属于应用运行必需的托管 API 投影和互操作依赖；`Microsoft.WindowsAppRuntime.Bootstrap*` 用于查找并加载已安装的共享运行时，应保留。WinUI 所需的 WebView2 托管类型投影保留，但本程序未使用浏览器控件，其原生组件和 Loader 不发布。不要凭文件名手动删除必需依赖。

`verify-publish.ps1` 会检查 .NET/WinUI 原生运行时及 AI/ML/搜索/Widgets 等组件未进入发布目录，同时确认应用自己的 PRI/XBF 资源没有遗漏。

## USB 名称与兼容性

客户端按 VID/PID 和音乐接口 Usage 筛选，不按产品名筛选。新版固件使用 `FEED:C475`，客户端也兼容旧版的 `1915:520B`；即使名称还是 `nRF52 USB Product`，只要音乐接口已经枚举就能识别。

如果提示“已找到键盘的 USB 设备，但未检测到音乐接口”，说明普通键盘接口可见，音乐接口还未出现。确认 USB 连的是已刷音乐固件的同一块键盘，刷入 R8 后重新拔插并刷新设备。USB 产品名在仓库 `keyboards/ciank67/keyboard.json` 的 `keyboard_name` 字段中修改。

应用图标为原创的键盘与彩色频谱图案，包含 16～256 像素多尺寸图层，同时用于 EXE、窗口和任务栏。图标及可重生成的绘制脚本位于 `Ciank67.Music.App/Assets/`；仅重新绘制图标时需要 Pillow，构建及运行客户端无需 Python。
