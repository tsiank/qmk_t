// SPDX-License-Identifier: GPL-2.0-or-later
namespace Ciank67.Music.Core;

public enum ScreenPowerMode : byte { Automatic, AlwaysOn, Off }
public enum KeyboardMusicMode : byte { Off, LocalMusic, Midi, MidiTyping }
public sealed record KeyboardControlState(byte Flags, KeyboardMusicMode Mode,byte Capabilities=31,byte SupportedModes=7,bool SupportsClock=false,bool ClockSynchronized=false,bool SupportsScreen=false,ScreenPowerMode ScreenMode=ScreenPowerMode.Automatic,bool ScreenOn=false)
{
    public bool Matrix => (Flags & 1) != 0;
    public bool Underglow => (Flags & 2) != 0;
    public bool Audio => (Flags & 4) != 0;
    public bool Clicky => (Flags & 8) != 0;
}
public interface IKeyboardControlTransport : IMusicTransport
{
    Task<KeyboardControlState> ControlAsync(byte mask,byte values,KeyboardMusicMode mode,CancellationToken token);
    Task SyncClockAsync(DateTime localTime,CancellationToken token) => throw new NotSupportedException("设备不支持校时。");
    Task<KeyboardControlState> SetScreenModeAsync(ScreenPowerMode mode,CancellationToken token) => throw new NotSupportedException("设备不支持屏幕控制。");
}
public static class KeyboardControlProtocol
{
    public static readonly Guid StateUuid = new("c1a00003-7d9b-4c7d-a568-7294e2166700");
    public static async Task<KeyboardControlState?> EnableMusicLightsAsync(IKeyboardControlTransport transport,LightTarget target,CancellationToken token,byte capabilities=31)
    {
        if((target&~LightTarget.Both)!=0)throw new ArgumentOutOfRangeException(nameof(target));
        token.ThrowIfCancellationRequested();
        if(target==LightTarget.None)return null;
        byte mask=(byte)((byte)target&capabilities&3);
        if(mask==0)throw new IOException("此键盘不支持所选律动灯组。");
        var state=await transport.ControlAsync(mask,mask,KeyboardMusicMode.Off,token);
        if((state.Flags&mask)!=mask)throw new IOException("所选律动灯组未能开启，请检查键盘灯光配置。");
        return state;
    }
    public static byte[] Command(byte sequence, byte mask = 0, byte values = 0, KeyboardMusicMode mode = KeyboardMusicMode.Off)
    {
        if ((mask & ~31) != 0 || (values & ~15) != 0 || (byte)mode > 3) throw new ArgumentOutOfRangeException(nameof(mask));
        if (mask == 0 && (values != 0 || mode != KeyboardMusicMode.Off)) throw new ArgumentException("查询不能修改开关");
        byte[] packet = new byte[20];
        packet[0] = 0xc7; packet[1] = mask == 0 ? (byte)4 : (byte)3;
        packet[2] = sequence; packet[3] = mask; packet[4] = values; packet[5] = (byte)mode;
        return packet;
    }
    public static byte[] ClockCommand(byte sequence,DateTime localTime)
    {
        byte[] packet=new byte[20];
        packet[0]=0xc7;packet[1]=5;packet[2]=sequence;
        packet[3]=(byte)localTime.Hour;packet[4]=(byte)localTime.Minute;packet[5]=(byte)localTime.Second;
        packet[6]=(byte)localTime.Millisecond;packet[7]=(byte)(localTime.Millisecond>>8);
        return packet;
    }
    public static byte[] ScreenCommand(byte sequence,ScreenPowerMode mode)
    {
        if(mode is not (ScreenPowerMode.Automatic or ScreenPowerMode.AlwaysOn or ScreenPowerMode.Off))throw new ArgumentOutOfRangeException(nameof(mode));
        byte[] packet=new byte[20];packet[0]=0xc7;packet[1]=6;packet[2]=sequence;packet[3]=(byte)mode;
        return packet;
    }
    public static KeyboardControlState? Parse(ReadOnlySpan<byte> packet, byte sequence)
    {
        if (packet.Length != 20 || packet[0] != 0xc7 || packet[1] != 0x83 || packet[2] != sequence ||
            (packet[3] & ~15) != 0 || packet[4] > 3 || packet[5] is not (1 or 2 or 3)) return null;
        byte capabilities=packet[5]>=2?packet[6]:(byte)31;
        byte modes=packet[5]>=2?packet[7]:(byte)7;
        if((capabilities&~31)!=0||(modes&~7)!=0||((modes!=0)!=((capabilities&16)!=0))||(packet[3]&~capabilities)!=0)return null;
        if(packet[4]>0&&(modes&(1<<(packet[4]-1)))==0)return null;
        bool extended=packet[5]==3;
        if(extended&&(packet[8]==0||(packet[8]&~3)!=0))return null;
        bool clock=extended&&(packet[8]&1)!=0,screen=extended&&(packet[8]&2)!=0;
        if(extended&&((clock?packet[9]>1:packet[9]!=0)||(screen?packet[10]>2||packet[11]>1:packet[10]!=0||packet[11]!=0)))return null;
        for (int i = extended?12:packet[5]==2?8:6; i < 20; i++) if (packet[i] != 0) return null;
        return new(packet[3], (KeyboardMusicMode)packet[4],capabilities,modes,clock,clock&&packet[9]==1,screen,
            screen?(ScreenPowerMode)packet[10]:ScreenPowerMode.Automatic,screen&&packet[11]==1);
    }
}
