// SPDX-License-Identifier: GPL-2.0-or-later
using System.Diagnostics;
using System.Text;
using System.Text.RegularExpressions;

namespace Ciank67.Music.Core;

public sealed record FlashOutput(string Text,bool Error,int? Percent);
public sealed record FlashResult(int ExitCode,bool Cancelled);
public static class FirmwareRunner
{
    public static ProcessStartInfo Command(string tool,IEnumerable<string> arguments)
    {
        var info=new ProcessStartInfo(tool){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true,StandardOutputEncoding=Encoding.UTF8,StandardErrorEncoding=Encoding.UTF8};
        foreach(string arg in arguments)info.ArgumentList.Add(arg);
        info.Environment["PYTHONUNBUFFERED"]="1";info.Environment["PYTHONIOENCODING"]="utf-8";return info;
    }
    public static ProcessStartInfo Dfu(string tool,string package,string port)
    {
        if(!Regex.IsMatch(port,"^COM[1-9][0-9]{0,4}$",RegexOptions.IgnoreCase))throw new ArgumentException("请选择有效的 DFU COM 串口。");
        return Command(tool,["-v","-v","dfu","usb-serial","-pkg",package,"-p",port.ToUpperInvariant()]);
    }
    public static string SingleProbe(IEnumerable<string> lines)
    {
        var ids=lines.SelectMany(line=>line.Split(['\r','\n'],StringSplitOptions.RemoveEmptyEntries)).Select(line=>line.Trim())
            .Where(line=>Regex.IsMatch(line,"^[0-9]{1,16}$")&&line.Any(c=>c!='0')).Distinct().ToArray();
        if(ids.Length==0)throw new IOException("未检测到 J-Link，请检查连接和驱动后重试。");
        if(ids.Length>1)throw new IOException("检测到多个 J-Link，请只保留目标键盘所用的一个后重试。");
        return ids[0];
    }
    public static ProcessStartInfo Swd(string tool,string serial,params string[] args)
    {
        if(!Regex.IsMatch(serial,"^[0-9]{1,16}$"))throw new ArgumentException("请选择明确的 J-Link 序列号。");
        return Command(tool,new[]{"-f","NRF52","--snr",serial}.Concat(args));
    }
    public static async Task<FlashResult> RunAsync(ProcessStartInfo command,Action<FlashOutput> output,CancellationToken token)
    {
        token.ThrowIfCancellationRequested();
        using var process=new Process { StartInfo=command };
        if(!process.Start())throw new IOException("无法启动刷入工具。");
        var stdout=PumpAsync(process.StandardOutput,false,output);var stderr=PumpAsync(process.StandardError,true,output);bool cancelled=false;
        try {await process.WaitForExitAsync(token).ConfigureAwait(false);}
        catch(OperationCanceledException) when(token.IsCancellationRequested)
        {
            cancelled=true;
            try { process.Kill(entireProcessTree:true); }
            catch(InvalidOperationException) { }
            catch(System.ComponentModel.Win32Exception ex) { output(new("未能立即中止工具，继续等待其退出："+ex.Message,true,null)); }
            // Never release devices or delete the image while the tool may still use them.
            await process.WaitForExitAsync().ConfigureAwait(false);
        }
        await Task.WhenAll(stdout,stderr).ConfigureAwait(false);
        return new(process.ExitCode,cancelled);
    }
    private static async Task PumpAsync(StreamReader reader,bool error,Action<FlashOutput> output)
    {
        char[] buffer=new char[1024];var line=new StringBuilder();int length;
        void Flush(){if(line.Length==0)return;string text=Regex.Replace(line.ToString(),"\u001b\\[[0-9;?]*[ -/]*[@-~]","");line.Clear();var match=Regex.Match(text,@"(?<!\d)(100|[0-9]{1,2})(?:\.\d+)?\s*%");output(new(text,error,match.Success?int.Parse(match.Groups[1].Value):null));}
        while((length=await reader.ReadAsync(buffer).ConfigureAwait(false))>0)
        {
            foreach(char c in buffer.AsSpan(0,length))
            {if(c is '\r' or '\n')Flush();else if(c!='\0'){line.Append(c);if(line.Length>=8192)Flush();}}
        }
        Flush();
    }
}
