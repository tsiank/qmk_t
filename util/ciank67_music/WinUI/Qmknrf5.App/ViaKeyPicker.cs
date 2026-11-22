// SPDX-License-Identifier: GPL-2.0-or-later
using Qmknrf5.Core;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;

namespace Qmknrf5.App;

// Native category palette and advanced editor inspired by VIA's keycode picker.
// Values always come from this firmware's QMK table, never VIA's older byte map.
internal sealed class ViaKeyPicker
{
    private readonly ComboBox category = new() { HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly TextBox search = new() { PlaceholderText = "搜索名称 / QMK 键码，例如：回车、Space、RM_TOGG、MIDI…" };
    private readonly GridView palette = new() { MaxHeight = 250, SelectionMode = ListViewSelectionMode.None, IsItemClickEnabled = true };
    private readonly TextBlock count = new() { FontSize = 12, Opacity = .65 };
    private readonly ComboBox kind = new() { HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly ComboBox hand = new() { HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly ComboBox tap = new() { HorizontalAlignment = HorizontalAlignment.Stretch, DisplayMemberPath = "Label" };
    private readonly ComboBox targetLayer = new() { HorizontalAlignment = HorizontalAlignment.Stretch };
    private readonly CheckBox ctrl = new() { Content = "Ctrl" }, shift = new() { Content = "Shift" }, alt = new() { Content = "Alt" }, win = new() { Content = "Win" };
    private readonly TextBlock preview = new() { TextWrapping = TextWrapping.Wrap };
    private readonly Button use = new() { Content = "使用此组合" };
    private readonly StackPanel modifiers = new() { Spacing = 8 };
    private readonly UIElement tapField, layerField;
    private readonly Expander composer;
    private readonly StackPanel panel = new() { Spacing = 10 };
    private readonly Action<ushort> choose;
    private IReadOnlyList<ViaKeyChoice> choices = ViaKeyCatalog.Choices();
    private int layers = 7, macros = 16;
    private bool editing, canAssign;
    public UIElement View => panel;
    public ViaKeyPicker(Action<ushort> choose)
    {
        this.choose = choose;
        panel.Children.Add(new TextBlock { Text = "选择新键位", FontSize = 18, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold });
        foreach(string text in ViaKeyCatalog.Categories)category.Items.Add(text);
        category.SelectedIndex=1;
        panel.Children.Add(category);panel.Children.Add(search);panel.Children.Add(count);
        palette.ItemTemplate=(DataTemplate)XamlReader.Load("""
            <DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
              <Border Width="98" Height="68" Margin="2" Padding="5" BorderThickness="1" BorderBrush="#30808080" CornerRadius="6" ToolTipService.ToolTip="{Binding Description}">
              <StackPanel Spacing="4">
                <TextBlock Text="{Binding Label}" TextAlignment="Center" TextWrapping="Wrap" MaxLines="2" FontSize="13"/>
                <TextBlock Text="{Binding Name}" TextAlignment="Center" TextTrimming="CharacterEllipsis" FontSize="10" Opacity="0.65"/>
              </StackPanel></Border>
            </DataTemplate>
            """);
        palette.ItemsPanel=(ItemsPanelTemplate)XamlReader.Load("""
            <ItemsPanelTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"><ItemsWrapGrid Orientation="Horizontal"/></ItemsPanelTemplate>
            """);
        ScrollViewer.SetHorizontalScrollBarVisibility(palette,ScrollBarVisibility.Disabled);
        ScrollViewer.SetVerticalScrollBarVisibility(palette,ScrollBarVisibility.Auto);
        panel.Children.Add(palette);
        var form=new StackPanel { Spacing=10 };
        foreach(string text in new[]{"快捷组合键（Ctrl+C 等）","轻按 / 按住修饰键 · Mod-Tap","轻按 / 按住切层 · Layer-Tap","层 + 修饰键 · Layer-Mod","单次修饰键 · One-Shot","按住进入层 · MO","切换层开关 · TG","跳转到层 · TO","默认层 · DF","单次进入层 · OSL","轻按切层 / 按住 · TT","保存默认层 · PDF"})kind.Items.Add(text);
        kind.SelectedIndex=0;
        form.Children.Add(Field("组合类型",kind));
        hand.Items.Add("左侧修饰键");hand.Items.Add("右侧修饰键");hand.SelectedIndex=0;
        modifiers.Children.Add(hand);
        var flags=new StackPanel { Orientation=Orientation.Horizontal,Spacing=10 };
        foreach(var flag in new[]{ctrl,shift,alt,win}) flags.Children.Add(flag);
        modifiers.Children.Add(new ScrollViewer { Content=flags,HorizontalScrollBarVisibility=ScrollBarVisibility.Auto,VerticalScrollBarVisibility=ScrollBarVisibility.Disabled });
        form.Children.Add(modifiers);
        tap.ItemsSource=choices.Where(k=>k.Code<=255).ToArray();
        tap.SelectedItem=((IEnumerable<ViaKeyChoice>)tap.ItemsSource).First(k=>k.Code==44);
        tapField=Field("轻按 / 基本按键",tap);layerField=Field("目标层",targetLayer);
        for(int i=0;i<layers;i++)targetLayer.Items.Add($"第 {i} 层");targetLayer.SelectedIndex=0;
        form.Children.Add(tapField);form.Children.Add(layerField);form.Children.Add(preview);form.Children.Add(use);
        composer=new Expander { Header="组合键 / 层功能",Content=form,HorizontalAlignment=HorizontalAlignment.Stretch,HorizontalContentAlignment=HorizontalAlignment.Stretch };
        panel.Children.Add(composer);
        foreach(var (element,id) in new (DependencyObject,string)[]{(category,"ViaKeyCategory"),(search,"ViaKeySearch"),(palette,"ViaKeyPalette"),(kind,"ViaComboKind"),(hand,"ViaComboHand"),(tap,"ViaComboTap"),(targetLayer,"ViaComboLayer"),(ctrl,"ViaComboCtrl"),(shift,"ViaComboShift"),(alt,"ViaComboAlt"),(win,"ViaComboWin"),(use,"ApplyViaCombo"),(preview,"ViaComboPreview"),(composer,"ViaComboEditor")})AutomationProperties.SetAutomationId(element,id);
        category.SelectionChanged+=(_,_)=>Filter();search.TextChanged+=(_,_)=>{if(!string.IsNullOrWhiteSpace(search.Text))category.SelectedIndex=0;Filter();};
        palette.ItemClick+=(_,e)=>choose(((ViaKeyChoice)e.ClickedItem).Code);
        foreach(var combo in new[]{kind,hand,tap,targetLayer})combo.SelectionChanged+=(_,_)=>Preview();
        foreach(var flag in new[]{ctrl,shift,alt,win}){flag.Checked+=(_,_)=>Preview();flag.Unchecked+=(_,_)=>Preview();}
        use.Click+=(_,_)=>{try{choose(Value());}catch(FormatException ex){preview.Text=ex.Message;}};
        Filter();Preview();
    }
    private static UIElement Field(string label,UIElement control)
    {
        var field=new StackPanel { Spacing=6 };field.Children.Add(new TextBlock { Text=label });field.Children.Add(control);return field;
    }
    private void Filter()
    {
        string selected=(string?)category.SelectedItem??"全部";
        var filtered=choices.Where(k=>(selected=="全部"||k.Category==selected)&&ViaKeyCatalog.Matches(k,search.Text)).ToArray();
        palette.ItemsSource=filtered;count.Text=$"{filtered.Length} 个键位 · 先选键盘图中的按键，再点选下方键位暂存";
    }
    private ushort Value()
    {
        int bits=(ctrl.IsChecked==true?1:0)|(shift.IsChecked==true?2:0)|(alt.IsChecked==true?4:0)|(win.IsChecked==true?8:0);
        if(bits!=0&&hand.SelectedIndex==1)bits|=16;
        string mods=ViaKeycodes.ModifierText(bits);string basic=(tap.SelectedItem as ViaKeyChoice)?.Name??"KC_SPC";int layer=Math.Max(0,targetLayer.SelectedIndex);
        if(kind.SelectedIndex is 0 or 1 or 3 or 4 && bits==0)throw new FormatException("请至少勾选一个修饰键。");
        string expression=kind.SelectedIndex switch {
            0=>$"MT({mods},{basic})", // Encode below without the Mod-Tap flag.
            1=>$"MT({mods},{basic})",2=>$"LT({layer},{basic})",3=>$"LM({layer},{mods})",4=>$"OSM({mods})",
            5=>$"MO({layer})",6=>$"TG({layer})",7=>$"TO({layer})",8=>$"DF({layer})",9=>$"OSL({layer})",10=>$"TT({layer})",11=>$"PDF({layer})",_=>throw new FormatException("请选择组合类型。")};
        ushort value=ViaKeycodes.Parse(expression);return kind.SelectedIndex==0?(ushort)(value&0x1FFF):value;
    }
    private void Preview()
    {
        bool usesMods=kind.SelectedIndex is 0 or 1 or 3 or 4, usesTap=kind.SelectedIndex is 0 or 1 or 2, usesLayer=kind.SelectedIndex is >=2 and not 4;
        modifiers.Visibility=usesMods?Visibility.Visible:Visibility.Collapsed;tapField.Visibility=usesTap?Visibility.Visible:Visibility.Collapsed;layerField.Visibility=usesLayer?Visibility.Visible:Visibility.Collapsed;
        try {ushort value=Value();preview.Text=ViaKeyCatalog.Describe(value);use.IsEnabled=editing&&canAssign;}
        catch(FormatException ex){preview.Text=ex.Message;use.IsEnabled=false;}
    }
    private int keycodeRevision=-1;
    public void Configure(bool enabled,bool assign,int layerCount,int macroCount)
    {
        editing=enabled;canAssign=assign;
        foreach(Control control in new Control[]{category,search,palette,kind,hand,tap,targetLayer,ctrl,shift,alt,win})control.IsEnabled=enabled;
        if(layers!=layerCount||macros!=macroCount||keycodeRevision!=ViaKeycodes.Revision)
        {
            keycodeRevision=ViaKeycodes.Revision;layers=layerCount;macros=macroCount;choices=ViaKeyCatalog.Choices(layers,macros);
            int previous=targetLayer.SelectedIndex;targetLayer.Items.Clear();for(int i=0;i<layers;i++)targetLayer.Items.Add($"第 {i} 层");targetLayer.SelectedIndex=Math.Clamp(previous,0,layers-1);Filter();
        }
        Preview();
    }
    public IEnumerable<ViaKeyChoice> Suggestions(string query) => choices.Where(k=>ViaKeyCatalog.Matches(k,query)).Take(40);
    public void SelectKey(ushort code)
    {
        int selectedKind, bits=0, layer=0;
        if(code is >=0x2000 and <=0x3FFF){selectedKind=1;bits=code>>8&31;}
        else if(code is >=0x4000 and <=0x4FFF){selectedKind=2;layer=code>>8&15;}
        else if(code is >=0x5000 and <=0x51FF){selectedKind=3;layer=code>>5&15;bits=code&31;}
        else if(code is >=0x52A0 and <=0x52BF){selectedKind=4;bits=code&31;}
        else if(code is >=0x5200 and <=0x52FF)
        {
            selectedKind=(code&0xFFE0) switch {0x5220=>5,0x5260=>6,0x5200=>7,0x5240=>8,0x5280=>9,0x52C0=>10,0x52E0=>11,_=>-1};layer=code&31;
        }
        else if(code<=0x1FFF){selectedKind=0;bits=code>>8;}
        else return;
        if(selectedKind<0||layer>=layers)return;
        kind.SelectedIndex=selectedKind;hand.SelectedIndex=(bits&16)==0?0:1;
        ctrl.IsChecked=(bits&1)!=0;shift.IsChecked=(bits&2)!=0;alt.IsChecked=(bits&4)!=0;win.IsChecked=(bits&8)!=0;
        targetLayer.SelectedIndex=layer;
        tap.SelectedItem=((IEnumerable<ViaKeyChoice>)tap.ItemsSource).FirstOrDefault(k=>k.Code==(code&255));
        Preview();
    }
}
