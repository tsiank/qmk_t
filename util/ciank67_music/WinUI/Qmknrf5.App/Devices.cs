using Qmknrf5.Core;
using HidSharp;
using NAudio.CoreAudioApi;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Devices.Enumeration;
using Windows.Security.Cryptography;

namespace Qmknrf5.App;

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
                var candidates = DeviceList.Local.GetHidDevices().ToArray();
                var music = candidates.Where(IsMusicInterface).ToArray();
                if (candidates.Any(d => MusicProtocol.IsSupportedUsbIdentity(d.VendorID,d.ProductID)) && music.Length == 0)
                    throw new IOException("已找到键盘的 USB 设备，但未检测到音乐接口。请刷入配套的 Qmknrf5 固件并重新拔插 USB，再刷新设备。");
                return music.Select(device =>
                {
                    string name;
                    try { name = device.GetProductName() + " · " + device.GetSerialNumber(); }
                    catch (Exception) { name = "nRF5 键盘 · " + device.DevicePath; }
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
        return devices.OrderBy(item => item.Name).ToList();
    }

    internal static bool IsMusicInterface(HidDevice device)
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

internal sealed class UsbTransport : IKeyboardControlTransport
{
    private static readonly SharedDevicePool<UsbConnection> connections = new(
        path => new UsbConnection(path), value => value.DisposeAsync().GetAwaiter().GetResult());
    private readonly SharedDevicePool<UsbConnection>.Lease lease;
    public UsbTransport(string path) { lease = connections.Acquire(path); }
    public ValueTask SendAsync(ReadOnlyMemory<byte> packet, CancellationToken token) => lease.Value.SendAsync(packet,token);
    public Task<KeyboardControlState> ControlAsync(byte mask,byte values,KeyboardMusicMode mode,CancellationToken token) =>
        lease.Value.ControlAsync(mask,values,mode,token);
    public Task SyncClockAsync(DateTime localTime,CancellationToken token) => lease.Value.SyncClockAsync(localTime,token);
    public Task<KeyboardControlState> SetScreenModeAsync(ScreenPowerMode mode,CancellationToken token) => lease.Value.SetScreenModeAsync(mode,token);
    public ValueTask DisposeAsync() { lease.Dispose();return ValueTask.CompletedTask; }
}

internal sealed class UsbConnection : IKeyboardControlTransport
{
    private readonly HidStream stream;
    private readonly int reportLength;
    private readonly string path;
    private readonly UsbOutputFallback output;
    private readonly SemaphoreSlim writeGate = new(1, 1);
    private WindowsHidOutput? control;

    public UsbConnection(string path)
    {
        this.path = path;
        var device = DeviceList.Local.GetHidDevices()
            .FirstOrDefault(item => string.Equals(item.DevicePath,path,StringComparison.OrdinalIgnoreCase) && DeviceCatalog.IsMusicInterface(item))
            ?? throw new IOException("USB 键盘已断开，请刷新设备。");
        reportLength = device.GetMaxOutputReportLength();
        if (reportLength != 21)
            throw new IOException($"USB 音乐报告应为 21 字节，当前设备为 {reportLength} 字节。请重新拔插 USB 并刷新设备，确认使用音乐固件。");
        stream = device.Open();
        stream.WriteTimeout = 500;
        output = new UsbOutputFallback(
            (report, token) => new ValueTask(Task.Run(() => { token.ThrowIfCancellationRequested(); stream.Write(report); })),
            (report, token) => new ValueTask(Task.Run(() => { control ??= new WindowsHidOutput(this.path); control.Write(report, token); })));
    }

    public async ValueTask SendAsync(ReadOnlyMemory<byte> packet, CancellationToken cancellationToken)
    {
        await writeGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try { await SendUnlockedAsync(packet,cancellationToken).ConfigureAwait(false); }
        finally { writeGate.Release(); }
    }

    private async ValueTask SendUnlockedAsync(ReadOnlyMemory<byte> packet, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        byte[] report = MusicProtocol.UsbReport(packet.Span, reportLength);
        try
        {
            bool usedControl = output.UsesControl;
            await output.SendAsync(report, cancellationToken).ConfigureAwait(false);
            if (!usedControl && output.UsesControl) Program.Trace("USB output switched to SET_REPORT(Output)");
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested) { throw; }
        catch (Exception error)
        {
            string operation = packet.Span[1] is 3 or 4 or 5 or 6 ? "控制或查询键盘开关" : packet.Span[1] == 2 ? "设置纯光颜色" : (packet.Span[3] & 3) == 0 ? "停止律动" : "发送音乐频谱";
            Program.Trace($"USB {operation}: {error}");
            try { File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "usb-error.log"),
                $"{DateTimeOffset.Now:O} USB {operation}, report={reportLength}, control={output.UsesControl}{Environment.NewLine}{error}"); }
            catch (Exception) { }
            throw new IOException($"USB {operation}失败：{error.Message}", error);
        }
    }

    private byte controlSequence;
    // The reply completes the transaction. Do not interleave spectrum writes
    // between a settings request and its matching device acknowledgement.
    public async Task<KeyboardControlState> ControlAsync(byte mask, byte values, KeyboardMusicMode mode, CancellationToken token)
    {
        await writeGate.WaitAsync(token).ConfigureAwait(false);
        try { return await ControlCoreAsync(++controlSequence,mask,values,mode,null,token).ConfigureAwait(false); }
        finally { writeGate.Release(); }
    }
    public async Task SyncClockAsync(DateTime localTime,CancellationToken token)
    {
        await writeGate.WaitAsync(token).ConfigureAwait(false);
        try
        {
            var state=await ControlCoreAsync(++controlSequence,0,0,KeyboardMusicMode.Off,localTime,token).ConfigureAwait(false);
            if(!state.ClockSynchronized)throw new IOException("键盘未确认校时。");
        }
        finally { writeGate.Release(); }
    }
    public async Task<KeyboardControlState> SetScreenModeAsync(ScreenPowerMode mode,CancellationToken token)
    {
        await writeGate.WaitAsync(token).ConfigureAwait(false);
        try
        {
            var state=await ControlCoreAsync(++controlSequence,0,0,KeyboardMusicMode.Off,null,token,mode).ConfigureAwait(false);
            if(!state.SupportsScreen||state.ScreenMode!=mode)throw new IOException("键盘未确认屏幕设置。");
            return state;
        }
        finally { writeGate.Release(); }
    }
    private async Task<KeyboardControlState> ControlCoreAsync(byte sequence,byte mask, byte values, KeyboardMusicMode mode,DateTime? localTime,CancellationToken token,ScreenPowerMode? screenMode=null)
    {
        await SendUnlockedAsync(screenMode is { } display?KeyboardControlProtocol.ScreenCommand(sequence,display):localTime is { } now?KeyboardControlProtocol.ClockCommand(sequence,now):KeyboardControlProtocol.Command(sequence, mask, values, mode), token);
        return await Task.Run(() =>
        {
            stream.ReadTimeout = 100;
            var watch = System.Diagnostics.Stopwatch.StartNew();
            byte[] report = new byte[21];
            while (watch.Elapsed < TimeSpan.FromSeconds(3))
            {
                token.ThrowIfCancellationRequested();
                try
                {
                    int size = stream.Read(report, 0, report.Length);
                    if (size == 21 && report[0] == MusicProtocol.ReportId &&
                        KeyboardControlProtocol.Parse(report.AsSpan(1), sequence) is { } state) return state;
                }
                catch (TimeoutException) { }
            }
            throw new IOException("键盘未返回控制状态，请刷入 R19 或更新固件，再重新连接。");
        }, token);
    }

    public ValueTask DisposeAsync()
    { control?.Dispose(); stream.Dispose(); writeGate.Dispose(); return ValueTask.CompletedTask; }
}

internal sealed class BluetoothTransport : IKeyboardControlTransport
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
            device = await CancellableResourceOpen.OpenAsync(
                () => BluetoothLEDevice.FromIdAsync(id).AsTask(), value => value.Dispose(), token) ?? throw new IOException("无法打开蓝牙键盘，请先在 Windows 设置中配对。");
            session = await CancellableResourceOpen.OpenAsync(
                () => GattSession.FromDeviceIdAsync(device.BluetoothDeviceId).AsTask(), value => value.Dispose(), token)
                ?? throw new IOException("无法建立蓝牙会话，请重新连接键盘。");
            session.MaintainConnection = true;
            var services = await CancellableResourceOpen.OpenAsync(
                () => device.GetGattServicesForUuidAsync(MusicProtocol.ServiceUuid, BluetoothCacheMode.Uncached).AsTask(),
                result => { foreach (var lateService in result.Services) lateService.Dispose(); }, token)
                ?? throw new IOException("蓝牙服务发现未返回结果。");
            if (services.Status != GattCommunicationStatus.Success || services.Services.Count == 0)
                throw new IOException($"未找到蓝牙音乐服务（{services.Status}，ATT={services.ProtocolError}）。请拔掉键盘 USB 并确认蓝牙可打字；必要时删除旧配对后重新配对。");
            service = services.Services[0];
            foreach (var extra in services.Services.Skip(1)) extra.Dispose();
            var characteristics = await service.GetCharacteristicsForUuidAsync(MusicProtocol.ReceiveUuid, BluetoothCacheMode.Uncached).AsTask().WaitAsync(token);
            if (characteristics.Status != GattCommunicationStatus.Success || characteristics.Characteristics.Count == 0)
                throw new IOException($"无法读取蓝牙音乐特征（{characteristics.Status}，ATT={characteristics.ProtocolError}），请重新连接键盘。");
            var characteristic = characteristics.Characteristics[0];
            // Firmware uses SEC_JUST_WORKS. Establish encryption before sending
            // write-without-response frames, which cannot report ATT rejection.
            characteristic.ProtectionLevel = GattProtectionLevel.EncryptionRequired;
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

    private byte controlSequence;
    private GattCharacteristic? stateCharacteristic;
    public Task<KeyboardControlState> ControlAsync(byte mask,byte values,KeyboardMusicMode mode,CancellationToken token) =>
        ControlCoreAsync(mask,values,mode,null,token);
    public async Task SyncClockAsync(DateTime localTime,CancellationToken token)
    {
        var state=await ControlCoreAsync(0,0,KeyboardMusicMode.Off,localTime,token);
        if(!state.ClockSynchronized)throw new IOException("键盘未确认校时。");
    }
    public async Task<KeyboardControlState> SetScreenModeAsync(ScreenPowerMode mode,CancellationToken token)
    {
        var state=await ControlCoreAsync(0,0,KeyboardMusicMode.Off,null,token,mode);
        if(!state.SupportsScreen||state.ScreenMode!=mode)throw new IOException("键盘未确认屏幕设置。");
        return state;
    }
    private async Task<KeyboardControlState> ControlCoreAsync(byte mask,byte values,KeyboardMusicMode mode,DateTime? localTime,CancellationToken token,ScreenPowerMode? screenMode=null)
    {
        if (stateCharacteristic is null)
        {
            var result = await service.GetCharacteristicsForUuidAsync(KeyboardControlProtocol.StateUuid, BluetoothCacheMode.Uncached).AsTask().WaitAsync(token);
            if (result.Status != GattCommunicationStatus.Success || result.Characteristics.Count == 0)
                throw new IOException($"未找到键盘控制状态特征（{result.Status}，ATT={result.ProtocolError}）。请确认固件支持键盘控制，并删除旧蓝牙配对后重新配对。");
            stateCharacteristic = result.Characteristics[0];
            stateCharacteristic.ProtectionLevel = GattProtectionLevel.EncryptionRequired;
        }
        byte sequence = ++controlSequence;
        var command = CryptographicBuffer.CreateFromByteArray(screenMode is { } display?KeyboardControlProtocol.ScreenCommand(sequence,display):localTime is { } now?KeyboardControlProtocol.ClockCommand(sequence,now):KeyboardControlProtocol.Command(sequence, mask, values, mode));
        // Settings use an acknowledged write, unlike the high-rate music frames.
        // This exposes encryption/permission failures instead of silently losing
        // a command and waiting forever for its state acknowledgement.
        var written = await characteristic.WriteValueWithResultAsync(command, GattWriteOption.WriteWithResponse).AsTask().WaitAsync(token);
        if (written.Status != GattCommunicationStatus.Success)
            throw new IOException($"蓝牙控制写入失败：{written.Status}，ATT={written.ProtocolError}。请确认键盘已配对并处于蓝牙模式。");
        var watch = System.Diagnostics.Stopwatch.StartNew();
        while (watch.Elapsed < TimeSpan.FromSeconds(3))
        {
            var result = await stateCharacteristic.ReadValueAsync(BluetoothCacheMode.Uncached).AsTask().WaitAsync(token);
            if (result.Status != GattCommunicationStatus.Success) throw new IOException($"读取蓝牙键盘状态失败：{result.Status}，ATT={result.ProtocolError}。请确认已配对且连接的是支持 Qmknrf5 协议的键盘。");
            CryptographicBuffer.CopyToByteArray(result.Value, out byte[] packet);
            if (KeyboardControlProtocol.Parse(packet, sequence) is { } state) return state;
            await Task.Delay(40, token);
        }
        throw new IOException("键盘控制响应超时，请重新连接。");
    }

    public ValueTask DisposeAsync()
    {
        session.MaintainConnection = false;
        service.Dispose(); session.Dispose(); device.Dispose();
        return ValueTask.CompletedTask;
    }
}
