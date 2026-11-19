using System.Numerics;

namespace Ciank67.Music.Core;

public sealed class SpectrumAnalyzer
{
    public const int Size = 2048;
    private readonly double[] window = new double[Size];
    private readonly int[][] bins = new int[MusicProtocol.BandCount][];
    private readonly double[] levels = new double[MusicProtocol.BandCount];
    private readonly double normalization;

    public SpectrumAnalyzer(int sampleRate)
    {
        if (sampleRate < 8000) throw new ArgumentOutOfRangeException(nameof(sampleRate));
        for (int i = 0; i < Size; ++i) window[i] = 0.5 - 0.5 * Math.Cos(2 * Math.PI * i / (Size - 1));
        normalization = 2 / window.Sum();
        double maximum = Math.Min(16000, sampleRate * 0.45);
        for (int band = 0; band < bins.Length; ++band)
        {
            double low = 40 * Math.Pow(maximum / 40, band / 16.0);
            double high = 40 * Math.Pow(maximum / 40, (band + 1) / 16.0);
            bins[band] = Enumerable.Range(0, Size / 2 + 1)
                .Where(i => i * (double)sampleRate / Size >= low && i * (double)sampleRate / Size < high).ToArray();
            if (bins[band].Length == 0)
                bins[band] = [Math.Clamp((int)Math.Round(Math.Sqrt(low * high) * Size / sampleRate), 0, Size / 2)];
        }
    }

    public byte[] Analyze(ReadOnlySpan<float> interleaved, int channels, double gainDb)
    {
        if (channels <= 0 || interleaved.Length % channels != 0) throw new ArgumentException("音频声道或采样长度不正确。");
        if (!double.IsFinite(gainDb)) throw new ArgumentOutOfRangeException(nameof(gainDb));
        int count = Math.Min(Size, interleaved.Length / channels);
        int offset = interleaved.Length - count * channels;
        double[] power = new double[Size / 2 + 1];
        Complex[] fft = new Complex[Size];
        for (int channel = 0; channel < channels; ++channel)
        {
            Array.Clear(fft);
            double mean = 0;
            for (int i = 0; i < count; ++i)
            {
                float value = interleaved[offset + i * channels + channel];
                if (!float.IsFinite(value)) throw new ArgumentException("音频数据包含无效值。");
                mean += value;
            }
            // Padding is silence, as in the original Python analyzer.
            mean /= Size;
            for (int i = 0; i < Size; ++i)
            {
                double value = i >= Size - count ? interleaved[offset + (i - (Size - count)) * channels + channel] : 0;
                fft[i] = new Complex((value - mean) * window[i], 0);
            }
            Transform(fft);
            for (int i = 0; i < power.Length; ++i)
                power[i] += (fft[i].Real * fft[i].Real + fft[i].Imaginary * fft[i].Imaginary) / channels;
        }
        byte[] result = new byte[levels.Length];
        for (int band = 0; band < result.Length; ++band)
        {
            double energy = Math.Sqrt(bins[band].Sum(i => power[i])) * normalization;
            double db = 20 * Math.Log10(Math.Max(energy, 1e-9)) + gainDb;
            double level = Math.Clamp((db + 60) / 54, 0, 1);
            levels[band] += (level > levels[band] ? 0.65 : 0.18) * (level - levels[band]);
            result[band] = (byte)Math.Clamp(Math.Round(levels[band] * 255), 0, 255);
        }
        return result;
    }

    private static void Transform(Complex[] values)
    {
        for (int i = 1, j = 0; i < values.Length; ++i)
        {
            int bit = values.Length >> 1;
            for (; (j & bit) != 0; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) (values[i], values[j]) = (values[j], values[i]);
        }
        for (int length = 2; length <= values.Length; length <<= 1)
        {
            Complex step = Complex.FromPolarCoordinates(1, -2 * Math.PI / length);
            for (int start = 0; start < values.Length; start += length)
            {
                Complex phase = Complex.One;
                for (int i = 0; i < length / 2; ++i)
                {
                    Complex even = values[start + i];
                    Complex odd = values[start + i + length / 2] * phase;
                    values[start + i] = even + odd;
                    values[start + i + length / 2] = even - odd;
                    phase *= step;
                }
            }
        }
    }
}
