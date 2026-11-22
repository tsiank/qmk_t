// SPDX-License-Identifier: GPL-2.0-or-later
using Qmknrf5.Core;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.Storage;
using Windows.Storage.Pickers;

namespace Qmknrf5.App;

internal sealed class ViaUiEditor
{
    private readonly Func<IViaTransport?> transport;
    private readonly Func<nint> handle;
    private readonly Action<Func<Task>> begin;
    private readonly Action<string> report;
    private readonly CancellationToken token;
    private readonly ComboBox widget = new() { Header = "显示组件", HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly NumberBox x = new() { Header = "X（像素）", Minimum = 0, Maximum = 127, Value = 0, SmallChange = 1, Width = 130 };
    private readonly NumberBox y = new() { Header = "Y（像素）", Minimum = 0, Maximum = 63, Value = 0, SmallChange = 1, Width = 130 };
    private readonly ComboBox scale = new() { Header = "字号／缩放", Width = 140 };
    private readonly ToggleSwitch visible = new() { Header = "显示此组件" };
    private readonly ComboBox power = new() { Header = "屏幕电源", HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly Canvas preview = new() { Width = 512, Height = 128, Background = new SolidColorBrush(Colors.Black) };
    private readonly TextBlock info = new() { Text = "先连接并读取 VIA，再读取屏幕与旋钮配置。", TextWrapping = TextWrapping.Wrap };
    private readonly TextBlock crop = new() { TextWrapping = TextWrapping.Wrap };
    private readonly TextBox title = new() { Header = "标题文字", Text = "紫雪蓝海", MaxLength = 32 };
    private readonly TextBox font = new() { Header = "Windows 字体", Text = "Microsoft YaHei", MaxLength = 100 };
    private readonly NumberBox fontSize = new() { Header = "文字像素高度", Minimum = 8, Maximum = 32, Value = 16, Width = 150 };
    private readonly NumberBox width = new() { Header = "导入画布宽", Minimum = 1, Maximum = 128, Value = 64, Width = 150 };
    private readonly NumberBox height = new() { Header = "导入画布高", Minimum = 1, Maximum = 32, Value = 16, Width = 150 };
    private readonly NumberBox threshold = new() { Header = "图片黑白阈值", Minimum = 1, Maximum = 255, Value = 128, Width = 150 };
    private readonly ToggleSwitch invert = new() { Header = "图片反色" };
    private readonly ComboBox lcdOrder = new() { Header = "TXT 数据排列", HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly ComboBox encoder = new() { Header = "旋钮" }, layer = new() { Header = "层" }, action = new() { Header = "旋转动作", HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly AutoSuggestBox cw = new() { Header = "顺时针键码", PlaceholderText = "搜索 QMK 键码或输入表达式" }, ccw = new() { Header = "逆时针键码", PlaceholderText = "搜索 QMK 键码或输入表达式" };
    private readonly StackPanel screen = new() { Spacing = 12 }, encoders = new() { Spacing = 12 };
    private readonly Button applyEncoder = new() { Content = "暂存旋钮动作" };
    private readonly List<Button> draftButtons = [];
    private readonly List<Button> connectionButtons = [];
    private readonly Button readScreen = new() { Content = "读取屏幕配置" }, readEncoder = new() { Content = "读取旋钮配置" };
    private ViaUiCapabilities? caps;
    private ViaUiConfiguration? original, working;
    private bool updating;
    private static readonly string[] WidgetNames = ["Logo", "键盘名称／标题", "传统时刻", "数字时间", "电量", "连接方式", "当前层"];
    private static readonly (string Name, byte Id)[] Actions = [("固件默认", 255), ("QMK 按键", 0), ("底灯色彩（Alt 亮度／Ctrl 饱和度）", 1), ("按键灯色彩（Alt 亮度／Ctrl 饱和度）", 2), ("不执行动作", 3)];
    private readonly UserControl screenRoot = new(), encoderRoot = new();
    public UIElement ScreenView => screenRoot;
    public UIElement EncoderView => encoderRoot;
    public bool Dirty => working is not null && original is not null && !working.Bytes.AsSpan().SequenceEqual(original.Bytes);
    public ViaUiEditor(Func<IViaTransport?> transport, Func<nint> handle, Action<Func<Task>> begin, Action<string> report, CancellationToken token)
    {
        screenRoot.Content = screen; encoderRoot.Content = encoders;
        this.transport = transport; this.handle = handle; this.begin = begin; this.report = report; this.token = token;
        screen.Children.Add(readScreen); screen.Children.Add(info);
        screen.Children.Add(new ScrollViewer { Content = preview, HorizontalScrollBarVisibility = ScrollBarVisibility.Auto, VerticalScrollBarVisibility = ScrollBarVisibility.Disabled });
        screen.Children.Add(new TextBlock { Text = "预览使用示例状态：午正初刻、12:00、USB、BASE；写入后显示实际状态。", TextWrapping = TextWrapping.Wrap, Opacity = .7 }); screen.Children.Add(crop);
        foreach (string name in WidgetNames) widget.Items.Add(name); widget.SelectedIndex = 0;
        scale.Items.Add("1×"); scale.Items.Add("2×"); scale.SelectedIndex = 0;
        foreach (string name in new[] { "自动（USB 常亮／电池超时）", "常亮", "熄屏" }) power.Items.Add(name); power.SelectedIndex = 0;
        screen.Children.Add(widget); screen.Children.Add(Row(x, y, scale, visible)); screen.Children.Add(power);
        var artwork = new StackPanel { Spacing = 10 }; artwork.Children.Add(title); artwork.Children.Add(font); artwork.Children.Add(fontSize);
        artwork.Children.Add(Row(width, height, threshold, invert));
        lcdOrder.Items.Add("逐字 16×16（自动识别字数和宽度）"); lcdOrder.Items.Add("整张图片逐行排列"); lcdOrder.SelectedIndex = 0; artwork.Children.Add(lcdOrder);
        var text = Button("生成标题字模", () => { var c = Require(); c.SetBitmap(false, Int(width), Int(height), OledImageImport.Text(title.Text, font.Text, Int(fontSize), Int(width), Int(height))); Changed(); return Task.CompletedTask; }, "UiRenderTitle");
        var image = Button("导入标题 TXT／图片", () => ImportBitmap(false), "UiImportTitle");
        var logo = Button("导入 Logo TXT／图片", () => ImportBitmap(true), "UiImportLogo");
        var clock = Button("导入传统时刻字模 TXT", ImportFont, "UiImportClockFont");
        var generate = Button("用所选字体生成时刻字模", () => { var c = Require(); var data = new List<byte>(); foreach (char ch in "子丑寅卯辰巳午未申酉戌亥初正一二三刻待校时") data.AddRange(OledImageImport.Text(ch.ToString(), font.Text, 16, 16, 16)); c.SetClockFont(data.ToArray()); Changed(); return Task.CompletedTask; }, "UiGenerateClockFont");
        artwork.Children.Add(Row(text, image, logo)); artwork.Children.Add(Row(clock, generate));
        artwork.Children.Add(new TextBlock { Text = "标题最大 128×32；Logo 最大 64×32。图片等比缩放并居中，深色部分发光；阈值和反色在导入时使用。TXT 支持横向逐行、高位在前的 0xNN 数组。时刻字模为 21 个 16×16 字，顺序：子丑寅卯辰巳午未申酉戌亥初正一二三刻待校时。生成的文字与图片一起保存在键盘，断开客户端也能显示。", TextWrapping = TextWrapping.Wrap });
        var artExpander = new Expander { Header = "中文字模与 Logo", Content = artwork, HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch }; AutomationProperties.SetAutomationId(artExpander, "UiArtworkExpander"); screen.Children.Add(artExpander);
        screen.Children.Add(Footer("Screen"));
        encoders.Children.Add(readEncoder); encoders.Children.Add(new TextBlock { Text = "每层独立设置顺逆时针动作；旋钮按下在“键位与宏”中编辑。固件默认会保留原有音量、翻页和带修饰键的调色行为。", TextWrapping = TextWrapping.Wrap });
        encoders.Children.Add(Row(encoder, layer)); encoders.Children.Add(action); encoders.Children.Add(cw); encoders.Children.Add(ccw); encoders.Children.Add(applyEncoder); encoders.Children.Add(Footer("Encoder"));
        foreach (var item in Actions) action.Items.Add(item.Name); action.SelectedIndex = 0;
        foreach (var box in new[] { cw, ccw })
        {
            box.TextChanged += (_, e) => { if (e.Reason == AutoSuggestionBoxTextChangeReason.UserInput) box.ItemsSource = ViaKeyCatalog.Choices().Where(k => k.Name.Contains(box.Text, StringComparison.OrdinalIgnoreCase) || k.Description.Contains(box.Text, StringComparison.OrdinalIgnoreCase)).Take(40).ToArray(); };
            box.TextMemberPath = "Name"; box.SuggestionChosen += (_, e) => box.Text = ((ViaKeyChoice)e.SelectedItem).Name;
        }
        readScreen.Click += (_, _) => begin(() => ReadSectionAsync(ViaUiSection.Screen)); readEncoder.Click += (_, _) => begin(() => ReadSectionAsync(ViaUiSection.Encoders));
        widget.SelectionChanged += (_, _) => ShowWidget(); x.ValueChanged += (_, _) => EditWidget(); y.ValueChanged += (_, _) => EditWidget(); scale.SelectionChanged += (_, _) => EditWidget(); visible.Toggled += (_, _) => EditWidget();
        power.SelectionChanged += (_, _) => { if (!updating && working is not null && power.SelectedIndex >= 0) { working.Bytes[2] = (byte)power.SelectedIndex; Changed(); } };
        encoder.SelectionChanged += (_, _) => ShowEncoder(); layer.SelectionChanged += (_, _) => ShowEncoder();
        action.SelectionChanged += (_, _) => { cw.IsEnabled = ccw.IsEnabled = action.SelectedIndex == 1 && working is not null; };
        applyEncoder.Click += (_, _) => begin(() =>
        {
            var c = Require(); if (encoder.SelectedIndex < 0 || layer.SelectedIndex < 0) throw new IOException("请选择旋钮与层。");
            byte a = Actions[action.SelectedIndex].Id; if (a == 1 && (caps!.Lighting & 1) == 0 || a == 2 && (caps!.Lighting & 2) == 0) throw new IOException("此键盘没有对应灯光功能。");
            ushort right = a == 0 ? ViaKeycodes.Parse(cw.Text) : (ushort)0, left = a == 0 ? ViaKeycodes.Parse(ccw.Text) : (ushort)0;
            int p = 48 + (layer.SelectedIndex * 4 + encoder.SelectedIndex) * 5; c.Bytes[p] = a; c.Bytes[p + 1] = (byte)right; c.Bytes[p + 2] = (byte)(right >> 8); c.Bytes[p + 3] = (byte)left; c.Bytes[p + 4] = (byte)(left >> 8); Changed(); return Task.CompletedTask;
        });
        foreach (var (c, id) in new (DependencyObject, string)[] { (readScreen, "ReadViaUi"), (widget, "UiWidget"), (x, "UiWidgetX"), (y, "UiWidgetY"), (scale, "UiWidgetScale"), (visible, "UiWidgetVisible"), (preview, "UiPreview"), (encoder, "UiEncoder"), (layer, "UiEncoderLayer"), (action, "UiEncoderAction"), (cw, "UiEncoderCw"), (ccw, "UiEncoderCcw"), (applyEncoder, "UiApplyEncoder"), (power, "UiScreenPower"), (width, "UiImportWidth"), (height, "UiImportHeight"), (title, "UiTitleText"), (font, "UiFontName"), (fontSize, "UiFontSize"), (lcdOrder, "UiLcdOrder") }) AutomationProperties.SetAutomationId(c, id);
        Configure(false, false);
    }
    private static int Int(NumberBox n) { if (!double.IsFinite(n.Value) || n.Value != Math.Truncate(n.Value) || n.Value < n.Minimum || n.Value > n.Maximum) throw new IOException("请输入范围内的整数。"); return (int)n.Value; }
    private static UIElement Row(params UIElement[] children) { var p = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 }; foreach (var c in children) p.Children.Add(c); return new ScrollViewer { Content = p, HorizontalScrollBarVisibility = ScrollBarVisibility.Auto, VerticalScrollBarVisibility = ScrollBarVisibility.Disabled }; }
    private Button Button(string text, Func<Task> task, string id) { var b = new Button { Content = text }; b.Click += (_, _) => begin(task); draftButtons.Add(b); AutomationProperties.SetAutomationId(b, id); return b; }
    private static string SectionName(ViaUiSection section) => section == ViaUiSection.Screen ? "屏幕" : "旋钮";
    private UIElement Footer(string suffix)
    {
        var section = suffix == "Screen" ? ViaUiSection.Screen : ViaUiSection.Encoders;
        string name = SectionName(section);
        var save = Button($"写入{name}并保存", () => SaveAsync(section), "SaveViaUi" + suffix);
        var defaults = Button($"恢复{name}默认（暂存）", () => DefaultsAsync(section), "ResetViaUi" + suffix);
        connectionButtons.Add(save); connectionButtons.Add(defaults);
        return Row(save, defaults, Button($"导出{name}配置", () => ExportAsync(section), "ExportViaUi" + suffix),
            Button($"导入{name}配置", () => ImportAsync(section), "ImportViaUi" + suffix));
    }
    private ViaUiConfiguration Require() => working ?? throw new IOException("请先读取屏幕与旋钮配置。");
    private ViaUiClient Client() => new(transport() ?? throw new IOException("请先连接并读取 VIA。"));
    public async Task ReadAsync() { if (!await ConfirmDiscardAsync()) return; using var limit = CancellationTokenSource.CreateLinkedTokenSource(token); limit.CancelAfter(TimeSpan.FromMinutes(3)); var client = Client(); var c = await client.CapabilitiesAsync(limit.Token); var value = await client.ReadAsync(c, limit.Token); caps = c; original = value; working = value.Copy(); Bind(); report("已读取屏幕、字模、Logo 和所有层的旋钮动作。"); }
    private bool SectionDirty(ViaUiSection section) => working is not null && original is not null && !working.SectionEquals(original, section);
    public async Task<bool> ConfirmDiscardAsync(ViaUiSection? section = null)
    {
        if (section is null ? !Dirty : !SectionDirty(section.Value)) return true;
        string name = section is null ? "屏幕／旋钮" : SectionName(section.Value);
        var dialog = new ContentDialog { XamlRoot = screenRoot.XamlRoot ?? encoderRoot.XamlRoot,
            Title = $"有尚未保存的{name}配置", Content = "继续会丢弃此部分编辑，可先在对应页导出备份。",
            PrimaryButtonText = "继续", CloseButtonText = "取消", DefaultButton = ContentDialogButton.Close };
        return await dialog.ShowAsync() == ContentDialogResult.Primary;
    }
    private async Task ReadSectionAsync(ViaUiSection section)
    {
        if (!await ConfirmDiscardAsync(section)) return;
        if (working is null || original is null || caps is null) { await ReadAsync(); return; }
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(token); timeout.CancelAfter(TimeSpan.FromMinutes(3));
        var client = Client(); var actualCaps = await client.CapabilitiesAsync(timeout.Token);
        if (actualCaps != caps) throw new IOException("键盘容量已变化，请重新连接并读取。");
        var actual = await client.ReadAsync(caps, timeout.Token);
        working.CopySectionFrom(actual, section); original.CopySectionFrom(actual, section);
        RefreshSection(section); report($"已读取{SectionName(section)}配置；另一页的编辑稿保持不变。");
    }
    private async Task DefaultsAsync(ViaUiSection section)
    {
        if (caps is null || !await ConfirmDiscardAsync(section)) return;
        var defaults = await Client().DefaultsAsync(caps, token);
        Require().CopySectionFrom(defaults, section); RefreshSection(section); Changed();
        report($"已暂存{SectionName(section)}默认配置，点击本页的写入按钮保存；另一页保持不变。");
    }
    private void RefreshSection(ViaUiSection section)
    {
        if (working is null) return;
        if (section == ViaUiSection.Encoders) { ShowEncoder(); return; }
        updating = true; try { power.SelectedIndex = working.Bytes[2]; } finally { updating = false; }
        ShowWidget(); Preview();
    }
    private void Bind() { if (caps is null || working is null) return; updating = true; try { preview.Width = caps.Width * 4; preview.Height = caps.Height * 4; x.Maximum = caps.Width - 1; y.Maximum = caps.Height - 1; power.SelectedIndex = working.Bytes[2]; encoder.Items.Clear(); for (int i = 0; i < caps.Encoders; i++) encoder.Items.Add($"旋钮 {i}"); layer.Items.Clear(); for (int i = 0; i < caps.Layers; i++) layer.Items.Add($"层 {i}"); encoder.SelectedIndex = 0; layer.SelectedIndex = 0; info.Text = $"{caps.Width}×{caps.Height} OLED · {caps.Encoders} 个旋钮 · {caps.Layers} 层 · 配置与图案保存在键盘"; } finally { updating = false; } ShowWidget(); ShowEncoder(); Preview(); }
    private void ShowWidget() { if (updating || working is null || widget.SelectedIndex < 0) return; updating = true; try { int p = 16 + widget.SelectedIndex * 4; x.Value = working.Bytes[p]; y.Value = working.Bytes[p + 1]; scale.SelectedIndex = working.Bytes[p + 2] - 1; visible.IsOn = working.Bytes[p + 3] != 0; } finally { updating = false; } }
    private void EditWidget() { if (updating || working is null || widget.SelectedIndex < 0) return; try { int p = 16 + widget.SelectedIndex * 4; working.Bytes[p] = (byte)Int(x); working.Bytes[p + 1] = (byte)Int(y); working.Bytes[p + 2] = (byte)(scale.SelectedIndex + 1); working.Bytes[p + 3] = (byte)(visible.IsOn ? 1 : 0); Changed(); } catch (IOException e) { report(e.Message); } }
    private void ShowEncoder() { if (updating || working is null || encoder.SelectedIndex < 0 || layer.SelectedIndex < 0) return; int p = 48 + (layer.SelectedIndex * 4 + encoder.SelectedIndex) * 5; action.SelectedIndex = Array.FindIndex(Actions, a => a.Id == working.Bytes[p]); cw.Text = ViaKeycodes.Format((ushort)(working.Bytes[p + 1] | working.Bytes[p + 2] << 8)); ccw.Text = ViaKeycodes.Format((ushort)(working.Bytes[p + 3] | working.Bytes[p + 4] << 8)); }
    private void Changed() { working?.Seal(); Preview(); report("有尚未写入的修改。请在对应页点击写入；屏幕与旋钮分别保存。"); }
    private void Preview() { if (working is null || caps is null) return; preview.Children.Clear(); bool[] pixels = OledPreview.Render(working, caps); var brush = new SolidColorBrush(Colors.White); for (int i = 0; i < pixels.Length; i++) if (pixels[i]) { var r = new Rectangle { Width = 4, Height = 4, Fill = brush }; Canvas.SetLeft(r, (i % caps.Width) * 4); Canvas.SetTop(r, (i / caps.Width) * 4); preview.Children.Add(r); } crop.Text = "超出屏幕的图案会裁切；组件按 Logo、标题、传统时刻、时间、电量、连接、层的顺序叠加。"; }
    private async Task<StorageFile?> Pick(params string[] extensions) { var p = new FileOpenPicker(); foreach (string e in extensions) p.FileTypeFilter.Add(e); WinRT.Interop.InitializeWithWindow.Initialize(p, handle()); return await p.PickSingleFileAsync().AsTask().WaitAsync(token); }
    private async Task ImportBitmap(bool logo)
    {
        var config = Require();
        var file = await Pick(".txt", ".h", ".png", ".bmp", ".jpg", ".jpeg");
        if (file is null) return;
        var properties = await file.GetBasicPropertiesAsync();
        if (properties.Size == 0)
            throw new IOException($"“{file.Name}”是空文件（0 字节）。请先生成或导出文件内容，再重新导入；原编辑稿已保留。");
        if (properties.Size > 10_000_000) throw new IOException("导入文件不能超过 10 MB。");
        int maxWidth = logo ? 64 : 128;
        int w, h;
        byte[] pixels;
        bool textFile = file.FileType.Equals(".txt", StringComparison.OrdinalIgnoreCase) ||
            file.FileType.Equals(".h", StringComparison.OrdinalIgnoreCase);
        bool automatic = textFile && lcdOrder.SelectedIndex == 0;
        if (automatic)
        {
            var bitmap = ViaUiConfiguration.Import16PixelGlyphs(await ReadLcdText(file), maxWidth);
            w = bitmap.Width; h = bitmap.Height; pixels = bitmap.Pixels;
        }
        else
        {
            w = Int(width); h = Int(height);
            if (w > maxWidth) throw new IOException($"导入宽度不能超过 {maxWidth} 像素。");
            pixels = textFile
                ? ViaUiConfiguration.ParseLcdText(await ReadLcdText(file))
                : await OledImageImport.ImageAsync(file, w, h, Int(threshold), invert.IsOn);
        }
        token.ThrowIfCancellationRequested();
        config.SetBitmap(logo, w, h, pixels);
        // Update the canvas fields only after successful parsing and validation.
        width.Value = w; height.Value = h;
        Changed();
        if (automatic) report($"已导入 {w / 16} 个 16×16 字，自动设置为 {w}×{h}；请检查预览，点击“写入屏幕并保存”生效。");
    }
    private static async Task<string> ReadLcdText(StorageFile file)
    {
        using var stream = await file.OpenStreamForReadAsync(); using var memory = new MemoryStream(); await stream.CopyToAsync(memory); byte[] b = memory.ToArray();
        return ViaUiConfiguration.DecodeLcdText(b);
    }
    private async Task ImportFont() { var c = Require(); var f = await Pick(".txt", ".h"); if (f is null) return; var properties = await f.GetBasicPropertiesAsync(); if (properties.Size == 0) throw new IOException($"“{f.Name}”是空文件（0 字节）。请先导出字模内容，再重新导入；原编辑稿已保留。"); if (properties.Size > 200_000) throw new IOException("字模文件过大。"); c.SetClockFont(ViaUiConfiguration.ParseLcdText(await ReadLcdText(f))); Changed(); }
    private async Task SaveAsync(ViaUiSection section)
    {
        var desired = Require().Copy(); if (caps is null || original is null) return;
        using var limit = CancellationTokenSource.CreateLinkedTokenSource(token); limit.CancelAfter(TimeSpan.FromMinutes(4));
        string name = SectionName(section); report($"正在上传并保存{name}配置，请勿断电…");
        bool saving = true;
        ViaUiConfiguration actual;
        try { actual = await Client().SaveSectionAsync(desired, caps, section, limit.Token,
            new Progress<int>(n => { if (saving) report($"{name}保存中 {n}%"); })); }
        finally { saving = false; }
        // A saved section must not erase or mark the other tab's pending edits saved.
        original.CopySectionFrom(actual, section); Require().CopySectionFrom(actual, section);
        report($"{name}配置已保存，回读校验和 Flash 写入完成；另一页配置及编辑稿保持不变。");
    }
    private async Task ExportAsync(ViaUiSection section)
    {
        var config = Require(); if (caps is null) return;
        string name = SectionName(section);
        var picker = new FileSavePicker { SuggestedFileName = section == ViaUiSection.Screen ? "qmknrf5-screen" : "qmknrf5-encoders" };
        picker.FileTypeChoices.Add(name + "配置备份", new List<string> { ".json" }); WinRT.Interop.InitializeWithWindow.Initialize(picker, handle());
        var file = await picker.PickSaveFileAsync().AsTask().WaitAsync(token);
        if (file is not null) { await FileIO.WriteTextAsync(file, ViaUiConfiguration.ExportSection(config, caps, section)); report($"已导出{name}配置。"); }
    }
    private async Task ImportAsync(ViaUiSection section)
    {
        if (caps is null || !await ConfirmDiscardAsync(section)) return;
        var file = await Pick(".json"); if (file is null) return;
        if ((await file.GetBasicPropertiesAsync()).Size > 20_000) throw new IOException("配置文件过大。");
        working = ViaUiConfiguration.ImportSection(await FileIO.ReadTextAsync(file), Require(), caps, section);
        RefreshSection(section); Changed(); report($"已导入{SectionName(section)}配置（暂存），另一页的编辑稿保持不变。");
    }
    public void Clear() { caps = null; original = working = null; preview.Children.Clear(); info.Text = "请先读取支持屏幕／旋钮配置的配套固件。"; }
    public void Configure(bool enabled, bool connected)
    {
        screenRoot.IsEnabled = encoderRoot.IsEnabled = enabled && (connected || working is not null);
        readScreen.IsEnabled = readEncoder.IsEnabled = enabled && connected;
        foreach (var button in draftButtons) button.IsEnabled = working is not null;
        foreach (var button in connectionButtons) button.IsEnabled = working is not null && connected;
        foreach (Control control in new Control[] { widget, x, y, scale, visible, power, encoder, layer, action, cw, ccw, applyEncoder }) control.IsEnabled = working is not null;
        cw.IsEnabled = ccw.IsEnabled = working is not null && action.SelectedIndex == 1;
    }
}
