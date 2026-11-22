// SPDX-License-Identifier: GPL-2.0-or-later
using Qmknrf5.Core;
using System.Buffers.Binary;

internal static class ViaTests
{
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    private static void Reject<T>(Action action) where T : Exception { try { action(); } catch (T) { return; } throw new Exception($"Expected {typeof(T).Name}"); }
    public static void Run(Action<string, Action> test)
    {
        test("Connected pad matches its built-in layout and preserves matching user layouts", () =>
        {
            var ciank = new KeyboardLayout("ciank67", 5, 14, []);
            var pad = new KeyboardLayout("ciank_pad", 5, 5, []);
            var user = pad with { Name = "my_custom_pad" };
            var matrix = new ViaMatrixDescription(5, 5, true);
            Check(ReferenceEquals(KeyboardLayout.MatchConnectedKeyboard(ciank, [ciank, pad], matrix, "Ciank_pad · 000000000000"), pad), "Pad kept the 67-key layout");
            Check(ReferenceEquals(KeyboardLayout.MatchConnectedKeyboard(user, [ciank, pad], matrix, "Ciank_pad · serial"), user), "Custom matching geometry was replaced");
            Check(ReferenceEquals(KeyboardLayout.MatchConnectedKeyboard(pad, [ciank, pad], new(5, 14, false), "Ciank67 · serial"), ciank), "Switching back kept the pad geometry");
            Reject<IOException>(() => KeyboardLayout.MatchConnectedKeyboard(ciank, [pad], matrix, "Unknown keyboard"));
            Reject<IOException>(() => KeyboardLayout.MatchConnectedKeyboard(ciank, [pad, pad], matrix, "Ciank_pad"));
            Reject<IOException>(() => KeyboardLayout.MatchConnectedKeyboard(ciank, [pad], new(6, 5, false), "Ciank_pad"));
        });
        test("VIA matrix discovery validates dimensions before any keymap read", () =>
        {
            using var device = new FakeVia { Rows = 5, Columns = 5 };
            var description = new ViaClient(device).DescribeMatrixAsync(default).GetAwaiter().GetResult();
            Check(description is { Rows: 5, Columns: 5, UiSupported: false }, "Wrong matrix descriptor");
            Check(device.MaxRead == 0, "Discovery read a keymap before checking geometry");
            device.Rows = 33;
            Reject<IOException>(() => new ViaClient(device).DescribeMatrixAsync(default).GetAwaiter().GetResult());
            device.Rows = 32; device.Columns = 32;
            Reject<IOException>(() => new ViaClient(device).DescribeMatrixAsync(default).GetAwaiter().GetResult());
            device.LegacyMatrix = true;
            Check(new ViaClient(device).DescribeMatrixAsync(default).GetAwaiter().GetResult() is null, "Legacy descriptor should be optional");
        });
        test("VIA reads all seven layers with big-endian chunk boundaries", () =>
        {
            using var device = new FakeVia();
            var snapshot = new ViaClient(device).ReadAsync(default).GetAwaiter().GetResult();
            Check(snapshot.Keys.SequenceEqual(device.Keys) && snapshot.Layers == 7 && snapshot.Keys.Length == 490 && snapshot.Macros.Length == 128, "Read lost matrix cells or macro data");
            Check(device.MaxRead <= 28, "VIA read exceeds packet capacity");
        });
        test("VIA alternate matrix reads and saves with its own dimensions and rejects mismatched layouts",()=>
        {
            using var device=new FakeVia { Rows=4,Columns=12,Keys=Enumerable.Range(0,7*4*12).Select(i=>(ushort)(i+100)).ToArray() };
            var client=new ViaClient(device,4,12);var original=client.ReadAsync(default).GetAwaiter().GetResult();
            Check(original.Rows==4&&original.Columns==12&&original.Keys.Length==336,"Alternate layout was truncated");
            var edited=original.Copy();edited.Keys[^1]=42;client.SaveAsync(edited,original,default).GetAwaiter().GetResult();
            Check(device.Keys[^1]==42,"Alternate layout writes used 5x14 offsets");
            Reject<IOException>(()=>new ViaClient(device).ReadAsync(default).GetAwaiter().GetResult());
            device.LegacyMatrix=true;Reject<IOException>(()=>client.ReadAsync(default).GetAwaiter().GetResult());
            using var legacy=new FakeVia { LegacyMatrix=true };Check(new ViaClient(legacy).ReadAsync(default).GetAwaiter().GetResult().Rows==5,"R22 fallback broken");
        });
        test("Keyboard layout profiles validate holes, key sizes, coordinates and per-keyboard custom keycodes",()=>
        {
            const string json="""{"name":"test40","matrix":{"rows":4,"cols":12},"layout":[{"matrix":[3,11],"x":2,"y":1,"w":2}],"custom_keycodes":{"TEST_ACTION":32320}}""";
            var profile=KeyboardLayout.Parse(json);Check(profile.Rows==4&&profile.Columns==12&&profile.Keys[0].Width==2,"Profile dimensions lost");
            Reject<IOException>(()=>KeyboardLayout.Parse(json.Replace("[3,11]","[4,11]")));
            Reject<IOException>(()=>KeyboardLayout.Parse(json.Replace("\"w\":2","\"w\":0")));
            Reject<IOException>(()=>KeyboardLayout.Parse(json.Replace("TEST_ACTION","KC_A")));
            var previous=ViaKeycodes.Names.Where(p=>p.Value>=ViaKeycodes.Names["QK_USER"]&&!p.Key.StartsWith("QK_")).ToDictionary(p=>p.Key,p=>p.Value);
            try{ViaKeycodes.ConfigureKeyboard(profile.CustomKeycodes);Check(!ViaKeycodes.Names.ContainsKey("MIDI_TYPING_TOGGLE")&&ViaKeycodes.Parse("TEST_ACTION")==32320,"Another keyboard inherited ciank67 custom names");}
            finally{ViaKeycodes.ConfigureKeyboard(previous);}
        });
        test("VIA save edits only changed keys and waits for verified persistence", () =>
        {
            using var device = new FakeVia();var client = new ViaClient(device);
            var original = client.ReadAsync(default).GetAwaiter().GetResult();var desired = original.Copy();
            desired.Keys[0] = ViaKeycodes.Parse("KC_ESC");desired.Keys[^1] = ViaKeycodes.Parse("LT(1,KC_SPC)");
            client.SaveAsync(desired, original, default).GetAwaiter().GetResult();
            Check(device.KeyWrites == 2 && device.FlashQueries >= 2 && device.Keys.SequenceEqual(desired.Keys), "Wrong changed-key writes or premature save acknowledgement");
            Check(original.Keys[0] != desired.Keys[0], "Snapshot shares editable storage");
        });
        test("VIA macro upload is invalidated first and remains inert when aborted", () =>
        {
            using var device = new FakeVia();var client = new ViaClient(device);
            var original = client.ReadAsync(default).GetAwaiter().GetResult();var desired = original.Copy();
            desired = desired with { Macros = ViaMacros.Replace(desired.Macros, desired.MacroCount, 0, "hello", false) };
            device.AbortMacro = true;
            Reject<IOException>(() => client.SaveAsync(desired, original, default).GetAwaiter().GetResult());
            Check(device.Macros[^1] == 255 && device.MacroWrites == 1, "Aborted macro remains executable");
            device.AbortMacro = false;
            client.SaveAsync(desired, original, default).GetAwaiter().GetResult();
            Check(device.Macros.SequenceEqual(desired.Macros) && device.Macros[^1] == 0, "Completed macro did not restore validity");
        });
        test("VIA rejects stale buffers, unhandled commands, firmware keycodes and flash errors", () =>
        {
            using var device = new FakeVia();var client = new ViaClient(device);
            device.Stale = true;Reject<IOException>(() => client.ReadAsync(default).GetAwaiter().GetResult());device.Stale = false;
            device.BadVersion = true;Reject<IOException>(() => client.ReadAsync(default).GetAwaiter().GetResult());device.BadVersion = false;
            var original = client.ReadAsync(default).GetAwaiter().GetResult();device.FlashError = true;
            Reject<IOException>(() => client.SaveAsync(original.Copy(), original, default).GetAwaiter().GetResult());
            device.Unhandled = true;Reject<IOException>(() => client.ReadAsync(default).GetAwaiter().GetResult());
        });
        test("VIA export import validates capacity and preserves unknown keycodes", () =>
        {
            using var device = new FakeVia();var original = new ViaClient(device).ReadAsync(default).GetAwaiter().GetResult();
            original.Keys[13] = 0xFFFF;var restored = ViaClient.Import(ViaClient.Export(original));
            Check(restored.Keys.SequenceEqual(original.Keys) && restored.Macros.SequenceEqual(original.Macros), "Backup roundtrip lost bytes");
            Reject<IOException>(() => ViaClient.Import("{}"));
            Reject<IOException>(() => ViaClient.Export(original with { Rows = 6 }));
            Reject<IOException>(() => ViaClient.Export(original with { Keys = new ushort[1] }));
            var malformed = new byte[128];Array.Fill(malformed, (byte)65);malformed[^1] = 0;
            Reject<IOException>(() => ViaClient.Export(original with { Macros = malformed }));
            using var cancel = new CancellationTokenSource();cancel.Cancel();
            Reject<OperationCanceledException>(() => new ViaClient(device).ReadAsync(cancel.Token).GetAwaiter().GetResult());
        });
        test("VIA keycode expressions use this QMK version and reject truncation", () =>
        {
            Check(ViaKeycodes.Parse("KC_A") == 4 && ViaKeycodes.Parse("MO(6)") == 0x5226 && ViaKeycodes.Parse("LT(1,KC_SPC)") == 0x412C, "Basic/layer codes mismatch");
            Check(ViaKeycodes.Parse("LCTL(KC_C)") == 0x106 && ViaKeycodes.Parse("LSFT_T(KC_ENT)") == 0x2228, "Modifier encodings mismatch");
            Check(ViaKeycodes.Parse("MIDI_TYPING_TOGGLE") == ViaKeycodes.Names["QK_USER"] + 22, "ciank67 custom keycodes shifted");
            foreach (ushort code in new ushort[] { 0, 1, 4, 0x412C, 0x5226, 0x7E40, 0xFFFF }) Check(ViaKeycodes.Parse(ViaKeycodes.Format(code)) == code, "Display changes underlying keycode");
            Reject<FormatException>(() => ViaKeycodes.Parse("MO(32)"));Reject<FormatException>(() => ViaKeycodes.Parse("LT(16,KC_A)"));
            Reject<FormatException>(() => ViaKeycodes.Parse("LT(1,MO(1))"));Reject<FormatException>(() => ViaKeycodes.Parse("RCTL(LCTL(KC_A))"));
        });
        test("VIA decodes every 16-bit keycode without changing its value", () =>
        {
            for(int i=0;i<=ushort.MaxValue;i++)
            {
                string text=ViaKeycodes.Format((ushort)i);
                Check(ViaKeycodes.Parse(text)==i,$"Keycode {i:X4} changed through {text}");
            }
        });
        test("VIA shifted symbols and hold-tap keys have editable names and labels", () =>
        {
            foreach(string name in new[]{"KC_EXCLAIM","KC_AT","KC_TILDE","KC_LEFT_PAREN","KC_DQT","LALT_T(KC_1)","RSFT_T(KC_SPC)","LT(2,KC_ENT)","OSM(MOD_LCTL | MOD_LSFT)","LM(3,MOD_RALT)"})
            {
                ushort code=ViaKeycodes.Parse(name);string text=ViaKeycodes.Format(code);
                Check(!text.StartsWith("0x")&&ViaKeycodes.Parse(text)==code,$"Missing symbol/compound name {name}");
            }
            Check(ViaKeyCatalog.Label(ViaKeycodes.Parse("KC_EXLM"))=="!","Shift punctuation is not a symbol");
            Check(ViaKeyCatalog.Describe(ViaKeycodes.Parse("RSFT_T(KC_SPC)")).Contains("轻按 Space，按住 Shift"),"Hold-tap meaning is not readable");
            Reject<FormatException>(()=>ViaKeycodes.Parse("MT(MOD_LCTL | MOD_RALT,KC_A)"));
            Reject<FormatException>(()=>ViaKeycodes.Parse("LM(16,MOD_LALT)"));
        });
        test("VIA palette separates real keycodes from internal ranges and finds Chinese or alias names", () =>
        {
            var choices=ViaKeyCatalog.Choices(7,16);
            Check(choices.Any(k=>k.Category=="键盘专用"&&k.Name=="MIDI_TYPING_TOGGLE"),"Keyboard-specific key missing");
            Check(choices.Any(k=>k.Category=="层切换"&&k.Name=="MO(6)"),"Seven layer choices missing");
            Check(!choices.Any(k=>k.Name=="MO(7)"||k.Name=="QK_MACRO_16"||k.Name.EndsWith("_MAX")),"Palette exposes unsupported layers or macro slots");
            Check(choices.Any(k=>k.Code==40&&ViaKeyCatalog.Matches(k,"回车")),"Chinese search missing");
            Check(choices.Any(k=>k.Code==40&&ViaKeyCatalog.Matches(k,"KC_ENTER")),"Alias search missing");
            Check(choices.Any(k=>k.Category=="鼠标"&&k.Label=="鼠标 ↑"),"Mouse key category missing");
            Check(choices.Any(k=>k.Category=="灯光"&&k.Label=="矩阵开关"),"Independent matrix lighting key missing");
            Check(!choices.Any(k=>k.Category=="灯光"&&k.Code<0x7820),"RGB palette includes disabled backlight/LED-matrix features");
            Check(choices.Select(k=>k.Code).Distinct().Count()==choices.Count,"Alias duplicates clutter palette");
        });
        test("VIA text macro editing preserves other binary macros and validates overflow", () =>
        {
            byte[] buffer = new byte[100];buffer[0] = 1;buffer[1] = 2;buffer[2] = 4;
            byte[] edited = ViaMacros.Replace(buffer, 16, 1, "abc\n", false);
            var slots = ViaMacros.Split(edited, 16);
            Check(slots[0].SequenceEqual(new byte[] { 1,2,4 }) && System.Text.Encoding.ASCII.GetString(slots[1]) == "abc\n", "Editing destroyed a different macro");
            Reject<FormatException>(() => ViaMacros.Replace(edited, 16, 1, "中文", false));
            Reject<FormatException>(() => ViaMacros.Replace(edited, 16, 1, "0100", true));
            Reject<IOException>(() => ViaMacros.Replace(edited, 16, 1, new string('A',100), false));
        });
    }
    private sealed class FakeVia : IViaTransport, IDisposable
    {
        public int Rows=5,Columns=14;
        public bool LegacyMatrix;
        public ushort[] Keys = Enumerable.Range(0,490).Select(i => (ushort)(i + 100)).ToArray();
        public byte[] Macros = new byte[128];
        public int MaxRead, KeyWrites, MacroWrites, FlashQueries;
        public bool AbortMacro, Stale, BadVersion, FlashError, Unhandled;
        public Task<byte[]> ExchangeAsync(byte[] request, CancellationToken token)
        {
            token.ThrowIfCancellationRequested();byte[] reply = (byte[])request.Clone();
            if (Unhandled) { reply[0] = 255;return Task.FromResult(reply); }
            int offset = request[1] << 8 | request[2], size = request[3];
            switch (request[0])
            {
                case 1: reply[1] = 0;reply[2] = 13;break;
                case 2: BinaryPrimitives.WriteUInt32BigEndian(reply.AsSpan(2),BadVersion ? 8u : 9u);break;
                case 0x11: reply[1] = 7;break;
                case 0x0C: reply[1] = 16;break;
                case 0x0D: BinaryPrimitives.WriteUInt16BigEndian(reply.AsSpan(1),128);break;
                case 0x12:
                    MaxRead=Math.Max(MaxRead,size);byte[] raw=new byte[Keys.Length*2];for(int i=0;i<Keys.Length;i++)BinaryPrimitives.WriteUInt16BigEndian(raw.AsSpan(i*2),Keys[i]);raw.AsSpan(offset,size).CopyTo(reply.AsSpan(4));if(Stale)reply[2]++;break;
                case 0x0E: Macros.AsSpan(offset,size).CopyTo(reply.AsSpan(4));break;
                case 5: Keys[(request[1]*Rows+request[2])*Columns+request[3]]=BinaryPrimitives.ReadUInt16BigEndian(request.AsSpan(4));KeyWrites++;break;
                case 0x0F:
                    if(AbortMacro&&MacroWrites>0)throw new IOException("Simulated disconnect");request.AsSpan(4,size).CopyTo(Macros.AsSpan(offset));MacroWrites++;break;
                case 0x81: if(LegacyMatrix)reply[0]=255;else{reply[1]=1;reply[2]=(byte)Rows;reply[3]=(byte)Columns;}break;
                case 0x80: reply[1]=(byte)(++FlashQueries==1?1:0);if(FlashError)reply[5]=1;break;
                default: throw new Exception("Unexpected VIA command");
            }
            return Task.FromResult(reply);
        }
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
        public void Dispose() { }
    }
}
