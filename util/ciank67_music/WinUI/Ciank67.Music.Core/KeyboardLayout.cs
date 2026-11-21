// SPDX-License-Identifier: GPL-2.0-or-later
using System.Text.Json;

namespace Ciank67.Music.Core;

public sealed record LayoutKey(int Row,int Column,double X,double Y,double Width,double Height);
public sealed record KeyboardLayout(string Name,int Rows,int Columns,LayoutKey[] Keys,IReadOnlyDictionary<string,ushort>? CustomKeycodes=null)
{
    // Layout profiles carry explicit electrical matrix dimensions. Physical
    // key positions alone cannot reveal unused trailing rows/columns.
    public static KeyboardLayout Parse(string json)
    {
        if(json.Length>500_000)throw new IOException("布局文件过大。");
        try
        {
            using var document=JsonDocument.Parse(json);var root=document.RootElement;
            var matrix=root.GetProperty("matrix");int rows=matrix.GetProperty("rows").GetInt32(),columns=matrix.GetProperty("cols").GetInt32();
            if(rows is <1 or >32 || columns is <1 or >32 || rows*columns>512)throw new IOException("布局矩阵尺寸无效。");
            string name=root.GetProperty("name").GetString()??"";
            if(string.IsNullOrWhiteSpace(name)||name.Length>100)throw new IOException("布局名称无效。");
            var keys=new List<LayoutKey>();var cells=new HashSet<(int,int)>();
            foreach(var key in root.GetProperty("layout").EnumerateArray())
            {
                if(key.GetProperty("matrix").GetArrayLength()!=2)throw new IOException("按键必须使用行列两个矩阵坐标。");
                int row=key.GetProperty("matrix")[0].GetInt32(),column=key.GetProperty("matrix")[1].GetInt32();
                double x=key.GetProperty("x").GetDouble(),y=key.GetProperty("y").GetDouble();
                double w=key.TryGetProperty("w",out var width)?width.GetDouble():1,h=key.TryGetProperty("h",out var height)?height.GetDouble():1;
                if(row<0||row>=rows||column<0||column>=columns||!cells.Add((row,column)))throw new IOException("布局按键的矩阵坐标越界或重复。");
                if(!double.IsFinite(x)||!double.IsFinite(y)||!double.IsFinite(w)||!double.IsFinite(h)||x<0||y<0||x>64||y>64||w<0.1||h<0.1||w>8||h>8)throw new IOException("布局按键的位置或尺寸无效。");
                if(key.TryGetProperty("r",out var rotation)&&rotation.GetDouble()!=0)throw new IOException("当前布局暂不支持旋转按键。");
                keys.Add(new(row,column,x,y,w,h));
            }
            if(keys.Count==0)throw new IOException("布局没有按键。");
            var custom=new Dictionary<string,ushort>();
            if(root.TryGetProperty("custom_keycodes",out var codes))
                foreach(var code in codes.EnumerateObject())
                {
                    if(!System.Text.RegularExpressions.Regex.IsMatch(code.Name,"^[A-Z][A-Z0-9_]{0,63}$")||ViaKeycodes.Names.TryGetValue(code.Name,out ushort existing)&&existing<ViaKeycodes.Names["QK_USER"]||!code.Value.TryGetUInt16(out ushort value)||value<ViaKeycodes.Names["QK_USER"]||code.Name.StartsWith("QK_",StringComparison.Ordinal)||!custom.TryAdd(code.Name,value))throw new IOException("布局的自定义键码名称或范围无效。");
                }
            return new(name,rows,columns,keys.ToArray(),custom);
        }
        catch(Exception ex) when(ex is JsonException or KeyNotFoundException or InvalidOperationException or IndexOutOfRangeException or FormatException or OverflowException)
        {throw new IOException("布局格式无效，需要 name、matrix.rows / cols 和 layout 按键数组。",ex);}
    }
}
