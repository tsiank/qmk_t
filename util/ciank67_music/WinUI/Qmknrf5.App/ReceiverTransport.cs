// SPDX-License-Identifier: GPL-2.0-or-later
using HidSharp;
using Qmknrf5.Core;

namespace Qmknrf5.App;

// Share the same HID connection and request gate with VIA/receiver management.
// Only the proxy changes: music/control payloads remain the existing 20 bytes.
internal sealed class ReceiverTransport : IKeyboardControlTransport
{
    private readonly ViaUsb transport;
    private int sequence;
    public ReceiverTransport(string path)
    {
        var device=DeviceList.Local.GetHidDevices().FirstOrDefault(d=>d.DevicePath==path&&DeviceCatalog.IsReceiverInterface(d))
            ?? throw new IOException("2.4G 接收器已断开，请刷新设备。");
        transport=new ViaUsb(device);
    }
    public ValueTask SendAsync(ReadOnlyMemory<byte> packet,CancellationToken token)
    {
        byte[] request=ReceiverProtocol.WrapMusic(packet.Span);
        return new ValueTask(transport.SendAsync(request,token));
    }
    private byte NextSequence() => (byte)Interlocked.Increment(ref sequence);
    private async Task<KeyboardControlState> ExchangeAsync(byte[] command,CancellationToken token)
    {
        byte[] response=await transport.ExchangeAsync(ReceiverProtocol.WrapMusic(command),token);
        return ReceiverProtocol.ParseControl(response,command[2]);
    }
    public Task<KeyboardControlState> ControlAsync(byte mask,byte values,KeyboardMusicMode mode,CancellationToken token) =>
        ExchangeAsync(KeyboardControlProtocol.Command(NextSequence(),mask,values,mode),token);
    public async Task SyncClockAsync(DateTime localTime,CancellationToken token)
    {
        var state=await ExchangeAsync(KeyboardControlProtocol.ClockCommand(NextSequence(),localTime),token);
        if(!state.ClockSynchronized)throw new IOException("键盘未确认 2.4G 校时。");
    }
    public async Task<KeyboardControlState> SetScreenModeAsync(ScreenPowerMode mode,CancellationToken token)
    {
        var state=await ExchangeAsync(KeyboardControlProtocol.ScreenCommand(NextSequence(),mode),token);
        if(!state.SupportsScreen||state.ScreenMode!=mode)throw new IOException("键盘未确认屏幕设置。");
        return state;
    }
    public ValueTask DisposeAsync() => transport.DisposeAsync();
}
