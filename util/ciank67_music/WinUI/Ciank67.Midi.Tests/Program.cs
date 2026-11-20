// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
using Ciank67.Music.App;
using Windows.Devices.Midi;
using Windows.Storage.Streams;

static void Assert(bool value, string message)
{
    if (!value) throw new Exception(message);
}
int passed = 0;
void Test(string name, Action action)
{
    action(); Console.WriteLine($"PASS {name}"); passed++;
}
Test("Forward NoteOn/Off without changing channel, pitch or velocity", () =>
{
    var output = new FakeOutput(); using var router = new MidiRouter(null,output,_ => throw new Exception());
    router.Forward(new MidiNoteOnMessage(2,69,101)); router.Forward(new MidiNoteOffMessage(2,69,20));
    Assert(output.Messages[0] is MidiNoteOnMessage { Channel:2, Note:69, Velocity:101 },"Changed note on");
    Assert(output.Messages[1] is MidiNoteOffMessage { Channel:2, Note:69, Velocity:20 },"Changed note off");
    Assert(router.Snapshot().Count==2,"Wrong receive count");
});
Test("Forward sustain, program changes and MIDI transport", () =>
{
    var output = new FakeOutput(); using var router = new MidiRouter(null,output,_ => throw new Exception());
    router.Forward(new MidiControlChangeMessage(0,64,127));
    router.Forward(new MidiProgramChangeMessage(0,40)); router.Forward(new MidiStopMessage());
    Assert(output.Messages[0] is MidiControlChangeMessage { Controller:64, ControlValue:127 },"Changed pedal");
    Assert(output.Messages[1] is MidiProgramChangeMessage { Program:40 } && output.Messages[2] is MidiStopMessage,"Lost transport");
});
Test("Velocity zero is displayed as release and forwarded unchanged", () =>
{
    var output = new FakeOutput(); using var router = new MidiRouter(null,output,_ => throw new Exception());
    router.Forward(new MidiNoteOnMessage(0,60,0));
    Assert(router.Snapshot().Latest.Contains("松开") && output.Messages[0] is MidiNoteOnMessage { Velocity:0 },"Incorrect zero velocity");
});
Test("Panic releases sustain and sends both stop commands on all 16 channels", () =>
{
    var output = new FakeOutput(); using var router = new MidiRouter(null,output,_ => throw new Exception());
    router.AllNotesOff(); Assert(output.Messages.Count==48,"Wrong panic length");
    for(byte channel=0;channel<16;channel++)
    {
        var messages = output.Messages.Skip(channel*3).Take(3).Cast<MidiControlChangeMessage>().ToArray();
        Assert(messages.All(m=>m.Channel==channel && m.ControlValue==0),"Wrong channel/value");
        Assert(messages.Select(m=>m.Controller).SequenceEqual(new byte[]{64,120,123}),"Wrong stop order");
    }
});
Test("Dispose stops notes, closes output once and rejects late callbacks", () =>
{
    var output = new FakeOutput(); var router = new MidiRouter(null,output,_=>throw new Exception());
    router.Dispose(); router.Dispose(); router.Forward(new MidiNoteOnMessage(0,60,100));
    Assert(output.DisposeCount==1 && output.Messages.Count==48,"Late callback or double dispose");
});
Test("Instrument changes update melodic channels and preserve drum channel", () =>
{
    var output=new FakeOutput(); using var router=new MidiRouter(null,output,_=>throw new Exception());
    router.ChangeProgram(73);
    var changes=output.Messages.Cast<MidiProgramChangeMessage>().ToArray();
    Assert(changes.Length==15 && changes.All(m=>m.Channel!=9 && m.Program==73),"Incorrect program mapping");
});
Test("Send error is surfaced and panic keeps trying other channels", () =>
{
    var output=new FakeOutput{Fail=true}; int errors=0;
    var router=new MidiRouter(null,output,_=>errors++);
    router.Forward(new MidiNoteOnMessage(0,60,100));Assert(errors==1,"Lost send failure");
    router.Dispose();Assert(output.Attempts==49 && output.DisposeCount==1,"Panic stopped after error");
});
Test("Concurrent callbacks and disposal serialize port access", () =>
{
    var output=new FakeOutput();var router=new MidiRouter(null,output,_=>throw new Exception());
    Parallel.For(0,100,i=>router.Forward(new MidiNoteOnMessage(0,(byte)(40+i%40),100)));
    Assert(router.Snapshot().Count==100,"Lost concurrent messages");
    router.Dispose();Assert(output.Messages.Count==148,"Unserialized cleanup");
});
Test("Cancelled opening does not acquire a MIDI port", () =>
{
    // Selection is validated by the UI; the factory handles cancellation before
    // touching Windows ports, including a previously unplugged device ID.
    using var cancellation=new CancellationTokenSource();cancellation.Cancel();
    try { MidiRouter.OpenAsync("missing","",0,_=>{},cancellation.Token).GetAwaiter().GetResult(); }
    catch(OperationCanceledException) { return; }
    throw new Exception("Expected cancellation");
});
Test("Windows built-in synthesizer opens and accepts program/stop commands", () =>
{
    var synth = MidiSynthesizer.CreateAsync().AsTask().GetAwaiter().GetResult();
    Assert(synth is not null,"Built-in synth unavailable");
    using var router = new MidiRouter(null,synth!,_=>throw new Exception());
    router.ChangeProgram(0); router.AllNotesOff();
});
Console.WriteLine($"{passed} MIDI tests passed.");

sealed class FakeOutput : IMidiOutPort
{
    public string DeviceId => "fake-synth";
    public List<IMidiMessage> Messages { get; } = new();
    public int DisposeCount, Attempts;
    public bool Fail;
    public void SendMessage(IMidiMessage message)
    {
        Attempts++;
        if(Fail)throw new IOException("Unplugged");
        if(DisposeCount!=0)throw new Exception("Write after dispose");
        Messages.Add(message);
    }
    public void SendBuffer(IBuffer buffer) => throw new NotSupportedException();
    public void Dispose() => DisposeCount++;
}
