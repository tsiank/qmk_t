// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Windows.Devices.Enumeration;
using Windows.Devices.Midi;

namespace Ciank67.Music.App;

internal sealed class MidiPanel
{
    private readonly ComboBox input = new() { HorizontalAlignment = HorizontalAlignment.Stretch, PlaceholderText = "请选择 ciank67 MIDI 输入" };
    private readonly ComboBox output = new() { HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly ComboBox instrument = new() { HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly Button refresh = new() { Content = "刷新 MIDI 设备" };
    private readonly Button start = new() { Content = "开始演奏" };
    private readonly Button stop = new() { Content = "停止演奏", IsEnabled = false };
    private readonly Button panic = new() { Content = "全部停音", IsEnabled = false };
    private readonly TextBlock status = new() { Text = "正在查找 MIDI 设备…", TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock monitor = new() { Text = "等待键盘音符…", TextWrapping = TextWrapping.Wrap };
    private readonly Microsoft.UI.Dispatching.DispatcherQueue dispatcher;
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromMilliseconds(100) };
    private readonly DeviceWatcher watcher;
    private MidiRouter? router;
    private CancellationTokenSource? opening;
    private CancellationTokenSource? discovery;
    private Task? openTask;
    private string? activeInput;
    private bool refreshing, closed;
    private int generation;
    private static readonly (string Name, byte Program)[] Instruments = {
        ("钢琴", 0), ("电钢琴", 4), ("音乐盒", 10), ("马林巴", 12),
        ("原声吉他", 24), ("小提琴", 40), ("弦乐", 48), ("长笛", 73),
        ("二胡（近似弓弦 · Fiddle）", 110), ("古筝（近似筝音 · Koto）", 107)
    };
    public UIElement View { get; }

    public MidiPanel(Microsoft.UI.Dispatching.DispatcherQueue dispatcher)
    {
        this.dispatcher = dispatcher;
        var panel = new StackPanel { Spacing = 12 };
        panel.Children.Add(new TextBlock { Text = "MIDI 演奏", FontSize = 24, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold });
        panel.Children.Add(new TextBlock { Text = "让键盘音符通过电脑扬声器发声，可与音乐律动同时使用。", TextWrapping = TextWrapping.Wrap, Opacity = .7 });
        panel.Children.Add(Field("键盘 MIDI 输入（USB / 蓝牙）", input));
        panel.Children.Add(Field("合成器 / MIDI 输出", output));
        foreach (var item in Instruments) instrument.Items.Add(item.Name);
        instrument.SelectedIndex = 0;
        panel.Children.Add(Field("音色", instrument));
        var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        actions.Children.Add(start); actions.Children.Add(stop); panel.Children.Add(actions);
        var tools = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        tools.Children.Add(refresh); tools.Children.Add(panic); panel.Children.Add(tools);
        panel.Children.Add(status); panel.Children.Add(monitor);
        panel.Children.Add(new TextBlock { Text = "键盘灯光层：P 开关打字伴奏，U / I 开启 / 关闭纯演奏。刷入固件或切换连接后，请停止演奏并刷新 MIDI 设备；其他 MIDI 软件需释放该输入端口。", TextWrapping = TextWrapping.Wrap, Opacity = .7 });
        View = panel;
        foreach (var (element, id) in new (DependencyObject, string)[] {
            (input,"MidiInput"),(output,"MidiOutput"),(instrument,"MidiInstrument"),
            (start,"StartMidi"),(stop,"StopMidi"),(refresh,"RefreshMidi"),(panic,"MidiPanic"),
            (status,"MidiStatus"),(monitor,"MidiMonitor") }) AutomationProperties.SetAutomationId(element,id);
        input.SelectionChanged += (_,_) => Controls();
        output.SelectionChanged += (_,_) => Controls();
        refresh.Click += async (_, _) => await RefreshAsync();
        start.Click += async (_, _) => { openTask = StartAsync(); await openTask; };
        stop.Click += (_, _) => Stop("已停止演奏 · 音符已释放");
        panic.Click += (_, _) => router?.AllNotesOff();
        instrument.SelectionChanged += (_, _) =>
        {
            try { router?.ChangeProgram(Instruments[Math.Max(0,instrument.SelectedIndex)].Program); }
            catch (Exception ex) { Stop($"音色切换失败：{ex.Message}"); }
        };
        timer.Tick += (_, _) =>
        {
            if (router is { } active)
            {
                var snapshot = active.Snapshot();
                monitor.Text = $"已转发 {snapshot.Count} 条 · {snapshot.Latest}";
            }
        };
        timer.Start();
        watcher = DeviceInformation.CreateWatcher(MidiInPort.GetDeviceSelector());
        // DeviceWatcher requires both Added and Removed handlers.
        watcher.Added += (_, _) => dispatcher.TryEnqueue(() =>
        {
            if (!closed && router is null && opening is null) _ = RefreshAsync();
        });
        watcher.Removed += (sender, device) => dispatcher.TryEnqueue(() =>
        {
            if (closed) return;
            if (activeInput == device.Id) Stop("键盘 MIDI 已断开 · 请重新连接后刷新设备");
            if (router is null && opening is null) _ = RefreshAsync();
        });
        watcher.Start();
    }

    private static UIElement Field(string text, Control control)
    {
        AutomationProperties.SetName(control,text);
        var panel = new StackPanel { Spacing = 6 };
        panel.Children.Add(new TextBlock { Text = text }); panel.Children.Add(control); return panel;
    }

    private void Controls()
    {
        bool busy = router is not null || opening is not null;
        input.IsEnabled = output.IsEnabled = !busy && !refreshing && !closed;
        start.IsEnabled = !busy && !refreshing && !closed && input.SelectedItem is MidiPortChoice;
        stop.IsEnabled = (busy || refreshing) && !closed;
        refresh.IsEnabled = !busy && !refreshing && !closed;
        panic.IsEnabled = router is not null && !closed;
    }

    public async Task RefreshAsync()
    {
        if (closed || refreshing || router is not null || opening is not null) return;
        using var cancellation = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        discovery = cancellation;
        refreshing = true; Controls(); status.Text = "正在查找 MIDI 输入和合成器…";
        try
        {
            string? previousInput = (input.SelectedItem as MidiPortChoice)?.Id;
            string? previousOutput = (output.SelectedItem as MidiPortChoice)?.Id;
            var inTask = DeviceInformation.FindAllAsync(MidiInPort.GetDeviceSelector()).AsTask().WaitAsync(cancellation.Token);
            var outTask = DeviceInformation.FindAllAsync(MidiOutPort.GetDeviceSelector()).AsTask().WaitAsync(cancellation.Token);
            await Task.WhenAll(inTask,outTask);
            if (closed) return;
            var inputs = inTask.Result.Select(d => new MidiPortChoice(d.Id,d.Name)).ToArray();
            // Keyboard MIDI OUT plays its piezo. This panel intentionally offers
            // computer synths only; feeding back into the keyboard is confusing.
            var outputs = new[] { new MidiPortChoice("", "内置合成器 · 默认电脑扬声器") }
                .Concat(outTask.Result.Where(d => !inputs.Any(i => i.Id == d.Id) &&
                    !d.Name.Contains("ciank67",StringComparison.OrdinalIgnoreCase))
                    .Select(d => new MidiPortChoice(d.Id,d.Name))).ToArray();
            input.ItemsSource = inputs;
            input.SelectedItem = inputs.FirstOrDefault(d => d.Id == previousInput) ??
                inputs.FirstOrDefault(d => d.Name.Contains("ciank67",StringComparison.OrdinalIgnoreCase));
            output.ItemsSource = outputs;
            output.SelectedItem = outputs.FirstOrDefault(d => d.Id == previousOutput) ?? outputs[0];
            status.Text = inputs.Length == 0 ? "没有 MIDI 输入，请连接键盘；蓝牙需先配对。" :
                input.SelectedItem is null ? "请选择键盘 MIDI 输入后开始演奏。" : "MIDI 已就绪 · 点击开始演奏";
        }
        catch (OperationCanceledException) { if (!closed) status.Text = "MIDI 查找已取消或超时，请确认蓝牙连接后刷新。"; }
        catch (Exception ex) { if (!closed) status.Text = $"MIDI 查找失败：{ex.Message}"; }
        finally { if (ReferenceEquals(discovery,cancellation)) discovery = null; refreshing = false; Controls(); }
    }

    private async Task StartAsync()
    {
        if (closed || opening is not null || router is not null || input.SelectedItem is not MidiPortChoice source ||
            output.SelectedItem is not MidiPortChoice destination) return;
        int current = ++generation;
        var cancellation = new CancellationTokenSource(TimeSpan.FromSeconds(20));
        opening = cancellation; activeInput = source.Id;
        status.Text = "正在打开 MIDI 输入和合成器…"; Controls();
        try
        {
            var opened = await MidiRouter.OpenAsync(source.Id,destination.Id,
                Instruments[Math.Max(0,instrument.SelectedIndex)].Program, ex => dispatcher.TryEnqueue(() =>
                {
                    if (!closed && generation == current) Stop($"MIDI 连接中断：{ex.Message}；请刷新后重试。");
                }),cancellation.Token);
            if (closed || generation != current || cancellation.IsCancellationRequested) { opened.Dispose(); return; }
            router = opened;
            status.Text = $"演奏中 · {source.Name} → {destination.Name}（切换输入请先停止演奏）";
            monitor.Text = "等待键盘音符 · 请开启 P 伴奏或 U 演奏";
        }
        catch (OperationCanceledException) { if (!closed && generation == current) status.Text = "MIDI 连接已取消或超时，请刷新后重试"; }
        catch (Exception ex) { if (!closed && generation == current) status.Text = $"MIDI 连接失败：{ex.Message}"; }
        finally
        {
            if (ReferenceEquals(opening,cancellation)) opening = null;
            cancellation.Dispose(); Controls();
        }
    }

    private void Stop(string message)
    {
        generation++; opening?.Cancel(); discovery?.Cancel();
        router?.Dispose(); router = null; activeInput = null;
        status.Text = message; Controls();
    }

    public async Task CloseAsync()
    {
        closed = true; Stop("已关闭"); timer.Stop();
        if (watcher.Status is DeviceWatcherStatus.Started or DeviceWatcherStatus.EnumerationCompleted) watcher.Stop();
        if (openTask is { } task) await task;
    }
}
