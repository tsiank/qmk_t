// SPDX-License-Identifier: GPL-2.0-or-later
using System.Buffers.Binary;
using System.Security.Cryptography;

namespace Qmknrf5.Core;

public enum WirelessMode : byte { Auto, Usb, Bluetooth, Radio, None }
public enum WirelessRole : byte { Keyboard = 1, Receiver = 2 }
public sealed record WirelessState(WirelessRole Role, WirelessMode Preferred, WirelessMode Current,
    bool Paired, byte Persistence, bool Connected, uint PairId, ulong DeviceId, uint StorageError,
    byte Version = 1, byte PairingState = 0, byte PairingSeconds = 0, uint Fingerprint = 0);

public sealed class WirelessClient(IViaTransport transport)
{
    private static int sequence = RandomNumberGenerator.GetInt32(1, 65536);
    private async Task<WirelessState> CommandAsync(byte[] request, CancellationToken token)
    {
        byte[] data = await transport.ExchangeAsync(request, token);
        if (data.Length != 32 || data[0] != 0x84 || data[1] is < 1 or > 2)
            throw new NotSupportedException("当前固件未支持三模配置，请刷入配套固件。无线配对需要两端使用 R3 或更新版本。");
        if (data[25] != request[30] || data[26] != request[31]) throw new IOException("无线配置响应序号不匹配，请重新连接。");
        if (data[2] != 0) throw new IOException(data[2] switch {
            2 => "请通过接收器本身的 USB 接口设置配对。",
            3 => "设备正忙或不支持此操作，请读取状态后重试。",
            _ => "键盘拒绝了无线配置请求。" });
        if (data[3] is < 1 or > 2 || data[4] > 4 || data[5] > 4 || data[6] > 1 || data[7] > 2 || data[8] > 1)
            throw new IOException("无线配置响应无效。");
        if (data[1] >= 2 && (data[27] > 8 || data[28] > 60)) throw new IOException("无线配对状态无效。");
        return new((WirelessRole)data[3], (WirelessMode)data[4], (WirelessMode)data[5], data[6] != 0,
            data[7], data[8] != 0, BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(9)),
            BinaryPrimitives.ReadUInt64BigEndian(data.AsSpan(13)), BinaryPrimitives.ReadUInt32BigEndian(data.AsSpan(21)),
            data[1], data[1] >= 2 ? data[27] : (byte)0, data[1] >= 2 ? data[28] : (byte)0,
            data[1] >= 2 ? (uint)(data[29] << 16 | data[30] << 8 | data[31]) : 0);
    }
    private static byte[] Request(byte operation)
    {
        byte[] packet = new byte[32]; packet[0] = 0x84; packet[1] = operation; packet[2] = 1;
        BinaryPrimitives.WriteUInt16BigEndian(packet.AsSpan(30), unchecked((ushort)Interlocked.Increment(ref sequence)));
        return packet;
    }
    public Task<WirelessState> ReadAsync(CancellationToken token) => CommandAsync(Request(0), token);
    private async Task<WirelessState> PairingCommandAsync(byte operation, CancellationToken token)
    {
        var state = await ReadAsync(token);
        if (state.Version < 2) throw new NotSupportedException("无线配对需要两端刷入 R3 或更新版本。");
        if (state.Role != WirelessRole.Receiver) throw new IOException("请选择 USB 接收器；键盘通过长按组合键进入配对。");
        return await CommandAsync(Request(operation), token);
    }
    public Task<WirelessState> StartPairingAsync(CancellationToken token) => PairingCommandAsync(4, token);
    public Task<WirelessState> ConfirmPairingAsync(CancellationToken token) => PairingCommandAsync(5, token);
    public Task<WirelessState> CancelPairingAsync(CancellationToken token) => PairingCommandAsync(6, token);
    // Dedicated receiver USB management only. It acknowledges before resetting.
    public Task<WirelessState> EnterDfuAsync(CancellationToken token) => PairingCommandAsync(7, token);
    private async Task<WirelessState> SavedAsync(CancellationToken token)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(token);
        timeout.CancelAfter(TimeSpan.FromSeconds(12));
        try
        {
            while (true)
            {
                var state = await ReadAsync(timeout.Token);
                if (state.Persistence == 1 && state.StorageError == 0) return state;
                if (state.Persistence == 2) throw new IOException($"键盘保存失败（{state.StorageError}）。");
                await Task.Delay(150, timeout.Token);
            }
        }
        catch (OperationCanceledException) when (!token.IsCancellationRequested)
        { throw new IOException("等待键盘保存超时；请保持 USB 连接，读取状态后重试。"); }
    }
    public async Task<WirelessState> SelectModeAsync(WirelessMode mode, CancellationToken token)
    {
        if (!Enum.IsDefined(mode)) throw new ArgumentOutOfRangeException(nameof(mode));
        byte[] request = Request(1); request[3] = (byte)mode;
        await CommandAsync(request, token);
        var state = await SavedAsync(token);
        await Task.Delay(350, token); // reply/save precedes the deferred host switch
        state = await ReadAsync(token);
        if (state.Preferred != mode) throw new IOException("键盘未保留所选输出模式。");
        return state;
    }
    public async Task<WirelessState> ClearAsync(CancellationToken token)
    {
        await CommandAsync(Request(3), token);
        var state = await SavedAsync(token);
        if (state.Paired) throw new IOException("键盘未清除配对，请重试。");
        return state;
    }
    private async Task<WirelessState> ProvisionAsync(byte[] key, uint pairId, CancellationToken token)
    {
        byte[] request = Request(2);
        try
        {
            key.CopyTo(request, 4); BinaryPrimitives.WriteUInt32BigEndian(request.AsSpan(20), pairId);
            await CommandAsync(request, token);
        }
        finally { CryptographicOperations.ZeroMemory(request); }
        var state = await SavedAsync(token);
        if (!state.Paired || state.PairId != pairId) throw new IOException("配对信息保存校验失败。");
        return state;
    }
    public static async Task<(WirelessState Keyboard, WirelessState Receiver)> PairAsync(
        WirelessClient keyboard, WirelessClient receiver, CancellationToken token)
    {
        var tx = await keyboard.ReadAsync(token); var rx = await receiver.ReadAsync(token);
        if (tx.Role != WirelessRole.Keyboard || rx.Role != WirelessRole.Receiver || tx.DeviceId == rx.DeviceId)
            throw new IOException("请分别选择发送键盘和接收器，不能选择同一块板。");
        byte[] key = RandomNumberGenerator.GetBytes(16);
        uint pairId = BinaryPrimitives.ReadUInt32LittleEndian(RandomNumberGenerator.GetBytes(4)) | 0x01010101u;
        bool receiverSaved = false;
        try
        {
            rx = await receiver.ProvisionAsync(key, pairId, token); receiverSaved = true;
            tx = await keyboard.ProvisionAsync(key, pairId, token);
            return (tx, rx);
        }
        catch (Exception error) when (receiverSaved)
        { throw new IOException("接收器已保存新配对，但发送键盘未完成。请保持两端 USB 连接并重新配对。", error); }
        finally { CryptographicOperations.ZeroMemory(key); }
    }
}
