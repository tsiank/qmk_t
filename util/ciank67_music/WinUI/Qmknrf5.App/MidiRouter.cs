// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
using Qmknrf5.Core;
using Windows.Devices.Midi;

namespace Qmknrf5.App;

internal sealed record MidiPortChoice(string Id, string Name)
{
    public override string ToString() => Name;
}

// Owns the MIDI ports independently of the lighting connection. Both USB MIDI
// and BLE MIDI are enumerated by Windows.Devices.Midi, not by HID product name.
internal sealed class MidiRouter : IDisposable
{
    private readonly object gate = new();
    private readonly MidiInPort? input;
    private readonly IMidiOutPort output;
    private readonly Action<Exception> failed;
    private bool disposed;
    private bool softPlaying;
    private long count;
    private string latest = "等待键盘音符…";

    internal MidiRouter(MidiInPort? input, IMidiOutPort output, Action<Exception> failed,bool softPlaying=false)
    {
        this.input = input; this.output = output; this.failed = failed; this.softPlaying=softPlaying;
        if(softPlaying)SetAmbience(output,true);
        if (input is not null) input.MessageReceived += Received;
    }

    public static async Task<MidiRouter> OpenAsync(string inputId, string outputId, byte program,
        Action<Exception> failed, CancellationToken token,bool softPlaying=false)
    {
        MidiInPort? input = null;
        IMidiOutPort? output = null;
        try
        {
            token.ThrowIfCancellationRequested();
            input = await CancellableResourceOpen.OpenAsync(
                () => MidiInPort.FromIdAsync(inputId).AsTask(), value => value.Dispose(), token);
            token.ThrowIfCancellationRequested();
            if (input is null) throw new IOException("无法打开 MIDI 输入，请关闭 MIDIberry 等占用端口的软件，再刷新设备。");
            output = await CancellableResourceOpen.OpenAsync<IMidiOutPort>(async () =>
                outputId.Length == 0 ? await MidiSynthesizer.CreateAsync() : await MidiOutPort.FromIdAsync(outputId),
                value => value.Dispose(), token);
            token.ThrowIfCancellationRequested();
            if (output is null) throw new IOException("无法打开 MIDI 合成器，请检查输出设备后重试。");
            SetProgram(output, program);
            return new MidiRouter(input, output, failed,softPlaying);
        }
        catch
        {
            input?.Dispose(); output?.Dispose(); throw;
        }
    }

    public static async Task<MidiRouter> OpenOutputAsync(string outputId,Action<Exception> failed,CancellationToken token)
    {
        token.ThrowIfCancellationRequested();
        var port=await CancellableResourceOpen.OpenAsync<IMidiOutPort>(async()=>
            outputId.Length==0?await MidiSynthesizer.CreateAsync():await MidiOutPort.FromIdAsync(outputId),p=>p.Dispose(),token);
        if(port is null)throw new IOException("无法打开 MIDI 合成器。");
        return new MidiRouter(null,port,failed);
    }
    private static void SetAmbience(IMidiOutPort output,bool soft)
    {
        for(byte channel=0;channel<16;channel++)if(channel!=9)
        {
            output.SendMessage(new MidiControlChangeMessage(channel,91,soft?(byte)28:(byte)0));
            output.SendMessage(new MidiControlChangeMessage(channel,93,soft?(byte)4:(byte)0));
        }
    }
    public void SetSoftPlaying(bool enabled)
    {
        lock(gate){if(disposed)return;SetAmbience(output,enabled);softPlaying=enabled;}
    }
    private static void SetProgram(IMidiOutPort output, byte program)
    {
        for (byte channel = 0; channel < 16; channel++)
            if (channel != 9) output.SendMessage(new MidiProgramChangeMessage(channel, program));
    }

    public void ChangeProgram(byte program)
    {
        lock (gate)
        {
            if (!disposed) SetProgram(output, program);
        }
    }

    public (long Count, string Latest) Snapshot()
    {
        lock (gate) return (count, latest);
    }

    private void Received(MidiInPort sender, MidiMessageReceivedEventArgs args) => Forward(args.Message);

    internal void Forward(IMidiMessage message)
    {
        Exception? error = null;
        lock (gate)
        {
            if (disposed) return;
            try
            {
                if(softPlaying&&message is MidiNoteOnMessage inputNote&&inputNote.Velocity>0&&inputNote.Channel!=9)
                {
                    int velocity=(inputNote.Velocity*88+63)/127;
                    if(inputNote.Note<60)velocity=(velocity*72+50)/100;
                    message=new MidiNoteOnMessage(inputNote.Channel,inputNote.Note,(byte)Math.Max(1,velocity));
                }
                output.SendMessage(message);
                count++;
                latest = message switch
                {
                    MidiNoteOnMessage n when n.Velocity != 0 => $"按下  音符 {n.Note} · 力度 {n.Velocity} · 通道 {n.Channel + 1}",
                    MidiNoteOnMessage n => $"松开  音符 {n.Note} · 通道 {n.Channel + 1}",
                    MidiNoteOffMessage n => $"松开  音符 {n.Note} · 通道 {n.Channel + 1}",
                    MidiControlChangeMessage c => $"控制  CC {c.Controller} = {c.ControlValue} · 通道 {c.Channel + 1}",
                    _ => message.Type.ToString()
                };
            }
            catch (Exception ex) { error = ex; }
        }
        // The UI stops/disposes outside this callback's lock.
        if (error is not null) failed(error);
    }

    public void AllNotesOff()
    {
        lock (gate) { if (!disposed) Panic(output); }
    }

    private static void Panic(IMidiOutPort output)
    {
        for (byte channel = 0; channel < 16; channel++)
        {
            // Continue other channels if one driver call fails during unplug.
            try { output.SendMessage(new MidiControlChangeMessage(channel, 64, 0)); } catch { }
            try { output.SendMessage(new MidiControlChangeMessage(channel, 120, 0)); } catch { }
            try { output.SendMessage(new MidiControlChangeMessage(channel, 123, 0)); } catch { }
        }
    }

    public void Dispose()
    {
        lock (gate)
        {
            if (disposed) return;
            disposed = true;
            if (input is not null) input.MessageReceived -= Received;
            Panic(output);
            try { input?.Dispose(); } catch (Exception) { }
            try { output.Dispose(); } catch (Exception) { }
        }
    }
}
