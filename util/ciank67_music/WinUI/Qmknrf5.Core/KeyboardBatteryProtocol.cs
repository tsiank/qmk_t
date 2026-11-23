// SPDX-License-Identifier: GPL-2.0-or-later
using System.Buffers.Binary;
using System.Security.Cryptography;
namespace Qmknrf5.Core;

public sealed record KeyboardBatteryState(bool Supported, bool HasSample, bool UsbPowered, byte Percent, ushort Millivolts)
{
    public string Description => !Supported ? "此键盘未配置电池检测。" : !HasSample
        ? "电量：等待电池采样" + (UsbPowered ? " · USB 供电" : "")
        : $"电量：{Percent}% · {Millivolts / 1000.0:F2} V" + (UsbPowered ? " · USB 供电（电量为上次采样）" : "");
}
public static class KeyboardBatteryProtocol
{
    public static byte[] Request()
    {
        byte[] data = new byte[32]; data[0] = 0x86; data[1] = 1;
        RandomNumberGenerator.Fill(data.AsSpan(28));
        return data;
    }
    public static KeyboardBatteryState? Parse(ReadOnlySpan<byte> data, ReadOnlySpan<byte> request)
    {
        if (data.Length == 32 && data[0] == 0xFF) return null; // Older firmware.
        if (data.Length != 32 || request.Length != 32 || data[0] != 0x86 || data[1] != 1 ||
            !data[28..].SequenceEqual(request[28..]) || (data[2] & ~7) != 0)
            throw new IOException("电量响应格式或请求编号错误。");
        for (int i = 6; i < 28; i++) if (data[i] != 0) throw new IOException("电量响应保留字段错误。");
        bool supported = (data[2] & 1) != 0, valid = (data[2] & 2) != 0;
        ushort voltage = BinaryPrimitives.ReadUInt16BigEndian(data[4..6]);
        if (valid && (!supported || data[3] > 100 || voltage == 0 || voltage > 6000) || !valid && (data[3] != 0 || voltage != 0))
            throw new IOException("电量采样数据错误。");
        return new(supported, valid, (data[2] & 4) != 0, data[3], voltage);
    }
    public static async Task<KeyboardBatteryState?> ReadAsync(IViaTransport transport, CancellationToken token)
    {
        token.ThrowIfCancellationRequested();
        byte[] request = Request();
        return Parse(await transport.ExchangeAsync(request, token), request);
    }
}
