// SPDX-License-Identifier: GPL-2.0-or-later
using System.Text.RegularExpressions;
using Microsoft.Win32;
using Windows.Devices.Enumeration;
using Windows.Devices.SerialCommunication;

namespace Qmknrf5.App;
internal sealed record FirmwarePort(string Port,string Name,bool Dfu)
{
    public override string ToString() => $"{Port} · {Name}";
}
internal static class FirmwareDevices
{
    public static async Task<List<FirmwarePort>> PortsAsync(CancellationToken token)
    {
        var found=new Dictionary<string,FirmwarePort>(StringComparer.OrdinalIgnoreCase);
        var devices=await DeviceInformation.FindAllAsync(SerialDevice.GetDeviceSelector(),new[]{"System.Devices.DeviceInstanceId","System.DeviceInterface.Serial.PortName"}).AsTask().WaitAsync(token);
        foreach(var device in devices)
        {
            string properties=string.Join(' ',device.Properties.Values.Select(v=>v?.ToString()));
            var match=Regex.Match(device.Name+" "+properties,@"\bCOM[1-9][0-9]*\b",RegexOptions.IgnoreCase);
            if(!match.Success)continue;
            bool dfu=(properties.Contains("VID_1915",StringComparison.OrdinalIgnoreCase)&&properties.Contains("PID_521F",StringComparison.OrdinalIgnoreCase))||device.Name.Contains("DFU",StringComparison.OrdinalIgnoreCase);
            found[match.Value.ToUpperInvariant()]=new(match.Value.ToUpperInvariant(),device.Name,dfu);
        }
        using var key=Registry.LocalMachine.OpenSubKey(@"HARDWARE\DEVICEMAP\SERIALCOMM");
        if(key is not null)foreach(string name in key.GetValueNames())
        {
            string? port=key.GetValue(name) as string;
            if(port is not null&&!found.ContainsKey(port)&&Regex.IsMatch(port,@"^COM[1-9][0-9]*$"))found[port]=new(port,name.Contains("Bth",StringComparison.OrdinalIgnoreCase)?"蓝牙串口（不是 USB DFU）":"串口",false);
        }
        return found.Values.OrderByDescending(p=>p.Dfu).ThenBy(p=>int.Parse(p.Port[3..])).ToList();
    }
    public static string ResolveTool(string text)
    {
        text=Environment.ExpandEnvironmentVariables(text.Trim().Trim('"'));
        if(File.Exists(text))return Path.GetFullPath(text);
        if(!text.Contains('\\')&&!text.Contains('/'))foreach(string directory in (Environment.GetEnvironmentVariable("PATH")??"").Split(Path.PathSeparator))
        {
            string path=Path.Combine(directory.Trim('"'),text);if(File.Exists(path))return path;
        }
        throw new FileNotFoundException($"找不到 {text}，请在“工具路径”中选择对应 EXE。");
    }
}
