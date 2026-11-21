// SPDX-License-Identifier: GPL-2.0-or-later
using System.Globalization;
using System.Text.RegularExpressions;
using System.Text.Json;

namespace Ciank67.Music.Core;

public static class ViaKeycodes
{
    private static readonly IReadOnlyDictionary<string,ushort> DefaultNames=Load();
    public static IReadOnlyDictionary<string, ushort> Names { get; private set; } = DefaultNames;
    public static int Revision { get; private set; }
    public static void ConfigureKeyboard(IReadOnlyDictionary<string,ushort>? custom)
    {
        ushort start=DefaultNames["QK_USER"];
        var names=DefaultNames.Where(p=>p.Value<start||p.Key.StartsWith("QK_",StringComparison.Ordinal)).ToDictionary(p=>p.Key,p=>p.Value);
        foreach(var pair in custom??new Dictionary<string,ushort>())
        {
            if(!Regex.IsMatch(pair.Key,"^[A-Z][A-Z0-9_]{0,63}$")||pair.Value<start||names.ContainsKey(pair.Key))throw new IOException("自定义键码名称或范围无效。");
            names.Add(pair.Key,pair.Value);
        }
        Names=names;Preferred=BuildPreferred();Revision++;
    }
    public static IReadOnlyDictionary<ushort, string> Preferred { get; private set; } = BuildPreferred();
    private static IReadOnlyDictionary<ushort,string> BuildPreferred() => Names
        .Where(p => !p.Key.EndsWith("_MAX") && !Names.ContainsKey(p.Key + "_MAX") && p.Key is not ("QK_USER" or "QK_KB") && !p.Key.StartsWith("QK_USER_") && !p.Key.StartsWith("QK_KB_") && !p.Key.StartsWith('_'))
        .GroupBy(p => p.Value).ToDictionary(g => g.Key, g => g.OrderBy(p => p.Key.Length).ThenBy(p => p.Key, StringComparer.Ordinal).First().Key);
    private static readonly Dictionary<string, string> Layers = new() { ["MO"]="QK_MOMENTARY", ["TG"]="QK_TOGGLE_LAYER", ["TO"]="QK_TO", ["DF"]="QK_DEF_LAYER", ["PDF"]="QK_PERSISTENT_DEF_LAYER", ["OSL"]="QK_ONE_SHOT_LAYER", ["TT"]="QK_LAYER_TAP_TOGGLE" };
    private static readonly Dictionary<string, int> Modifiers = new() { ["LCTL"]=1,["LSFT"]=2,["LALT"]=4,["LGUI"]=8,["RCTL"]=17,["RSFT"]=18,["RALT"]=20,["RGUI"]=24,["MEH"]=7,["HYPR"]=15,["C"]=1,["S"]=2,["A"]=4,["G"]=8,["LWIN"]=8,["RWIN"]=24 };
    private static Dictionary<string, ushort> Load()
    {
        using var stream = typeof(ViaKeycodes).Assembly.GetManifestResourceStream("Ciank67.Music.Core.ViaKeycodes.json")!;
        return JsonSerializer.Deserialize<Dictionary<string, ushort>>(stream)!;
    }
    public static string ModifierText(int bits)
    {
        if (bits == 0) return "0";
        string hand = (bits & 16) == 0 ? "L" : "R";
        var result = new List<string>(); string[] names = ["CTL","SFT","ALT","GUI"];
        for (int i=0;i<4;i++) if ((bits & 1<<i)!=0) result.Add("MOD_"+hand+names[i]);
        return result.Count == 0 ? "0x10" : string.Join(" | ",result);
    }
    public static string Format(ushort code)
    {
        if (Preferred.TryGetValue(code, out var name)) return name;
        foreach (var (symbol, prefix) in Layers)
            if (code >= Names[prefix] && code <= Names[prefix] + 31) return $"{symbol}({code & 31})";
        if (code is >= 0x0100 and <= 0x1FFF)
        {
            int bits=code>>8; string value=Format((ushort)(code&255)); string hand=(bits&16)==0?"L":"R";
            if ((bits&15)==0) return $"0x{code:X4}";
            string[] mods=["CTL","SFT","ALT","GUI"];
            for(int i=3;i>=0;i--) if((bits&1<<i)!=0)value=$"{hand}{mods[i]}({value})";
            return value;
        }
        if (code is >= 0x2000 and <= 0x3FFF)
        {
            int bits=(code>>8)&31;
            string? single=Modifiers.FirstOrDefault(p=>p.Value==bits&&p.Key.Length==4).Key;
            return single is not null ? $"{single}_T({Format((ushort)(code&255))})" : $"MT({ModifierText(bits)},{Format((ushort)(code&255))})";
        }
        if (code is >= 0x4000 and <= 0x4FFF) return $"LT({code >> 8 & 15},{Format((ushort)(code & 255))})";
        if (code is >= 0x5000 and <= 0x51FF) return $"LM({code>>5&15},{ModifierText(code&31)})";
        if (code is >= 0x52A0 and <= 0x52BF) return $"OSM({ModifierText(code&31)})";
        if (code is >= 0x5700 and <= 0x57FF) return $"TD({code&255})";
        return $"0x{code:X4}";
    }
    public static ushort Parse(string text)
    {
        text = text.Trim().ToUpperInvariant();
        if (text.Length > 180) throw new FormatException("键码表达式过长。");
        return ParseInner(text, 0);
    }
    private static int Mods(string text)
    {
        int bits=0; bool? right=null;
        foreach(string term in text.Split('|',StringSplitOptions.TrimEntries))
        {
            if(term is "0" or "0X00")continue;
            if(term=="0X10") {bits|=16;continue;}
            if(!term.StartsWith("MOD_")||!Modifiers.TryGetValue(term[4..],out int value))throw new FormatException("修饰键必须选择 Ctrl / Shift / Alt / Win。");
            bool hand=(value&16)!=0;
            if(right is bool previous&&previous!=hand)throw new FormatException("一个 QMK 修饰组合不能混用左右修饰键。");
            right=hand;bits|=value;
        }
        return bits;
    }
    private static ushort ParseInner(string text, int depth)
    {
        if (depth > 8) throw new FormatException("键码嵌套过深。");
        if (Names.TryGetValue(text, out var code)) return code;
        if (text.StartsWith("0X") && ushort.TryParse(text.AsSpan(2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out code)) return code;
        int open = text.IndexOf('(');
        if (open < 1 || !text.EndsWith(')')) throw new FormatException("请选择键码或组合键，也可输入 KC_A、MO(1)、LT(1,KC_SPC)、LCTL(KC_C)。");
        string function = text[..open]; var args=Arguments(text[(open+1)..^1]);
        if (Layers.TryGetValue(function, out var range) && args.Count == 1 && int.TryParse(args[0], out int layer) && layer is >= 0 and < 32) return (ushort)(Names[range] | layer);
        if (function == "LT" && args.Count == 2 && int.TryParse(args[0], out layer) && layer is >= 0 and < 16)
        {
            ushort basic = ParseInner(args[1], depth + 1);
            if (basic > 255) throw new FormatException("LT 的轻按键必须是基本键码。");
            return (ushort)(Names["QK_LAYER_TAP"] | layer << 8 | basic);
        }
        if(function=="LM"&&args.Count==2&&int.TryParse(args[0],out layer)&&layer is >=0 and <16)return (ushort)(0x5000|layer<<5|Mods(args[1]));
        if(function=="OSM"&&args.Count==1)return (ushort)(0x52A0|Mods(args[0]));
        if(function=="MT"&&args.Count==2)
        {
            ushort basic=ParseInner(args[1],depth+1);if(basic>255)throw new FormatException("MT 的轻按键必须是基本键码。");
            return (ushort)(0x2000|Mods(args[0])<<8|basic);
        }
        if(function=="TD"&&args.Count==1&&int.TryParse(args[0],out int dance)&&dance is >=0 and <=255)return (ushort)(0x5700|dance);
        bool tap = function.EndsWith("_T"); string modifier = tap ? function[..^2] : function;
        if (Modifiers.TryGetValue(modifier, out int bits) && args.Count == 1)
        {
            ushort basic = ParseInner(args[0], depth + 1);
            if (tap && basic > 255 || !tap && basic > 0x1FFF) throw new FormatException("修饰键组合的键码不正确。");
            if (!tap && basic > 255 && ((basic >> 8 ^ bits) & 16) != 0) throw new FormatException("不能在一个键码中混用左右修饰键。");
            return (ushort)((tap ? Names["QK_MOD_TAP"] : 0) | bits << 8 | basic);
        }
        throw new FormatException("无效的键码或参数。");
    }
    public static List<string> Arguments(string body)
    {
        var args = new List<string>(); int start = 0, nesting = 0;
        for (int i = 0; i < body.Length; i++)
        {
            if (body[i] == '(') nesting++;
            if (body[i] == ')') nesting--;
            if (nesting < 0) throw new FormatException("括号不匹配。");
            if (body[i] == ',' && nesting == 0) { args.Add(body[start..i].Trim()); start = i + 1; }
        }
        if (nesting != 0) throw new FormatException("括号不匹配。");
        args.Add(body[start..].Trim());return args;
    }
}
