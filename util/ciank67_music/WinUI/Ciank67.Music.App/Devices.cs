using Ciank67.Music.Core;
using HidSharp;
using NAudio.CoreAudioApi;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Devices.Enumeration;
using Windows.Security.Cryptography;

namespace Ciank67.Music.App;

internal enum ConnectionKind { Usb, Bluetooth, Preview }
internal sealed record DeviceChoice(ConnectionKind Kind, string Id, string Name)
{
    public override string ToString() => Name;
}
internal sealed record PlaybackChoice(string Id, string Name)
{
    public override string ToString() => Name;
}

internal static class DeviceCatalog
{
    public static async Task<List<DeviceChoice>> GetKeyboardsAsync(ConnectionKind kind, CancellationToken token)
    {
        if (kind == ConnectionKind.Preview) return [new(kind, "", "本机频谱预览")];
        if (kind == ConnectionKind.Usb)
            return await Task.Run(() =>
            {
                var candidates = DeviceList.Local.GetHidDevices()
                    .Where(device => MusicProtocol.IsSupportedUsbIdentity(device.VendorID, device.ProductID)).ToArray();
                var music = candidates.Where(IsMusicInterface).ToArray();
                if (candidates.Length > 0 && music.Length == 0)
                    throw new IOException("已找到键盘的 USB 设备，但未检测到音乐接口。请刷入 R7 音乐固件并重新拔插 USB，再刷新设备。");
                return music.Select(device =>
                {
                    string name;
                    try { name = device.GetProductName() + " · " + device.GetSerialNumber(); }
                    catch (Exception) { name = "ciank67 · " + device.DevicePath; }
                    return new DeviceChoice(kind, device.DevicePath, name);
                }).ToList();
            }, token);
        List<DeviceChoice> devices = [];
        var information = await DeviceInformation.FindAllAsync(BluetoothLEDevice.GetDeviceSelectorFromPairingState(true)).AsTask(token);
        foreach (var item in information)
        {
            token.ThrowIfCancellationRequested();
            using var device = await BluetoothLEDevice.FromIdAsync(item.Id).AsTask(token);
            if (device is null) continue;
            string hex = device.BluetoothAddress.ToString("X12");
            string address = string.Join(":", Enumerable.Range(0, 6).Select(i => hex.Substring(i * 2, 2)));
            devices.Add(new DeviceChoice(kind, item.Id, $"{item.Name} · {address}"));
        }
        return devices;
    }

    private static bool IsMusicInterface(HidDevice device)
    {
        try { return MusicProtocol.IsMusicUsbInterface(device.VendorID, device.ProductID,
            device.GetReportDescriptor().DeviceItems.SelectMany(item => item.Usages.GetAllValues())); }
        catch (Exception) { return false; }
    }

    public static List<PlaybackChoice> GetPlaybackDevices()
    {
        using var enumerator = new MMDeviceEnumerator();
        List<PlaybackChoice> devices = [new("", "系统默认播放设备")];
        foreach (var device in enumerator.EnumerateAudioEndPoints(DataFlow.Render, DeviceState.Active))
        {
            using (device) devices.Add(new PlaybackChoice(device.ID, device.FriendlyName));
        }
        return devices;
    }

    public static async Task<IMusicTransport> ConnectAsync(DeviceChoice choice, CancellationToken token)
    {
        if (choice.Kind == ConnectionKind.Preview) return new PreviewTransport();
        if (choice.Kind == ConnectionKind.Usb) return await Task.Run<IMusicTransport>(() => new UsbTransport(choice.Id), token);
        return await BluetoothTransport.ConnectAsync(choice.Id, token);
    }
}

internal sealed class PreviewTransport : IMusicTransport
{
    public ValueTask SendAsync(ReadOnlyMemory<byte> packet, CancellationToken cancellationToken)
    { cancellationToken.ThrowIfCancellationRequested(); return ValueTask.CompletedTask; }
    public ValueTask DisposeAsync() => ValueTask.CompletedTask;
}

internal sealed class UsbTransport : IMusicTransport
{
    private readonly HidStream stream;
    private readonly int reportLength;

    public UsbTransport(string path)
    {
        var device = DeviceList.Local.GetHidDevices()
            .FirstOrDefault(item => MusicProtocol.IsSupportedUsbIdentity(item.VendorID, item.ProductID) && item.DevicePath == path)
            ?? throw new IOException("USB 键盘已断开，请刷新设备。");
        reportLength = device.GetMaxOutputReportLength();
        if (reportLength is < 21 or > 64) throw new IOException("HID 音乐报告长度不正确，请检查固件版本。");
        stream = device.Open();
        stream.WriteTimeout = 500;
    }

    public async ValueTask SendAsync(ReadOnlyMemory<byte> packet, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        byte[] report = MusicProtocol.UsbReport(packet.Span, reportLength);
        // Do not overlap a cancelled HID write with the subsequent stop report.
        // Synchronous writes have a bounded timeout and run off the UI thread.
        await Task.Run(() => stream.Write(report)).ConfigureAwait(false);
    }

    public ValueTask DisposeAsync() { stream.Dispose(); return ValueTask.CompletedTask; }
}

internal sealed class BluetoothTransport : IMusicTransport
{
    private readonly BluetoothLEDevice device;
    private readonly GattDeviceService service;
    private readonly GattSession session;
    private readonly GattCharacteristic characteristic;

    private BluetoothTransport(BluetoothLEDevice device, GattDeviceService service, GattSession session, GattCharacteristic characteristic)
    { this.device = device; this.service = service; this.session = session; this.characteristic = characteristic; }

    public static async Task<BluetoothTransport> ConnectAsync(string id, CancellationToken token)
    {
        BluetoothLEDevice? device = null;
        GattDeviceService? service = null;
        GattSession? session = null;
        try
        {
            device = await BluetoothLEDevice.FromIdAsync(id).AsTask(token) ?? throw new IOException("无法打开蓝牙键盘，请先在 Windows 设置中配对。");
            session = await GattSession.FromDeviceIdAsync(device.BluetoothDeviceId).AsTask(token);
            session.MaintainConnection = true;
            var services = await device.GetGattServicesForUuidAsync(MusicProtocol.ServiceUuid, BluetoothCacheMode.Uncached).AsTask(token);
            if (services.Status != GattCommunicationStatus.Success || services.Services.Count == 0)
                throw new IOException("未找到音乐服务。请确认键盘已刷入 R6 固件、已切换到蓝牙模式，并在必要时移除后重新配对。");
            service = services.Services[0];
            foreach (var extra in services.Services.Skip(1)) extra.Dispose();
            var characteristics = await service.GetCharacteristicsForUuidAsync(MusicProtocol.ReceiveUuid, BluetoothCacheMode.Uncached).AsTask(token);
            if (characteristics.Status != GattCommunicationStatus.Success || characteristics.Characteristics.Count == 0)
                throw new IOException("无法读取音乐特征，请重新连接键盘。");
            var characteristic = characteristics.Characteristics[0];
            if (!characteristic.CharacteristicProperties.HasFlag(GattCharacteristicProperties.WriteWithoutResponse))
                throw new IOException("所选设备不支持音乐数据写入。");
            return new BluetoothTransport(device, service, session, characteristic);
        }
        catch
        {
            session?.Dispose(); service?.Dispose(); device?.Dispose();
            throw;
        }
    }

    public async ValueTask SendAsync(ReadOnlyMemory<byte> packet, CancellationToken cancellationToken)
    {
        var buffer = CryptographicBuffer.CreateFromByteArray(packet.ToArray());
        var result = await characteristic.WriteValueWithResultAsync(buffer, GattWriteOption.WriteWithoutResponse).AsTask(cancellationToken).ConfigureAwait(false);
        if (result.Status != GattCommunicationStatus.Success)
            throw new IOException($"蓝牙发送失败：{result.Status}。请检查键盘连接。");
    }

    public ValueTask DisposeAsync()
    {
        session.MaintainConnection = false;
        service.Dispose(); session.Dispose(); device.Dispose();
        return ValueTask.CompletedTask;
    }
}
