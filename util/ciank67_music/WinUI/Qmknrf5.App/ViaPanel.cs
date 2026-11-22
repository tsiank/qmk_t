// SPDX-License-Identifier: GPL-2.0-or-later
using Qmknrf5.Core;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI;
using System.Text.Json;
using Windows.Storage;
using Windows.Storage.Pickers;

namespace Qmknrf5.App;

internal sealed class ViaPanel
{
    private readonly Func<DeviceChoice?> selected;
    private readonly Func<nint> windowHandle;
    private readonly Button read = new() { Content = "连接并读取键位" };
    private readonly Button disconnect = new() { Content = "断开改键连接" };
    private readonly Button reconnect = new() { Content = "重新连接（保留编辑稿）" };
    private DeviceChoice? connectedDevice;
    private readonly Button save = new() { Content = "写入键盘并保存" };
    private readonly Button import = new() { Content = "导入配置" };
    private readonly Button export = new() { Content = "导出配置" };
    private readonly Button loadLayout = new() { Content = "加载键盘布局" };
    private readonly TextBlock layoutInfo = new() { TextWrapping = TextWrapping.Wrap };
    private KeyboardLayout layout = null!;
    private readonly Button apply = new() { Content = "暂存此键" };
    private readonly ComboBox layer = new() { HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly ViaKeyPicker picker;
    private readonly ViaUiEditor uiEditor;
    private readonly AutoSuggestBox keycode = new() { PlaceholderText = "搜索键码，或输入 MO(1)、LT(1,KC_SPC)…" };
    private readonly TextBlock keyLabel = new() { Text = "先读取键位，再点击键盘图选择要修改的按键", TextWrapping = TextWrapping.Wrap };
    private readonly InfoBar notice = new() { IsOpen = true, IsClosable = false, Severity = InfoBarSeverity.Informational };
    private readonly TextBlock status = new() { Text = "USB / 蓝牙改键 · 需要支持 VIA 的配套固件。读取后先编辑，再写入键盘。", TextWrapping = TextWrapping.Wrap };
    private readonly Canvas keyboard = new() { Width = 620, Height = 210 };
    private readonly ComboBox macroSlot = new() { HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly ToggleSwitch macroHex = new() { Header = "宏编辑格式", OffContent = "文字", OnContent = "VIA 宏字节（十六进制）" };
    private readonly TextBox macroText = new() { AcceptsReturn = true, MinHeight = 70, MaxHeight = 160, TextWrapping = TextWrapping.Wrap };
    private readonly Button applyMacro = new() { Content = "暂存此宏" };
    private readonly List<(Button Button, int Index)> keys = [];
    private readonly CancellationTokenSource cancellation = new();
    private IViaTransport? transport;
    private ViaSnapshot? original, working;
    private string? connectedName;
    private Task? operation;
    private int selectedKey = -1;
    private bool busy, closed, updating, suspended;
    public UIElement View { get; }
    public ViaPanel(Func<DeviceChoice?> selected, Func<nint> windowHandle)
    {
        this.selected = selected; this.windowHandle = windowHandle;
        picker = new ViaKeyPicker(StageKey);
        var root = new StackPanel { Spacing = 12 };
        root.Children.Add(new TextBlock { Text = "VIA 配置", FontSize = 24, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold });
        root.Children.Add(Row(read, disconnect, reconnect));
        notice.Content = status;
        AutomationProperties.SetAutomationId(notice, "ViaNotice");
        root.Children.Add(notice);
        uiEditor = new ViaUiEditor(() => transport, windowHandle, Begin, text => status.Text = text, cancellation.Token);
        var panel = new StackPanel { Spacing = 12 };
        panel.Children.Add(new TextBlock { Text = "键位编辑", FontSize = 24, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold });
        panel.Children.Add(new TextBlock { Text = "键位层与宏保存在键盘中，USB 和蓝牙共用。改键连接独立于律动、键盘控制和 MIDI。", TextWrapping = TextWrapping.Wrap });
        panel.Children.Add(layer);
        panel.Children.Add(Row(loadLayout));panel.Children.Add(layoutInfo);
        string defaultProfile=File.ReadAllText(Path.Combine(AppContext.BaseDirectory,"Layouts","ciank67.json"));
        layout=KeyboardLayout.Parse(defaultProfile);
        try {string saved=Path.Combine(AppStorage.DirectoryPath,"layout.json");if(File.Exists(saved))layout=KeyboardLayout.Parse(File.ReadAllText(saved));}catch(IOException){}
        BuildKeyboard();
        loadLayout.Click+=(_,_)=>Begin(LoadLayoutAsync);
        AutomationProperties.SetAutomationId(loadLayout,"LoadKeyboardLayout");
        AutomationProperties.SetAutomationId(layoutInfo,"KeyboardLayoutInfo");
        panel.Children.Add(new ScrollViewer { Content = keyboard, HorizontalScrollBarVisibility = ScrollBarVisibility.Auto, VerticalScrollBarVisibility = ScrollBarVisibility.Disabled });
        panel.Children.Add(keyLabel); panel.Children.Add(picker.View);
        var advanced = new StackPanel { Spacing = 10 }; advanced.Children.Add(keycode); advanced.Children.Add(apply);
        advanced.Children.Add(new TextBlock { Text = "支持 QMK 名称、LT / MT / OSM / LM 等表达式；未知键码仍可用十六进制保留。", TextWrapping = TextWrapping.Wrap, Opacity = .7 });
        var advancedEditor = new Expander { Header = "任意 QMK 键码（高级）", Content = advanced, HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch };
        AutomationProperties.SetAutomationId(advancedEditor, "ViaAdvancedEditor");
        panel.Children.Add(advancedEditor);
        var macros = new StackPanel { Spacing = 10 };
        macros.Children.Add(new TextBlock { Text = "把 QK_MACRO_0 等键码分配给按键，即可触发对应宏。宏会发送普通按键，可能受 MIDI 伴奏模式影响。", TextWrapping = TextWrapping.Wrap });
        macros.Children.Add(macroSlot); macros.Children.Add(macroHex); macros.Children.Add(macroText); macros.Children.Add(applyMacro);
        panel.Children.Add(new Expander { Header = "宏编辑", Content = macros, HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch });
        panel.Children.Add(Row(save, import, export));
        panel.Children.Add(new TextBlock { Text = "暂存仅修改当前编辑稿；写入会回读校验并等待 Flash 保存。写入中请勿断电；中断时可能部分键位已生效，请重新读取。导入导出使用本客户端的完整备份格式。", TextWrapping = TextWrapping.Wrap, Opacity = .7 });
        var tabs = new Pivot();
        foreach (var (title,content,id) in new (string,UIElement,string)[] {("键位与宏",panel,"ViaKeysTab"),("屏幕与字模",uiEditor.ScreenView,"ViaScreenTab"),("旋钮",uiEditor.EncoderView,"ViaEncoderTab") }) {
            var tab = new PivotItem { Header = title, Content = content };
            AutomationProperties.SetAutomationId(tab,id); tabs.Items.Add(tab);
        }
        root.Children.Add(tabs); View = root;
        foreach (var (control, id) in new (DependencyObject, string)[] { (read,"ReadVia"),(disconnect,"DisconnectVia"),(save,"SaveVia"),(import,"ImportVia"),(export,"ExportVia"),(layer,"ViaLayer"),(keycode,"ViaKeycode"),(apply,"ApplyViaKey"),(status,"ViaStatus"),(macroSlot,"ViaMacroSlot"),(macroText,"ViaMacroText"),(applyMacro,"ApplyViaMacro") }) AutomationProperties.SetAutomationId(control, id);
        read.Click += (_, _) => Begin(ReadAsync);
        reconnect.Click += (_, _) => Begin(ReconnectAsync);
        AutomationProperties.SetAutomationId(reconnect, "ReconnectVia");
        disconnect.Click += (_, _) => Begin(async () => { if (await DiscardAsync()) { await DisconnectAsync(); status.Text = "改键连接已断开，键盘保留已写入的配置。"; } });
        save.Click += (_, _) => Begin(SaveAsync); import.Click += (_, _) => Begin(ImportAsync); export.Click += (_, _) => Begin(ExportAsync);
        layer.SelectionChanged += (_, _) => { if (!updating) { Render(); ShowKey(); } };
        macroSlot.SelectionChanged += (_, _) => ShowMacro(); macroHex.Toggled += (_, _) => ShowMacro();
        keycode.TextMemberPath = "Name";
        keycode.TextChanged += (_, e) => { if (e.Reason == AutoSuggestionBoxTextChangeReason.UserInput) keycode.ItemsSource = picker.Suggestions(keycode.Text).ToArray(); };
        keycode.SuggestionChosen += (_, e) => keycode.Text = ((ViaKeyChoice)e.SelectedItem).Name;
        apply.Click += (_, _) =>
        {
            try
            {
                StageKey(ViaKeycodes.Parse(keycode.Text));
            }
            catch (Exception ex) { status.Text = ex.Message; }
        };
        applyMacro.Click += (_, _) =>
        {
            try { if (working is null) return; working = working with { Macros = ViaMacros.Replace(working.Macros, working.MacroCount, macroSlot.SelectedIndex, macroText.Text, macroHex.IsOn) }; DirtyStatus(); }
            catch (Exception ex) { status.Text = ex.Message; }
        };
        Controls();
    }
    private static UIElement Row(params UIElement[] children)
    {
        var panel = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        foreach (var child in children) panel.Children.Add(child);
        return new ScrollViewer { Content = panel, HorizontalScrollBarVisibility = ScrollBarVisibility.Auto, VerticalScrollBarVisibility = ScrollBarVisibility.Disabled };
    }
    private void BuildKeyboard()
    {
        ViaKeycodes.ConfigureKeyboard(layout.CustomKeycodes);
        keyboard.Children.Clear();keys.Clear();selectedKey=-1;
        foreach(var k in layout.Keys)
        {
            int index=k.Row*layout.Columns+k.Column;
            var button=new Button { Width=k.Width*40-3,Height=k.Height*40-3,Padding=new Thickness(1),FontSize=10,Content="—" };
            Canvas.SetLeft(button,k.X*40);Canvas.SetTop(button,k.Y*40);
            AutomationProperties.SetAutomationId(button,$"ViaKey{k.Row}_{k.Column}");
            button.Click+=(_,_)=>{selectedKey=index;Render();ShowKey();};
            keyboard.Children.Add(button);keys.Add((button,index));
        }
        keyboard.Width=layout.Keys.Max(k=>(k.X+k.Width)*40);
        keyboard.Height=layout.Keys.Max(k=>(k.Y+k.Height)*40);
        layoutInfo.Text=$"当前布局：{layout.Name} · 矩阵 {layout.Rows}×{layout.Columns} · {layout.Keys.Length} 键";
    }
    private async Task LoadLayoutAsync()
    {
        var picker=new FileOpenPicker();picker.FileTypeFilter.Add(".json");WinRT.Interop.InitializeWithWindow.Initialize(picker,windowHandle());
        var file=await picker.PickSingleFileAsync().AsTask().WaitAsync(cancellation.Token);if(file is null||closed)return;
        var info=await file.GetBasicPropertiesAsync();if(info.Size>500_000)throw new IOException("布局文件过大。");
        string json=await FileIO.ReadTextAsync(file);var profile=KeyboardLayout.Parse(json);
        if(!await DiscardAsync())return;
        Directory.CreateDirectory(AppStorage.DirectoryPath);File.WriteAllText(Path.Combine(AppStorage.DirectoryPath,"layout.json"),json);
        await DisconnectAsync();layout=profile;BuildKeyboard();
        keyLabel.Text="布局已加载，请重新连接并读取键位。";status.Text="布局已切换；读取时会核对固件矩阵尺寸。";
    }
    private bool Dirty => uiEditor.Dirty || working is not null && original is not null && (!working.Keys.AsSpan().SequenceEqual(original.Keys) || !working.Macros.AsSpan().SequenceEqual(original.Macros));
    private void DirtyStatus() => status.Text = Dirty ? $"{connectedName} · 有未写入的修改，点击“写入键盘并保存”生效。" : $"{connectedName} · 与键盘一致。";
    private void Controls()
    {
        bool enabled = !busy && !closed && !suspended;
        read.Content = busy ? "处理中…" : "连接并读取键位";
        reconnect.IsEnabled = enabled && transport is null && connectedDevice is not null && working is not null;
        loadLayout.IsEnabled = enabled;read.IsEnabled = enabled; disconnect.IsEnabled = enabled && transport is not null;
        foreach (Control c in new Control[] { save, import, export, layer, macroSlot, macroHex, macroText, applyMacro }) c.IsEnabled = enabled && working is not null;
        save.IsEnabled = enabled && working is not null && transport is not null;
        keycode.IsEnabled = apply.IsEnabled = enabled && working is not null && selectedKey >= 0;
        foreach (var item in keys) item.Button.IsEnabled = enabled && working is not null;
        uiEditor.Configure(enabled, transport is not null);
        picker.Configure(enabled, working is not null && selectedKey >= 0, working?.Layers ?? 7, working?.MacroCount ?? 16);
    }
    private void Begin(Func<Task> action) { if (!busy && !closed && !suspended) operation = RunAsync(action); }
    private async Task RunAsync(Func<Task> action)
    {
        busy = true; Controls();
        try { notice.Severity = InfoBarSeverity.Informational; await action(); }
        catch (Exception ex)
        {
            notice.Severity = InfoBarSeverity.Error;
            if (!closed) status.Text = ex is OperationCanceledException ? "VIA 操作已取消或超时。若正在写入，请重新读取确认，避免覆盖部分已生效的配置。" : ex.Message;
            // File/validation failures keep the connection and draft editable.
            // An exchange failure can leave delayed replies and requires reconnecting.
            if (transport is ViaTransportSession { IsFaulted: true })
            {
                try { File.AppendAllText(Path.Combine(AppContext.BaseDirectory, "via-error.log"), DateTime.Now.ToString("O") + Environment.NewLine + ex + Environment.NewLine); } catch (IOException) { } catch (UnauthorizedAccessException) { }
                await DisconnectAsync(clearDraft: false);
                if (!closed) status.Text = "设备通信已中断，键盘可能重启或 USB 重新连接。编辑稿已保留，可导出备份，或点击“重新连接（保留编辑稿）”后再试。";
            }
        }
        finally { busy = false; Controls(); }
    }
    private async Task<bool> DiscardAsync()
    {
        if (!Dirty) return true;
        var dialog = new ContentDialog { XamlRoot = View.XamlRoot, Title = "有尚未写入的编辑", Content = "继续将丢弃当前编辑稿。可取消后先导出配置备份。", PrimaryButtonText = "继续", CloseButtonText = "取消", DefaultButton = ContentDialogButton.Close };
        return await dialog.ShowAsync() == ContentDialogResult.Primary;
    }
    private async Task ReadAsync()
    {
        if (!await DiscardAsync()) return;
        var choice = selected() ?? throw new IOException("请先在“设备连接”选择键盘。");
        await DisconnectAsync(); status.Text = "正在连接并读取 VIA 键位与宏…";
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellation.Token); deadline.CancelAfter(TimeSpan.FromMinutes(3));
        // Bound connection setup separately from the many small BLE read requests.
        using var opening = CancellationTokenSource.CreateLinkedTokenSource(deadline.Token); opening.CancelAfter(TimeSpan.FromSeconds(20));
        transport = new ViaTransportSession(await ViaDevices.OpenAsync(choice, opening.Token));
        status.Text = "正在识别键盘矩阵与布局…";
        var matrix = await new ViaClient(transport).DescribeMatrixAsync(deadline.Token);
        bool matched = false;
        if (matrix is not null && (layout.Rows != matrix.Rows || layout.Columns != matrix.Columns))
        {
            var profiles = new List<KeyboardLayout>();
            foreach (string path in Directory.EnumerateFiles(Path.Combine(AppContext.BaseDirectory, "Layouts"), "*.json"))
            {
                try { profiles.Add(KeyboardLayout.Parse(File.ReadAllText(path))); }
                catch (IOException) { /* Ignore an unusable optional profile. */ }
            }
            var target = KeyboardLayout.MatchConnectedKeyboard(layout, profiles, matrix, choice.Name);
            layout = target; BuildKeyboard(); matched = true;
        }
        status.Text = $"正在读取 {layout.Name} 的键位与宏…";
        var client = new ViaClient(transport,layout.Rows,layout.Columns);
        var snapshot = await client.ReadAsync(deadline.Token);
        ViaClient.Validate(snapshot);
        original = snapshot; working = snapshot.Copy(); connectedName = choice.Name; connectedDevice = choice; Bind();
        if (client.UiSupported)
        {
            status.Text = $"{layout.Name} 键位已读取，正在读取屏幕与旋钮配置…";
            await uiEditor.ReadAsync();
        }
        status.Text = $"已读取 · {connectedName} · 布局 {layout.Name}（{layout.Rows}×{layout.Columns}） · {snapshot.Layers} 层 / {snapshot.MacroCount} 个宏" + (matched ? " · 已自动匹配布局" : "");
    }
    private void Bind()
    {
        if (working is null) return;
        updating = true;
        try
        {
            int previous = layer.SelectedIndex;
            layer.Items.Clear();
            string[] names = ["DVORAKR", "符号", "数字", "功能", "QWERTY", "灯光", "鼠标"];
            for (int i = 0; i < working.Layers; i++) layer.Items.Add($"第 {i} 层 · {(i < names.Length ? names[i] : "自定义")}");
            layer.SelectedIndex = Math.Clamp(previous, 0, working.Layers - 1);
            macroSlot.Items.Clear(); for (int i = 0; i < working.MacroCount; i++) macroSlot.Items.Add($"宏 {i} · QK_MACRO_{i}");
            macroSlot.SelectedIndex = 0;
        }
        finally { updating = false; }
        Render(); ShowKey(); ShowMacro();
    }
    private void Render()
    {
        if (working is null || layer.SelectedIndex < 0) return;
        foreach (var (button, index) in keys)
        {
            ushort code = working.Keys[layer.SelectedIndex * layout.Rows * layout.Columns + index];
            string text = ViaKeyCatalog.Label(code);
            button.Content = new TextBlock { Text = text, FontSize = 10, TextWrapping = TextWrapping.Wrap, TextAlignment = TextAlignment.Center, LineHeight = 11, MaxLines = 2, TextTrimming = TextTrimming.CharacterEllipsis };
            button.BorderThickness = new Thickness(index == selectedKey ? 2 : 1);
            button.ClearValue(Control.BorderBrushProperty);
            if (index == selectedKey) button.BorderBrush = Application.Current.Resources.TryGetValue("AccentFillColorDefaultBrush", out object brush) ? (Brush)brush : new SolidColorBrush(Colors.DodgerBlue);
            AutomationProperties.SetName(button, ViaKeyCatalog.Describe(code));
            ToolTipService.SetToolTip(button, ViaKeyCatalog.Describe(code));
        }
    }
    private void ShowKey()
    {
        if (working is null || selectedKey < 0 || layer.SelectedIndex < 0) return;
        ushort code = working.Keys[layer.SelectedIndex * layout.Rows * layout.Columns + selectedKey];
        keyLabel.Text = $"第 {layer.SelectedIndex} 层 · 行 {selectedKey / layout.Columns} 列 {selectedKey % layout.Columns} · {ViaKeyCatalog.Describe(code)}";
        keycode.Text = ViaKeycodes.Format(code);
        picker.SelectKey(code);
        Controls();
    }
    private void StageKey(ushort code)
    {
        if (working is null || selectedKey < 0 || layer.SelectedIndex < 0) { status.Text = "请先连接并读取键位，再点击键盘图选择要修改的按键。"; return; }
        if (busy || closed) return;
        working.Keys[layer.SelectedIndex * layout.Rows * layout.Columns + selectedKey] = code;
        Render(); ShowKey(); DirtyStatus();
    }
    private void ShowMacro()
    {
        if (working is null || macroSlot.SelectedIndex < 0 || updating) return;
        try
        {
            byte[] bytes = ViaMacros.Split(working.Macros, working.MacroCount)[macroSlot.SelectedIndex];
            bool binary = bytes.Any(b => b != 9 && b != 10 && b != 13 && (b < 32 || b > 126));
            if (binary && !macroHex.IsOn) { macroHex.IsOn = true; return; }
            macroText.Text = macroHex.IsOn ? Convert.ToHexString(bytes) : System.Text.Encoding.ASCII.GetString(bytes);
        }
        catch (Exception ex) { status.Text = ex.Message; }
    }
    private async Task SaveAsync()
    {
        if (transport is null || working is null || original is null) throw new IOException("请重新连接并读取后再写入；可先导出当前编辑稿。");
        status.Text = "正在写入、回读校验并等待 Flash 保存，请勿断电…";
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellation.Token); deadline.CancelAfter(TimeSpan.FromSeconds(120));
        await new ViaClient(transport,layout.Rows,layout.Columns).SaveAsync(working, original, deadline.Token);
        original = working.Copy(); status.Text = "保存成功 · 键位与宏已回读校验，Flash 写入已完成。USB / 蓝牙均生效。";
    }
    private async Task ImportAsync()
    {
        if (working is null || !await DiscardAsync()) return;
        var picker = new FileOpenPicker(); picker.FileTypeFilter.Add(".json"); WinRT.Interop.InitializeWithWindow.Initialize(picker, windowHandle());
        var file = await picker.PickSingleFileAsync(); if (file is null || closed) return;
        var info = await file.GetBasicPropertiesAsync(); if (info.Size > 500_000) throw new FormatException("配置文件过大。");
        var value = ViaClient.Import(await FileIO.ReadTextAsync(file));
        if (value.Rows!=layout.Rows || value.Columns!=layout.Columns || value.Layers != working.Layers || value.Macros.Length != working.Macros.Length || value.MacroCount != working.MacroCount) throw new FormatException("备份容量与键盘不一致。");
        working = value; Bind(); DirtyStatus();
    }
    private async Task ExportAsync()
    {
        if (working is null) return;
        var picker = new FileSavePicker { SuggestedFileName = "qmknrf5-via-backup" }; picker.FileTypeChoices.Add("Qmknrf5 完整配置", new List<string> { ".json" });
        WinRT.Interop.InitializeWithWindow.Initialize(picker, windowHandle());
        var file = await picker.PickSaveFileAsync(); if (file is null || closed) return;
        await FileIO.WriteTextAsync(file, ViaClient.Export(working)); status.Text = $"已导出当前编辑稿：{file.Name}";
    }
    private async Task ReconnectAsync()
    {
        var target = connectedDevice ?? throw new IOException("请先连接并读取键盘。");
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellation.Token); timeout.CancelAfter(TimeSpan.FromSeconds(20));
        var reopened = new ViaTransportSession(await ViaDevices.OpenAsync(target, timeout.Token));
        try
        {
            var descriptor = await new ViaClient(reopened).DescribeMatrixAsync(timeout.Token);
            if (descriptor is null || descriptor.Rows != layout.Rows || descriptor.Columns != layout.Columns)
                throw new IOException("重新连接的设备与当前布局不一致，请重新连接并读取。");
            transport = reopened;
        }
        catch { await reopened.DisposeAsync(); throw; }
        status.Text = "已重新连接，键位、屏幕与旋钮编辑稿均已保留。可在对应页继续保存。";
    }
    private async Task DisconnectAsync(bool clearDraft = true)
    {
        var old = transport; transport = null; if (old is not null) await old.DisposeAsync();
        if (clearDraft) { connectedDevice = null; uiEditor.Clear(); original = working = null; selectedKey = -1; foreach (var item in keys) item.Button.Content = "—"; }
    }
    public async Task<bool> SuspendForFirmwareAsync()
    {
        if (busy) throw new IOException("VIA 正在读写，请等待完成后再刷入。");
        if (!await DiscardAsync()) return false;
        suspended = true; Controls();
        await DisconnectAsync(); status.Text = "刷入前已断开 VIA 连接。";
        return true;
    }
    public void ResumeAfterFirmware() { suspended = false; Controls(); }
    public async Task CloseAsync()
    {
        closed = true; cancellation.Cancel(); if (operation is { } task) await task;
        await DisconnectAsync(); cancellation.Dispose();
    }
}
