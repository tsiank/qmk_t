// SPDX-License-Identifier: GPL-2.0-or-later
using System.Diagnostics;
using System.IO.Compression;
using System.Text;
using System.Text.Json;
using Ciank67.Music.Core;

internal static class FirmwareTests
{
    private static void Check(bool value,string message){if(!value)throw new Exception(message);}
    private static void Reject<T>(Action action) where T:Exception{try{action();}catch(T){return;}throw new Exception("Expected "+typeof(T).Name);}
    private static string Zip(string directory,string name,Dictionary<string,byte[]> files)
    {
        string path=Path.Combine(directory,name);using var z=ZipFile.Open(path,ZipArchiveMode.Create);
        foreach(var (n,b) in files){using var s=z.CreateEntry(n).Open();s.Write(b);}return path;
    }
    private static Dictionary<string,byte[]> Dfu(string type)=>new(){["manifest.json"]=JsonSerializer.SerializeToUtf8Bytes(new { manifest=new Dictionary<string,object>{[type]=new { bin_file="firmware.bin",dat_file="firmware.dat" }} }),["firmware.bin"]=[1,2,3],["firmware.dat"]=[1,2,3]};
    private static string Row(ushort address,byte type,params byte[] data)
    {
        byte[] bytes=[(byte)data.Length,(byte)(address>>8),(byte)address,type,..data];return ":"+Convert.ToHexString([..bytes,(byte)(-bytes.Sum(b=>(int)b)&255)]);
    }
    public static void Run(Action<string,Action> test)
    {
        test("Firmware ZIP validates application and bootloader types before any tool starts",()=>
        {
            string dir=Path.Combine(Path.GetTempPath(),Guid.NewGuid().ToString("N"));Directory.CreateDirectory(dir);
            try
            {
                string app=Zip(dir,"application.zip",Dfu("application"));var info=FirmwarePackage.Inspect(app,FirmwareKind.Application);
                Check(info.Dfu&&info.Kind==FirmwareKind.Application&&info.Sha256.Length==64,"Application metadata missing");
                Reject<IOException>(()=>FirmwarePackage.Inspect(app,FirmwareKind.Bootloader));
                string boot=Zip(dir,"boot.zip",Dfu("bootloader"));Check(FirmwarePackage.Inspect(boot,FirmwareKind.Bootloader).Dfu,"Bootloader DFU rejected");
                Reject<IOException>(()=>FirmwarePackage.Inspect(boot,FirmwareKind.Application));
                string combined=Zip(dir,"combined.zip",Dfu("softdevice_bootloader"));Check(FirmwarePackage.Inspect(combined,FirmwareKind.Bootloader).Components.Contains("SoftDevice"),"Combined SD image not identified");
                var missing=Dfu("application");missing.Remove("firmware.dat");Reject<IOException>(()=>FirmwarePackage.Inspect(Zip(dir,"missing.zip",missing),FirmwareKind.Application));
                var traversal=Dfu("application");traversal["../outside.exe"]=[1];Reject<IOException>(()=>FirmwarePackage.Inspect(Zip(dir,"traversal.zip",traversal),FirmwareKind.Application));
                Reject<IOException>(()=>FirmwarePackage.Inspect(Zip(dir,"invalid.zip",new(){["manifest.json"]=Encoding.UTF8.GetBytes("{}")}),FirmwareKind.Application));
                var duplicate=Dfu("application");duplicate["FIRMWARE.BIN"]=[1];Reject<IOException>(()=>FirmwarePackage.Inspect(Zip(dir,"duplicate.zip",duplicate),FirmwareKind.Application));
            }
            finally{Directory.Delete(dir,true);}
        });
        test("Bootloader HEX validates checksums, segmented addresses and SoftDevice companions",()=>
        {
            string dir=Path.Combine(Path.GetTempPath(),Guid.NewGuid().ToString("N"));Directory.CreateDirectory(dir);
            try
            {
                string high=Row(0,2,0xF0,0)+"\n"+Row(0x4000,0,0,0,4,0x20)+"\n"+Row(0,1)+"\n";
                string low=Row(0,4,0,0)+"\n"+Row(0x1000,0,1,2,3,4)+"\n"+Row(0,1)+"\n";
                var files=new Dictionary<string,byte[]>{["bootloader/nrf52840_xxaa.hex"]=Encoding.ASCII.GetBytes(high),["bootloader/softdevice.hex"]=Encoding.ASCII.GetBytes(low),["bootloader/flush.bat"]=Encoding.ASCII.GetBytes("must never execute")};
                string zip=Zip(dir,"hex.zip",files);var info=FirmwarePackage.Inspect(zip,FirmwareKind.Bootloader);
                Check(!info.Dfu&&info.HexImages.Count(h=>h.Bootloader)==1&&info.HexImages.Count(h=>h.SoftDevice)==1,"HEX component classification wrong");
                files["bootloader/nrf52840_xxaa.hex"]=Encoding.ASCII.GetBytes(high.Replace(":02000002F0000C",":02000002F0000D"));
                Reject<IOException>(()=>FirmwarePackage.Inspect(Zip(dir,"bad-checksum.zip",files),FirmwareKind.Bootloader));
                files["bootloader/nrf52840_xxaa.hex"]=Encoding.ASCII.GetBytes(high[..high.LastIndexOf(':')]);
                Reject<IOException>(()=>FirmwarePackage.Inspect(Zip(dir,"no-eof.zip",files),FirmwareKind.Bootloader));
            }
            finally{Directory.Delete(dir,true);}
        });
        test("Firmware snapshot freezes selected bytes and removes temporary files",()=>
        {
            string dir=Path.Combine(Path.GetTempPath(),Guid.NewGuid().ToString("N"));Directory.CreateDirectory(dir);string? snapshotDir=null;
            try
            {
                string zip=Zip(dir,"app.zip",Dfu("application"));
                using(var snapshot=new FirmwareSnapshot(zip,FirmwareKind.Application))
                {
                    snapshotDir=snapshot.Directory;File.Delete(zip);Check(File.Exists(snapshot.Package.Path),"Snapshot depends on external ZIP");
                    Check(snapshot.Package.Sha256==FirmwarePackage.Inspect(snapshot.Package.Path,FirmwareKind.Application).Sha256,"Snapshot hash changed");
                }
                Check(!Directory.Exists(snapshotDir),"Snapshot was not cleaned");
            }
            finally{Directory.Delete(dir,true);}
        });
        test("J-Link auto-detection requires one unique nonzero probe before any SWD operation",()=>
        {
            Check(FirmwareRunner.SingleProbe(["Connected probes:"," 12345678 ","12345678"])=="12345678","Unique probe not resolved");
            Check(FirmwareRunner.SingleProbe(["12345678\r\n"])=="12345678","CRLF output rejected");
            Reject<IOException>(()=>FirmwareRunner.SingleProbe([]));
            Reject<IOException>(()=>FirmwareRunner.SingleProbe(["0","0000","No debuggers found"]));
            Reject<IOException>(()=>FirmwareRunner.SingleProbe(["12345678","87654321"]));
        });
        test("Flash commands preserve spaces, unicode and shell metacharacters as literal arguments",()=>
        {
            string path=@"D:\固件 文件\a & $(touch file).zip";var command=FirmwareRunner.Dfu("nrfutil.exe",path,"COM12");
            Check(!command.UseShellExecute&&command.ArgumentList.SequenceEqual(new[]{"-v","-v","dfu","usb-serial","-pkg",path,"-p","COM12"}),"DFU arguments were interpolated through a shell");
            Reject<ArgumentException>(()=>FirmwareRunner.Dfu("tool",path,"COM1 & evil"));
            var swd=FirmwareRunner.Swd("nrfjprog.exe","12345678","--program",path,"--verify");
            Check(swd.ArgumentList.Contains("--snr")&&swd.ArgumentList.Contains("12345678")&&swd.ArgumentList.Contains("--verify"),"SWD command lacks explicit target or verify");
            Reject<ArgumentException>(()=>FirmwareRunner.Swd("tool","","--recover"));
            var echo=FirmwareRunner.Command(Environment.ProcessPath!,command.ArgumentList);echo.Environment["CIANK67_FLASH_TEST_CHILD"]="echo";
            var lines=new List<string>();var result=FirmwareRunner.RunAsync(echo,o=>{lock(lines)lines.Add(o.Text);},default).GetAwaiter().GetResult();
            var args=JsonSerializer.Deserialize<string[]>(lines.Single());Check(result.ExitCode==0&&args!.Contains(path),"Process runner changed quoted argument bytes");
        });
        test("Flash runner streams carriage-return progress and stderr and reports nonzero exit",()=>
        {
            foreach(string mode in new[]{"success","fail"})
            {
                var command=FirmwareRunner.Command(Environment.ProcessPath!,[]);command.Environment["CIANK67_FLASH_TEST_CHILD"]=mode;
                var lines=new List<FlashOutput>();var result=FirmwareRunner.RunAsync(command,o=>{lock(lines)lines.Add(o);},default).GetAwaiter().GetResult();
                Check(!result.Cancelled&&result.ExitCode==(mode=="fail"?7:0),"Tool failure treated as success");
                Check(lines.Any(l=>l.Percent==0)&&lines.Any(l=>l.Percent==50)&&lines.Any(l=>l.Percent==100)&&lines.Any(l=>l.Error&&l.Text.Contains("stderr")),"Redirected output/progress was lost");
                Check(!lines.Any(l=>l.Text.Contains('\u001b')),"ANSI escapes leaked into UI");
            }
        });
        test("Flash runner cancellation kills and drains the child without reporting success",()=>
        {
            var command=FirmwareRunner.Command(Environment.ProcessPath!,[]);command.Environment["CIANK67_FLASH_TEST_CHILD"]="wait";
            using var cts=new CancellationTokenSource(TimeSpan.FromMilliseconds(500));var clock=Stopwatch.StartNew();
            var result=FirmwareRunner.RunAsync(command,_=>{},cts.Token).GetAwaiter().GetResult();
            Check(result.Cancelled&&clock.Elapsed<TimeSpan.FromSeconds(6),"Cancelled child was left running");
            using var cancelled=new CancellationTokenSource();cancelled.Cancel();
            Reject<OperationCanceledException>(()=>FirmwareRunner.RunAsync(command,_=>{},cancelled.Token).GetAwaiter().GetResult());
        });
        test("Real supplied bootloader archive is inspected without running its BAT files",()=>
        {
            const string path=@"F:\qmk_firmmwire_tools\bootloader.zip";if(!File.Exists(path))return;
            var info=FirmwarePackage.Inspect(path,FirmwareKind.Bootloader);
            Check(!info.Dfu&&info.HexImages.Count(h=>h.Bootloader)==2,"Real HEX archive missing variants");
            Check(info.HexImages.Any(h=>h.Entry.EndsWith("nrf52840_xxaa.hex")&&h.Bootloader&&!h.SoftDevice),"Nordic segmented-address image not recognized");
            Check(info.HexImages.Any(h=>h.Entry.Contains("pca10056_bootloader")&&h.Bootloader&&h.SoftDevice),"Adafruit merged image not recognized");
        });
    }
}
