using Qmknrf5.Core;
using NAudio.CoreAudioApi;
using NAudio.Wave;

namespace Qmknrf5.App;

internal sealed class LoopbackAudio : IAudioSource
{
    private readonly MMDevice device;
    private readonly WasapiLoopbackCapture capture;
    private readonly AudioBuffer buffer;
    private readonly PcmEncoding encoding;
    private Exception? fault;
    private volatile bool disposed;
    public int SampleRate { get; }
    public int Channels { get; }

    public LoopbackAudio(string id)
    {
        using var enumerator = new MMDeviceEnumerator();
        device = string.IsNullOrEmpty(id) ? enumerator.GetDefaultAudioEndpoint(DataFlow.Render, Role.Multimedia) : enumerator.GetDevice(id);
        try
        {
            capture = new WasapiLoopbackCapture(device);
            try
            {
                var format = capture.WaveFormat;
                SampleRate = format.SampleRate;
                Channels = format.Channels;
                buffer = new AudioBuffer(Channels);
                var kind = format.Encoding;
                if (format is WaveFormatExtensible extended)
                {
                    kind = extended.SubFormat == new Guid("00000003-0000-0010-8000-00aa00389b71") ? WaveFormatEncoding.IeeeFloat :
                        extended.SubFormat == new Guid("00000001-0000-0010-8000-00aa00389b71") ? WaveFormatEncoding.Pcm :
                        throw new IOException("播放设备使用了不支持的音频格式。");
                }
                encoding = kind == WaveFormatEncoding.IeeeFloat ? PcmEncoding.Float :
                    kind == WaveFormatEncoding.Pcm ? PcmEncoding.Integer : throw new IOException("播放设备使用了不支持的音频格式。");
                capture.DataAvailable += OnDataAvailable;
                capture.RecordingStopped += (_, args) =>
                {
                    if (!disposed) Interlocked.CompareExchange(ref fault, args.Exception ?? new IOException("音频采集已停止，请检查播放设备。"), null);
                };
                capture.StartRecording();
            }
            catch { capture.Dispose(); throw; }
        }
        catch { device.Dispose(); throw; }
    }

    private void OnDataAvailable(object? sender, WaveInEventArgs args)
    {
        if (disposed) return;
        try { buffer.Append(PcmDecoder.Decode(args.Buffer.AsSpan(0, args.BytesRecorded), encoding, capture.WaveFormat.BitsPerSample, Channels)); }
        catch (Exception error) { Interlocked.CompareExchange(ref fault, error, null); }
    }

    public float[] GetSamples()
    {
        if (Volatile.Read(ref fault) is { } error) throw new IOException("系统声音采集失败：" + error.Message, error);
        return buffer.Snapshot();
    }

    public void Dispose()
    {
        disposed = true;
        try { capture.StopRecording(); }
        finally { capture.Dispose(); device.Dispose(); }
    }
}
