// SPDX-License-Identifier: GPL-2.0-or-later
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Qmknrf5.Core;

namespace Qmknrf5.App;

internal sealed class WirelessPanel
{
    private readonly Func<DeviceChoice?> selected;
    private readonly ComboBox mode = new() { Header = "键盘输出模式", HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly ComboBox receiver = new() { Header = "USB 接收器", HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly Button read = new() { Content = "读取输出与配对状态" };
    private readonly Button apply = new() { Content = "应用输出模式" };
    private readonly Button scan = new() { Content = "查找 USB 接收器" };
    private readonly Button start = new() { Content = "开启无线配对（60 秒）" };
    private readonly Button cancel = new() { Content = "取消配对" };
    private readonly Button clear = new() { Content = "清除当前设备的 2.4G 配对" };
    private readonly Button dfu = new() { Content = "接收器进入固件刷入模式" };
    private readonly TextBlock stateText = Note("键盘输出模式可选用 USB 配置；无线配对无需客户端。");
    private readonly TextBlock pairText = Note("接收器插入 USB 后 60 秒内，同时长按键盘左右 Ctrl 3 秒即可自动配对。");
    private readonly InfoBar result = new() { IsClosable = true };
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromMilliseconds(500) };
    private readonly SemaphoreSlim gate = new(1, 1);
    private readonly CancellationTokenSource cancellation = new();
    private WirelessState? state, pairing;
    private Candidate? target;
    private bool busy, polling;
    private DateTime? savedAt;
    private bool PairActive => pairing?.PairingState is >= 1 and <= 4;
    public UIElement View { get; }
    private sealed record Candidate(DeviceChoice Choice, WirelessState State)
    {
        public override string ToString() => $"{Choice.Name} · {State.DeviceId:X16}";
    }
    public WirelessPanel(Func<DeviceChoice?> selected)
    {
        this.selected = selected;
        var panel = new StackPanel { Spacing = 12 };
        panel.Children.Add(new TextBlock { Text = "2.4G 无线配对与输出模式", FontSize = 22 });
        panel.Children.Add(stateText); panel.Children.Add(read);
        foreach (string name in new[] { "自动 · USB 优先，其次已配对 2.4G／蓝牙", "USB 有线", "蓝牙", "2.4G 接收器", "关闭按键输出" }) mode.Items.Add(name);
        panel.Children.Add(mode); panel.Children.Add(apply);
        panel.Children.Add(Note("手动输出模式保存在键盘中，插拔 USB 不会覆盖。键盘端可用 OU_USB、OU_BT、OU_2P4G、OU_AUTO 切换。"));
        panel.Children.Add(scan); panel.Children.Add(receiver); panel.Children.Add(start); panel.Children.Add(pairText);
        var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        actions.Children.Add(cancel); panel.Children.Add(actions);
        panel.Children.Add(Note("通常不需要打开此软件：插入接收器后 60 秒内，同时长按 ciank67 左右 Ctrl 3 秒，或长按已配置的 2.4G 键 3 秒，两端自动交换并保存配对。超时后重新插拔接收器即可重试，也可用此处的可选管理入口开启窗口。已有绑定自动重连；两端需 R3 或更新固件。"));
        panel.Children.Add(clear); panel.Children.Add(dfu); panel.Children.Add(result); View = panel;
        foreach (var (element, id) in new (DependencyObject, string)[] {
            (mode,"WirelessOutputMode"),(apply,"ApplyWirelessMode"),(read,"ReadWirelessState"),
            (scan,"ScanWirelessDevices"),(receiver,"WirelessReceiver"),(start,"StartWirelessPairing"),
            (cancel,"CancelWirelessPairing"),(pairText,"WirelessPairingStatus"),
            (clear,"ClearWirelessPairing"),(dfu,"ReceiverEnterDfu"),(stateText,"WirelessStateText"),(result,"WirelessResult") })
            AutomationProperties.SetAutomationId(element, id);
        read.Click += async (_, _) => await RunAsync(ReadSelectedAsync);
        apply.Click += async (_, _) => await RunAsync(async token => {
            if (mode.SelectedIndex < 0) throw new IOException("请选择输出模式。");
            var choice = SelectedUsb(); await using var transport = await OpenAsync(choice, token);
            Apply(choice, await new WirelessClient(transport).SelectModeAsync((WirelessMode)mode.SelectedIndex, token));
            Success("输出模式已保存。");
        });
        scan.Click += async (_, _) => await RunAsync(async token => {
            var choices = await DeviceCatalog.GetKeyboardsAsync(ConnectionKind.Receiver, token);
            List<Candidate> found = [];
            foreach (var choice in choices.Where(item => item.IsReceiver)) {
                try {
                    await using var transport = await OpenAsync(choice, token);
                    var current = await new WirelessClient(transport).ReadAsync(token);
                    if (current.Role == WirelessRole.Receiver && current.Version >= 2) found.Add(new(choice, current));
                } catch (NotSupportedException) { }
            }
            receiver.ItemsSource = found; receiver.SelectedIndex = found.Count > 0 ? 0 : -1;
            if (found.Count == 0) throw new IOException("未找到专用 USB 接收器，请刷入 R3 专用接收端固件。键盘无需插 USB。");
        });
        start.Click += async (_, _) => await PairCommandAsync(4);
        cancel.Click += async (_, _) => await PairCommandAsync(6);
        dfu.Click += async (_, _) => await RunAsync(async token => {
            var choice = target?.Choice ?? throw new IOException("请先查找并选择 USB 接收器。");
            if (!choice.IsReceiver) throw new IOException("仅专用接收器支持此入口。");
            timer.Stop();
            await using var transport = await OpenAsync(choice, token);
            await new WirelessClient(transport).EnterDfuAsync(token);
            target = null; pairing = null; state = null;
            receiver.ItemsSource = null;
            pairText.Text = "接收器已进入 DFU，请前往“固件刷入”选择应用固件 ZIP 与 DFU 串口。";
            Success("接收器正在重启到固件刷入模式。");
        });
        clear.Click += async (_, _) => await RunAsync(async token => {
            var choice = SelectedUsb(); await using var transport = await OpenAsync(choice, token);
            Apply(choice, await new WirelessClient(transport).ClearAsync(token));
            Success("当前设备已清除 2.4G 配对，可重新开启无线配对。");
        });
        receiver.SelectionChanged += (_, _) => {
            target = receiver.SelectedItem as Candidate; pairing = target?.State; savedAt = null;
            if (pairing is not null) ShowPairing(pairing); Update();
        };
        timer.Tick += async (_, _) => {
            if (busy || polling || target is null || !gate.Wait(0)) return;
            var polled = target;
            polling = true;
            try {
                await using var transport = await OpenAsync(polled.Choice, cancellation.Token);
                var current = await new WirelessClient(transport).ReadAsync(cancellation.Token);
                if (target == polled) { Apply(polled.Choice, current); ShowPairing(current); }
            } catch (OperationCanceledException) { timer.Stop(); }
            catch (Exception error) { timer.Stop(); Error(error.Message); }
            finally { polling = false; gate.Release(); Update(); }
        };
        Update();
    }
    private async Task PairCommandAsync(byte operation) => await RunAsync(async token => {
        var choice = target?.Choice ?? throw new IOException("请先查找并选择 USB 接收器。");
        await using var transport = await OpenAsync(choice, token);
        var client = new WirelessClient(transport);
        var current = operation switch {
            4 => await client.StartPairingAsync(token),
            _ => await client.CancelPairingAsync(token) };
        if (operation == 4) savedAt = null;
        Apply(choice, current); ShowPairing(current);
    });
    private void ShowPairing(WirelessState current)
    {
        pairing = current;
        pairText.Text = current.PairingState switch {
            1 => $"配对窗口剩余 {current.PairingSeconds} 秒。请同时长按键盘左右 Ctrl 3 秒。",
            2 => "已发现键盘，正在验证双方密钥并自动配对。",
            3 => $"正在完成键盘密钥校验（标识 {current.Fingerprint:X6}），无需手动确认。",
            4 => "正在保存配对，请保持设备供电。",
            5 when current.Connected => $"无线配对已完成，键盘已连接（{current.PairId:X8}）。",
            5 => "接收器已保存，等待键盘完成保存并自动连接。",
            6 => "配对未完成，请读取存储状态后重试。",
            7 => "配对窗口已超时，保存前的旧绑定保留。",
            8 => "已取消配对，保存前的旧绑定保留。",
            _ => current.Paired ? $"接收器已绑定（{current.PairId:X8}），日常使用无需客户端。" : "接收器尚未配对，重新插入 USB 后操作键盘配对键即可。" };
        if (current.PairingState == 5 && !current.Connected) {
            savedAt ??= DateTime.UtcNow;
            if (DateTime.UtcNow - savedAt > TimeSpan.FromSeconds(30)) {
                timer.Stop(); pairText.Text = "接收器已保存，但键盘尚未连接。请检查键盘供电和 2.4G 输出模式，必要时重新配对。";
                return;
            }
        }
        if (current.PairingState is >= 1 and <= 4 || current.PairingState == 5 && !current.Connected) timer.Start();
        else timer.Stop();
    }
    private static TextBlock Note(string text) => new() { Text = text, TextWrapping = TextWrapping.Wrap, Opacity = .75 };
    private DeviceChoice SelectedUsb() => selected() is { Kind: ConnectionKind.Usb or ConnectionKind.Receiver } choice ? choice
        : throw new IOException("请在上方选择这块板子自身的 USB 设备。");
    private static async Task<IViaTransport> OpenAsync(DeviceChoice choice, CancellationToken token) =>
        new ViaTransportSession(await ViaDevices.OpenAsync(choice, token));
    private async Task ReadSelectedAsync(CancellationToken token) {
        var choice = SelectedUsb(); await using var transport = await OpenAsync(choice, token);
        Apply(choice, await new WirelessClient(transport).ReadAsync(token));
    }
    private void Apply(DeviceChoice choice, WirelessState current) {
        if (selected()?.Id != choice.Id) return;
        state = current; mode.SelectedIndex = (int)current.Preferred;
        string output = current.Current switch { WirelessMode.Usb => "USB", WirelessMode.Bluetooth => "蓝牙", WirelessMode.Radio => "2.4G", _ => "关闭" };
        stateText.Text = $"{(current.Role == WirelessRole.Receiver ? "USB 接收器" : "三模键盘")} · 当前输出 {output} · {(current.Paired ? $"已配对 {current.PairId:X8}" : "未配对")} · 无线{(current.Connected ? "已连接" : "未连接")}";
    }
    private async Task RunAsync(Func<CancellationToken, Task> action) {
        if (busy) return; busy = true; result.IsOpen = false; Update();
        bool entered = false;
        try { await gate.WaitAsync(cancellation.Token); entered = true; await action(cancellation.Token); }
        catch (NotSupportedException error) { state = null; stateText.Text = error.Message; }
        catch (OperationCanceledException) { }
        catch (Exception error) { Error(error.Message); }
        finally { if (entered) gate.Release(); busy = false; Update(); }
    }
    private void Success(string message) { result.Severity = InfoBarSeverity.Success; result.Title = "已完成"; result.Message = message; result.IsOpen = true; }
    private void Error(string message) { result.Severity = InfoBarSeverity.Error; result.Title = "未完成"; result.Message = message; result.IsOpen = true; }
    private void Update() {
        read.IsEnabled = !busy && selected()?.Kind is ConnectionKind.Usb or ConnectionKind.Receiver;
        mode.IsEnabled = apply.IsEnabled = !busy && !PairActive && state?.Role == WirelessRole.Keyboard;
        clear.IsEnabled = !busy && !PairActive && state?.Paired == true;
        scan.IsEnabled = receiver.IsEnabled = !busy && !PairActive;
        start.IsEnabled = !busy && !PairActive && target is not null;
        cancel.IsEnabled = !busy && pairing?.PairingState is >= 1 and <= 3;
        dfu.IsEnabled = !busy && (!PairActive || pairing is { PairingState: 1, Paired: true }) && target?.Choice.IsReceiver == true;
        mode.Visibility = apply.Visibility = selected()?.IsReceiver == true ? Visibility.Collapsed : Visibility.Visible;
    }
    public async Task SelectDeviceAsync() {
        state = null; Update();
        if (selected()?.Kind is not (ConnectionKind.Usb or ConnectionKind.Receiver)) { stateText.Text = "输出模式可由本板 USB 配置，也可用键盘切换键。无线配对只需接收器插 USB。"; return; }
        await RunAsync(ReadSelectedAsync);
    }
    public void Close() { timer.Stop(); cancellation.Cancel(); }
}
