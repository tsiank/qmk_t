using System.Buffers.Binary;
using System.Diagnostics;

namespace Qmknrf5.Core;

public enum PcmEncoding { Integer, Float }

public static class PcmDecoder
{
    public static float[] Decode(ReadOnlySpan<byte> bytes, PcmEncoding encoding, int bits, int channels)
    {
        if (channels <= 0 || (encoding == PcmEncoding.Float ? bits != 32 : bits is not (16 or 24 or 32)))
            throw new ArgumentException("不支持的 WASAPI 音频格式。");
        int stride = bits / 8;
        if (bytes.Length % (stride * channels) != 0) throw new ArgumentException("音频帧不完整。");
        float[] result = new float[bytes.Length / stride];
        for (int i = 0; i < result.Length; ++i)
        {
            ReadOnlySpan<byte> sample = bytes.Slice(i * stride, stride);
            float value;
            if (encoding == PcmEncoding.Float) value = BitConverter.Int32BitsToSingle(BinaryPrimitives.ReadInt32LittleEndian(sample));
            else if (bits == 16) value = BinaryPrimitives.ReadInt16LittleEndian(sample) / 32768f;
            else if (bits == 24)
            {
                int integer = sample[0] | sample[1] << 8 | sample[2] << 16;
                if ((integer & 0x800000) != 0) integer |= unchecked((int)0xFF000000);
                value = integer / 8388608f;
            }
            else value = BinaryPrimitives.ReadInt32LittleEndian(sample) / 2147483648f;
            result[i] = float.IsFinite(value) ? Math.Clamp(value, -1, 1) : 0;
        }
        return result;
    }
}

public sealed class AudioBuffer(int channels, Func<long>? clock = null)
{
    private readonly object sync = new();
    private readonly float[] samples = new float[checked(SpectrumAnalyzer.Size * (channels > 0 ? channels : throw new ArgumentOutOfRangeException(nameof(channels))))];
    private readonly Func<long> timestamp = clock ?? Stopwatch.GetTimestamp;
    private int writePosition;
    private long lastUpdate;
    private bool hasSamples;

    public void Append(ReadOnlySpan<float> data)
    {
        if (data.Length % channels != 0) throw new ArgumentException("音频帧不完整。");
        if (data.IsEmpty) return;
        lock (sync)
        {
            if (data.Length >= samples.Length) data = data[^samples.Length..];
            foreach (float value in data)
            {
                samples[writePosition] = value;
                writePosition = (writePosition + 1) % samples.Length;
            }
            lastUpdate = timestamp();
            hasSamples = true;
        }
    }

    public float[] Snapshot()
    {
        float[] result = new float[samples.Length];
        lock (sync)
        {
            if (!hasSamples || (timestamp() - lastUpdate) / (double)Stopwatch.Frequency > 0.25) return result;
            samples.AsSpan(writePosition).CopyTo(result);
            samples.AsSpan(0, writePosition).CopyTo(result.AsSpan(samples.Length - writePosition));
        }
        return result;
    }
}
