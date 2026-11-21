// SPDX-License-Identifier: GPL-2.0-or-later
using System.Diagnostics;
using System.IO.Compression;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Ciank67.Music.Core;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Storage;
using Windows.Storage.Pickers;

namespace Ciank67.Music.App;

internal sealed class FirmwarePanel
{
    private readonly Func<nint> handle;
    private readonly DispatcherQueue dispatcher;
    private readonly Func<Task<bool>> prepare;
    private readonly Action finish;
    private static readonly string NrfutilPath = Path.Combine(AppContext.BaseDirectory,"Tools","nrfutil","nrfutil.exe");
    private string? bundledDirectory;
    private readonly TextBox nrfjprog = new() { Text = "nrfjprog.exe" };
    private readonly Button browseProg = new() { Content = "选择 nrfjprog.exe" }, restoreBoot = new() { Content = "恢复内置 Bootloader" };
    private readonly Button chooseApp = new() { Content = "选择应用固件 ZIP" }, chooseBoot = new() { Content = "使用其他 Bootloader ZIP（可选）" };
    private readonly Button flashApp = new() { Content = "刷入应用固件" }, flashBoot = new() { Content = "刷入 Bootloader" };
    private readonly TextBlock appInfo = Note("尚未选择应用固件。"), bootInfo = Note("尚未选择 Bootloader 包。支持 DFU ZIP 和包含 HEX 的压缩包。");
    private readonly ComboBox port = new() { HorizontalAlignment = HorizontalAlignment.Stretch, IsEditable = true, PlaceholderText = "自动等待 DFU，或选择 / 输入 COM 端口" };
    private readonly ComboBox bootPort = new() { HorizontalAlignment = HorizontalAlignment.Stretch, IsEditable = true, PlaceholderText = "自动等待 DFU，或选择 / 输入 COM 端口" };
    private readonly TextBlock probeInfo = Note("连接 J-Link 后直接开始，程序会自动识别。请只连接一个 J-Link。" );
    private readonly StackPanel bootDfuOptions = new() { Spacing = 10, Visibility = Visibility.Collapsed };
    private readonly Button refreshBoot = new() { Content = "检测 Bootloader 连接" };
    private readonly TabView tabs = new() { IsAddTabButtonVisible = false, CanDragTabs = false, CanReorderTabs = false, TabWidthMode = TabViewWidthMode.Equal };
    private readonly ComboBox bootImage = new() { HorizontalAlignment = HorizontalAlignment.Stretch, DisplayMemberPath = "Entry" };
    private readonly ComboBox softDevice = new() { HorizontalAlignment = HorizontalAlignment.Stretch, DisplayMemberPath = "Entry" };
    private readonly StackPanel hexOptions = new() { Spacing = 10, Visibility = Visibility.Collapsed };
    private readonly Button refresh = new() { Content = "刷新 USB 串口" }, cancel = new() { Content = "取消等待", IsEnabled = false }, exportLog = new() { Content = "导出日志" };
    private readonly ProgressBar progress = new() { Minimum = 0, Maximum = 100, Value = 0 };
    private readonly TextBlock status = Note("选择升级包和连接方式后开始。"), elapsed = Note("");
    private readonly TextBox log = new() { IsReadOnly = true, AcceptsReturn = true, TextWrapping = TextWrapping.NoWrap, MinHeight = 180, MaxHeight = 360, FontFamily = new FontFamily("Consolas"), FontSize = 12 };
    private readonly Microsoft.UI.Xaml.DispatcherTimer timer = new() { Interval = TimeSpan.FromSeconds(1) };
    private readonly Stopwatch watch = new();
    private readonly CancellationTokenSource lifetime = new();
    private readonly object logLock = new();
    private sealed record PageResult(string Status,string Elapsed,string Log,double Progress,string? LogPath);
    private readonly PageResult?[] pageResults=new PageResult?[2];
    private int selectedTab;
    private FirmwarePackage? application, bootloader;
    private CancellationTokenSource? active;
    private Task? operation;
    private StreamWriter? writer;
    private string? logPath;
    private bool busy, closed, transferStarted, asking;
    private static readonly string SettingsFile = AppStorage.FlashSettings();
    public UIElement View { get; }
    public FirmwarePanel(Func<nint> handle,DispatcherQueue dispatcher,Func<Task<bool>> prepare,Action finish)
    {
        this.handle=handle;this.dispatcher=dispatcher;this.prepare=prepare;this.finish=finish;
        var panel=new StackPanel { Spacing=16 };
        var appPanel=new StackPanel { Spacing=14,Margin=new Thickness(0,16,0,0) };
        appPanel.Children.Add(Note("通过 USB DFU 刷入应用固件，已内置 nrfutil，无需 J-Link。"));
        appPanel.Children.Add(chooseApp);appPanel.Children.Add(appInfo);
        appPanel.Children.Add(Field("USB DFU 串口",port));appPanel.Children.Add(refresh);
        appPanel.Children.Add(Note("让键盘进入 DFU 模式后刷新并选择串口，或使用自动等待。蓝牙 COM 端口不能用于 USB DFU。"));
        appPanel.Children.Add(flashApp);
        var bootPanel=new StackPanel { Spacing=14,Margin=new Thickness(0,16,0,0) };
        bootPanel.Children.Add(Note("已内置 Nordic Bootloader 和配套 S140 6.1.1。连接 J-Link / SWD 后即可刷入，无需填写序列号。"));
        bootPanel.Children.Add(bootInfo);
        var tools=new StackPanel { Spacing=10 };
        tools.Children.Add(Field("nrfjprog.exe（需安装 Nordic 命令行工具及 J-Link 驱动）",nrfjprog));tools.Children.Add(browseProg);
        tools.Children.Add(chooseBoot);tools.Children.Add(restoreBoot);
        var toolSettings=new Expander { Header="高级选项（工具 / 其他 Bootloader）",Content=tools,HorizontalAlignment=HorizontalAlignment.Stretch,HorizontalContentAlignment=HorizontalAlignment.Stretch };
        AutomationProperties.SetAutomationId(toolSettings,"FirmwareToolSettings");bootPanel.Children.Add(toolSettings);
        hexOptions.Children.Add(Field("Bootloader HEX",bootImage));hexOptions.Children.Add(Field("配套 SoftDevice HEX（合并镜像无需另选）",softDevice));hexOptions.Children.Add(probeInfo);
        hexOptions.Children.Add(Note("SWD 会清除现有应用、VIA 配置和蓝牙配对。完成后需重新刷入应用固件。"));
        bootPanel.Children.Add(hexOptions);
        bootDfuOptions.Children.Add(Field("Bootloader USB DFU 串口",bootPort));
        bootDfuOptions.Children.Add(Note("当前选择的是 Bootloader DFU 升级包，请让键盘进入 USB DFU 模式。"));
        bootPanel.Children.Add(bootDfuOptions);bootPanel.Children.Add(refreshBoot);bootPanel.Children.Add(flashBoot);
        var appTab=new TabViewItem { Header="固件刷入",IsClosable=false,Content=appPanel };
        var bootTab=new TabViewItem { Header="Bootloader 刷入",IsClosable=false,Content=bootPanel };
        AutomationProperties.SetAutomationId(appTab,"ApplicationFlashTab");AutomationProperties.SetAutomationId(bootTab,"BootloaderFlashTab");
        tabs.TabItems.Add(appTab);tabs.TabItems.Add(bootTab);tabs.SelectedIndex=0;
        panel.Children.Add(tabs);
        ScrollViewer.SetHorizontalScrollBarVisibility(log,ScrollBarVisibility.Auto);
        ScrollViewer.SetVerticalScrollBarVisibility(log,ScrollBarVisibility.Auto);
        panel.Children.Add(Heading("刷入过程"));panel.Children.Add(status);panel.Children.Add(progress);panel.Children.Add(elapsed);panel.Children.Add(cancel);panel.Children.Add(log);panel.Children.Add(exportLog);
        View=panel;
        foreach(var (element,id) in new (DependencyObject,string)[]{(chooseApp,"ChooseApplicationZip"),(chooseBoot,"ChooseBootloaderZip"),(flashApp,"FlashApplication"),(flashBoot,"FlashBootloader"),(port,"FirmwarePort"),(bootPort,"BootloaderPort"),(refreshBoot,"RefreshBootloaderDevices"),(probeInfo,"BootloaderProbeInfo"),(bootImage,"FirmwareBootImage"),(softDevice,"FirmwareSoftDevice"),(nrfjprog,"NrfjprogPath"),(restoreBoot,"RestoreBundledBootloader"),(browseProg,"BrowseNrfjprog"),(refresh,"RefreshFirmwareDevices"),(cancel,"CancelFirmwareFlash"),(status,"FirmwareStatus"),(log,"FirmwareLog"),(appInfo,"FirmwareApplicationInfo"),(bootInfo,"FirmwareBootloaderInfo"),(exportLog,"ExportFirmwareLog")})AutomationProperties.SetAutomationId(element,id);
        chooseApp.Click+=(_,_)=>Begin(()=>ChooseAsync(FirmwareKind.Application));chooseBoot.Click+=(_,_)=>Begin(()=>ChooseAsync(FirmwareKind.Bootloader));
        flashApp.Click+=(_,_)=>Begin(()=>FlashAsync(application!));flashBoot.Click+=(_,_)=>Begin(()=>FlashAsync(bootloader!));
        refresh.Click+=(_,_)=>Begin(()=>RefreshPortsAsync(port));refreshBoot.Click+=(_,_)=>Begin(RefreshBootAsync);restoreBoot.Click+=(_,_)=>Begin(()=>{LoadBundledBootloader();return Task.CompletedTask;});browseProg.Click+=(_,_)=>Begin(()=>BrowseToolAsync(nrfjprog));
        cancel.Click+=async(_,_)=>{if(await ConfirmAbortAsync())active?.Cancel();};exportLog.Click+=async(_,_)=>{try{await ExportAsync();}catch(Exception ex){status.Text=ex.Message;}};
        bootImage.SelectionChanged+=(_,_)=>softDevice.IsEnabled=!busy&&bootImage.SelectedItem is HexImage { SoftDevice:false };
        timer.Tick+=(_,_)=>elapsed.Text=$"已用时 {watch.Elapsed:mm\\:ss}";
        foreach(var selector in new[]{port,bootPort}){selector.Items.Add(new FirmwarePort("","自动等待 USB DFU 串口",false));selector.SelectedIndex=0;}
        try {if(File.Exists(SettingsFile)){var saved=JsonSerializer.Deserialize<string[]>(File.ReadAllText(SettingsFile));if(saved is { Length:2 }){nrfjprog.Text=saved[1];}}}catch(Exception ex) when(ex is IOException or JsonException or UnauthorizedAccessException){}
        nrfjprog.LostFocus+=(_,_)=>SaveSettings();
        try{LoadBundledBootloader();}catch(Exception ex) when(ex is IOException or UnauthorizedAccessException){bootInfo.Text="内置 Bootloader 无法加载："+ex.Message;}
        pageResults[1]=new(status.Text,"","",0,null);
        status.Text="请选择应用固件 ZIP，连接 USB DFU 后开始。";
        tabs.SelectionChanged+=(_,_)=>SwitchResult();
        Controls();
    }
    private void SwitchResult()
    {
        int next=tabs.SelectedIndex;
        if(next<0||next==selectedTab)return;
        pageResults[selectedTab]=new(status.Text,elapsed.Text,log.Text,progress.Value,logPath);
        selectedTab=next;
        var result=pageResults[next]!;
        status.Text=result.Status;elapsed.Text=result.Elapsed;log.Text=result.Log;progress.Value=result.Progress;logPath=result.LogPath;
        watch.Reset();Controls();
    }
    private static TextBlock Heading(string text)=>new(){Text=text,FontSize=22,FontWeight=Microsoft.UI.Text.FontWeights.SemiBold};
    private static TextBlock Note(string text)=>new(){Text=text,TextWrapping=TextWrapping.Wrap};
    private static UIElement Field(string label,UIElement control){var p=new StackPanel{Spacing=6};p.Children.Add(Note(label));p.Children.Add(control);return p;}
    private void Controls()
    {
        bool ready=!busy&&!closed;
        foreach(Control c in new Control[]{chooseApp,chooseBoot,restoreBoot,browseProg,nrfjprog,port,bootPort,tabs,bootImage,refresh,refreshBoot})c.IsEnabled=ready;
        flashApp.IsEnabled=ready&&application is not null;flashBoot.IsEnabled=ready&&bootloader is not null;
        softDevice.IsEnabled=ready&&bootImage.SelectedItem is HexImage { SoftDevice:false };
        cancel.IsEnabled=!closed&&active is not null;exportLog.IsEnabled=!closed&&log.Text.Length>0;
    }
    private void Begin(Func<Task> action){if(!busy&&!closed)operation=RunAsync(action);}
    private async Task RunAsync(Func<Task> action)
    {
        busy=true;Controls();
        try{await action();}
        catch(OperationCanceledException){status.Text=transferStarted?"刷入已中止，未确认成功。请保持连接，重新进入 DFU / SWD 后重试。":"已取消或等待超时，尚未开始刷入。";Log(status.Text);}
        catch(Exception ex){status.Text=$"未完成：{ex.Message}";Log(status.Text,true);}
        finally{timer.Stop();watch.Stop();if(watch.Elapsed>TimeSpan.Zero)elapsed.Text=$"用时 {watch.Elapsed:mm\\:ss}";progress.IsIndeterminate=false;lock(logLock){writer?.Dispose();writer=null;}busy=false;transferStarted=false;Controls();}
    }
    private async Task ChooseAsync(FirmwareKind kind)
    {
        var picker=new FileOpenPicker();picker.FileTypeFilter.Add(".zip");WinRT.Interop.InitializeWithWindow.Initialize(picker,handle());
        var file=await picker.PickSingleFileAsync().AsTask().WaitAsync(lifetime.Token);if(file is null||closed)return;
        var package=await Task.Run(()=>FirmwarePackage.Inspect(file.Path,kind));if(closed)return;
        string text=$"{file.Name}\n{package.Components} · {package.Length/1024.0:F1} KB\nSHA256: {package.Sha256}";
        if(kind==FirmwareKind.Application){application=package;appInfo.Text=text;}
        else SetBootloader(package,text);
        status.Text="升级包检查通过，请确认连接方式后开始。";
    }
    private void SetBootloader(FirmwarePackage package,string description)
    {
        bootloader=package;bootInfo.Text=description;hexOptions.Visibility=package.Dfu?Visibility.Collapsed:Visibility.Visible;bootDfuOptions.Visibility=package.Dfu?Visibility.Visible:Visibility.Collapsed;
        bootImage.ItemsSource=package.HexImages.Where(h=>h.Bootloader).ToArray();
        bootImage.SelectedItem=package.HexImages.FirstOrDefault(h=>h.Entry.EndsWith("nrf52840_xxaa.hex",StringComparison.OrdinalIgnoreCase))??package.HexImages.FirstOrDefault(h=>h.Bootloader);
        softDevice.ItemsSource=package.HexImages.Where(h=>h.SoftDevice&&!h.Bootloader&&!h.Application).ToArray();softDevice.SelectedIndex=((HexImage[])softDevice.ItemsSource).Length==1?0:-1;
    }
    private void LoadBundledBootloader()
    {
        string source=Path.Combine(AppContext.BaseDirectory,"Tools","bootloader");
        if(bundledDirectory is null)
        {
            string directory=Path.Combine(Path.GetTempPath(),"qmknrf5-bundled-"+Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(directory);
            try{ZipFile.CreateFromDirectory(source,Path.Combine(directory,"bootloader.zip"));bundledDirectory=directory;}
            catch{Directory.Delete(directory,true);throw;}
        }
        var package=FirmwarePackage.Inspect(Path.Combine(bundledDirectory,"bootloader.zip"),FirmwareKind.Bootloader);
        SetBootloader(package,"内置 Bootloader · 默认 Nordic nrf52840_xxaa + S140 6.1.1\nSHA256: "+package.Sha256);
        status.Text="内置 Bootloader 已就绪；应用固件请选择 ZIP。";
    }
    private async Task BrowseToolAsync(TextBox target)
    {
        var picker=new FileOpenPicker();picker.FileTypeFilter.Add(".exe");WinRT.Interop.InitializeWithWindow.Initialize(picker,handle());
        var file=await picker.PickSingleFileAsync().AsTask().WaitAsync(lifetime.Token);if(file is null||closed)return;target.Text=file.Path;SaveSettings();
    }
    private void SaveSettings(){try{Directory.CreateDirectory(Path.GetDirectoryName(SettingsFile)!);File.WriteAllText(SettingsFile,JsonSerializer.Serialize(new[]{NrfutilPath,nrfjprog.Text}));}catch(IOException){}catch(UnauthorizedAccessException){}}
    private async Task RefreshPortsAsync(ComboBox selector)
    {
        using var timeout=new CancellationTokenSource(TimeSpan.FromSeconds(12));string? previous=(selector.SelectedItem as FirmwarePort)?.Port;
        var ports=await FirmwareDevices.PortsAsync(timeout.Token);
        selector.Items.Clear();selector.Items.Add(new FirmwarePort("","自动等待 USB DFU 串口",false));foreach(var p in ports)selector.Items.Add(p);
        selector.SelectedItem=selector.Items.Cast<FirmwarePort>().FirstOrDefault(p=>p.Port==previous)??selector.Items[0];
        status.Text=$"找到 {ports.Count} 个串口，其中 {ports.Count(p=>p.Dfu)} 个识别为 USB DFU。";
    }
    private async Task RefreshBootAsync()
    {
        if(bootloader?.Dfu==true){await RefreshPortsAsync(bootPort);return;}
        using var timeout=CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);timeout.CancelAfter(TimeSpan.FromSeconds(12));
        probeInfo.Text="已识别 J-Link："+await ResolveProbeAsync(FirmwareDevices.ResolveTool(nrfjprog.Text),timeout.Token);
        status.Text="J-Link 已就绪。";
    }
    private async Task<string> ResolveProbeAsync(string tool,CancellationToken token)
    {
        var lines=new List<string>();
        using var timeout=CancellationTokenSource.CreateLinkedTokenSource(token);timeout.CancelAfter(TimeSpan.FromSeconds(12));
        var result=await FirmwareRunner.RunAsync(FirmwareRunner.Command(tool,["--ids"]),o=>{lock(lines)lines.Add(o.Text);},timeout.Token);
        if(result.Cancelled)throw new OperationCanceledException("J-Link 枚举已取消或超时。",token);
        if(result.ExitCode!=0)throw new IOException("J-Link 检测失败："+string.Join("\n",lines));
        return FirmwareRunner.SingleProbe(lines);
    }
    private async Task<string> WaitPortAsync(ComboBox selector,CancellationToken token)
    {
        var chosen=selector.SelectedItem as FirmwarePort;string typed=selector.Text.Trim();
        if(chosen is not null&&chosen.Port.Length>0&&typed==chosen.ToString())
        {
            if(chosen.Name.Contains("蓝牙",StringComparison.Ordinal))throw new IOException("蓝牙串口不能用于 USB DFU，请选择键盘的 USB DFU 串口。");return chosen.Port;
        }
        if(Regex.IsMatch(typed,"^COM[1-9][0-9]*$",RegexOptions.IgnoreCase))return typed.ToUpperInvariant();
        status.Text="等待 USB DFU 串口（最多 120 秒）。请现在手动让键盘进入 DFU 模式。";cancel.Content="取消等待";Log(status.Text);
        using var timeout=CancellationTokenSource.CreateLinkedTokenSource(token);timeout.CancelAfter(TimeSpan.FromSeconds(120));
        while(true)
        {
            var found=(await FirmwareDevices.PortsAsync(timeout.Token)).Where(p=>p.Dfu).ToArray();
            if(found.Length==1){Log("识别到 "+found[0]);return found[0].Port;}
            if(found.Length>1)throw new IOException("发现多个 USB DFU 设备，请取消后手动选择目标串口。");
            await Task.Delay(700,timeout.Token);
        }
    }
    private async Task FlashAsync(FirmwarePackage selected)
    {
        using var cancellation=new CancellationTokenSource();active=cancellation;bool prepared=false;
        try
        {
            progress.Value=0;cancel.Content="取消";Controls();using var snapshot=await Task.Run(()=>new FirmwareSnapshot(selected.Path,selected.Kind));
            if(snapshot.Package.Sha256!=selected.Sha256)throw new IOException("ZIP 在选择后已发生变化，请重新选择并确认。");
            string tool=FirmwareDevices.ResolveTool(snapshot.Package.Dfu?NrfutilPath:nrfjprog.Text);
            var image=bootImage.SelectedItem as HexImage;var sd=softDevice.SelectedItem as HexImage;
            var targetPort=selected.Kind==FirmwareKind.Application?port:bootPort;
            string serial="";
            if(!snapshot.Package.Dfu)
            {
                if(image is null)throw new IOException("请选择 Bootloader HEX。");
                if(!image.SoftDevice&&sd is null)throw new IOException("所选 Bootloader HEX 不含 SoftDevice；全擦后需要匹配的 SoftDevice，请选择压缩包中的配套 HEX。");
                status.Text="正在自动识别 J-Link…";
                serial=await ResolveProbeAsync(tool,cancellation.Token);
                probeInfo.Text="已识别 J-Link："+serial;
            }
            string summary=$"文件：{Path.GetFileName(selected.Path)}\n内容：{selected.Components}\nSHA256：{selected.Sha256}\n工具：{tool}\n";
            summary+=snapshot.Package.Dfu?$"USB DFU 目标：{targetPort.Text}\n":$"J-Link：{serial}\n镜像：{image!.Entry}\nSoftDevice：{(image.SoftDevice?"已包含在合并镜像中":sd!.Entry)}\n\n将清除现有应用、VIA 配置和蓝牙配对，完成后需重新刷入应用。\n";
            if(!snapshot.Package.Dfu&&image is { Application:true })summary+="所选 HEX 还包含应用区域，将一并写入。\n";
            if(snapshot.Package.Dfu&&selected.Components.Contains("SoftDevice",StringComparison.Ordinal))summary+="此升级包同时更新 SoftDevice，请确认与硬件和应用固件匹配。\n";
            var confirm=new ContentDialog { XamlRoot=View.XamlRoot,Title=selected.Kind==FirmwareKind.Application?"刷入应用固件":"刷入 Bootloader",Content=summary,PrimaryButtonText=snapshot.Package.Dfu?"开始刷入":"清除并刷入 Bootloader",CloseButtonText="取消",DefaultButton=ContentDialogButton.Close };
            cancellation.Token.ThrowIfCancellationRequested();asking=true;
            try {if(await confirm.ShowAsync()!=ContentDialogResult.Primary)return;} finally {asking=false;}
            SaveSettings();StartLog();Log(summary);Log("原始 ZIP："+selected.Path);
            status.Text="正在停止律动 / MIDI 并释放键盘连接…";progress.IsIndeterminate=true;watch.Restart();timer.Start();
            bool allowed;asking=true;
            try {allowed=await prepare();} finally {asking=false;}
            if(!allowed){status.Text="已取消，尚未刷入。";return;}prepared=true;
            using var power=new WindowsPowerRequest("Qmknrf5配置工具正在等待或执行固件刷入（允许熄屏）");
            cancellation.Token.ThrowIfCancellationRequested();
            if(snapshot.Package.Dfu)
            {
                string target=await WaitPortAsync(targetPort,cancellation.Token);cancellation.Token.ThrowIfCancellationRequested();
                transferStarted=true;cancel.Content="中止刷入";status.Text="正在刷入，等待 nrfutil 完成…";
                await StepAsync(FirmwareRunner.Dfu(tool,snapshot.Package.Path,target),cancellation.Token);
            }
            else
            {
                status.Text="正在检查 J-Link 目标型号…";var chip=await StepAsync(FirmwareRunner.Swd(tool,serial,"--deviceversion"),cancellation.Token);
                if(!chip.Contains("NRF52840",StringComparison.OrdinalIgnoreCase))throw new IOException("J-Link 目标不是 nRF52840，已停止；没有执行擦除。");
                string bootPath=snapshot.Package.ExtractHex(image!.Entry,snapshot.Directory);string? sdPath=image.SoftDevice?null:snapshot.Package.ExtractHex(sd!.Entry,snapshot.Directory);
                transferStarted=true;cancel.Content="中止刷入";
                var steps=new List<(string Label,string[] Args)>{("解除保护并恢复 / recover",["--recover"]),("擦除 Flash / eraseall",["--eraseall"])};
                if(sdPath is not null)steps.Add(("写入并校验 SoftDevice",["--program",sdPath,"--verify"]));
                steps.Add(("写入并校验 Bootloader",["--program",bootPath,"--verify"]));steps.Add(("复位键盘",["--reset"]));
                for(int i=0;i<steps.Count;i++){status.Text=$"步骤 {i+1}/{steps.Count}：{steps[i].Label}";Log(status.Text);await StepAsync(FirmwareRunner.Swd(tool,serial,steps[i].Args),cancellation.Token);}
            }
            progress.IsIndeterminate=false;progress.Value=100;status.Text=selected.Kind==FirmwareKind.Application?"应用固件刷入成功。请重新连接键盘并刷新设备。":"Bootloader 刷入成功。请重新连接；SWD 全擦后还需刷入应用固件。";Log(status.Text);
        }
        finally
        {
            if(prepared)finish();active=null;
        }
    }
    private void StartLog()
    {
        string directory=Path.Combine(AppStorage.DirectoryPath,"logs");Directory.CreateDirectory(directory);
        logPath=Path.Combine(directory,$"flash-{DateTime.Now:yyyyMMdd-HHmmss}-{Guid.NewGuid().ToString("N")[..6]}.log");
        lock(logLock)writer=new StreamWriter(logPath,false,new UTF8Encoding(false)){AutoFlush=true};log.Text="";Log("日志："+logPath);
    }
    private void Log(string message,bool error=false,int? percent=null)
    {
        lock(logLock)
        {
            string line=$"[{DateTime.Now:HH:mm:ss}] {(error?"[stderr] ":"")}{message}";
            try{writer?.WriteLine(line);}catch(IOException){}
            dispatcher.TryEnqueue(()=>{if(closed)return;log.Text=log.Text.Length>250_000?log.Text[^180_000..]+"\n"+line:log.Text+line+"\n";log.Select(log.Text.Length,0);if(percent is int n){progress.IsIndeterminate=false;progress.Value=n;}exportLog.IsEnabled=true;});
        }
    }
    private async Task<string> StepAsync(ProcessStartInfo command,CancellationToken token)
    {
        Log("> "+command.FileName+" "+string.Join(" ",command.ArgumentList.Select(a=>a.Contains(' ')?'"'+a+'"':a)));
        var output=new StringBuilder();
        using var timeout=CancellationTokenSource.CreateLinkedTokenSource(token);timeout.CancelAfter(TimeSpan.FromMinutes(5));
        var result=await FirmwareRunner.RunAsync(command,o=>{lock(output)output.AppendLine(o.Text);Log(o.Text,o.Error,o.Percent);},timeout.Token);
        Log($"进程退出码：{result.ExitCode}");
        if(result.Cancelled)throw new OperationCanceledException("刷入工具已中止或执行超时。",token);
        if(result.ExitCode!=0)throw new IOException($"工具退出码 {result.ExitCode}，请查看下方日志。后续步骤已停止。");
        return output.ToString();
    }
    private async Task<bool> ConfirmAbortAsync()
    {
        if(active is null)return true;if(!transferStarted)return true;if(asking)return false;
        asking=true;
        try{return await new ContentDialog { XamlRoot=View.XamlRoot,Title="中止正在进行的刷入？",Content="中断传输或擦除后不能确认升级成功，可能需要重新进入 DFU 或通过 SWD 重刷。",PrimaryButtonText="中止刷入",CloseButtonText="继续等待",DefaultButton=ContentDialogButton.Close }.ShowAsync()==ContentDialogResult.Primary;}
        finally{asking=false;}
    }
    private async Task ExportAsync()
    {
        var picker=new FileSavePicker { SuggestedFileName="qmknrf5-flash-log" };picker.FileTypeChoices.Add("刷入日志",new List<string>{".log"});WinRT.Interop.InitializeWithWindow.Initialize(picker,handle());
        var file=await picker.PickSaveFileAsync().AsTask().WaitAsync(lifetime.Token);if(file is null)return;
        string content=log.Text;
        if(logPath is not null&&File.Exists(logPath))
        {
            using var input=new FileStream(logPath,FileMode.Open,FileAccess.Read,FileShare.ReadWrite);
            using var reader=new StreamReader(input,Encoding.UTF8);content=await reader.ReadToEndAsync();
        }
        await FileIO.WriteTextAsync(file,content);
    }
    public async Task<bool> ConfirmCloseAsync()
    {
        if(active is null)return true;
        if(asking)return false;
        if(!await ConfirmAbortAsync())return false;
        active?.Cancel();if(operation is { } task)await task;return true;
    }
    public async Task CloseAsync(){closed=true;lifetime.Cancel();active?.Cancel();if(operation is { } task)await task;timer.Stop();if(bundledDirectory is not null){try{Directory.Delete(bundledDirectory,true);}catch(IOException){}catch(UnauthorizedAccessException){}}}
}
