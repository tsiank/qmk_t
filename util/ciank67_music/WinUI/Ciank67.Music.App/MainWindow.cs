using System.Diagnostics;
using System.Runtime.InteropServices;
using Ciank67.Music.Core;
using Microsoft.UI;
using Microsoft.UI.Text;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace Ciank67.Music.App;

public sealed partial class MainWindow : Window
{
    private readonly ComboBox connection = new() { HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly ComboBox keyboard = new() { HorizontalAlignment = HorizontalAlignment.Stretch, PlaceholderText = "请选择键盘" };
    private readonly ComboBox playback = new() { HorizontalAlignment = HorizontalAlignment.Stretch, PlaceholderText = "请选择播放设备" };
    private readonly ComboBox effect = new() { HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly ComboBox colorMode = new() { HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly ColorPicker solidColor = new() { Color = Colors.White, IsAlphaEnabled = false, IsMoreButtonVisible = false };
    private readonly Button chooseColor = new() { HorizontalAlignment = HorizontalAlignment.Stretch, IsEnabled = false };
    private readonly Border colorSwatch = new() { Width = 20, Height = 20, CornerRadius = new CornerRadius(4), Background = new SolidColorBrush(Colors.White) };
    private readonly TextBlock colorHex = new() { Text = "#FFFFFF", VerticalAlignment = VerticalAlignment.Center };
    private readonly Button refresh = new() { Content = "刷新设备", HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly Button start = new() { Content = "开始律动", MinWidth = 140 };
    private readonly Button stop = new() { Content = "停止", IsEnabled = false, MinWidth = 90 };
    private readonly CheckBox matrix = new() { Content = "按键矩阵灯", IsChecked = true };
    private readonly CheckBox underglow = new() { Content = "底灯", IsChecked = true };
    private readonly Slider gain = new() { Minimum = -20, Maximum = 30, StepFrequency = 1, Value = 0 };
    private readonly TextBlock gainLabel = new() { Text = "灵敏度  0 dB" };
    private readonly NumberBox rate = new() { Minimum = 1, Maximum = 30, Value = 25, SpinButtonPlacementMode = NumberBoxSpinButtonPlacementMode.Compact };
    private readonly ToggleSwitch demo = new() { Header = "演示模式", OffContent = "跟随系统声音", OnContent = "生成演示频谱" };
    private readonly TextBlock status = new() { Text = "准备就绪", TextWrapping = TextWrapping.Wrap };
    private readonly InfoBar notice = new() { IsOpen = true, IsClosable = true, Severity = InfoBarSeverity.Informational,
        Title = "连接键盘，或先试试离线预览", Message = "USB 连接可直接刷新设备。蓝牙请先在 Windows 设置中配对，并拔掉键盘 USB。" };
    private readonly SpectrumView spectrum = new();
    private readonly FirmwarePanel firmwarePanel;
    private readonly ViaPanel viaPanel;
    private readonly MidiPanel midiPanel;
    private readonly KeyboardControlPanel keyboardControls;
    private readonly NavigationView navigation = new() {
        PaneDisplayMode = NavigationViewPaneDisplayMode.Auto, OpenPaneLength = 208,
        CompactModeThresholdWidth = 640, ExpandedModeThresholdWidth = 1100,
        IsBackButtonVisible = NavigationViewBackButtonVisible.Collapsed, IsSettingsVisible = false,
        PaneTitle = "Qmknrf5配置工具"
    };
    private readonly Grid pageHost = new();
    private readonly Dictionary<string, (NavigationViewItem Item, ScrollViewer Page)> pages = [];
    private readonly TextBlock pageTitle = new() { FontSize = 28, FontWeight = FontWeights.SemiBold };
    private readonly TextBlock pageDescription = new() { FontSize = 14, TextWrapping = TextWrapping.Wrap, Opacity = .65 };
    private readonly TextBlock selectedDevice = new() { Text = "尚未选择键盘", TextWrapping = TextWrapping.Wrap, FontSize = 12 };
    private readonly TextBlock musicActivity = new() { Text = "律动未运行", FontSize = 12, Opacity = .65 };
    private readonly TextBlock deviceStatus = new() { Text = "正在查找设备…", TextWrapping = TextWrapping.Wrap };
    private readonly Grid body = new() { ColumnSpacing = 24, RowSpacing = 24 };
    private readonly Border controlCard;
    private readonly StackPanel visualization;
    private readonly Microsoft.UI.Xaml.DispatcherTimer previewTimer = new() { Interval = TimeSpan.FromMilliseconds(33) };
    private MusicSettings settings = new(25, 0, LightTarget.Both, false);
    private byte[] latestBands = new byte[16];
    private Task? sessionTask;
    private CancellationTokenSource? sessionCancellation;
    private CancellationTokenSource? refreshCancellation;
    private bool closing;
    private bool closeReady;
    private bool checkingClose;
    private bool firmwareMaintenance;
    private bool refreshing;
    private bool running;
    private bool? narrowLayout;

    public MainWindow()
    {
        InitializeComponent();
        Title = "Qmknrf5配置工具";
        viaPanel = new ViaPanel(() => keyboard.SelectedItem as DeviceChoice, () => WinRT.Interop.WindowNative.GetWindowHandle(this));
        midiPanel = new MidiPanel(DispatcherQueue);
        keyboardControls = new KeyboardControlPanel(() => keyboard.SelectedItem as DeviceChoice);
        firmwarePanel = new FirmwarePanel(() => WinRT.Interop.WindowNative.GetWindowHandle(this), DispatcherQueue, PrepareFirmwareAsync, FinishFirmware);
        AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "Qmknrf5.ico"));
        var workArea = DisplayArea.GetFromWindowId(AppWindow.Id, DisplayAreaFallback.Primary).WorkArea;
        double scale = GetDpiForWindow(WinRT.Interop.WindowNative.GetWindowHandle(this)) / 96.0;
        int width = Math.Min((int)(1240 * scale), workArea.Width - 48);
        int height = Math.Min((int)(900 * scale), workArea.Height - 48);
        AppWindow.MoveAndResize(new Windows.Graphics.RectInt32(
            workArea.X + (workArea.Width - width) / 2, workArea.Y + (workArea.Height - height) / 2, width, height));
        SystemBackdrop = new MicaBackdrop();
        connection.Items.Add("USB"); connection.Items.Add("蓝牙（已配对）"); connection.Items.Add("仅预览");
        connection.SelectedIndex = 0;
        effect.Items.Add("频谱柱"); effect.Items.Add("双心共舞"); effect.SelectedIndex = 0;
        colorMode.Items.Add("彩光"); colorMode.Items.Add("纯光"); colorMode.SelectedIndex = 0;
        foreach (var (element, id) in new (DependencyObject, string)[] {
            (connection, "ConnectionType"), (keyboard, "KeyboardDevice"), (playback, "PlaybackDevice"),
            (refresh, "RefreshDevices"), (start, "StartMusic"), (stop, "StopMusic"),
            (demo, "DemoMode"), (effect, "MusicEffect"), (colorMode, "MusicColorMode"), (chooseColor, "ChooseSolidColor"),
            (matrix,"MusicTargetMatrix"),(underglow,"MusicTargetUnderglow"),
            (solidColor, "SolidColorPicker"), (status, "StatusText"), (spectrum.View, "FrequencySpectrum") })
            AutomationProperties.SetAutomationId(element, id);
        AutomationProperties.SetAutomationId(colorHex, "SolidColorHex");

        var deviceControls = new StackPanel { Spacing = 16 };
        deviceControls.Children.Add(Heading("键盘连接", 22));
        deviceControls.Children.Add(Field("连接方式", connection));
        deviceControls.Children.Add(Field("键盘", keyboard));
        deviceControls.Children.Add(refresh);
        var bluetoothSettings = new Button { Content = "打开 Windows 蓝牙设置" };
        bluetoothSettings.Click += (_, _) =>
        {
            try { Process.Start(new ProcessStartInfo("ms-settings:bluetooth") { UseShellExecute = true }); }
            catch (Exception error) { ShowError(error.Message); }
        };
        deviceControls.Children.Add(bluetoothSettings);
        deviceControls.Children.Add(deviceStatus);
        deviceControls.Children.Add(new TextBlock { Text = "USB 可直接刷新。蓝牙请先在 Windows 设置中配对，并拔掉键盘 USB。选择的键盘供律动、灯光与演奏控制、VIA 改键使用；MIDI 页面单独选择输入端口。", TextWrapping = TextWrapping.Wrap, Opacity = .7 });
        var openMusic = new Button { Content = "前往音乐律动", HorizontalAlignment = HorizontalAlignment.Left };
        AutomationProperties.SetAutomationId(openMusic, "OpenMusicPage");
        openMusic.Click += (_, _) => Navigate("music");
        deviceControls.Children.Add(openMusic);
        AutomationProperties.SetAutomationId(deviceStatus, "DeviceStatusText");
        var controls = new StackPanel { Spacing = 16 };
        controls.Children.Add(keyboardControls.LightingView);
        controls.Children.Add(new Border { Height = 1, Background = new SolidColorBrush(Colors.Gray) { Opacity = .2 } });
        controls.Children.Add(Heading("律动设置", 22));
        controls.Children.Add(Field("播放设备", playback));
        controls.Children.Add(demo);
        controls.Children.Add(Field("矩阵律动效果", effect));
        controls.Children.Add(Field("律动配色", colorMode));
        var colorButtonContent = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10 };
        colorButtonContent.Children.Add(colorSwatch); colorButtonContent.Children.Add(colorHex);
        colorButtonContent.Children.Add(new TextBlock { Text = "选择颜色", VerticalAlignment = VerticalAlignment.Center });
        chooseColor.Content = colorButtonContent;
        chooseColor.Flyout = new Flyout { Content = solidColor };
        controls.Children.Add(chooseColor);
        var targets = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 16 };
        targets.Children.Add(matrix); targets.Children.Add(underglow);
        controls.Children.Add(new TextBlock { Text = "律动作用灯组" });
        controls.Children.Add(targets);
        controls.Children.Add(Field("发送帧率（1～30 FPS）", rate));
        controls.Children.Add(gainLabel); controls.Children.Add(gain);
        controlCard = Card(controls);

        visualization = new StackPanel { Spacing = 24 };
        var top = new StackPanel { Spacing = 8 };
        top.Children.Add(Heading("实时频谱", 24));
        top.Children.Add(new TextBlock { Text = "16 段频谱 · 从低频到高频", Opacity = 0.65 });
        top.Children.Add(spectrum.View);
        var frequencies = new Grid();
        frequencies.Children.Add(new TextBlock { Text = "40 Hz", Opacity = 0.6 });
        frequencies.Children.Add(new TextBlock { Text = "16 kHz", Opacity = 0.6, HorizontalAlignment = HorizontalAlignment.Right });
        top.Children.Add(frequencies);
        visualization.Children.Add(Card(top));
        var actions = new StackPanel { Spacing = 16 };
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12 };
        if (Application.Current.Resources.TryGetValue("AccentButtonStyle", out object value) && value is Style style) start.Style = style;
        buttons.Children.Add(start); buttons.Children.Add(stop); actions.Children.Add(buttons); actions.Children.Add(status);
        visualization.Children.Insert(0, Card(actions));
        visualization.Children.Add(new TextBlock
        {
            Text = "律动期间仍可切换页面。停止后恢复原灯效；Caps Lock 指示保留原有规则。",
            TextWrapping = TextWrapping.Wrap, Opacity = .7
        });
        body.Children.Add(controlCard); body.Children.Add(visualization);
        Arrange(950);
        var musicPage = AddPage("music", "音乐律动", "采集电脑正在播放的声音，让按键灯和底灯跟随音乐。", "\uE8D6", body);
        musicPage.SizeChanged += (_, args) => Arrange(Math.Max(0, args.NewSize.Width - 48));
        var devicePage = new StackPanel { Spacing = 20 };
        devicePage.Children.Add(Card(deviceControls)); devicePage.Children.Add(Card(keyboardControls.ConnectionView));
        AddPage("devices", "设备连接", "选择 USB 或蓝牙键盘，连接键盘控制。", "\uE772", devicePage, 760, 0);
        var midiPage = new StackPanel { Spacing = 20 };
        midiPage.Children.Add(Card(keyboardControls.PerformanceView)); midiPage.Children.Add(Card(midiPanel.View));
        AddPage("midi", "MIDI 演奏", "设置键盘演奏模式、声音与电脑 MIDI 合成器。", "\uE767", midiPage, 860);
        AddPage("via", "VIA 改键", "编辑七层键位和宏，将配置保存在键盘中。", "\uE70F", Card(viaPanel.View));
        AddPage("flash", "固件刷入", "应用固件与 Bootloader 升级，支持 USB DFU 和 J-Link / SWD。", "\uE896", Card(firmwarePanel.View), 980);
        var header = new StackPanel { Spacing = 6, Margin = new Thickness(24, 24, 24, 0) };
        header.Children.Add(pageTitle); header.Children.Add(pageDescription);
        var footer = new StackPanel { Spacing = 6, Padding = new Thickness(16, 12, 16, 16) };
        footer.Children.Add(selectedDevice); footer.Children.Add(musicActivity);
        navigation.PaneFooter = footer;
        navigation.RegisterPropertyChangedCallback(NavigationView.IsPaneOpenProperty, (_, _) => footer.Visibility = navigation.IsPaneOpen ? Visibility.Visible : Visibility.Collapsed);
        AutomationProperties.SetAutomationId(selectedDevice, "SelectedDeviceSummary");
        AutomationProperties.SetAutomationId(musicActivity, "MusicActivity");
        var shell = new Grid();
        shell.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        shell.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        shell.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        notice.Margin = new Thickness(24, 12, 24, 0);
        shell.Children.Add(header); shell.Children.Add(notice); shell.Children.Add(pageHost);
        Grid.SetRow(notice, 1); Grid.SetRow(pageHost, 2);
        navigation.Content = shell;
        AutomationProperties.SetAutomationId(navigation, "MainNavigation");
        navigation.SelectionChanged += (_, args) =>
        {
            if (args.SelectedItem is NavigationViewItem { Tag: string key }) ShowPage(key);
        };
        navigation.ItemInvoked += (_, _) => { if (navigation.DisplayMode == NavigationViewDisplayMode.Minimal) navigation.IsPaneOpen = false; };
        Content = navigation;
        Navigate("devices");
        bool initialized = false;
        navigation.Loaded += async (_, _) =>
        {
            if (initialized) return;
            initialized = true;
            Program.Trace("Navigation loaded"); await RefreshAsync(); await midiPanel.RefreshAsync(); Program.Trace("Initial refresh complete");
        };
        keyboard.SelectionChanged += async (_, _) => {
            var choice=keyboard.SelectedItem as DeviceChoice;
            selectedDevice.Text=choice?.Name??"尚未选择键盘";
            await keyboardControls.SelectDeviceAsync(choice);
        };
        refresh.Click += async (_, _) => await RefreshAsync();
        connection.SelectionChanged += async (_, _) => { if (!running) await RefreshAsync(); };
        start.Click += async (_, _) => await StartAsync();
        stop.Click += (_, _) => { sessionCancellation?.Cancel(); status.Text = "正在停止并恢复灯效…"; stop.IsEnabled = false; };
        gain.ValueChanged += (_, _) => { gainLabel.Text = $"灵敏度  {gain.Value:+0;-0;0} dB"; UpdateSettings(); };
        rate.ValueChanged += (_, _) => UpdateSettings();
        effect.SelectionChanged += (_, _) => UpdateSettings();
        colorMode.SelectionChanged += (_, _) => UpdateSettings();
        solidColor.ColorChanged += (_, args) =>
        {
            colorSwatch.Background = new SolidColorBrush(args.NewColor);
            colorHex.Text = $"#{args.NewColor.R:X2}{args.NewColor.G:X2}{args.NewColor.B:X2}";
            UpdateSettings();
        };
        matrix.Checked += (_, _) => UpdateSettings(); matrix.Unchecked += (_, _) => UpdateSettings();
        underglow.Checked += (_, _) => UpdateSettings(); underglow.Unchecked += (_, _) => UpdateSettings();
        demo.Toggled += (_, _) => { UpdateSettings(); UpdateControls(); };
        previewTimer.Tick += (_, _) => spectrum.SetLevels(Volatile.Read(ref latestBands));
        previewTimer.Start();
        AppWindow.Closing += async (_, args) =>
        {
            if (closeReady) return;
            args.Cancel = true;
            if (closing || checkingClose) return;
            checkingClose = true;
            try { if (!await firmwarePanel.ConfirmCloseAsync()) return; }
            finally { checkingClose = false; }
            closing = true;
            refreshCancellation?.Cancel(); sessionCancellation?.Cancel();
            if (sessionTask is { } task) { try { await task; } catch (Exception) { } }
            await firmwarePanel.CloseAsync();
            await viaPanel.CloseAsync();
            await keyboardControls.CloseAsync();
            await midiPanel.CloseAsync();
            closeReady = true; previewTimer.Stop(); Close();
        };
    }

    private static TextBlock Heading(string text, double size) => new() { Text = text, FontSize = size, FontWeight = FontWeights.SemiBold, TextWrapping = TextWrapping.Wrap };
    private static StackPanel Field(string label, UIElement input)
    {
        var panel = new StackPanel { Spacing = 8 };
        panel.Children.Add(new TextBlock { Text = label }); panel.Children.Add(input);
        AutomationProperties.SetName(input, label);
        return panel;
    }
    private static Border Card(UIElement content) => new()
    {
        Child = content, Padding = new Thickness(24), CornerRadius = new CornerRadius(12), BorderThickness = new Thickness(1),
        BorderBrush = new SolidColorBrush(Colors.Gray) { Opacity = 0.15 },
        Background = Application.Current.Resources.TryGetValue("CardBackgroundFillColorDefaultBrush", out object brush) ? (Brush)brush : new SolidColorBrush(Colors.Transparent)
    };

    private ScrollViewer AddPage(string key, string title, string description, string glyph, UIElement content, double maxWidth = 1120, int? position = null)
    {
        var item = new NavigationViewItem { Content = title, Tag = key, Icon = new FontIcon { Glyph = glyph } };
        ToolTipService.SetToolTip(item, title);
        AutomationProperties.SetAutomationId(item, $"Nav{key}");
        AutomationProperties.SetHelpText(item, description);
        var container = new Grid { Padding = new Thickness(24), MaxWidth = maxWidth, HorizontalAlignment = HorizontalAlignment.Left, VerticalAlignment = VerticalAlignment.Top };
        container.Children.Add(content);
        var scroll = new ScrollViewer { Content = container, Visibility = Visibility.Collapsed,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled, HorizontalContentAlignment = HorizontalAlignment.Stretch };
        scroll.SizeChanged += (_, args) =>
        {
            if (args.NewSize.Width > 0) container.Width = Math.Min(maxWidth, args.NewSize.Width);
        };
        AutomationProperties.SetAutomationId(scroll, $"Page{key}");
        pages.Add(key, (item, scroll)); pageHost.Children.Add(scroll);
        if (position is int index) navigation.MenuItems.Insert(index, item);
        else navigation.MenuItems.Add(item);
        return scroll;
    }
    private void Navigate(string key)
    {
        navigation.SelectedItem = pages[key].Item;
        ShowPage(key);
    }
    private void ShowPage(string key)
    {
        foreach (var pair in pages) pair.Value.Page.Visibility = pair.Key == key ? Visibility.Visible : Visibility.Collapsed;
        var item = pages[key].Item;
        pageTitle.Text = (string)item.Content;
        pageDescription.Text = AutomationProperties.GetHelpText(item);
        // Keep panel instances alive: navigating must not reopen ports or cancel sessions.
        Program.Trace($"Page selected: {key}");
    }

    private void Arrange(double width)
    {
        bool narrow = width < 780;
        if (narrowLayout == narrow) return;
        narrowLayout = narrow;
        body.ColumnDefinitions.Clear(); body.RowDefinitions.Clear();
        body.ColumnDefinitions.Add(new ColumnDefinition { Width = narrow ? new GridLength(1, GridUnitType.Star) : new GridLength(280) });
        body.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        if (narrow) body.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        else body.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        Grid.SetRow(visualization, 0); Grid.SetColumn(visualization, narrow ? 0 : 1);
        Grid.SetRow(controlCard, narrow ? 1 : 0);
    }

    private void UpdateSettings()
    {
        LightTarget target = (matrix.IsChecked == true ? LightTarget.Matrix : 0) | (underglow.IsChecked == true ? LightTarget.Underglow : 0);
        int fps = double.IsFinite(rate.Value) ? Math.Clamp((int)rate.Value, 1, 30) : 25;
        MusicEffect selectedEffect = effect.SelectedIndex == 1 ? MusicEffect.DualDance : MusicEffect.Spectrum;
        MusicColorMode selectedColor = colorMode.SelectedIndex == 1 ? MusicColorMode.Solid : MusicColorMode.Colorful;
        chooseColor.IsEnabled = selectedColor == MusicColorMode.Solid;
        var rgb = new MusicRgb(solidColor.Color.R, solidColor.Color.G, solidColor.Color.B);
        spectrum.SetColor(selectedColor == MusicColorMode.Solid ? rgb : null);
        Volatile.Write(ref settings, new MusicSettings(fps, gain.Value, target, demo.IsOn, selectedEffect, selectedColor, rgb));
    }

    private void UpdateControls()
    {
        connection.IsEnabled = !running && !firmwareMaintenance;
        keyboard.IsEnabled = !running && !refreshing && !firmwareMaintenance && connection.SelectedIndex != 2;
        playback.IsEnabled = !running && !refreshing && !firmwareMaintenance && !demo.IsOn;
        demo.IsEnabled = !running && !firmwareMaintenance;
        refresh.IsEnabled = !running && !refreshing && !firmwareMaintenance;
        start.IsEnabled = !running && !refreshing && !firmwareMaintenance;
        stop.IsEnabled = running && !firmwareMaintenance;
        musicActivity.Text = running ? "● 律动运行中" : "律动未运行";
    }

    private async Task RefreshAsync()
    {
        refreshCancellation?.Cancel();
        var cancellation = new CancellationTokenSource(TimeSpan.FromSeconds(15));
        refreshCancellation = cancellation;
        refreshing = true; UpdateControls();
        var previousKeyboard=keyboard.SelectedItem as DeviceChoice;
        keyboard.ItemsSource = null;
        keyboard.SelectedIndex = -1;
        deviceStatus.Text = "正在查找设备…";
        int index = connection.SelectedIndex;
        try
        {
            var kind = index == 0 ? ConnectionKind.Usb : index == 1 ? ConnectionKind.Bluetooth : ConnectionKind.Preview;
            var devices = await DeviceCatalog.GetKeyboardsAsync(kind, cancellation.Token);
            var audio = await Task.Run(DeviceCatalog.GetPlaybackDevices, cancellation.Token);
            if (cancellation.IsCancellationRequested || closing) return;
            keyboard.ItemsSource = devices;keyboard.SelectedItem=devices.FirstOrDefault(item=>item.Id==previousKeyboard?.Id&&item.Kind==previousKeyboard.Kind)??devices.FirstOrDefault();
            string? previousAudio = (playback.SelectedItem as PlaybackChoice)?.Id;
            playback.ItemsSource = audio;
            playback.SelectedItem = audio.FirstOrDefault(item => item.Id == previousAudio) ?? audio[0];
            deviceStatus.Text = index == 2 ? "离线预览已就绪" : devices.Count > 0 ? $"找到 {devices.Count} 个设备，请选择键盘后开始" :
                index == 0 ? "未找到 USB 音乐接口，请检查 R6 固件与连接" : "未找到已配对蓝牙设备，请先在 Windows 设置中配对";
        }
        catch (OperationCanceledException) { if (!closing && ReferenceEquals(refreshCancellation, cancellation)) deviceStatus.Text = "查找已取消或超时，请重试"; }
        catch (Exception error) { if (!closing && ReferenceEquals(refreshCancellation, cancellation)) { deviceStatus.Text = "查找失败，请检查连接后重试。"; ShowError(error.Message); } }
        finally
        {
            if (ReferenceEquals(refreshCancellation, cancellation)) { refreshCancellation = null; refreshing = false; if (!closing) UpdateControls(); }
            cancellation.Dispose();
        }
    }

    private async Task StartAsync()
    {
        if (running || closing || firmwareMaintenance) return;
        if (keyboard.SelectedItem is not DeviceChoice choice) { ShowError("请先在“设备连接”中选择键盘，或切换到仅预览。"); Navigate("devices"); return; }
        UpdateSettings();
        MusicSettings current = Volatile.Read(ref settings);
        string audioId = (playback.SelectedItem as PlaybackChoice)?.Id ?? "";
        var cancellation = new CancellationTokenSource();
        sessionCancellation = cancellation;
        running = true; notice.IsOpen = false; status.Text = "正在连接…"; UpdateControls();
        sessionTask = RunSessionAsync(choice, audioId, current.Demo, cancellation.Token);
        try { await sessionTask; }
        catch (OperationCanceledException) { if (!closing) status.Text = "已停止 · 原灯效已恢复"; }
        catch (Exception error) { if (!closing) ShowError(error.Message); }
        finally
        {
            sessionTask = null; sessionCancellation = null; cancellation.Dispose(); running = false;
            Volatile.Write(ref latestBands, new byte[16]);
            if (!closing) UpdateControls();
        }
    }

    private async Task RunSessionAsync(DeviceChoice choice, string audioId, bool useDemo, CancellationToken token)
    {
        using var connectTimeout = CancellationTokenSource.CreateLinkedTokenSource(token);
        connectTimeout.CancelAfter(TimeSpan.FromSeconds(25));
        IMusicTransport connected;
        try { connected = await DeviceCatalog.ConnectAsync(choice, connectTimeout.Token); }
        catch (OperationCanceledException) when (!token.IsCancellationRequested)
        { throw new TimeoutException("连接键盘超时，请检查连接方式后重试。"); }
        await using var transport = connected;
        token.ThrowIfCancellationRequested();
        using IAudioSource? audio = useDemo ? null : await Task.Run(() => new LoopbackAudio(audioId));
        await keyboardControls.EnableMusicLightsAsync(choice,Volatile.Read(ref settings).Target,token);
        token.ThrowIfCancellationRequested();
        using var power = new WindowsPowerRequest();
        status.Text = choice.Kind == ConnectionKind.Preview ? "离线预览中 · 不向键盘发送数据" : "已连接 · 音乐律动中（允许熄屏）";
        await MusicSession.RunAsync(transport, audio, () => Volatile.Read(ref settings),
            bands => Volatile.Write(ref latestBands, bands), token);
    }

    private async Task<bool> PrepareFirmwareAsync()
    {
        firmwareMaintenance = true; UpdateControls();
        try
        {
            if (!await viaPanel.SuspendForFirmwareAsync()) { FinishFirmware(); return false; }
            sessionCancellation?.Cancel();
            if (sessionTask is { } task) { try { await task; } catch (Exception) { /* USB may already have re-enumerated into DFU. */ } }
            await keyboardControls.SuspendForFirmwareAsync();
            await midiPanel.SuspendForFirmwareAsync();
            return true;
        }
        catch { FinishFirmware(); throw; }
    }
    private void FinishFirmware()
    {
        firmwareMaintenance = false;
        viaPanel.ResumeAfterFirmware(); keyboardControls.ResumeAfterFirmware(); midiPanel.ResumeAfterFirmware();
        if (!closing) UpdateControls();
    }

    private void ShowError(string message)
    {
        status.Text = "未能继续，请检查连接或播放设备";
        notice.Title = "需要处理"; notice.Message = message; notice.Severity = InfoBarSeverity.Error; notice.IsOpen = true;
    }

    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr window);
}
