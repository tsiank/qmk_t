// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
using Windows.Devices.Enumeration;
using Windows.Devices.Midi;

try
{
    string command = args.Length > 0 ? args[0].ToLowerInvariant() : "list";
    if (command is not ("list" or "play" or "monitor"))
        throw new ArgumentException("Usage: Ciank67.MidiCheck list | play <output-index> | monitor <input-index>");
    var outputs = await DeviceInformation.FindAllAsync(MidiOutPort.GetDeviceSelector());
    var inputs = await DeviceInformation.FindAllAsync(MidiInPort.GetDeviceSelector());
    Console.WriteLine("MIDI outputs (PC -> keyboard/synth):");
    for (int i = 0; i < outputs.Count; i++) Console.WriteLine($"  {i}: {outputs[i].Name} [{outputs[i].Id}]");
    Console.WriteLine("MIDI inputs (keyboard -> PC):");
    for (int i = 0; i < inputs.Count; i++) Console.WriteLine($"  {i}: {inputs[i].Name} [{inputs[i].Id}]");
    if (command == "list") return 0;
    if (args.Length != 2 || !int.TryParse(args[1], out int index) || index < 0 ||
        index >= (command == "play" ? outputs.Count : inputs.Count))
        throw new ArgumentException("Select an index from the corresponding list. No device was opened.");
    using var cancel = new CancellationTokenSource();
    Console.CancelKeyPress += (_, e) => { e.Cancel = true; cancel.Cancel(); };
    if (command == "play")
    {
        using var port = await MidiOutPort.FromIdAsync(outputs[index].Id)
            ?? throw new IOException("Cannot open output. Close other MIDI apps and reconnect the keyboard.");
        Console.WriteLine($"Playing C5 E5 G5 A4 on {outputs[index].Name}. Ctrl+C stops playback.");
        try
        {
            foreach (byte note in new byte[] { 72, 76, 79, 69 })
            {
                cancel.Token.ThrowIfCancellationRequested();
                port.SendMessage(new MidiNoteOnMessage(0, note, 96));
                await Task.Delay(350, cancel.Token);
                port.SendMessage(new MidiNoteOffMessage(0, note, 0));
                await Task.Delay(100, cancel.Token);
            }
        }
        finally
        {
            port.SendMessage(new MidiControlChangeMessage(0, 64, 0));
            port.SendMessage(new MidiControlChangeMessage(0, 120, 0));
            port.SendMessage(new MidiControlChangeMessage(0, 123, 0));
            await Task.Delay(150); // let BLE transmit the final stop before closing
        }
    }
    else
    {
        using var port = await MidiInPort.FromIdAsync(inputs[index].Id)
            ?? throw new IOException("Cannot open input. Close other MIDI apps and reconnect the keyboard.");
        Console.WriteLine($"Listening to {inputs[index].Name}. Enable MI_ON on the keyboard; Ctrl+C exits.");
        port.MessageReceived += (_, e) =>
        {
            var m = e.Message;
            Console.WriteLine(m switch
            {
                MidiNoteOnMessage n => $"NoteOn ch={n.Channel + 1} note={n.Note} velocity={n.Velocity}",
                MidiNoteOffMessage n => $"NoteOff ch={n.Channel + 1} note={n.Note} velocity={n.Velocity}",
                MidiControlChangeMessage c => $"CC ch={c.Channel + 1} controller={c.Controller} value={c.ControlValue}",
                _ => m.Type.ToString(),
            });
        };
        await Task.Delay(Timeout.Infinite, cancel.Token);
    }
    return 0;
}
catch (OperationCanceledException)
{
    return 0;
}
catch (Exception error)
{
    Console.Error.WriteLine(error.Message);
    return 1;
}
