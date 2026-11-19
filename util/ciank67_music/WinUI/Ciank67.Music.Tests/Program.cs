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
    byte[] rays = MusicProtocol.Packet(7, bands, LightTarget.Both, MusicEffect.DualDance);
    Check(rays.SequenceEqual(new byte[] { 0xC7, 1, 7, 0x13 }.Concat(bands)), "Effect field mismatch");
    Check(MusicProtocol.Packet(0, bands, LightTarget.None, MusicEffect.DualDance)[3] == 0, "Effect must not override stop");
    Check(MusicProtocol.Packet(7, bands, LightTarget.Both, colorMode: MusicColorMode.Solid)[3] == 0x23, "Solid spectrum flag");
    Check(MusicProtocol.Packet(7, bands, LightTarget.Both, MusicEffect.DualDance, MusicColorMode.Solid)[3] == 0x33, "Solid dance flag");
    Check(MusicProtocol.Packet(0, bands, LightTarget.None, MusicEffect.DualDance, MusicColorMode.Solid)[3] == 0, "Solid stop must be zero");
    Throws<ArgumentOutOfRangeException>(() => MusicProtocol.Packet(0, bands, LightTarget.Both, colorMode: (MusicColorMode)2));
    Throws<ArgumentOutOfRangeException>(() => MusicProtocol.Packet(0, bands, LightTarget.Both, (MusicEffect)2));
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
Test("Custom RGB command keeps the 20-byte BLE and HID format", () =>
{
    byte[] packet = MusicProtocol.ColorPacket(new MusicRgb(255, 128, 64));
    Check(packet.Take(5).SequenceEqual(new byte[] {0xC7, 2, 255, 128, 64}) && packet.Skip(5).All(b => b == 0), "RGB layout");
    Check(packet.Length == 20 && MusicProtocol.UsbReport(packet).Length == 21, "RGB packet size");
    Check(MusicProtocol.ColorPacket(new MusicRgb(0, 0, 0))[1] == 2, "Black is a valid selected color");
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
Test("Fast spectrum catches one-frame beats and clears short gaps", () =>
{
    var fast = new SpectrumAnalyzer(48000);
    var original = new SpectrumAnalyzer(48000);
    byte fastAttack = fast.Analyze(Tone(440), 1, 0, fastResponse: true).Max();
    byte originalAttack = original.Analyze(Tone(440), 1, 0).Max();
    Check(fastAttack > 200 && fastAttack > originalAttack, "Short beat was flattened");
    byte fastTail = 0, originalTail = 0;
    for (int frame = 0; frame < 3; ++frame)
    {
        fastTail = fast.Analyze(new float[2048], 1, 0, fastResponse: true).Max();
        originalTail = original.Analyze(new float[2048], 1, 0).Max();
    }
    Check(fastTail < 15 && originalTail > 60, "Spectrum still carries a long tail");
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
{
    using var cancel = new CancellationTokenSource();
    var transport = new SwitchingTransport(cancel);
    try
    {
        await MusicSession.RunAsync(transport, null,
            () => new MusicSettings(30, 0, LightTarget.Both, true,
                transport.Packets.Count == 1 ? MusicEffect.DualDance : MusicEffect.Spectrum),
            _ => { }, cancel.Token);
        throw new Exception("Session should have stopped");
    }
    catch (OperationCanceledException) { }
    Check(transport.Packets.Select(p => p[3]).SequenceEqual(new byte[] {3, 0x13, 3, 0}),
        "Running session must switch effects without reconnecting and send a plain stop");
    Check(transport.Packets.All(p => p.Length == 20), "Effect must not increase BLE payload size");
    Console.WriteLine("PASS Live effect selection switches both ways without reconnecting"); passed++;
}
foreach (MusicEffect selected in new[] { MusicEffect.Spectrum, MusicEffect.DualDance })
{
    using var cancel = new CancellationTokenSource();
    var transport = new SwitchingTransport(cancel);
    try
    {
        await MusicSession.RunAsync(transport, null,
            () => new MusicSettings(30, 0, LightTarget.Both, true, selected,
                transport.Packets.Count == 1 ? MusicColorMode.Solid : MusicColorMode.Colorful),
            _ => { }, cancel.Token);
    }
    catch (OperationCanceledException) { }
    byte flags = (byte)(3 | ((byte)selected << 4));
    Check(transport.Packets.Select(p => p[3]).SequenceEqual(new byte[] {flags, (byte)(flags | 0x20), flags, 0}),
        "Running session must switch color both ways and send a plain stop");
    Console.WriteLine($"PASS Live color selection for {selected}"); passed++;
}
foreach (MusicEffect effect in new[] { MusicEffect.Spectrum, MusicEffect.DualDance })
{
    using var cancel = new CancellationTokenSource();
    var transport = new FakeTransport(cancel, false);
    try
    {
        await MusicSession.RunAsync(transport, new ToneAudio(),
            () => new MusicSettings(25, 0, LightTarget.Both, false, effect), _ => { }, cancel.Token);
    }
    catch (OperationCanceledException) { }
    byte peak = transport.Packets[0].Skip(4).Max();
    Check(peak > 200, "Both music effects must respond to the first audio frame");
    Check(transport.Packets[^1][3] == 0, "Audio test must restore lighting");
    Console.WriteLine($"PASS Audio session selects the right response for {effect}"); passed++;
}
{
    using var cancel = new CancellationTokenSource();
    var transport = new ColorTransport(cancel);
    try
    {
        await MusicSession.RunAsync(transport, null,
            () => new MusicSettings(30, 0, LightTarget.Both, true, MusicEffect.DualDance,
                transport.Frames < 3 ? MusicColorMode.Solid : MusicColorMode.Colorful,
                transport.Frames < 2 ? new MusicRgb(255, 0, 0) : new MusicRgb(0, 128, 255)), _ => { }, cancel.Token);
    }
    catch (OperationCanceledException) { }
    Check(transport.Packets.Select(p => p[1]).SequenceEqual(new byte[] {2, 1, 1, 2, 1, 1, 1}),
        "RGB must be sent before audio, only on change, and not for colorful mode");
    Check(transport.Packets[0].AsSpan(2, 3).SequenceEqual(new byte[] {255, 0, 0}) &&
        transport.Packets[3].AsSpan(2, 3).SequenceEqual(new byte[] {0, 128, 255}), "Live RGB selection");
    Check(transport.Packets[^1][3] == 0, "Stop after selected color");
    Console.WriteLine("PASS Live RGB selection is ordered and does not resend unchanged colors"); passed++;
}
{
    using var cancel = new CancellationTokenSource();
    var transport = new ColorTransport(cancel, true);
    try
    {
        await MusicSession.RunAsync(transport, null,
            () => new MusicSettings(25, 0, LightTarget.Both, true, MusicEffect.Spectrum, MusicColorMode.Solid,
                new MusicRgb(0, 0, 0)), _ => { }, cancel.Token);
        throw new Exception("Expected RGB write failure");
    }
    catch (IOException) { }
    Check(transport.Packets.Count == 2 && transport.Packets[^1][1] == 1 && transport.Packets[^1][3] == 0,
        "RGB write failure must still send the restore command");
    Console.WriteLine("PASS RGB write failure restores lighting"); passed++;
}
{
    int writes = 0, controls = 0;
    var output = new UsbOutputFallback((report, token) => { writes++; return ValueTask.CompletedTask; },
        (report, token) => { controls++; return ValueTask.CompletedTask; });
    await output.SendAsync(MusicProtocol.UsbReport(MusicProtocol.Packet(0, new byte[16], LightTarget.Both)), CancellationToken.None);
    Check(writes == 1 && controls == 0 && !output.UsesControl, "Working interrupt path must stay unchanged");
    Console.WriteLine("PASS USB keeps the working interrupt output path"); passed++;
}
{
    int writes = 0;
    List<byte[]> controls = [];
    var output = new UsbOutputFallback((report, token) => { writes++; throw new TimeoutException("Operation timed out 500"); },
        (report, token) => { controls.Add(report.ToArray()); return ValueTask.CompletedTask; });
    var reports = new[] {
        MusicProtocol.UsbReport(MusicProtocol.ColorPacket(new MusicRgb(255, 128, 64))),
        MusicProtocol.UsbReport(MusicProtocol.Packet(1, new byte[16], LightTarget.Both, MusicEffect.DualDance, MusicColorMode.Solid)),
        MusicProtocol.UsbReport(MusicProtocol.Packet(2, new byte[16], LightTarget.None)) };
    foreach (byte[] report in reports) await output.SendAsync(report, CancellationToken.None);
    Check(writes == 1 && output.UsesControl && controls.Count == 3, "Timed out path must switch once and keep control for stop");
    Check(controls.Zip(reports).All(pair => pair.First.SequenceEqual(pair.Second)), "Fallback must preserve report ID, RGB, audio and stop");
    Console.WriteLine("PASS USB timeout retries the same report and keeps the control path"); passed++;
}
{
    using var cancel = new CancellationTokenSource(); int controls = 0;
    var output = new UsbOutputFallback((report, token) => { cancel.Cancel(); throw new TimeoutException(); },
        (report, token) => { controls++; return ValueTask.CompletedTask; });
    try { await output.SendAsync(new byte[21], cancel.Token); throw new Exception("Expected cancellation"); }
    catch (OperationCanceledException) { }
    Check(controls == 0, "Cancellation must not start another HID write");
    Console.WriteLine("PASS USB cancellation does not start the fallback write"); passed++;
}
{
    var output = new UsbOutputFallback((report, token) => throw new TimeoutException(),
        (report, token) => throw new IOException("control failed"));
    try { await output.SendAsync(new byte[21], CancellationToken.None); throw new Exception("Expected both-path failure"); }
    catch (IOException error) { Check(error.InnerException is AggregateException, "Keep both failure causes"); }
    Console.WriteLine("PASS USB reports both output failures"); passed++;
}
if (OperatingSystem.IsWindows())
{
    Throws<IOException>(() => { using var output = new Ciank67.Music.App.WindowsHidOutput(@"\\?\ciank67-nonexistent-test-device"); });
    string path = Path.GetTempFileName();
    try
    {
        using var output = new Ciank67.Music.App.WindowsHidOutput(path);
        Throws<IOException>(() => output.Write(new byte[21], CancellationToken.None));
        using var cancel = new CancellationTokenSource(); cancel.Cancel();
        Throws<OperationCanceledException>(() => output.Write(new byte[21], cancel.Token));
    }
    finally { File.Delete(path); }
    Console.WriteLine("PASS Windows control-output native binding reports an invalid path cleanly"); passed++;
}
Console.WriteLine($"{passed} tests passed.");

sealed class ColorTransport(CancellationTokenSource cancel, bool failColor = false) : IMusicTransport
{
    public List<byte[]> Packets { get; } = [];
    public int Frames { get; private set; }
    public ValueTask SendAsync(ReadOnlyMemory<byte> packet, CancellationToken token)
    {
        Packets.Add(packet.ToArray());
        if (packet.Span[1] == 2 && failColor) throw new IOException("color write failed");
        if (packet.Span[1] == 1 && packet.Span[3] != 0 && ++Frames == 4) cancel.Cancel();
        return ValueTask.CompletedTask;
    }
    public ValueTask DisposeAsync() => ValueTask.CompletedTask;
}

sealed class ToneAudio : IAudioSource
{
    public int SampleRate => 48000;
    public int Channels => 1;
    public float[] GetSamples() => Enumerable.Range(0, 2048)
        .Select(i => (float)(0.5 * Math.Sin(2 * Math.PI * 440 * i / 48000))).ToArray();
    public void Dispose() { }
}

sealed class SwitchingTransport(CancellationTokenSource cancel) : IMusicTransport
{
    public List<byte[]> Packets { get; } = [];
    public ValueTask SendAsync(ReadOnlyMemory<byte> packet, CancellationToken token)
    {
        Packets.Add(packet.ToArray());
        if (Packets.Count == 3) cancel.Cancel();
        return ValueTask.CompletedTask;
    }
    public ValueTask DisposeAsync() => ValueTask.CompletedTask;
}

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
