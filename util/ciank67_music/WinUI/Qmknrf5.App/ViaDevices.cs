// SPDX-License-Identifier: GPL-2.0-or-later
using Qmknrf5.Core;
using HidSharp;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Security.Cryptography;

namespace Qmknrf5.App;

internal static class ViaDevices
{
    public static async Task<IViaTransport> OpenAsync(DeviceChoice choice, CancellationToken token)
    {
        if (choice.Kind == ConnectionKind.Bluetooth) return await ViaBluetooth.OpenAsync(choice.Id, token);
        if (choice.Kind is not (ConnectionKind.Usb or ConnectionKind.Receiver)) throw new IOException("请先选择 USB、蓝牙或 2.4G 接收器。");
        return await Task.Run<IViaTransport>(() =>
        {
            token.ThrowIfCancellationRequested();
            var all = DeviceList.Local.GetHidDevices().ToArray();
            var source = all.FirstOrDefault(d => d.DevicePath == choice.Id) ?? throw new IOException("键盘已断开，请刷新设备。");
            // The receiver proxies VIA through its shared management collection.
            if (choice.IsReceiver) {
                if (!DeviceCatalog.IsReceiverInterface(source)) throw new IOException("接收器控制接口已改变，请刷新设备。");
                return new ViaUsb(source);
            }
            var raw = all.Where(d => d.VendorID == source.VendorID && d.ProductID == source.ProductID).Where(d =>
            {
                try { return d.GetReportDescriptor().DeviceItems.SelectMany(i => i.Usages.GetAllValues()).Contains(0xFF600061u); }
                catch { return false; }
            }).ToArray();
            string serial;
            try { serial = source.GetSerialNumber(); } catch { serial = ""; }
            if (!string.IsNullOrEmpty(serial)) raw = raw.Where(d => { try { return d.GetSerialNumber() == serial; } catch { return false; } }).ToArray();
            if (raw.Length != 1) throw new IOException(raw.Length == 0 ? "未找到 VIA 接口，请刷入支持 VIA 的配套固件并重新插拔 USB。" : "发现多个同名键盘，无法确定改键目标，请暂时只连接一个键盘。");
            token.ThrowIfCancellationRequested();
            return new ViaUsb(raw[0]);
        }, token);
    }
}

internal sealed class ViaUsb : IViaTransport
{
    private static readonly SharedDevicePool<ViaUsbConnection> connections = new(
        path => new ViaUsbConnection(DeviceList.Local.GetHidDevices().FirstOrDefault(d => d.DevicePath == path)
            ?? throw new IOException("VIA USB 设备已断开。")),
        value => value.DisposeAsync().GetAwaiter().GetResult());
    private readonly SharedDevicePool<ViaUsbConnection>.Lease lease;
    public ViaUsb(HidDevice device) { lease = connections.Acquire(device.DevicePath); }
    public Task<byte[]> ExchangeAsync(byte[] request, CancellationToken token) => lease.Value.ExchangeAsync(request, token);
    public Task SendAsync(byte[] request, CancellationToken token) => lease.Value.SendAsync(request,token);
    public ValueTask DisposeAsync() { lease.Dispose(); return ValueTask.CompletedTask; }
}

internal sealed class ViaUsbConnection : IViaTransport
{
    private readonly HidStream stream;
    private readonly UsbOutputFallback output;
    private readonly SemaphoreSlim gate = new(1, 1);
    private readonly string path;
    private WindowsHidOutput? fallback;
    public ViaUsbConnection(HidDevice device)
    {
        if (device.GetMaxOutputReportLength() != 33 || device.GetMaxInputReportLength() != 33) throw new IOException("VIA USB 报告长度错误。");
        path = device.DevicePath; stream = device.Open(); stream.WriteTimeout = 500; stream.ReadTimeout = 100;
        output = new UsbOutputFallback(
            (bytes, ct) => new ValueTask(Task.Run(() => { ct.ThrowIfCancellationRequested(); stream.Write(bytes); })),
            (bytes, ct) => new ValueTask(Task.Run(() => { fallback ??= new WindowsHidOutput(path); fallback.Write(bytes, ct); })));
    }
    public async Task SendAsync(byte[] request, CancellationToken token)
    {
        if(request.Length!=32 || request[0]!=0xC7 || request[1] is not (1 or 2))throw new ArgumentException("无响应写入仅支持音乐帧和颜色设置。");
        await gate.WaitAsync(token);
        byte[] report=new byte[33];request.CopyTo(report,1);
        try { await output.SendAsync(report,token); }
        finally { Array.Clear(report);gate.Release(); }
    }
    public async Task<byte[]> ExchangeAsync(byte[] request, CancellationToken token)
    {
        if (request.Length != 32) throw new ArgumentException("VIA request must be 32 bytes.");
        await gate.WaitAsync(token);
        byte[] report = new byte[33]; request.CopyTo(report, 1);
        try
        {
            await output.SendAsync(report, token);
            return await Task.Run(() =>
            {
                var watch = System.Diagnostics.Stopwatch.StartNew(); byte[] data = new byte[33];
                while (watch.Elapsed < TimeSpan.FromSeconds(4))
                {
                    token.ThrowIfCancellationRequested();
                    try
                    {
                        int length = stream.Read(data);
                        if (length == 33 && data[0] == 0 && (data[1] == request[0] || data[1] == 0xFF)) {
                            if (request[0] == 0xC7 && data[1] == 0xC7 && data[3] != request[2]) continue;
                            if (data[1] == 0x84 && (data[26] != request[30] || data[27] != request[31])) continue;
                            return data[1..];
                        }
                    }
                    catch (TimeoutException) { }
                }
                throw new IOException("USB 配置响应超时，请刷新设备后重试；尚未完成的写入可能已部分生效。");
            }, token);
        }
        finally { Array.Clear(report); gate.Release(); }
    }
    public ValueTask DisposeAsync() { fallback?.Dispose(); stream.Dispose(); gate.Dispose(); return ValueTask.CompletedTask; }
}

internal sealed class ViaBluetooth : IViaTransport
{
    private readonly BluetoothLEDevice device;
    private readonly GattSession session;
    private readonly GattDeviceService service;
    private readonly GattCharacteristic rx, state;
    private readonly SemaphoreSlim gate = new(1, 1);
    private ushort sequence = (ushort)System.Security.Cryptography.RandomNumberGenerator.GetInt32(1, ushort.MaxValue);
    private ViaBluetooth(BluetoothLEDevice device, GattSession session, GattDeviceService service, GattCharacteristic rx, GattCharacteristic state)
    { this.device = device; this.session = session; this.service = service; this.rx = rx; this.state = state; }
    public static async Task<ViaBluetooth> OpenAsync(string id, CancellationToken token)
    {
        BluetoothLEDevice? device = null; GattSession? session = null; GattDeviceService? service = null;
        try
        {
            device = await CancellableResourceOpen.OpenAsync(() => BluetoothLEDevice.FromIdAsync(id).AsTask(), x => x.Dispose(), token) ?? throw new IOException("无法打开蓝牙键盘。");
            session = await CancellableResourceOpen.OpenAsync(() => GattSession.FromDeviceIdAsync(device.BluetoothDeviceId).AsTask(), x => x.Dispose(), token) ?? throw new IOException("无法打开蓝牙会话。");
            session.MaintainConnection = true;
            var services = await CancellableResourceOpen.OpenAsync(() => device.GetGattServicesForUuidAsync(ViaClient.ServiceUuid, BluetoothCacheMode.Uncached).AsTask(), x => { foreach (var s in x.Services) s.Dispose(); }, token) ?? throw new IOException("VIA 服务发现失败。");
            if (services.Status != GattCommunicationStatus.Success || services.Services.Count == 0)
            {
                foreach (var unused in services.Services) unused.Dispose();
                throw new IOException($"未找到蓝牙 VIA 服务（{services.Status}）。请确认 R21 或更新固件；Windows 缓存旧服务时需删除旧配对后重新配对。");
            }
            service = services.Services[0]; foreach (var extra in services.Services.Skip(1)) extra.Dispose();
            async Task<GattCharacteristic> Find(Guid uuid)
            {
                var result = await service.GetCharacteristicsForUuidAsync(uuid, BluetoothCacheMode.Uncached).AsTask().WaitAsync(token);
                if (result.Status != GattCommunicationStatus.Success || result.Characteristics.Count != 1) throw new IOException($"VIA 特征发现失败：{result.Status}");
                var c = result.Characteristics[0]; c.ProtectionLevel = GattProtectionLevel.EncryptionRequired; return c;
            }
            var rx = await Find(ViaClient.ReceiveUuid); var state = await Find(ViaClient.StateUuid);
            return new(device, session, service, rx, state);
        }
        catch { service?.Dispose(); if (session is not null) { session.MaintainConnection = false; session.Dispose(); } device?.Dispose(); throw; }
    }
    public async Task<byte[]> ExchangeAsync(byte[] request, CancellationToken token)
    {
        if (request.Length != 32) throw new ArgumentException("VIA request must be 32 bytes.");
        await gate.WaitAsync(token);
        try
        {
            using var deadline = CancellationTokenSource.CreateLinkedTokenSource(token); deadline.CancelAfter(TimeSpan.FromSeconds(6));
            var ct = deadline.Token;
            if (++sequence == 0) ++sequence;
            for (int offset = 0; offset < 32; offset += 16)
            {
                byte[] frame = new byte[19]; frame[0] = (byte)sequence; frame[1] = (byte)(sequence >> 8); frame[2] = (byte)offset;
                request.AsSpan(offset, 16).CopyTo(frame.AsSpan(3));
                var written = await rx.WriteValueWithResultAsync(CryptographicBuffer.CreateFromByteArray(frame), GattWriteOption.WriteWithResponse).AsTask().WaitAsync(ct);
                if (written.Status != GattCommunicationStatus.Success) throw new IOException($"VIA 蓝牙写入失败：{written.Status}，ATT={written.ProtocolError}");
            }
            while (true)
            {
                ct.ThrowIfCancellationRequested();
                var result = await state.ReadValueAsync(BluetoothCacheMode.Uncached).AsTask().WaitAsync(ct);
                if (result.Status != GattCommunicationStatus.Success) throw new IOException($"VIA 蓝牙回读失败：{result.Status}，ATT={result.ProtocolError}");
                CryptographicBuffer.CopyToByteArray(result.Value, out byte[] bytes);
                if (bytes.Length == 34 && (bytes[0] | bytes[1] << 8) == sequence) return bytes[2..];
                await Task.Delay(20, ct);
            }
        }
        finally { gate.Release(); }
    }
    public ValueTask DisposeAsync()
    { session.MaintainConnection = false; service.Dispose(); session.Dispose(); device.Dispose(); gate.Dispose(); return ValueTask.CompletedTask; }
}
