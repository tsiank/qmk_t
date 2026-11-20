// SPDX-License-Identifier: GPL-2.0-or-later
namespace Ciank67.Music.Core;

public enum KeyboardMusicMode : byte { Off, LocalMusic, Midi, MidiTyping }
public sealed record KeyboardControlState(byte Flags, KeyboardMusicMode Mode)
{
    public bool Matrix => (Flags & 1) != 0;
    public bool Underglow => (Flags & 2) != 0;
    public bool Audio => (Flags & 4) != 0;
    public bool Clicky => (Flags & 8) != 0;
}
public static class KeyboardControlProtocol
{
    public static readonly Guid StateUuid = new("c1a00003-7d9b-4c7d-a568-7294e2166700");
    public static byte[] Command(byte sequence, byte mask = 0, byte values = 0, KeyboardMusicMode mode = KeyboardMusicMode.Off)
    {
        if ((mask & ~31) != 0 || (values & ~15) != 0 || (byte)mode > 3) throw new ArgumentOutOfRangeException(nameof(mask));
        if (mask == 0 && (values != 0 || mode != KeyboardMusicMode.Off)) throw new ArgumentException("查询不能修改开关");
        byte[] packet = new byte[20];
        packet[0] = 0xc7; packet[1] = mask == 0 ? (byte)4 : (byte)3;
        packet[2] = sequence; packet[3] = mask; packet[4] = values; packet[5] = (byte)mode;
        return packet;
    }
    public static KeyboardControlState? Parse(ReadOnlySpan<byte> packet, byte sequence)
    {
        if (packet.Length != 20 || packet[0] != 0xc7 || packet[1] != 0x83 || packet[2] != sequence ||
            (packet[3] & ~15) != 0 || packet[4] > 3 || packet[5] != 1) return null;
        for (int i = 6; i < 20; i++) if (packet[i] != 0) return null;
        return new(packet[3], (KeyboardMusicMode)packet[4]);
    }
}
