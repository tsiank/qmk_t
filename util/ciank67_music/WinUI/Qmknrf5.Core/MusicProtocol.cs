namespace Qmknrf5.Core;

[Flags]
public enum LightTarget : byte { None = 0, Matrix = 1, Underglow = 2, Both = 3 }

public enum MusicEffect : byte { Spectrum = 0, DualDance = 1 }
public enum MusicColorMode : byte { Colorful = 0, Solid = 1 }
public readonly record struct MusicRgb(byte R, byte G, byte B);

public static class MusicProtocol
{
    public const int BandCount = 16;
    public const byte ReportId = 4;
    public const int VendorId = 0xFEED;
    public const int ProductId = 0xC475;
    public const int LegacyVendorId = 0x1915;
    public const int LegacyProductId = 0x520B;
    public const uint HidUsage = 0xFF670067;
    public static readonly Guid ServiceUuid = new("c1a00001-7d9b-4c7d-a568-7294e2166700");
    public static readonly Guid ReceiveUuid = new("c1a00002-7d9b-4c7d-a568-7294e2166700");

    public static bool IsSupportedUsbIdentity(int vendorId, int productId) =>
        (vendorId == VendorId && productId == ProductId) ||
        (vendorId == LegacyVendorId && productId == LegacyProductId);

    public static bool IsMusicUsbInterface(int vendorId, int productId, IEnumerable<uint> usages) =>
        usages.Contains(HidUsage);

    public static byte[] Packet(byte sequence, ReadOnlySpan<byte> bands, LightTarget target, MusicEffect effect = MusicEffect.Spectrum,
        MusicColorMode colorMode = MusicColorMode.Colorful)
    {
        if (bands.Length != BandCount) throw new ArgumentException("需要 16 个频谱强度值。", nameof(bands));
        if ((target & ~LightTarget.Both) != 0) throw new ArgumentOutOfRangeException(nameof(target));
        if (effect is not MusicEffect.Spectrum and not MusicEffect.DualDance) throw new ArgumentOutOfRangeException(nameof(effect));
        if (colorMode is not MusicColorMode.Colorful and not MusicColorMode.Solid) throw new ArgumentOutOfRangeException(nameof(colorMode));
        byte[] data = new byte[20];
        data[0] = 0xC7;
        data[1] = 1;
        data[2] = sequence;
        data[3] = target == LightTarget.None ? (byte)0 : (byte)((byte)target | ((byte)effect << 4) | ((byte)colorMode << 5));
        bands.CopyTo(data.AsSpan(4));
        return data;
    }

    public static byte[] UsbReport(ReadOnlySpan<byte> packet, int reportLength = 21)
    {
        if (packet.Length != 20 || reportLength < 21) throw new ArgumentException("音乐报告长度不正确。");
        byte[] data = new byte[reportLength];
        data[0] = ReportId;
        packet.CopyTo(data.AsSpan(1));
        return data;
    }

    public static byte[] ColorPacket(MusicRgb color)
    {
        byte[] data = new byte[20];
        data[0] = 0xC7; data[1] = 2;
        data[2] = color.R; data[3] = color.G; data[4] = color.B;
        return data;
    }
}
