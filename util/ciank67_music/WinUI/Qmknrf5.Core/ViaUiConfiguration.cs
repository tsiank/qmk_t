// SPDX-License-Identifier: GPL-2.0-or-later
using System.Buffers.Binary;
using System.Text.Json;
using System.Text;
using System.Text.RegularExpressions;

namespace Qmknrf5.Core;

public enum ViaUiSection { Screen, Encoders }
public sealed record LcdBitmap(int Width, int Height, byte[] Pixels);
public sealed record ViaUiCapabilities(int Width, int Height, int Encoders, int Layers, byte Lighting);
public sealed class ViaUiConfiguration(byte[] bytes)
{
    public const int Size = 1792, TitleOffset = 224, LogoOffset = 736, FontOffset = 992, CrcOffset = 1788;
    public byte[] Bytes { get; } = bytes.Length == Size ? (byte[])bytes.Clone() : throw new IOException("屏幕配置长度错误。");
    public ViaUiConfiguration Copy() => new(Bytes);
    public static uint Crc32(ReadOnlySpan<byte> bytes)
    {
        uint c = uint.MaxValue;
        foreach (byte b in bytes) { c ^= b; for (int i = 0; i < 8; i++) c = (c >> 1) ^ ((c & 1) != 0 ? 0xEDB88320u : 0); }
        return ~c;
    }
    public void Seal() => BinaryPrimitives.WriteUInt32LittleEndian(Bytes.AsSpan(CrcOffset), Crc32(Bytes.AsSpan(0, CrcOffset)));
    public void Validate(ViaUiCapabilities caps)
    {
        if (Bytes[0] != 1 || (Bytes[1] & ~7) != 0 || Bytes[2] > 2 || Bytes[3] != 0 || caps.Width is < 1 or > 128 || caps.Height is < 1 or > 64 || caps.Encoders is < 1 or > 4 || caps.Layers is < 1 or > 8) throw new IOException("不支持的屏幕／旋钮配置版本或容量。");
        for (int i = 0; i < 7; i++) { int p = 16 + i * 4; if (Bytes[p] >= caps.Width || Bytes[p + 1] >= caps.Height || Bytes[p + 2] is < 1 or > 2 || Bytes[p + 3] > 1) throw new IOException("屏幕组件坐标、字号或开关无效。"); }
        for (int i = 0; i < 2; i++) if ((Bytes[1] & (1 << i)) != 0 && (Bytes[4 + i * 2] < 1 || Bytes[4 + i * 2] > (i == 0 ? 128 : 64) || Bytes[5 + i * 2] is < 1 or > 32)) throw new IOException("字模／Logo 尺寸无效。");
        for (int i = 0; i < 32; i++) { int action = Bytes[48 + i * 5]; if (action != 255 && (action > 3 || action == 1 && (caps.Lighting & 1) == 0 || action == 2 && (caps.Lighting & 2) == 0)) throw new IOException("旋钮动作不受此键盘支持。"); }
        if (Crc32(Bytes.AsSpan(0, CrcOffset)) != BinaryPrimitives.ReadUInt32LittleEndian(Bytes.AsSpan(CrcOffset))) throw new IOException("屏幕配置校验失败。");
    }
    public void SetBitmap(bool logo, int width, int height, byte[] pixels)
    {
        if (width < 1 || width > (logo ? 64 : 128) || height is < 1 or > 32 || pixels.Length != (width + 7) / 8 * height) throw new IOException("字模尺寸或数据长度错误（标题最大 128×32，Logo 最大 64×32）。");
        int p = logo ? LogoOffset : TitleOffset, capacity = logo ? 256 : 512;
        Array.Clear(Bytes, p, capacity); pixels.CopyTo(Bytes, p); Bytes[1] |= (byte)(logo ? 2 : 1); Bytes[logo ? 6 : 4] = (byte)width; Bytes[logo ? 7 : 5] = (byte)height; Seal();
    }
    public void SetClockFont(byte[] glyphs)
    {
        if (glyphs.Length != 21 * 32) throw new IOException("时刻字模需要 21 个 16×16 字，顺序：子丑寅卯辰巳午未申酉戌亥初正一二三刻待校时。");
        glyphs.CopyTo(Bytes, FontOffset); Bytes[1] |= 4; Seal();
    }
    public static string DecodeLcdText(byte[] bytes)
    {
        if (bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
            return Encoding.UTF8.GetString(bytes, 3, bytes.Length - 3);
        if (bytes.Length >= 4 && bytes[0] == 0xFF && bytes[1] == 0xFE && bytes[2] == 0 && bytes[3] == 0)
            return Encoding.UTF32.GetString(bytes, 4, bytes.Length - 4);
        if (bytes.Length >= 4 && bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0xFE && bytes[3] == 0xFF)
            return new UTF32Encoding(true, true).GetString(bytes, 4, bytes.Length - 4);
        if (bytes.Length >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
            return Encoding.Unicode.GetString(bytes, 2, bytes.Length - 2);
        if (bytes.Length >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF)
            return Encoding.BigEndianUnicode.GetString(bytes, 2, bytes.Length - 2);
        // Some exporters omit the Unicode BOM. C-array tokens have enough
        // ASCII characters to identify their alternating UTF-16 zero bytes.
        int length = Math.Min(bytes.Length, 512), evenZeros = 0, oddZeros = 0;
        for (int i = 0; i < length; i++) if (bytes[i] == 0) { if (i % 2 == 0) evenZeros++; else oddZeros++; }
        if (oddZeros >= 8 && oddZeros > evenZeros * 3 && oddZeros >= length / 8)
            return Encoding.Unicode.GetString(bytes);
        if (evenZeros >= 8 && evenZeros > oddZeros * 3 && evenZeros >= length / 8)
            return Encoding.BigEndianUnicode.GetString(bytes);
        // GBK/ANSI and UTF-8 share the ASCII tokens; comments need no code-page decoder.
        return Encoding.Latin1.GetString(bytes);
    }
    public static byte[] ParseLcdText(string text)
    {
        if (string.IsNullOrWhiteSpace(text)) throw new IOException("字模文件为空或没有字模内容，请先导出内容再导入；原编辑稿已保留。");
        if (text.Length > 200_000) throw new IOException("字模文件过大。");
        text = Regex.Replace(text, @"/\*[\s\S]*?\*/|//[^\r\n]*", "");
        // Ignore #define/header values when a C-array body is present.
        int first = text.IndexOf('{'), last = text.LastIndexOf('}');
        if (first >= 0 && last > first) text = text[first..(last + 1)];
        var values = Regex.Matches(text, @"(?<![\w])0[xX]([0-9a-fA-F]+)(?![\w])");
        if (values.Count > 0)
        {
            if (values.Any(m => m.Groups[1].Value.Length > 2))
                throw new IOException("字模包含超过 8 位的数值，请按 8 位字节输出（0x00～0xFF）。");
            return values.Select(m => Convert.ToByte(m.Groups[1].Value, 16)).ToArray();
        }
        if (Regex.IsMatch(text, @"\A\s*[0-9A-Fa-f]{2}(?:[\s,;]+[0-9A-Fa-f]{2})*\s*\z"))
            return Regex.Split(text.Trim(), @"[\s,;]+").Select(s => Convert.ToByte(s, 16)).ToArray();
        throw new IOException("未识别到字模数据。请选择包含 0xNN 的 8 位 C 数组 TXT/H，或用空格分隔的十六进制字节；图片请选 PNG、BMP 或 JPG。编辑稿未改变，可以重新选择文件。");
    }
    public static LcdBitmap Import16PixelGlyphs(string text, int maxWidth = 128)
    {
        if (maxWidth is < 16 or > 128) throw new ArgumentOutOfRangeException(nameof(maxWidth));
        // PCtoLCD emits a comment per character, followed by its byte array.
        // The dimensions in these comments describe a glyph, not the whole title.
        var dimensions = Regex.Matches(text, @"/\*\s*\(\s*(\d{1,3})\s*[xX×]\s*(\d{1,3})\s*[,，)]");
        if (dimensions.Any(m => m.Groups[1].Value != "16" || m.Groups[2].Value != "16"))
            throw new IOException("当前逐字模式支持 16×16 字模。此文件标注了其他字模尺寸，请重新取模或选择整图模式并设置对应尺寸。");
        byte[] bytes = ParseLcdText(text);
        if (bytes.Length % 32 != 0)
            throw new IOException($"16×16 字模每字需要 32 字节，文件共 {bytes.Length} 字节，字模数据不完整。原编辑稿已保留。");
        int count = bytes.Length / 32;
        if (count > maxWidth / 16)
            throw new IOException($"文件包含 {count} 个 16×16 字，需要 {count * 16}×16 像素；当前导入区域最大宽度为 {maxWidth} 像素，请减少字数。原编辑稿已保留。");
        return new(count * 16, 16, Join16PixelGlyphs(bytes, count));
    }
    public static byte[] Join16PixelGlyphs(byte[] bytes, int count)
    {
        if (count < 1 || count > 8 || bytes.Length != count * 32) throw new IOException("16×16 字模数量与宽度不一致。");
        byte[] result = new byte[bytes.Length]; for (int g = 0; g < count; g++) for (int row = 0; row < 16; row++) Array.Copy(bytes, g * 32 + row * 2, result, row * count * 2 + g * 2, 2); return result;
    }
    internal static (int Offset, int Length)[] SectionRanges(ViaUiSection section) => section switch
    {
        ViaUiSection.Screen => [(1, 7), (16, 28), (TitleOffset, FontOffset + 672 - TitleOffset)],
        ViaUiSection.Encoders => [(48, 32 * 5)],
        _ => throw new ArgumentOutOfRangeException(nameof(section))
    };
    public bool SectionEquals(ViaUiConfiguration other, ViaUiSection section) =>
        SectionRanges(section).All(r => Bytes.AsSpan(r.Offset, r.Length).SequenceEqual(other.Bytes.AsSpan(r.Offset, r.Length)));
    public void CopySectionFrom(ViaUiConfiguration source, ViaUiSection section)
    {
        foreach (var (offset, length) in SectionRanges(section))
            source.Bytes.AsSpan(offset, length).CopyTo(Bytes.AsSpan(offset, length));
        Seal();
    }
    public static string ExportSection(ViaUiConfiguration config, ViaUiCapabilities caps, ViaUiSection section)
    {
        config.Validate(caps);
        var data = SectionRanges(section).SelectMany(r => config.Bytes.AsSpan(r.Offset, r.Length).ToArray()).ToArray();
        return JsonSerializer.Serialize(new { Version = 1, Section = section.ToString(),
            Width = section == ViaUiSection.Screen ? caps.Width : 0,
            Height = section == ViaUiSection.Screen ? caps.Height : 0,
            Encoders = section == ViaUiSection.Encoders ? caps.Encoders : 0,
            Layers = section == ViaUiSection.Encoders ? caps.Layers : 0,
            Data = Convert.ToBase64String(data), Crc32 = Crc32(data) }, new JsonSerializerOptions { WriteIndented = true });
    }
    public static ViaUiConfiguration ImportSection(string json, ViaUiConfiguration current, ViaUiCapabilities caps, ViaUiSection section)
    {
        if (json.Length > 20_000) throw new IOException("配置文件过大。");
        using var d = JsonDocument.Parse(json); var root = d.RootElement;
        // Read legacy combined backups, but apply only the selected tab's data.
        if (!root.TryGetProperty("Section", out var kind))
        {
            var legacy = Import(json, caps); var merged = current.Copy(); merged.CopySectionFrom(legacy, section); return merged;
        }
        if (root.GetProperty("Version").GetInt32() != 1 || kind.GetString() != section.ToString())
            throw new IOException("备份类型与当前页签不符，请在对应的屏幕或旋钮页导入。");
        bool compatible = section == ViaUiSection.Screen
            ? root.GetProperty("Width").GetInt32() == caps.Width && root.GetProperty("Height").GetInt32() == caps.Height
            : root.GetProperty("Encoders").GetInt32() == caps.Encoders && root.GetProperty("Layers").GetInt32() == caps.Layers;
        if (!compatible) throw new IOException("备份的屏幕尺寸或旋钮／层数量与键盘不一致。");
        byte[] data = Convert.FromBase64String(root.GetProperty("Data").GetString() ?? "");
        var ranges = SectionRanges(section);
        if (data.Length != ranges.Sum(r => r.Length) || Crc32(data) != root.GetProperty("Crc32").GetUInt32())
            throw new IOException("备份数据长度或校验不正确。");
        var result = current.Copy(); int position = 0;
        foreach (var (offset, length) in ranges) { data.AsSpan(position, length).CopyTo(result.Bytes.AsSpan(offset, length)); position += length; }
        result.Seal(); result.Validate(caps); return result;
    }
    public static string Export(ViaUiConfiguration config) => JsonSerializer.Serialize(new { Version = 1, Data = Convert.ToBase64String(config.Bytes) }, new JsonSerializerOptions { WriteIndented = true });
    public static ViaUiConfiguration Import(string json, ViaUiCapabilities caps)
    {
        if (json.Length > 20_000) throw new IOException("屏幕配置文件过大。");
        using var d = JsonDocument.Parse(json);
        if (d.RootElement.GetProperty("Version").GetInt32() != 1) throw new IOException("不支持的屏幕备份版本。");
        var c = new ViaUiConfiguration(Convert.FromBase64String(d.RootElement.GetProperty("Data").GetString() ?? "")); c.Validate(caps); return c;
    }
}

public sealed class ViaUiClient(IViaTransport transport)
{
    private async Task<byte[]> Command(byte verb, byte op, CancellationToken token, ushort id = 0, ushort offset = 0, byte count = 0, byte[]? payload = null)
    {
        byte[] r = new byte[32]; r[0] = verb; r[1] = 0x71; r[2] = op;
        BinaryPrimitives.WriteUInt16LittleEndian(r.AsSpan(4), id); BinaryPrimitives.WriteUInt16LittleEndian(r.AsSpan(6), offset); r[8] = count;
        if (payload is not null) payload.CopyTo(r, 9);
        byte[] a = await transport.ExchangeAsync(r, token);
        if (a.Length != 32 || a[0] != verb || a[1] != 0x71 || a[2] != op) throw new IOException("此固件尚不支持屏幕／旋钮配置，请刷入配套的新固件。");
        if (a[3] != 0) throw new IOException(a[3] switch { 2 => "配置数据越界。", 3 => "配置会话已失效或另一台主机正在编辑，请重新读取。", 4 => "上传校验失败，键盘保留原配置。", _ => "固件不支持此配置命令。" });
        if (op is 2 or 3 && !a.AsSpan(4, 5).SequenceEqual(r.AsSpan(4, 5))) throw new IOException("配置分包位置不匹配，请重新连接。");
        return a;
    }
    public async Task<ViaUiCapabilities> CapabilitiesAsync(CancellationToken token)
    {
        var a = await Command(8, 0, token);
        if (a[4] != 1 || BinaryPrimitives.ReadUInt16LittleEndian(a.AsSpan(9)) != ViaUiConfiguration.Size) throw new IOException("不支持的屏幕协议版本。");
        return new(a[5], a[6], a[7], a[8], a[11]);
    }
    private async Task<ViaUiConfiguration> Read(CancellationToken token, ushort session = 0)
    {
        byte[] b = new byte[ViaUiConfiguration.Size];
        for (int i = 0; i < b.Length; i += 23) { byte n = (byte)Math.Min(23, b.Length - i); var r = await Command(8, 3, token, session, (ushort)i, n); r.AsSpan(9, n).CopyTo(b.AsSpan(i)); }
        return new(b);
    }
    public async Task<ViaUiConfiguration> ReadAsync(ViaUiCapabilities caps, CancellationToken token)
    {
        ushort session = 0;
        try { session = BinaryPrimitives.ReadUInt16LittleEndian((await Command(7, 1, token)).AsSpan(4)); var c = await Read(token, session); c.Validate(caps); return c; }
        finally { await Abort(session); }
    }
    public async Task<ViaUiConfiguration> DefaultsAsync(ViaUiCapabilities caps, CancellationToken token)
    {
        ushort session = 0;
        try { session = BinaryPrimitives.ReadUInt16LittleEndian((await Command(7, 1, token, 1)).AsSpan(4)); var c = await Read(token, session); c.Validate(caps); return c; }
        finally { await Abort(session); }
    }
    private async Task Abort(ushort id)
    {
        if (id == 0) return;
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(4));
        try { await Command(7, 5, timeout.Token, id); } catch (Exception) {/* Connection loss expires the firmware session. */}
    }
    public async Task<ViaUiConfiguration> SaveSectionAsync(ViaUiConfiguration desired, ViaUiCapabilities caps,
        ViaUiSection section, CancellationToken token, IProgress<int>? progress = null)
    {
        var c = desired.Copy(); c.Seal(); c.Validate(caps); ushort session = 0;
        try
        {
            session = BinaryPrimitives.ReadUInt16LittleEndian((await Command(7, 1, token)).AsSpan(4));
            var actual = await Read(token, session); actual.Validate(caps);
            actual.CopySectionFrom(c, section);
            await UploadSectionAndCommit(actual, section, session, token, progress); session = 0;
            await VerifyAndWait(actual, token, progress);
            return actual;
        }
        finally { await Abort(session); }
    }
    private async Task UploadSectionAndCommit(ViaUiConfiguration config, ViaUiSection section, ushort session,
        CancellationToken token, IProgress<int>? progress)
    {
        var ranges = ViaUiConfiguration.SectionRanges(section).Append((ViaUiConfiguration.CrcOffset, 4)).ToArray();
        int total = ranges.Sum(r => r.Item2), done = 0;
        foreach (var (start, length) in ranges) for (int i = 0; i < length; i += 23)
        {
            byte count = (byte)Math.Min(23, length - i); int offset = start + i;
            await Command(7, 2, token, session, (ushort)offset, count, config.Bytes.AsSpan(offset, count).ToArray());
            done += count; progress?.Report(done * 85 / total);
        }
        await Command(9, 4, token, session);
    }
    private async Task UploadAndCommit(ViaUiConfiguration config, ushort session, CancellationToken token, IProgress<int>? progress)
    {
        for (int i = 0; i < config.Bytes.Length; i += 23)
        {
            byte n = (byte)Math.Min(23, config.Bytes.Length - i);
            await Command(7, 2, token, session, (ushort)i, n, config.Bytes.AsSpan(i, n).ToArray());
            progress?.Report((i + n) * 85 / config.Bytes.Length);
        }
        await Command(9, 4, token, session);
    }
    private async Task VerifyAndWait(ViaUiConfiguration expected, CancellationToken token, IProgress<int>? progress)
    {
        var actual = await Read(token);
        if (!actual.Bytes.AsSpan().SequenceEqual(expected.Bytes)) throw new IOException("配置回读不一致，请重新读取。编辑稿已保留。");
        var watch = System.Diagnostics.Stopwatch.StartNew();
        while (watch.Elapsed < TimeSpan.FromSeconds(20))
        {
            byte[] r = new byte[32]; r[0] = 0x80; var a = await transport.ExchangeAsync(r, token);
            if (a.Length != 32 || a[0] != 0x80) throw new IOException("Flash 状态响应错误。");
            if (a[1] == 0)
            {
                if (BinaryPrimitives.ReadUInt32BigEndian(a.AsSpan(2)) != 0) throw new IOException("配置已生效，但 Flash 保存失败，请重试。");
                progress?.Report(100); return;
            }
            await Task.Delay(150, token);
        }
        throw new IOException("配置已生效，等待 Flash 保存超时，请勿立即断电。");
    }
    public async Task SaveAsync(ViaUiConfiguration config, ViaUiCapabilities caps, CancellationToken token, IProgress<int>? progress = null)
    {
        var c = config.Copy(); c.Seal(); c.Validate(caps); ushort session = 0;
        try
        {
            session = BinaryPrimitives.ReadUInt16LittleEndian((await Command(7, 1, token)).AsSpan(4));
            await UploadAndCommit(c, session, token, progress); session = 0;
            await VerifyAndWait(c, token, progress);
        }
        finally { await Abort(session); }
    }
}
