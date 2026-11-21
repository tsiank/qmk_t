// SPDX-License-Identifier: GPL-2.0-or-later
namespace Ciank67.Music.Core;

public sealed record ViaKeyChoice(ushort Code, string Name, string Label, string Description, string Category, string SearchText)
{
    public override string ToString() => $"{Label} · {Name}";
}

public static class ViaKeyCatalog
{
    public static readonly string[] Categories = ["全部", "基础", "符号", "媒体", "鼠标", "层切换", "组合", "灯光", "音频 / MIDI", "宏", "键盘专用", "其他 QMK"];
    private static readonly Dictionary<string,string> Labels = new() {
        ["KC_NO"]="禁用",["KC_TRNS"]="透明",["KC_ENT"]="Enter",["KC_ESC"]="Esc",["KC_BSPC"]="Backspace",["KC_TAB"]="Tab",["KC_SPC"]="Space",
        ["KC_CAPS"]="Caps Lock",["KC_DEL"]="Delete",["KC_INS"]="Insert",["KC_HOME"]="Home",["KC_END"]="End",["KC_PGUP"]="Page Up",["KC_PGDN"]="Page Down",
        ["KC_UP"]="↑",["KC_DOWN"]="↓",["KC_LEFT"]="←",["KC_RGHT"]="→",["KC_LCTL"]="左 Ctrl",["KC_RCTL"]="右 Ctrl",["KC_LSFT"]="左 Shift",["KC_RSFT"]="右 Shift",
        ["KC_LALT"]="左 Alt",["KC_RALT"]="右 Alt",["KC_LGUI"]="左 Win",["KC_RGUI"]="右 Win",["KC_PSCR"]="Print Screen",["KC_SCRL"]="Scroll Lock",["KC_PAUS"]="Pause",
        ["KC_MINS"]="−",["KC_EQL"]="=",["KC_LBRC"]="[",["KC_RBRC"]="]",["KC_BSLS"]="\\",["KC_SCLN"]=";",["KC_QUOT"]="'",["KC_GRV"]="`",["KC_COMM"]=",",["KC_DOT"]=".",["KC_SLSH"]="/",
        ["KC_TILD"]="~",["KC_EXLM"]="!",["KC_AT"]="@",["KC_HASH"]="#",["KC_DLR"]="$",["KC_PERC"]="%",["KC_CIRC"]="^",["KC_AMPR"]="&",["KC_ASTR"]="*",["KC_LPRN"]="(",["KC_RPRN"]=")",
        ["KC_UNDS"]="_",["KC_PLUS"]="+",["KC_LCBR"]="{",["KC_RCBR"]="}",["KC_PIPE"]="|",["KC_COLN"]=":",["KC_DQUO"]="\"",["KC_LABK"]="<",["KC_RABK"]=">",["KC_QUES"]="?",
        ["MI_ON"]="开启 MIDI",["MI_OFF"]="关闭 MIDI",["MI_TOGG"]="MIDI 开关",["AU_ON"]="开启声音",["AU_OFF"]="关闭声音",["AU_TOGG"]="声音开关",["MU_ON"]="开启本地演奏",["MU_OFF"]="关闭本地演奏",["MU_TOGG"]="本地演奏开关",["CK_TOGG"]="按键音开关",["NK_TOGG"]="NKRO 开关",
        ["KC_MUTE"]="静音",["KC_VOLU"]="音量＋",["KC_VOLD"]="音量−",["KC_MPLY"]="播放 / 暂停",["KC_MSTP"]="停止播放",["KC_MNXT"]="下一曲",["KC_MPRV"]="上一曲",
        ["DISC"]="断开蓝牙",["ADVW"]="开放配对",["ADVS"]="蓝牙广播",["SEL0"]="蓝牙设备 1",["SEL1"]="蓝牙设备 2",["SEL2"]="蓝牙设备 3",["DELB"]="重新配对",
        ["SLEEP"]="键盘休眠",["REBOOT"]="重启键盘",["ENT_DFU"]="刷机模式",["RGBRST"]="重置底灯",["RGBG_TOG"]="底灯开关",["BATT_LV"]="电池电压",["BATT_LV2"]="电池电量",
        ["VOLUP"]="按键测试 A/B",["RGBM_M_P"]="矩阵纯色",["RGBM_M_B"]="矩阵呼吸",["RGBM_M_R"]="矩阵彩虹",["RGBM_M_SW"]="矩阵旋转",
        ["LIGHT_STATUS"]="灯光诊断",["UNDERGLOW_TEST"]="底灯测试",["RGBM_M_SPECTRUM"]="矩阵频谱",["MIDI_TYPING_TOGGLE"]="MIDI 打字伴奏"
    };
    private static readonly Dictionary<string,string> Synonyms = new() { ["KC_ENT"]="回车 换行",["KC_ESC"]="退出 取消",["KC_TAB"]="制表",["KC_SPC"]="空格",["KC_BSPC"]="退格 删除",["KC_CAPS"]="大写锁定",["KC_DEL"]="删除",["KC_PGUP"]="向上翻页",["KC_PGDN"]="向下翻页",["KC_HOME"]="行首",["KC_END"]="行尾" };
    private static int labelRevision=-1;
    private static Dictionary<ushort,string> labelsByCode=[];
    private static Dictionary<ushort,string> LabelByCode
    {
        get
        {
            if(labelRevision!=ViaKeycodes.Revision){labelsByCode=Labels.Where(p=>ViaKeycodes.Names.ContainsKey(p.Key)).GroupBy(p=>ViaKeycodes.Names[p.Key]).ToDictionary(g=>g.Key,g=>g.First().Value);labelRevision=ViaKeycodes.Revision;}
            return labelsByCode;
        }
    }
    private static readonly Dictionary<string,string> Lighting = new() { ["ON"]="开启",["OFF"]="关闭",["TOGGLE"]="开关",["MODE_NEXT"]="下个灯效",["MODE_PREVIOUS"]="上个灯效",["HUE_UP"]="色相＋",["HUE_DOWN"]="色相−",["SATURATION_UP"]="饱和度＋",["SATURATION_DOWN"]="饱和度−",["VALUE_UP"]="亮度＋",["VALUE_DOWN"]="亮度−",["SPEED_UP"]="速度＋",["SPEED_DOWN"]="速度−" };
    public static string Label(ushort code)
    {
        if(LabelByCode.TryGetValue(code,out var label))return label;
        string name=ViaKeycodes.Format(code);
        if(code>=4&&code<=29)return ((char)('A'+code-4)).ToString();
        if(code>=30&&code<=38)return (code-29).ToString();if(code==39)return "0";
        if(code is >=0xCD and <=0xD0)return new[]{"鼠标 ↑","鼠标 ↓","鼠标 ←","鼠标 →"}[code-0xCD];
        if(code is >=0xD1 and <=0xD8)return $"鼠标键 {code-0xD0}";
        if(code is >=0xD9 and <=0xDC)return new[]{"滚轮 ↑","滚轮 ↓","滚轮 ←","滚轮 →"}[code-0xD9];
        if(code is >=0xDD and <=0xDF)return $"鼠标速度 {code-0xDD}";
        if(code is >=0x59 and <=0x61)return $"Num {code-0x58}";
        if(code==0x62)return "Num 0";
        if(code>=58&&code<=69)return $"F{code-57}";if(code>=104&&code<=115)return $"F{code-91}";
        string? canonical=ViaKeycodes.Names.FirstOrDefault(p=>p.Value==code&&(p.Key.StartsWith("QK_RGB_MATRIX_")||p.Key.StartsWith("QK_UNDERGLOW_"))).Key;
        if(canonical is not null)
        {
            bool matrix=canonical.StartsWith("QK_RGB_MATRIX_");string tail=canonical[(matrix?14:13)..];
            if(Lighting.TryGetValue(tail,out string? action))return (matrix?"矩阵":"底灯")+action;
        }
        if(code is >=0x5200 and <=0x52FF && (code&0xFFE0)!=0x52A0)
        {
            int n=code&31;return (code&0xFFE0) switch { 0x5220=>$"Fn{n}",0x5260=>$"层{n}开关",0x5200=>$"跳到层{n}",0x5240=>$"默认层{n}",0x5280=>$"单次层{n}",0x52C0=>$"轻按层{n}",0x52E0=>$"保存层{n}",_=>name };
        }
        if(code>=0x7700&&code<=0x771F)return $"宏 {code-0x7700}";
        int open=name.IndexOf('(');
        if(open>0)
        {
            string op=name[..open];var args=ViaKeycodes.Arguments(name[(open+1)..^1]);
            if(op=="LT")return $"{Label(ViaKeycodes.Parse(args[1]))}\nFn{args[0]}";
            if(op=="MT"||op.EndsWith("_T"))return $"{Label((ushort)(code&255))}\n{ModLabel(code>>8&31)}";
            if(op=="LM")return $"Fn{code>>5&15}\n{ModLabel(code&31)}";
            if(op=="OSM")return "单次 "+ModLabel(code&31);
            if(code is >=0x0100 and <=0x1FFF)return $"{ModLabel(code>>8)}+{Label((ushort)(code&255))}";
        }
        if(name.StartsWith("KC_"))return name[3..];
        return name;
    }
    private static string ModLabel(int bits)
    {
        string[] names=["Ctrl","Shift","Alt","Win"];return string.Join("+",Enumerable.Range(0,4).Where(i=>(bits&1<<i)!=0).Select(i=>names[i]));
    }
    private static string Group(ushort code)
    {
        if(code is >=0x7E40 and <0x7E40+23)return "键盘专用";
        if(code is >=0x7820 and <=0x7834 || code is >=0x7840 and <=0x784E)return "灯光";
        if(code is >=0x7100 and <=0x71FF || code is >=0x7480 and <=0x74BF)return "音频 / MIDI";
        if(code is >=0x7700 and <=0x777F)return "宏";
        if(code is >=0x5200 and <=0x52FF)return "层切换";
        if(code is >=0x00CD and <=0x00DF)return "鼠标";
        if(code is >=0x00A5 and <=0x00CD)return "媒体";
        if(code is >=0x0200 and <=0x027F || code is >=45 and <=56)return "符号";
        if(code is >=0x0100 and <=0x51FF)return "组合";
        if(code<=0x00A4 || code is >=0x00E0 and <=0x00E7)return "基础";
        return "其他 QMK";
    }
    public static string Describe(ushort code)
    {
        string name=ViaKeycodes.Format(code),label=Label(code).Replace('\n',' ');
        if(code is >=0x2000 and <=0x3FFF)return $"轻按 {Label((ushort)(code&255))}，按住 {ModLabel(code>>8&31)} · {name}";
        if(code is >=0x4000 and <=0x4FFF)return $"轻按 {Label((ushort)(code&255))}，按住进入第 {code>>8&15} 层 · {name}";
        if(code is >=0x5200 and <=0x52FF && (code&0xFFE0)!=0x52A0)
        {
            int n=code&31;string action=(code&0xFFE0) switch {0x5220=>$"按住进入第 {n} 层，松开返回",0x5260=>$"切换第 {n} 层开关",0x5200=>$"跳转到第 {n} 层",0x5240=>$"将第 {n} 层设为默认层",0x5280=>$"下一个按键使用第 {n} 层",0x52C0=>$"按住进入第 {n} 层，连续轻按切换",0x52E0=>$"保存第 {n} 层为默认层",_=>label};return $"{action} · {name}";
        }
        return $"{label} · {name}";
    }
    public static IReadOnlyList<ViaKeyChoice> Choices(int layers=7,int macros=16)
    {
        var codes=ViaKeycodes.Preferred.Keys.Where(c=>c<0x7700||c>0x777F||c<0x7700+macros).ToHashSet();
        foreach(string op in new[]{"MO","TG","TO","DF","PDF","OSL","TT"})for(int i=0;i<layers;i++)codes.Add(ViaKeycodes.Parse($"{op}({i})"));
        foreach(string expression in new[]{"LCTL(KC_C)","LCTL(KC_V)","LCTL(KC_X)","LCTL(KC_Z)","LCTL(KC_A)","LALT(KC_TAB)","LSFT_T(KC_SPC)","LCTL_T(KC_ESC)"})codes.Add(ViaKeycodes.Parse(expression));
        if(layers>1)codes.Add(ViaKeycodes.Parse("LT(1,KC_SPC)"));
        return codes.Order().Select(c=>new ViaKeyChoice(c,ViaKeycodes.Format(c),Label(c),Describe(c),Group(c),string.Join(' ',ViaKeycodes.Names.Where(p=>p.Value==c).Select(p=>p.Key)) + " " + string.Join(' ',Synonyms.Where(p=>ViaKeycodes.Names[p.Key]==c).Select(p=>p.Value)))).ToArray();
    }
    public static bool Matches(ViaKeyChoice item,string query)=>string.IsNullOrWhiteSpace(query)||$"{item.Label} {item.Name} {item.Description} {item.SearchText}".Contains(query.Trim(),StringComparison.OrdinalIgnoreCase);
}
