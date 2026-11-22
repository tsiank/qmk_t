// SPDX-License-Identifier: GPL-2.0-or-later
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;

namespace Qmknrf5.Core;

public enum FirmwareKind { Application, Bootloader }
public sealed record HexImage(string Entry, bool Bootloader, bool SoftDevice, bool Application, int Bytes);
public sealed record FirmwarePackage(string Path, FirmwareKind Kind, bool Dfu, string Components, string Sha256, long Length, HexImage[] HexImages)
{
    public static FirmwarePackage Inspect(string path, FirmwareKind expected)
    {
        try { return InspectCore(path,expected); }
        catch(Exception ex) when(ex is JsonException or KeyNotFoundException or InvalidOperationException or InvalidDataException or OverflowException)
        { throw new IOException("升级包格式无效或清单不完整。",ex); }
    }
    private static FirmwarePackage InspectCore(string path, FirmwareKind expected)
    {
        var info = new FileInfo(path);
        if (!info.Exists || info.Length is <= 0 or > 32 * 1024 * 1024) throw new IOException("请选择有效的 ZIP（最大 32 MB）。");
        using var archive = ZipFile.OpenRead(path);
        if (archive.Entries.Count is 0 or > 64 || archive.Entries.Sum(e => e.Length) > 64 * 1024 * 1024) throw new IOException("升级包内容过大或文件过多。");
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in archive.Entries)
        {
            if (!SafeName(entry.FullName) || !names.Add(entry.FullName)) throw new IOException("升级包包含不安全路径或重复文件。");
        }
        var manifest = archive.GetEntry("manifest.json");
        var hex = new List<HexImage>(); string components; bool dfu = manifest is not null;
        if (manifest is not null)
        {
            if (manifest.Length > 64 * 1024) throw new IOException("升级包清单过大。");
            using var json = JsonDocument.Parse(ReadBounded(manifest,64 * 1024));
            var body = json.RootElement.GetProperty("manifest");
            var parts = body.EnumerateObject().Where(p => p.Value.ValueKind != JsonValueKind.Null).ToArray();
            string[] known = ["application","bootloader","softdevice","softdevice_bootloader"];
            if (parts.Length == 0 || parts.Any(p => !known.Contains(p.Name))) throw new IOException("不支持的 Nordic DFU 清单。");
            bool app = parts.Any(p => p.Name == "application"), boot = parts.Any(p => p.Name is "bootloader" or "softdevice_bootloader");
            if (expected == FirmwareKind.Application ? !app || parts.Length != 1 : !boot || app)
                throw new IOException(expected == FirmwareKind.Application ? "此处需要仅包含应用固件的 DFU ZIP。" : "此处需要 Bootloader DFU ZIP（可含 SoftDevice，不可混入应用固件）。");
            foreach (var part in parts)
            {
                foreach (string key in new[] { "bin_file","dat_file" })
                {
                    string name = part.Value.GetProperty(key).GetString() ?? "";
                    var entry = archive.GetEntry(name);
                    int limit = key == "bin_file" ? 2 * 1024 * 1024 : 64 * 1024;
                    if (!SafeName(name) || entry is null || entry.Length <= 0 || entry.Length > limit || !name.EndsWith(key == "bin_file" ? ".bin" : ".dat",StringComparison.OrdinalIgnoreCase)) throw new IOException("DFU 清单引用的 BIN / DAT 缺失或无效。");
                    ReadBounded(entry,limit);
                }
            }
            components = string.Join(" + ",parts.Select(p => p.Name switch { "application"=>"应用固件","bootloader"=>"Bootloader","softdevice"=>"SoftDevice",_=>"Bootloader + SoftDevice" }));
        }
        else
        {
            if (expected != FirmwareKind.Bootloader) throw new IOException("应用固件需要含 manifest.json、BIN 和 DAT 的 Nordic DFU ZIP。");
            foreach (var entry in archive.Entries.Where(e => e.FullName.EndsWith(".hex",StringComparison.OrdinalIgnoreCase))) hex.Add(ParseHex(entry.FullName,ReadBounded(entry,8 * 1024 * 1024)));
            if (!hex.Any(h => h.Bootloader)) throw new IOException("此压缩包既不是 DFU 包，也没有 nRF52840 Bootloader HEX。");
            components = "J-Link / SWD Bootloader HEX 包";
        }
        using var stream = File.OpenRead(path);
        string hash = Convert.ToHexString(SHA256.HashData(stream));
        return new(System.IO.Path.GetFullPath(path),expected,dfu,components,hash,info.Length,hex.ToArray());
    }
    private static bool SafeName(string name) => name.Length is > 0 and < 240 && !name.StartsWith('/') && !name.Contains('\\') && !name.Contains(':') && !name.Any(char.IsControl) && !name.Split('/').Any(p => p is ".." or ".");
    private static byte[] ReadBounded(ZipArchiveEntry entry,int maximum)
    {
        if (entry.Length > maximum) throw new IOException("升级包中的文件过大。");
        using var input=entry.Open();using var output=new MemoryStream();byte[] buffer=new byte[8192];int size;
        while((size=input.Read(buffer))>0){if(output.Length+size>maximum)throw new IOException("升级包解压数据超限。");output.Write(buffer,0,size);}
        if(output.Length!=entry.Length)throw new IOException("升级包文件长度不匹配。");return output.ToArray();
    }
    private static HexImage ParseHex(string name,byte[] bytes)
    {
        uint upper=0;bool end=false,boot=false,sd=false,app=false;int total=0;
        using var reader=new StringReader(System.Text.Encoding.ASCII.GetString(bytes));string? line;
        while((line=reader.ReadLine()) is not null)
        {
            line=line.Trim();if(line.Length==0)continue;
            if(end||!line.StartsWith(':')||line.Length>1024)throw new IOException("HEX 记录或结束符无效。");
            byte[] row;try{row=Convert.FromHexString(line[1..]);}catch(FormatException){throw new IOException("HEX 记录不是有效十六进制。");}
            if(row.Length<5||row.Length!=row[0]+5||(row.Sum(b=>(int)b)&255)!=0)throw new IOException("HEX 记录长度或校验和错误。");
            uint address=(uint)(row[1]<<8|row[2]);int type=row[3],length=row[0];
            switch(type)
            {
                case 0:
                    ulong start=(ulong)upper+address,last=start+(uint)length;
                    if(last>0x100000 && !(start>=0x10001000&&last<=0x10002000))throw new IOException("HEX 地址超出 nRF52840 Flash / UICR 范围。");
                    boot |= start<0x100000&&last>0xE0000;sd |= start<0x26000&&last>0x1000;
                    app |= start<0xE0000&&last>0x26000;total+=length;break;
                case 1: if(length!=0||address!=0)throw new IOException("HEX 结束记录无效。");end=true;break;
                case 2: case 4:
                    if(length!=2||address!=0)throw new IOException("HEX 地址扩展记录无效。");upper=(uint)(row[4]<<8|row[5])<<(type==2?4:16);break;
                case 3: case 5: if(length!=4)throw new IOException("HEX 入口记录无效。");break;
                default:throw new IOException("不支持的 HEX 记录类型。");
            }
        }
        if(!end||total==0)throw new IOException("HEX 不完整或没有数据。");
        return new(name,boot,sd,app,total);
    }
    public string ExtractHex(string entryName,string directory)
    {
        if(!HexImages.Any(h=>h.Entry==entryName))throw new IOException("未验证的 HEX 镜像。");
        using var archive=ZipFile.OpenRead(Path);var entry=archive.GetEntry(entryName)??throw new IOException("HEX 文件已不存在。");
        int index=Array.FindIndex(HexImages,h=>h.Entry==entryName);
        string output=System.IO.Path.Combine(directory,$"{index:D2}-"+System.IO.Path.GetFileName(entryName));
        File.WriteAllBytes(output,ReadBounded(entry,8*1024*1024));return output;
    }
}

public sealed class FirmwareSnapshot : IDisposable
{
    public string Directory { get; }
    public FirmwarePackage Package { get; }
    public FirmwareSnapshot(string original,FirmwareKind kind)
    {
        Directory=System.IO.Path.Combine(System.IO.Path.GetTempPath(),"qmknrf5-dfu-"+Guid.NewGuid().ToString("N"));
        System.IO.Directory.CreateDirectory(Directory);
        try
        {
            string copy=System.IO.Path.Combine(Directory,"upgrade.zip");
            using(var input=new FileStream(original,FileMode.Open,FileAccess.Read,FileShare.Read))
            using(var output=File.Create(copy)) {if(input.Length>32*1024*1024)throw new IOException("ZIP 文件过大。");input.CopyTo(output);}
            Package=FirmwarePackage.Inspect(copy,kind);
        }
        catch{Dispose();throw;}
    }
    public void Dispose(){try{System.IO.Directory.Delete(Directory,true);}catch(IOException){}catch(UnauthorizedAccessException){}}
}
