using System.Buffers.Binary;
using System.Diagnostics;
using Ciank67.Music.Core;

int passed = 0;
void Check(bool value, string message) { if (!value) throw new Exception(message); }
void Test(string name, Action action) { action(); Console.WriteLine("PASS " + name); passed++; }
void Throws<T>(Action action) where T : Exception
{
    try { action(); } catch (T) { return; }
    throw new Exception("Expected " + typeof(T).Name);
}
float[] Tone(double frequency) => Enumerable.Range(0, SpectrumAnalyzer.Size)
    .Select(i => (float)(0.5 * Math.Sin(2 * Math.PI * frequency * i / 48000))).ToArray();
int Peak(byte[] values) => Array.IndexOf(values, values.Max());

Test("Wire packet matches firmware and Python layout", () =>
{
    byte[] bands = Enumerable.Range(0, 16).Select(i => (byte)i).ToArray();
    byte[] packet = MusicProtocol.Packet(255, bands, LightTarget.Both);
    Check(packet.SequenceEqual(new byte[] { 0xC7, 1, 255, 3 }.Concat(bands)), "Packet mismatch");
    byte[] usb = MusicProtocol.UsbReport(packet);
    Check(usb.Length == 21 && usb[0] == 4 && usb.AsSpan(1).SequenceEqual(packet), "Report ID mismatch");
    byte[] padded = MusicProtocol.UsbReport(packet, 64);
    Check(padded.AsSpan(21).ToArray().All(i => i == 0), "Padding must be zero");
    Check(MusicProtocol.Packet(0, bands, LightTarget.None)[3] == 0, "Stop flags");
    Throws<ArgumentException>(() => MusicProtocol.Packet(0, bands.AsSpan(1), LightTarget.Both));
    Throws<ArgumentOutOfRangeException>(() => MusicProtocol.Packet(0, bands, (LightTarget)4));
    Throws<ArgumentException>(() => MusicProtocol.UsbReport(packet, 20));
});
Test("USB discovery accepts current and legacy identities only on music collections", () =>
{
    Check(MusicProtocol.IsMusicUsbInterface(0xFEED, 0xC475, [0xFF670067]), "Current identity rejected");
    Check(MusicProtocol.IsMusicUsbInterface(0x1915, 0x520B, [0xFF670067]), "Legacy identity rejected");
    Check(!MusicProtocol.IsMusicUsbInterface(0x1915, 0x520B, [0x00010006]), "Ordinary keyboard selected");
    Check(!MusicProtocol.IsMusicUsbInterface(0x1915, 0x520B, [0x00010002]), "Mouse selected");
    Check(!MusicProtocol.IsMusicUsbInterface(0x1915, 0x9999, [0xFF670067]), "Unrelated Nordic device selected");
});
Test("Silence and DC are black", () =>
{
    foreach (float[] audio in new[] { new float[2048], Enumerable.Repeat(0.5f, 2048).ToArray() })
        Check(new SpectrumAnalyzer(48000).Analyze(audio, 1, 0).All(i => i == 0), "DC or silence lit LEDs");
});
Test("Low and high tones select different spectrum bands", () =>
{
    byte[] low = new SpectrumAnalyzer(48000).Analyze(Tone(100), 1, 0);
    byte[] high = new SpectrumAnalyzer(48000).Analyze(Tone(6000), 1, 0);
    Check(Peak(low) < 5 && Peak(high) > 10 && low.Max() > 100 && high.Max() > 100, "Frequency mapping");
});
Test("Opposite-phase stereo does not cancel", () =>
{
    float[] mono = Tone(440);
    float[] stereo = mono.SelectMany(value => new[] { value, -value }).ToArray();
    Check(new SpectrumAnalyzer(48000).Analyze(mono, 1, 0).SequenceEqual(
        new SpectrumAnalyzer(48000).Analyze(stereo, 2, 0)), "Stereo cancellation");
});
Test("Release decays to silence; invalid audio is rejected", () =>
{
    var analyzer = new SpectrumAnalyzer(48000);
    byte previous = analyzer.Analyze(Tone(440), 1, 0).Max();
    for (int i = 0; i < 40; ++i)
    {
        byte current = analyzer.Analyze(new float[2048], 1, 0).Max();
        Check(current <= previous, "Release increased"); previous = current;
    }
    Check(previous == 0, "Release did not finish");
    Throws<ArgumentException>(() => analyzer.Analyze([float.NaN], 1, 0));
    Throws<ArgumentException>(() => analyzer.Analyze([0, 0, 0], 2, 0));
    Throws<ArgumentOutOfRangeException>(() => analyzer.Analyze([], 1, double.NaN));
    Check(analyzer.Analyze(new float[20], 1, 0).Length == 16, "Short buffer");
});
Test("WASAPI PCM16/24/32 and float conversion", () =>
{
    byte[] pcm16 = new byte[4];
    BinaryPrimitives.WriteInt16LittleEndian(pcm16.AsSpan(0), short.MinValue);
    BinaryPrimitives.WriteInt16LittleEndian(pcm16.AsSpan(2), short.MaxValue);
    var values = PcmDecoder.Decode(pcm16, PcmEncoding.Integer, 16, 1);
    Check(values[0] == -1 && values[1] > 0.999, "PCM16 signedness");
    values = PcmDecoder.Decode([0, 0, 128, 255, 255, 127], PcmEncoding.Integer, 24, 1);
    Check(values[0] == -1 && values[1] > 0.999, "PCM24 sign extension");
    values = PcmDecoder.Decode(BitConverter.GetBytes(int.MinValue), PcmEncoding.Integer, 32, 1);
    Check(values[0] == -1, "PCM32 signedness");
    values = PcmDecoder.Decode(BitConverter.GetBytes(0.25f), PcmEncoding.Float, 32, 1);
    Check(values[0] == 0.25f, "Float decoding");
    Check(PcmDecoder.Decode(BitConverter.GetBytes(float.NaN), PcmEncoding.Float, 32, 1)[0] == 0, "Nonfinite audio sanitization");
    Throws<ArgumentException>(() => PcmDecoder.Decode([0, 0, 0], PcmEncoding.Integer, 16, 2));
    Throws<ArgumentException>(() => PcmDecoder.Decode([], PcmEncoding.Integer, 8, 1));
});
Test("Capture buffer keeps the latest full window and expires stale audio", () =>
{
    long now = 0;
    var buffer = new AudioBuffer(2, () => now);
    Check(buffer.Snapshot().All(i => i == 0), "Uninitialized buffer");
    float[] data = Enumerable.Range(0, 5000).Select(i => (float)i).ToArray();
    buffer.Append(data);
    Check(buffer.Snapshot().SequenceEqual(data[^4096..]), "Latest window mismatch");
    buffer.Append([100, 101]);
    Check(buffer.Snapshot()[^2..].SequenceEqual(new float[] { 100, 101 }), "Circular write order");
    now += Stopwatch.Frequency / 2;
    buffer.Append([]); // Empty WASAPI callbacks must not keep old sound alive.
    Check(buffer.Snapshot().All(i => i == 0), "Stale stream not silent");
    Throws<ArgumentException>(() => buffer.Append([1]));
});

foreach (bool failWrite in new[] { false, true })
{
    using var cancel = new CancellationTokenSource();
    var transport = new FakeTransport(cancel, failWrite);
    try
    {
        await MusicSession.RunAsync(transport, null, () => new MusicSettings(25, 0, LightTarget.Both, true), _ => { }, cancel.Token);
        throw new Exception("Session should have stopped");
    }
    catch (OperationCanceledException) when (!failWrite) { }
    catch (IOException) when (failWrite) { }
    Check(transport.Packets.Count == 2 && transport.Packets[0][3] == 3 && transport.Packets[1][3] == 0,
        "Session must send exactly one stop after cancellation or write failure");
    Console.WriteLine("PASS Session restores lighting after " + (failWrite ? "write failure" : "cancellation")); passed++;
}
{
    using var cancel = new CancellationTokenSource();
    var transport = new FakeTransport(cancel, false);
    try
    {
        await MusicSession.RunAsync(transport, new FailedAudio(), () => new MusicSettings(25, 0, LightTarget.Both, false), _ => { }, cancel.Token);
        throw new Exception("Expected capture failure");
    }
    catch (IOException) { }
    Check(transport.Packets.Count == 1 && transport.Packets[0][3] == 0, "Capture failure must still send stop");
    Console.WriteLine("PASS Audio capture failure restores lighting"); passed++;
}
Console.WriteLine($"{passed} tests passed.");

sealed class FakeTransport(CancellationTokenSource cancel, bool failWrite) : IMusicTransport
{
    public List<byte[]> Packets { get; } = [];
    public ValueTask SendAsync(ReadOnlyMemory<byte> packet, CancellationToken token)
    {
        Packets.Add(packet.ToArray());
        if (packet.Span[3] != 0)
        {
            if (failWrite) throw new IOException("unplugged");
            cancel.Cancel();
        }
        return ValueTask.CompletedTask;
    }
    public ValueTask DisposeAsync() => ValueTask.CompletedTask;
}
sealed class FailedAudio : IAudioSource
{
    public int SampleRate => 48000;
    public int Channels => 2;
    public float[] GetSamples() => throw new IOException("output device removed");
    public void Dispose() { }
}
