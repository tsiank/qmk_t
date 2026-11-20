// SPDX-License-Identifier: GPL-2.0-or-later
using Ciank67.Music.Core;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;

namespace Ciank67.Music.App;

internal sealed class KeyboardControlPanel
{
    private readonly Func<DeviceChoice?> selected;
    private readonly Button connect = new() { Content = "连接键盘控制" };
    private readonly Button disconnect = new() { Content = "断开控制", IsEnabled = false };
    private readonly ToggleSwitch matrix = new() { Header = "按键矩阵灯" };
    private readonly ToggleSwitch under = new() { Header = "底灯" };
    private readonly ToggleSwitch audio = new() { Header = "键盘本地声音" };
    private readonly ToggleSwitch clicky = new() { Header = "键盘按键音" };
    private readonly ComboBox mode = new() { HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly TextBlock status = new() { Text = "选择左侧键盘后连接，可独立控制，无须开始律动。需要 R19 固件。", TextWrapping = TextWrapping.Wrap };
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromSeconds(1) };
    private readonly CancellationTokenSource cancellation = new();
    private IKeyboardControlTransport? transport;
    private Task? operation;
    private bool syncing, busy, closed;
    public UIElement View { get; }

    public KeyboardControlPanel(Func<DeviceChoice?> selected)
    {
        this.selected = selected;
        var panel = new StackPanel { Spacing = 12 };
        panel.Children.Add(new TextBlock { Text = "键盘开关", FontSize = 24, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold });
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        buttons.Children.Add(connect); buttons.Children.Add(disconnect); panel.Children.Add(buttons);
        panel.Children.Add(matrix); panel.Children.Add(under); panel.Children.Add(audio); panel.Children.Add(clicky);
        panel.Children.Add(new TextBlock { Text = "键盘演奏模式" });
        foreach (string name in new[] { "关闭演奏 · 正常打字", "本地音乐 · 蜂鸣器演奏", "MIDI 纯演奏", "MIDI 打字伴奏" }) mode.Items.Add(name);
        panel.Children.Add(mode); panel.Children.Add(status);
        panel.Children.Add(new TextBlock { Text = "这里控制键盘实际开关；下方 MIDI 演奏控制电脑合成器。键盘按键操作后，状态会自动同步。", TextWrapping = TextWrapping.Wrap, Opacity = .7 });
        View = panel;
        foreach (var (element,id) in new (DependencyObject,string)[] {
            (connect,"ConnectKeyboardControl"),(disconnect,"DisconnectKeyboardControl"),
            (matrix,"KeyboardMatrixPower"),(under,"KeyboardUnderglowPower"),(audio,"KeyboardAudioPower"),
            (clicky,"KeyboardClickyPower"),(mode,"KeyboardPerformanceMode"),(status,"KeyboardControlStatus") })
            AutomationProperties.SetAutomationId(element,id);
        connect.Click += (_,_) => Begin(ConnectAsync);
        disconnect.Click += (_,_) => Begin(DisconnectAsync);
        matrix.Toggled += (_,_) => Change(1,matrix.IsOn);
        under.Toggled += (_,_) => Change(2,under.IsOn);
        audio.Toggled += (_,_) => Change(4,audio.IsOn);
        clicky.Toggled += (_,_) => Change(8,clicky.IsOn);
        mode.SelectionChanged += (_,_) => { if (!syncing && mode.SelectedIndex >= 0) Begin(() => RequestAsync(16,0,(KeyboardMusicMode)mode.SelectedIndex)); };
        timer.Tick += (_,_) => { if (transport is not null && !mode.IsDropDownOpen) Begin(() => RequestAsync()); };
        Controls(); timer.Start();
    }
    private void Controls()
    {
        foreach (Control control in new Control[] {matrix,under,audio,clicky,mode}) control.IsEnabled = !closed && !busy && transport is not null;
        connect.IsEnabled = !closed && !busy && transport is null;
        disconnect.IsEnabled = !closed && !busy && transport is not null;
    }
    private void Change(byte mask,bool on)
    {
        if (!syncing) Begin(() => RequestAsync(mask,on ? mask : (byte)0));
    }
    private void Begin(Func<Task> action)
    {
        if (syncing || busy || closed) return;
        operation = RunAsync(action);
    }
    private async Task RunAsync(Func<Task> action)
    {
        busy = true; Controls();
        try { await action(); }
        catch (OperationCanceledException)
        {
            if (!closed) status.Text = "控制连接已取消或超时，请重新连接。";
            await DisconnectAsync(false);
        }
        catch (Exception ex)
        {
            status.Text = ex.Message;
            await DisconnectAsync(false);
        }
        finally { busy = false; Controls(); }
    }
    private async Task ConnectAsync()
    {
        DeviceChoice? choice = selected();
        if (choice is null || choice.Kind == ConnectionKind.Preview) throw new IOException("请先在左侧选择 USB 或蓝牙键盘。");
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellation.Token);
        timeout.CancelAfter(TimeSpan.FromSeconds(20));
        status.Text = "正在连接并读取键盘开关…";
        var opened = await DeviceCatalog.ConnectAsync(choice,timeout.Token);
        if (opened is not IKeyboardControlTransport control) { await opened.DisposeAsync(); throw new IOException("该连接不支持键盘控制。"); }
        transport = control;
        await RequestAsync();
        status.Text = $"已连接 · {choice.Name} · 状态自动同步中";
    }
    private async Task RequestAsync(byte mask=0,byte values=0,KeyboardMusicMode selectedMode=KeyboardMusicMode.Off)
    {
        if (transport is null) return;
        var state = await transport.ControlAsync(mask,values,selectedMode,cancellation.Token);
        if (closed) return;
        syncing = true;
        try
        {
            matrix.IsOn = state.Matrix; under.IsOn = state.Underglow;
            audio.IsOn = state.Audio; clicky.IsOn = state.Clicky;
            mode.SelectedIndex = (int)state.Mode;
        }
        finally { syncing = false; }
    }
    private async Task DisconnectAsync() => await DisconnectAsync(true);
    private async Task DisconnectAsync(bool showStatus)
    {
        var previous = transport; transport = null;
        if (previous is not null) await previous.DisposeAsync();
        if (showStatus) status.Text = "控制已断开，键盘保留当前开关状态。";
    }
    public async Task CloseAsync()
    {
        closed = true; timer.Stop(); cancellation.Cancel();
        if (operation is { } task) await task;
        await DisconnectAsync(false); cancellation.Dispose();
    }
}
