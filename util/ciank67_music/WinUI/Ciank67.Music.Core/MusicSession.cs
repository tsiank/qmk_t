using System.Diagnostics;

namespace Ciank67.Music.Core;

public interface IMusicTransport : IAsyncDisposable
{
    ValueTask SendAsync(ReadOnlyMemory<byte> packet, CancellationToken cancellationToken);
}

public interface IAudioSource : IDisposable
{
    int SampleRate { get; }
    int Channels { get; }
    float[] GetSamples();
}

public sealed record MusicSettings(int FramesPerSecond, double GainDb, LightTarget Target, bool Demo,
    MusicEffect Effect = MusicEffect.Spectrum, MusicColorMode ColorMode = MusicColorMode.Colorful,
    MusicRgb? SolidColor = null);

public static class MusicSession
{
    public static async Task RunAsync(IMusicTransport transport, IAudioSource? audio,
        Func<MusicSettings> settings, Action<byte[]> preview, CancellationToken cancellationToken)
    {
        var analyzer = audio is null ? null : new SpectrumAnalyzer(audio.SampleRate);
        Stopwatch watch = Stopwatch.StartNew();
        byte sequence = 0;
        MusicRgb? sentColor = null;
        try
        {
            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();
                double before = watch.Elapsed.TotalSeconds;
                MusicSettings current = settings();
                if (current.FramesPerSecond is < 1 or > 30) throw new ArgumentOutOfRangeException(nameof(settings));
                byte[] bands;
                if (current.Demo)
                    bands = Enumerable.Range(0, 16).Select(i => (byte)Math.Round(255 * (0.5 + 0.5 * Math.Sin(before * 3 - i * 0.45)))).ToArray();
                else if (analyzer is not null && audio is not null)
                    bands = analyzer.Analyze(audio.GetSamples(), audio.Channels, current.GainDb,
                        fastResponse: true);
                else throw new InvalidOperationException("尚未选择播放设备。");
                if (current.ColorMode == MusicColorMode.Solid && current.Target != LightTarget.None && current.SolidColor is { } color)
                {
                    if (sentColor != color)
                    {
                        await transport.SendAsync(MusicProtocol.ColorPacket(color), cancellationToken).ConfigureAwait(false);
                        sentColor = color;
                    }
                }
                else sentColor = null;
                await transport.SendAsync(MusicProtocol.Packet(sequence++, bands, current.Target, current.Effect, current.ColorMode), cancellationToken).ConfigureAwait(false);
                preview(bands);
                double remaining = 1.0 / current.FramesPerSecond - (watch.Elapsed.TotalSeconds - before);
                // Send only current frames. Never catch up with a burst of stale frames.
                if (remaining > 0) await Task.Delay(TimeSpan.FromSeconds(remaining), cancellationToken).ConfigureAwait(false);
            }
        }
        finally
        {
            using var stop = new CancellationTokenSource(TimeSpan.FromSeconds(2));
            try { await transport.SendAsync(MusicProtocol.Packet(sequence, new byte[16], LightTarget.None), stop.Token).ConfigureAwait(false); }
            catch (Exception) { /* Firmware also restores the base effect after 750 ms. */ }
        }
    }
}
