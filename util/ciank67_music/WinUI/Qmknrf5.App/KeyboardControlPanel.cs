// SPDX-License-Identifier: GPL-2.0-or-later
using Qmknrf5.Core;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;

namespace Qmknrf5.App;

internal sealed class KeyboardControlPanel
{
    private readonly Func<DeviceChoice?> selected;
    private readonly Button connect = new() { Content = "重新连接控制" };
    private readonly Button disconnect = new() { Content = "断开控制", IsEnabled = false };
    private readonly ToggleSwitch matrix = new() { Header = "按键矩阵灯" };
    private readonly ToggleSwitch under = new() { Header = "底灯" };
    private readonly ToggleSwitch audio = new() { Header = "键盘本地声音" };
    private readonly ToggleSwitch clicky = new() { Header = "键盘按键音" };
    private readonly Button syncClock = new() { Content = "手动校时" };
    private readonly TextBlock clockStatus = new() { Text = "连接后读取时钟状态。", TextWrapping = TextWrapping.Wrap };
    private readonly ComboBox screen = new() { Header="屏幕", HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly ComboBox mode = new() { HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly TextBlock status = new() { Text = "选择键盘后自动连接控制，控制开关无需开始律动。", TextWrapping = TextWrapping.Wrap };
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromSeconds(1) };
    private readonly CancellationTokenSource cancellation = new();
    private IKeyboardControlTransport? transport;
    private KeyboardControlState? lastState;
    public event Action<byte>? CapabilitiesChanged;
    private Task? operation;
    private CancellationTokenSource deviceCancellation = new();
    private DeviceChoice? device;
    private int generation, pending;
    private bool syncing, polling, closed, suspended, manualDisconnect;
    private DateTime retryAfter;
    private DateTime? lastClockSync;
    private bool busy => pending > 0;
    public UIElement ConnectionView { get; }
    public UIElement LightingView { get; }
    public UIElement PerformanceView { get; }

    public KeyboardControlPanel(Func<DeviceChoice?> selected)
    {
        this.selected = selected;
        var connectionPanel = new StackPanel { Spacing = 12 };
        connectionPanel.Children.Add(Heading("键盘控制"));
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        buttons.Children.Add(connect); buttons.Children.Add(disconnect); connectionPanel.Children.Add(buttons);
        connectionPanel.Children.Add(status);
        foreach(string name in new[]{"自动 · USB 常亮／蓝牙空闲熄屏","常亮","熄屏"})screen.Items.Add(new ComboBoxItem {Content=name});
        connectionPanel.Children.Add(screen);
        connectionPanel.Children.Add(syncClock); connectionPanel.Children.Add(clockStatus);
        connectionPanel.Children.Add(Note("首次连接仅在键盘未校时时自动校准。之后由键盘独立走时；需要纠偏时点击“手动校时”，使用电脑当前时间校准。"));
        connectionPanel.Children.Add(Note("自动模式下蓝牙空闲 60 秒熄屏，按键或旋钮唤醒。熄屏模式会保持关闭；设置保持到键盘重启。"));
        connectionPanel.Children.Add(Note("连接后可在“音乐律动”控制灯光开关，在“MIDI 演奏”控制键盘声音与演奏模式。切换页面保持连接，键盘按键修改后也会自动同步。"));
        ConnectionView = connectionPanel;

        var lightingPanel = new StackPanel { Spacing = 12 };
        lightingPanel.Children.Add(Heading("灯光开关"));
        lightingPanel.Children.Add(matrix); lightingPanel.Children.Add(under);
        var lightingStatus = Note(status.Text);
        lightingPanel.Children.Add(lightingStatus);
        LightingView = lightingPanel;

        var performancePanel = new StackPanel { Spacing = 12 };
        performancePanel.Children.Add(Heading("键盘演奏与声音"));
        performancePanel.Children.Add(new TextBlock { Text = "键盘演奏模式" });
        foreach (string name in new[] { "关闭演奏 · 正常打字", "本地音乐 · 蜂鸣器演奏", "MIDI 纯演奏", "MIDI 打字伴奏" }) mode.Items.Add(new ComboBoxItem { Content=name });
        performancePanel.Children.Add(mode);
        var sounds = new Grid { ColumnSpacing = 16 };
        sounds.ColumnDefinitions.Add(new ColumnDefinition()); sounds.ColumnDefinitions.Add(new ColumnDefinition());
        sounds.Children.Add(audio); sounds.Children.Add(clicky); Grid.SetColumn(clicky, 1);
        performancePanel.Children.Add(sounds);
        performancePanel.Children.Add(Note("本地声音由键盘发出。要让电脑播放 MIDI 音符，还需在下方电脑合成器中开始演奏。"));
        var performanceStatus = Note(status.Text);
        performancePanel.Children.Add(performanceStatus);
        PerformanceView = performancePanel;
        void UpdateStatus()
        {
            string message = status.Text.StartsWith("已连接 ·", StringComparison.Ordinal) ? "已连接键盘控制，状态自动同步。" : status.Text;
            lightingStatus.Text = performanceStatus.Text = message;
        }
        status.RegisterPropertyChangedCallback(TextBlock.TextProperty, (_, _) => UpdateStatus());
        AutomationProperties.SetAutomationId(lightingStatus, "LightingControlStatus");
        AutomationProperties.SetAutomationId(performanceStatus, "PerformanceControlStatus");
        UpdateStatus();
        foreach (var (element,id) in new (DependencyObject,string)[] {
            (connect,"ConnectKeyboardControl"),(disconnect,"DisconnectKeyboardControl"),(screen,"ScreenPowerMode"),(syncClock,"SyncKeyboardClock"),(clockStatus,"KeyboardClockStatus"),
            (matrix,"KeyboardMatrixPower"),(under,"KeyboardUnderglowPower"),(audio,"KeyboardAudioPower"),
            (clicky,"KeyboardClickyPower"),(mode,"KeyboardPerformanceMode"),(status,"KeyboardControlStatus") })
            AutomationProperties.SetAutomationId(element,id);
        connect.Click += async (_,_) => await SelectDeviceAsync(selected(), force: true);
        disconnect.Click += async (_,_) => await PauseAsync();
        syncClock.Click += (_,_) => Begin(SynchronizeClockAsync);
        screen.SelectionChanged += (_,_) => {
            if(!syncing&&screen.SelectedIndex>=0)Begin(async token=>{
                if(transport is null)throw new IOException("键盘控制尚未连接。");
                using var deadline=CancellationTokenSource.CreateLinkedTokenSource(token);deadline.CancelAfter(TimeSpan.FromSeconds(12));
                ApplyState(await transport.SetScreenModeAsync((ScreenPowerMode)screen.SelectedIndex,deadline.Token));
            });
        };
        matrix.Toggled += (_,_) => Change(1,matrix.IsOn);
        under.Toggled += (_,_) => Change(2,under.IsOn);
        audio.Toggled += (_,_) => Change(4,audio.IsOn);
        clicky.Toggled += (_,_) => Change(8,clicky.IsOn);
        mode.SelectionChanged += (_,_) => {
            if (!syncing && mode.SelectedIndex >= 0)
            {
                var requested = (KeyboardMusicMode)mode.SelectedIndex;
                Begin(token => RequestAsync(16,0,requested,token:token));
            }
        };
        timer.Tick += (_,_) => {
            if (closed || suspended || busy || polling || mode.IsDropDownOpen || screen.IsDropDownOpen) return;
            if (transport is null)
            {
                if (!manualDisconnect && device is { Kind: not ConnectionKind.Preview } choice && DateTime.UtcNow >= retryAfter)
                    _ = QueueAsync(token => ConnectAsync(choice,token));
                return;
            }
            polling = true;
            _ = QueueAsync(token => RequestAsync(background: true,token:token), background: true);
        };
        Controls(); timer.Start();
    }
    private static TextBlock Heading(string text) => new() { Text = text, FontSize = 22, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold };
    private static TextBlock Note(string text) => new() { Text = text, TextWrapping = TextWrapping.Wrap, Opacity = .7 };
    private void Controls()
    {
        bool enabled=!closed&&!suspended&&!busy&&transport is not null;
        screen.IsEnabled=enabled&&lastState?.SupportsScreen==true;
        syncClock.IsEnabled=enabled&&lastState?.SupportsClock==true;
        byte capabilities=lastState?.Capabilities??(byte)31;
        foreach(var(control,flag) in new (Control,int)[]{(matrix,1),(under,2),(audio,4),(clicky,8),(mode,16)})control.IsEnabled=enabled&&(capabilities&flag)!=0;
        byte modes=lastState?.SupportedModes??(byte)7;
        for(int index=1;index<mode.Items.Count;index++)((ComboBoxItem)mode.Items[index]).IsEnabled=(modes&(1<<(index-1)))!=0;
        connect.IsEnabled = !closed && !suspended && !busy && transport is null && device is { Kind: not ConnectionKind.Preview };
        disconnect.IsEnabled = !closed && !suspended && !busy && transport is not null;
    }
    private void Change(byte mask,bool on)
    {
        if (!syncing) Begin(token => RequestAsync(mask,on ? mask : (byte)0,token:token));
    }
    private void Begin(Func<CancellationToken,Task> action)
    {
        if (syncing || busy || closed || suspended) return;
        _ = QueueAsync(action);
    }
    private Task QueueAsync(Func<CancellationToken,Task> action,bool background=false,bool propagate=false)
    {
        var preceding=operation;int version=generation;var token=deviceCancellation.Token;
        if(!background){pending++;Controls();}
        return operation=RunAsync(action,preceding,version,token,background,propagate);
    }
    private async Task RunAsync(Func<CancellationToken,Task> action,Task? preceding,int version,CancellationToken token,bool background,bool propagate)
    {
        try
        {
            if(preceding is not null){try{await preceding;}catch(Exception){/* The previous operation already reported its error. */}}
            if(closed||suspended||version!=generation){if(propagate)throw new OperationCanceledException();return;}
            token.ThrowIfCancellationRequested();
            await action(token);
        }
        catch(Exception ex)
        {
            bool stale=closed||suspended||version!=generation;
            if(!stale)
            {
                status.Text=ex is OperationCanceledException?"控制连接已取消或超时，将自动重试。":ex.Message+" · 将自动重试";
                retryAfter=DateTime.UtcNow.AddSeconds(3);
                try{File.WriteAllText(Path.Combine(AppContext.BaseDirectory,"keyboard-control-error.log"),$"{DateTimeOffset.Now:O} {device?.Name}\n{ex}");}catch(Exception){}
            }
            await DisconnectAsync(false);
            if(propagate)throw;
        }
        finally
        {
            if(background)polling=false;else pending--;
            if(!background||transport is null)Controls();
        }
    }
    private void CancelDeviceOperation()
    {
        generation++;
        deviceCancellation.Cancel();deviceCancellation.Dispose();
        deviceCancellation=CancellationTokenSource.CreateLinkedTokenSource(cancellation.Token);
    }
    public Task SelectDeviceAsync(DeviceChoice? choice,bool force=false)
    {
        if(closed)return Task.CompletedTask;
        if(!force&&device?.Id==choice?.Id&&device?.Kind==choice?.Kind)return Task.CompletedTask;
        device=choice;manualDisconnect=false;CancelDeviceOperation();
        if(suspended)return Task.CompletedTask;
        return QueueAsync(async token=>{
            await DisconnectAsync(false);
            if(choice is null||choice.Kind==ConnectionKind.Preview)
            {status.Text=choice?.Kind==ConnectionKind.Preview?"离线预览，无需键盘控制。":"等待键盘连接，选择设备后自动建立控制。";return;}
            await ConnectAsync(choice,token);
        });
    }
    private Task PauseAsync()
    {
        manualDisconnect=true;CancelDeviceOperation();
        return QueueAsync(async _=>{await DisconnectAsync(true);});
    }
    private async Task ConnectAsync(DeviceChoice choice,CancellationToken token)
    {
        using var timeout=CancellationTokenSource.CreateLinkedTokenSource(token);
        timeout.CancelAfter(TimeSpan.FromSeconds(20));
        status.Text="正在自动连接并读取键盘开关…";
        var opened=await DeviceCatalog.ConnectAsync(choice,timeout.Token);
        if(opened is not IKeyboardControlTransport control){await opened.DisposeAsync();throw new IOException("该连接不支持键盘控制。");}
        transport=control;timeout.Token.ThrowIfCancellationRequested();
        lastClockSync=null;
        await RequestAsync(token:timeout.Token);
        status.Text=$"已连接 · {choice.Name} · 状态自动同步中";
    }
    public async Task EnableMusicLightsAsync(DeviceChoice choice,LightTarget target,CancellationToken token)
    {
        if(choice.Kind==ConnectionKind.Preview)return;
        token.ThrowIfCancellationRequested();
        await SelectDeviceAsync(choice);
        await QueueAsync(async deviceToken=>{
            using var deadline=CancellationTokenSource.CreateLinkedTokenSource(deviceToken,token);deadline.CancelAfter(TimeSpan.FromSeconds(25));
            manualDisconnect=false;
            if(transport is null)await ConnectAsync(choice,deadline.Token);
            var state=await KeyboardControlProtocol.EnableMusicLightsAsync(transport!,target,deadline.Token,lastState?.Capabilities??(byte)31);
            if(state is not null)ApplyState(state);
        },propagate:true);
    }
    private async Task RequestAsync(byte mask=0,byte values=0,KeyboardMusicMode selectedMode=KeyboardMusicMode.Off,bool background=false,CancellationToken token=default)
    {
        if (transport is null) throw new IOException("键盘控制尚未连接，请稍后重试。");
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellation.Token,token);
        deadline.CancelAfter(TimeSpan.FromSeconds(12));
        var state = await transport.ControlAsync(mask,values,selectedMode,deadline.Token);
        bool needsInitialSync = state.SupportsClock && !state.ClockSynchronized;
        var localTime = DateTime.Now;
        state = await KeyboardControlProtocol.SynchronizeClockAsync(transport, state, localTime, deadline.Token);
        if (needsInitialSync) lastClockSync = localTime;
        // A pending user command owns the visible state; an older poll must not
        // undo its toggle or dismiss the mode menu while the command waits.
        if (closed || (background && (busy || mode.IsDropDownOpen || screen.IsDropDownOpen))) return;
        ApplyState(state);
    }
    private async Task SynchronizeClockAsync(CancellationToken token)
    {
        if (transport is null || lastState is null) throw new IOException("键盘控制尚未连接。");
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(token); deadline.CancelAfter(TimeSpan.FromSeconds(12));
        var localTime = DateTime.Now;
        await KeyboardControlProtocol.SynchronizeClockAsync(transport, lastState, localTime, deadline.Token, force: true);
        lastClockSync = localTime;
        await RequestAsync(token: deadline.Token);
    }
    private void UpdateClockStatus()
    {
        clockStatus.Text = lastState switch
        {
            null => "未连接，连接后读取时钟状态。",
            { SupportsClock: false } => "此键盘未启用时钟功能。",
            { ClockSynchronized: false } => "键盘尚未校时。",
            _ => lastClockSync is DateTime time
                ? $"键盘独立走时 · 本次校时 {time:HH:mm:ss}"
                : "已校时 · 键盘独立走时，无需保持客户端连接。"
        };
    }
    private void ApplyState(KeyboardControlState state)
    {
        bool changed=lastState?.Capabilities!=state.Capabilities||lastState?.SupportedModes!=state.SupportedModes||lastState?.SupportsScreen!=state.SupportsScreen||lastState?.SupportsClock!=state.SupportsClock;
        lastState=state;
        UpdateClockStatus();
        syncing = true;
        try
        {
            matrix.IsOn = state.Matrix; under.IsOn = state.Underglow;
            audio.IsOn = state.Audio; clicky.IsOn = state.Clicky;
            mode.SelectedIndex = (int)state.Mode;
            screen.SelectedIndex=state.SupportsScreen?(int)state.ScreenMode:-1;
        }
        finally { syncing = false; }
        if(changed){Controls();CapabilitiesChanged?.Invoke(state.Capabilities);}
    }
    private async Task DisconnectAsync(bool showStatus)
    {
        var previous = transport; transport = null;lastState=null;lastClockSync=null;UpdateClockStatus();
        if (previous is not null){try{await previous.DisposeAsync();}catch(Exception ex){Program.Trace("Control dispose: "+ex.Message);}}
        if (showStatus) status.Text = "控制已断开，键盘保留当前开关状态。";
    }
    public async Task SuspendForFirmwareAsync()
    {
        suspended = true; CancelDeviceOperation(); Controls();
        if (operation is { } task){try{await task;}catch(Exception){}}
        await DisconnectAsync(false); status.Text = "刷入前已断开键盘控制。";
        Controls();
    }
    public void ResumeAfterFirmware() { suspended = false;retryAfter=DateTime.UtcNow.AddSeconds(3);Controls(); }
    public async Task CloseAsync()
    {
        closed = true; timer.Stop(); cancellation.Cancel();deviceCancellation.Cancel();
        if (operation is { } task){try{await task;}catch(Exception){}}
        await DisconnectAsync(false); deviceCancellation.Dispose();cancellation.Dispose();
    }
}
