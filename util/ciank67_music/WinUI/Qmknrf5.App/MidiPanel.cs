// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
using Qmknrf5.Core;
using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Windows.Devices.Enumeration;
using Windows.Devices.Midi;

namespace Qmknrf5.App;

internal sealed class MidiPanel
{
    private readonly ComboBox input = new() { HorizontalAlignment = HorizontalAlignment.Stretch, PlaceholderText = "请选择键盘 MIDI 输入" };
    private readonly ComboBox output = new() { HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly ComboBox instrument = new() { HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly Button refresh = new() { Content = "刷新 MIDI 设备" };
    private readonly Button start = new() { Content = "开始演奏" };
    private readonly Button stop = new() { Content = "停止演奏", IsEnabled = false };
    private readonly ToggleSwitch soft = new() { Header="柔和演奏", IsOn=true };
    private readonly Button example = new() { Content="试听《小星星》" };
    private CancellationTokenSource? exampleCancellation;
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
    private bool refreshing, closed, suspended;
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
        panel.Children.Add(new TextBlock { Text = "电脑合成器", FontSize = 24, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold });
        panel.Children.Add(new TextBlock { Text = "让键盘音符通过电脑扬声器发声，可与音乐律动同时使用。", TextWrapping = TextWrapping.Wrap, Opacity = .7 });
        panel.Children.Add(Field("键盘 MIDI 输入（USB / 蓝牙）", input));
        panel.Children.Add(Field("合成器 / MIDI 输出", output));
        foreach (var item in Instruments) instrument.Items.Add(item.Name);
        instrument.SelectedIndex = 0;
        panel.Children.Add(Field("音色", instrument));
        panel.Children.Add(soft);
        panel.Children.Add(new TextBlock {Text="柔和演奏降低固定满力度，减轻低音并加入少量混响。试听版含完整旋律和吉他分解伴奏，无需连接键盘。",TextWrapping=TextWrapping.Wrap,Opacity=.7});
        panel.Children.Add(example);
        var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        actions.Children.Add(start); actions.Children.Add(stop); panel.Children.Add(actions);
        var tools = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        tools.Children.Add(refresh); tools.Children.Add(panic); panel.Children.Add(tools);
        panel.Children.Add(status); panel.Children.Add(monitor);
        panel.Children.Add(new TextBlock { Text = "在键盘控制中选择“MIDI 打字伴奏”后可按多排曲谱练习；物理快捷键按配列而定。刷入固件或切换连接后，请停止演奏并刷新 MIDI 设备；其他 MIDI 软件需释放该输入端口。", TextWrapping = TextWrapping.Wrap, Opacity = .7 });
        View = panel;
        foreach (var (element, id) in new (DependencyObject, string)[] {
            (input,"MidiInput"),(output,"MidiOutput"),(instrument,"MidiInstrument"),(soft,"MidiSoftPlaying"),(example,"MidiTwinkleExample"),
            (start,"StartMidi"),(stop,"StopMidi"),(refresh,"RefreshMidi"),(panic,"MidiPanic"),
            (status,"MidiStatus"),(monitor,"MidiMonitor") }) AutomationProperties.SetAutomationId(element,id);
        input.SelectionChanged += (_,_) => Controls();
        output.SelectionChanged += (_,_) => Controls();
        refresh.Click += async (_, _) => await RefreshAsync();
        start.Click += async (_, _) => { openTask = StartAsync(); await openTask; };
        stop.Click += (_, _) => Stop("已停止演奏 · 音符已释放");
        panic.Click += (_, _) => {if(exampleCancellation is not null)Stop("已停止试听 · 音符已释放");else router?.AllNotesOff();};
        example.Click += async (_,_) => {if(exampleCancellation is not null){Stop("已停止试听 · 音符已释放");return;}openTask=PlayExampleAsync();await openTask;};
        soft.Toggled += (_,_) => {try{router?.SetSoftPlaying(soft.IsOn);}catch(Exception ex){Stop($"演奏设置失败：{ex.Message}");}};
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
        bool busy = router is not null || opening is not null || exampleCancellation is not null;
        input.IsEnabled = output.IsEnabled = !busy && !refreshing && !closed && !suspended;
        start.IsEnabled = !busy && !refreshing && !closed && !suspended && input.SelectedItem is MidiPortChoice;
        stop.IsEnabled = (busy || refreshing) && !closed;
        refresh.IsEnabled = !busy && !refreshing && !closed && !suspended;
        panic.IsEnabled = router is not null && !closed;
        instrument.IsEnabled=soft.IsEnabled=!closed&&!suspended&&exampleCancellation is null&&opening is null;
        example.IsEnabled=!closed&&!suspended&&(exampleCancellation is not null||(!busy&&!refreshing&&output.SelectedItem is MidiPortChoice));
        example.Content=exampleCancellation is null?"试听《小星星》":"停止试听";
    }

    public async Task RefreshAsync()
    {
        if (closed || suspended || refreshing || router is not null || opening is not null) return;
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
                    !inputs.Any(i => i.Name.Equals(d.Name,StringComparison.OrdinalIgnoreCase)))
                    .Select(d => new MidiPortChoice(d.Id,d.Name))).ToArray();
            input.ItemsSource = inputs;
            input.SelectedItem = inputs.FirstOrDefault(d => d.Id == previousInput) ??
                (inputs.Length==1?inputs[0]:null);
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
        if (closed || suspended || opening is not null || router is not null || exampleCancellation is not null || input.SelectedItem is not MidiPortChoice source ||
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
                }),cancellation.Token,soft.IsOn);
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

    private async Task PlayExampleAsync()
    {
        if(closed||suspended||router is not null||opening is not null||exampleCancellation is not null||output.SelectedItem is not MidiPortChoice destination)return;
        int current=++generation;
        var cancellation=new CancellationTokenSource();exampleCancellation=cancellation;
        using var deadline=CancellationTokenSource.CreateLinkedTokenSource(cancellation.Token);deadline.CancelAfter(TimeSpan.FromSeconds(20));
        opening=deadline;Controls();status.Text="正在打开合成器试听…";
        MidiRouter? player=null;
        try
        {
            var song=TwinkleSong.Create(Instruments[Math.Max(0,instrument.SelectedIndex)].Program);
            player=await MidiRouter.OpenOutputAsync(destination.Id,ex=>dispatcher.TryEnqueue(()=>{
                if(!closed&&generation==current)Stop($"试听中断：{ex.Message}");
            }),deadline.Token);
            if(closed||generation!=current||cancellation.IsCancellationRequested)return;
            if(ReferenceEquals(opening,deadline))opening=null;
            router=player;Controls();
            status.Text="《小星星》完整版 · 92 BPM · 中音旋律＋分解和弦（试听不改变键盘模式）";
            var watch=Stopwatch.StartNew();
            foreach(var item in song.Events)
            {
                var delay=song.TimeAt(item.Tick)-watch.Elapsed;
                if(delay>TimeSpan.Zero)await Task.Delay(delay,cancellation.Token);
                cancellation.Token.ThrowIfCancellationRequested();
                IMidiMessage message=(item.Status&0xf0) switch {
                    0x80=>new MidiNoteOffMessage((byte)item.Channel,item.Data1,item.Data2),
                    0x90=>new MidiNoteOnMessage((byte)item.Channel,item.Data1,item.Data2),
                    0xb0=>new MidiControlChangeMessage((byte)item.Channel,item.Data1,item.Data2),
                    0xc0=>new MidiProgramChangeMessage((byte)item.Channel,item.Data1),
                    _=>throw new InvalidOperationException("无效试听事件")
                };
                player.Forward(message);
            }
            if(!closed&&generation==current)status.Text="试听完成 · 可按《小星星演奏说明》用中下排按键练习";
        }
        catch(OperationCanceledException){if(!closed&&generation==current)status.Text="试听已取消或打开超时";}
        catch(Exception ex){if(!closed&&generation==current)status.Text=$"试听失败：{ex.Message}";}
        finally
        {
            player?.Dispose();
            if(ReferenceEquals(router,player))router=null;
            if(ReferenceEquals(opening,deadline))opening=null;
            if(ReferenceEquals(exampleCancellation,cancellation))exampleCancellation=null;
            cancellation.Dispose();Controls();
        }
    }

    private void Stop(string message)
    {
        generation++; opening?.Cancel(); discovery?.Cancel(); exampleCancellation?.Cancel();
        router?.Dispose(); router = null; activeInput = null;
        status.Text = message; Controls();
    }

    public async Task SuspendForFirmwareAsync()
    {
        suspended = true; Stop("刷入前已停止 MIDI 演奏。用新固件重新连接后请刷新 MIDI 设备。");
        if (openTask is { } task) await task;
        Controls();
    }
    public void ResumeAfterFirmware() { suspended = false; Controls(); }
    public async Task CloseAsync()
    {
        closed = true; Stop("已关闭"); timer.Stop();
        if (watcher.Status is DeviceWatcherStatus.Started or DeviceWatcherStatus.EnumerationCompleted) watcher.Stop();
        if (openTask is { } task) await task;
    }
}
