// SPDX-License-Identifier: GPL-2.0-or-later
using System.Buffers.Binary;
using System.Text.Json;

namespace Qmknrf5.Core;

public interface IViaTransport : IAsyncDisposable
{
    Task<byte[]> ExchangeAsync(byte[] request, CancellationToken token);
}

public sealed record ViaSnapshot(int Version, int Layers, int Rows, int Columns, ushort[] Keys, int MacroCount, byte[] Macros)
{
    public ViaSnapshot Copy() => this with { Keys = (ushort[])Keys.Clone(), Macros = (byte[])Macros.Clone() };
}

public sealed record ViaMatrixDescription(int Rows, int Columns, bool UiSupported);

public sealed class ViaClient(IViaTransport transport,int rows=5,int columns=14)
{
    public bool UiSupported { get; private set; }
    public const int Rows = 5, Columns = 14;
    public static readonly Guid ServiceUuid = new("c1a00010-7d9b-4c7d-a568-7294e2166700");
    public static readonly Guid ReceiveUuid = new("c1a00011-7d9b-4c7d-a568-7294e2166700");
    public static readonly Guid StateUuid = new("c1a00012-7d9b-4c7d-a568-7294e2166700");
    private async Task<byte[]> CommandAsync(byte command, CancellationToken token, params byte[] data)
    {
        if (data.Length > 31) throw new ArgumentException("VIA payload exceeds 31 bytes.");
        byte[] request = new byte[32]; request[0] = command; data.CopyTo(request, 1);
        byte[] response = await transport.ExchangeAsync(request, token);
        if (response.Length != 32 || response[0] != command) throw new IOException($"VIA 命令 0x{command:X2} 未获正确响应，请确认使用支持 VIA 的配套固件。");
        return response;
    }
    public async Task<ViaSnapshot> ReadAsync(CancellationToken token)
    {
        if(rows is <1 or >32 || columns is <1 or >32 || rows*columns>512)throw new IOException("布局矩阵尺寸无效。");
        byte[] version = await CommandAsync(1, token);
        int protocol = BinaryPrimitives.ReadUInt16BigEndian(version.AsSpan(1));
        if (protocol != 13) throw new IOException($"VIA 协议版本 {protocol} 与此客户端不兼容（需要 13）。");
        byte[] keyVersion = await CommandAsync(2, token, 6);
        if (BinaryPrimitives.ReadUInt32BigEndian(keyVersion.AsSpan(2)) != 9) throw new IOException("键码版本不匹配，请使用与固件配套的客户端。");
        var matrix = await DescribeMatrixAsync(token);
        UiSupported = matrix?.UiSupported ?? false;
        if (matrix is not null)
        {
            if (matrix.Rows != rows || matrix.Columns != columns)
                throw new IOException($"当前布局 {rows}×{columns} 与键盘矩阵 {matrix.Rows}×{matrix.Columns} 不一致，请加载对应布局。");
        }
        else if (rows != Rows || columns != Columns)
            throw new IOException("此固件不能验证矩阵尺寸，请升级 Qmknrf5 固件后再使用其他布局。");
        int layers = (await CommandAsync(0x11, token))[1];
        if (layers is < 1 or > 32) throw new IOException("无效的 VIA 层数。");
        byte[] keys = await ReadBufferAsync(0x12, layers * rows * columns * 2, token);
        ushort[] values = new ushort[keys.Length / 2];
        for (int i = 0; i < values.Length; i++) values[i] = BinaryPrimitives.ReadUInt16BigEndian(keys.AsSpan(i * 2));
        int count = (await CommandAsync(0x0C, token))[1];
        int size = BinaryPrimitives.ReadUInt16BigEndian((await CommandAsync(0x0D, token)).AsSpan(1));
        if (count > 32 || size < count || size > 8192) throw new IOException("无效的 VIA 宏容量。");
        return new(protocol, layers, rows, columns, values, count, await ReadBufferAsync(0x0E, size, token));
    }
    public async Task<ViaMatrixDescription?> DescribeMatrixAsync(CancellationToken token)
    {
        byte[] request = new byte[32]; request[0] = 0x81;
        byte[] response = await transport.ExchangeAsync(request, token);
        if (response.Length != 32) throw new IOException("矩阵信息响应长度错误。");
        if (response[0] == 0xFF) return null;
        if (response[0] != 0x81 || response[1] != 1 || response[2] is < 1 or > 32 ||
            response[3] is < 1 or > 32 || response[2] * response[3] > 512)
            throw new IOException("键盘返回的矩阵描述无效，请确认使用配套固件。");
        return new(response[2], response[3], (response[4] & 1) != 0);
    }
    private async Task<byte[]> ReadBufferAsync(byte command, int size, CancellationToken token)
    {
        byte[] result = new byte[size];
        for (int offset = 0; offset < size; offset += 28)
        {
            int length = Math.Min(28, size - offset);
            byte[] response = await CommandAsync(command, token, (byte)(offset >> 8), (byte)offset, (byte)length);
            if (response[1] != (byte)(offset >> 8) || response[2] != (byte)offset || response[3] != length) throw new IOException("VIA 返回了不匹配的缓冲区位置。");
            response.AsSpan(4, length).CopyTo(result.AsSpan(offset));
        }
        return result;
    }
    private async Task WriteBufferAsync(byte command, byte[] bytes, CancellationToken token)
    {
        for (int offset = 0; offset < bytes.Length; offset += 28)
        {
            int length = Math.Min(28, bytes.Length - offset);
            byte[] data = new byte[length + 3];
            data[0] = (byte)(offset >> 8); data[1] = (byte)offset; data[2] = (byte)length;
            bytes.AsSpan(offset, length).CopyTo(data.AsSpan(3));
            var response = await CommandAsync(command, token, data);
            if (!response.AsSpan(1, data.Length).SequenceEqual(data)) throw new IOException("VIA 写入确认不匹配，请重新读取键盘。");
        }
    }
    public async Task SaveAsync(ViaSnapshot desired, ViaSnapshot original, CancellationToken token)
    {
        Validate(desired);
        if (desired.Rows!=rows || desired.Columns!=columns || original.Rows!=rows || original.Columns!=columns || desired.Layers != original.Layers || desired.Macros.Length != original.Macros.Length || desired.MacroCount != original.MacroCount)
            throw new IOException("配置容量与当前键盘不一致。");
        // Change only edited keys, preserving unrelated fields and reducing BLE traffic.
        for (int i = 0; i < desired.Keys.Length; i++)
        {
            if (desired.Keys[i] == original.Keys[i]) continue;
            var response = await CommandAsync(5, token, (byte)(i / (rows * columns)), (byte)(i / columns % rows), (byte)(i % columns), (byte)(desired.Keys[i] >> 8), (byte)desired.Keys[i]);
            if (!response.AsSpan(1, 5).SequenceEqual(new byte[] { (byte)(i / (rows * columns)), (byte)(i / columns % rows), (byte)(i % columns), (byte)(desired.Keys[i] >> 8), (byte)desired.Keys[i] }))
                throw new IOException("键位写入确认不匹配。");
        }
        if (!desired.Macros.AsSpan().SequenceEqual(original.Macros))
        {
            // QMK's final byte is the validity marker. Leave aborted uploads inert.
            int last = desired.Macros.Length - 1;
            await CommandAsync(0x0F, token, (byte)(last >> 8), (byte)last, 1, 0xFF);
            await WriteBufferAsync(0x0F, desired.Macros, token);
        }
        var actual = await ReadAsync(token);
        if (!actual.Keys.AsSpan().SequenceEqual(desired.Keys) || !actual.Macros.AsSpan().SequenceEqual(desired.Macros))
            throw new IOException("键盘回读与目标配置不一致，请重新读取。");
        var watch = System.Diagnostics.Stopwatch.StartNew();
        while (watch.Elapsed < TimeSpan.FromSeconds(12))
        {
            var state = await CommandAsync(0x80, token);
            if (state[1] == 0)
            {
                if (BinaryPrimitives.ReadUInt32BigEndian(state.AsSpan(2)) != 0) throw new IOException("键位已生效，但 Flash 保存失败，请重新读取后重试。");
                return;
            }
            await Task.Delay(150, token);
        }
        throw new IOException("键位已生效，但等待 Flash 保存超时，请不要立即断电。");
    }
    public static void Validate(ViaSnapshot value)
    {
        if (value.Version != 13 || value.Rows is <1 or >32 || value.Columns is <1 or >32 || value.Rows*value.Columns>512 || value.Layers is < 1 or > 32 || value.Keys.Length != value.Layers * value.Rows * value.Columns || value.MacroCount is < 1 or > 32 || value.Macros.Length < value.MacroCount || value.Macros.Length > 8192 || value.Macros[^1] != 0)
            throw new IOException("配置文件的协议、矩阵或容量不正确。");
        ViaMacros.Split(value.Macros, value.MacroCount);
    }
    public static string Export(ViaSnapshot value) { Validate(value); return JsonSerializer.Serialize(value, new JsonSerializerOptions { WriteIndented = true }); }
    public static ViaSnapshot Import(string json)
    {
        if (json.Length > 500_000) throw new IOException("配置文件过大。");
        var value = JsonSerializer.Deserialize<ViaSnapshot>(json) ?? throw new IOException("空配置文件。");
        if (value.Keys is null || value.Macros is null) throw new IOException("配置缺少键位或宏。");
        Validate(value); return value;
    }
}
