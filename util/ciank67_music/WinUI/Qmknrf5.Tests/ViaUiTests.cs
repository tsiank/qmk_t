// SPDX-License-Identifier: GPL-2.0-or-later
using Qmknrf5.Core;
using System.Buffers.Binary;
using System.Text;

internal static class ViaUiTests
{
    private static readonly ViaUiCapabilities Caps = new(128, 32, 1, 5, 1);
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    private static void Reject(Action action) { try { action(); } catch (IOException) { return; } throw new Exception("Expected a rejected configuration"); }
    private static ViaUiConfiguration Config()
    {
        byte[] b = new byte[ViaUiConfiguration.Size]; b[0] = 1; b[1] = 7; b[4] = b[5] = b[6] = b[7] = 16;
        for (int i = 0; i < 7; i++) b[18 + i * 4] = 1;
        for (int i = 0; i < 32; i++) b[48 + i * 5] = 255;
        var c = new ViaUiConfiguration(b); c.Seal(); return c;
    }
    public static void Run(Action<string, Action> test)
    {
        test("Screen and encoder backup/import/default merges affect only their own section", () =>
        {
            var baseline = Config();var screen = baseline.Copy();screen.SetBitmap(false, 80, 16, Enumerable.Repeat((byte)128, 160).ToArray());
            var encoder = baseline.Copy();encoder.Bytes[48] = 0;encoder.Bytes[49] = 42;encoder.Seal();
            string screenJson = ViaUiConfiguration.ExportSection(screen, Caps, ViaUiSection.Screen);
            string encoderJson = ViaUiConfiguration.ExportSection(encoder, Caps, ViaUiSection.Encoders);
            var merged = ViaUiConfiguration.ImportSection(screenJson, encoder, Caps, ViaUiSection.Screen);
            Check(merged.SectionEquals(screen, ViaUiSection.Screen) && merged.SectionEquals(encoder, ViaUiSection.Encoders), "Screen import overwrote encoders");
            merged = ViaUiConfiguration.ImportSection(encoderJson, screen, Caps, ViaUiSection.Encoders);
            Check(merged.SectionEquals(screen, ViaUiSection.Screen) && merged.SectionEquals(encoder, ViaUiSection.Encoders), "Encoder import overwrote screen");
            Reject(() => ViaUiConfiguration.ImportSection(screenJson, baseline, Caps, ViaUiSection.Encoders));
            Reject(() => ViaUiConfiguration.ImportSection(encoderJson, baseline, Caps, ViaUiSection.Screen));
            merged.CopySectionFrom(baseline, ViaUiSection.Screen);
            Check(merged.SectionEquals(baseline, ViaUiSection.Screen) && merged.SectionEquals(encoder, ViaUiSection.Encoders), "Screen reset changed encoders");
            merged.Validate(Caps);
            using var e = System.Text.Json.JsonDocument.Parse(encoderJson);
            Check(Convert.FromBase64String(e.RootElement.GetProperty("Data").GetString()!).Length == 160, "Encoder backup contains graphics");
        });
        test("Section save merges only selected edits into a fresh device snapshot", () =>
        {
            var device = new Device();device.Active.Bytes[48] = 0;device.Active.Bytes[49] = 11;device.Active.Seal();
            var desired = Config();desired.SetBitmap(false, 80, 16, Enumerable.Repeat((byte)128, 160).ToArray());
            desired.Bytes[48] = 0;desired.Bytes[49] = 99;desired.Seal();
            var saved = new ViaUiClient(device).SaveSectionAsync(desired, Caps, ViaUiSection.Screen, default).GetAwaiter().GetResult();
            Check(saved.Bytes[49] == 11 && device.Active.Bytes[49] == 11 && desired.Bytes[49] == 99, "Screen save uploaded or discarded pending encoder edits");
            Check(device.Active.Bytes[4] == 80 && device.Writes == 67, "Screen save did not patch its own ranges");
            device.Writes = 0;
            saved = new ViaUiClient(device).SaveSectionAsync(desired, Caps, ViaUiSection.Encoders, default).GetAwaiter().GetResult();
            Check(saved.Bytes[49] == 99 && saved.Bytes[4] == 80 && device.Writes == 8, "Encoder save resent or changed graphics");
        });
        test("Legacy combined backups can be imported without changing the other tab", () =>
        {
            var legacy = Config();legacy.Bytes[48] = 0;legacy.Bytes[49] = 99;legacy.Seal();
            var current = Config();current.SetBitmap(false, 80, 16, new byte[160]);
            var merged = ViaUiConfiguration.ImportSection(ViaUiConfiguration.Export(legacy), current, Caps, ViaUiSection.Encoders);
            Check(merged.Bytes[4] == 80 && merged.Bytes[49] == 99, "Legacy backup replaced the nonselected tab");
        });
        test("PCtoLCD five-character title automatically imports as 80x16", () =>
        {
            const string export = """
            键(0) 盘(1) 侠(2) 归(3) 来(4)

            {0x20,0x10,0x20,0x10,0x3B,0x7C,0x21,0x14,0x41,0xFE,0x7A,0x14,0xA2,0x7C,0x27,0x10,0xF9,0x7C,0x25,0x10,0x25,0xFE,0x22,0x10,0x2A,0x10,0x35,0x00,0x28,0xFE,0x00,0x00},/*"键",0*/
            /* (16 X 16 , 宋体 )*/

            {0x02,0x00,0x04,0x00,0x1F,0xF0,0x11,0x10,0x10,0x90,0xFF,0xFE,0x10,0x10,0x12,0x10,0x21,0x50,0x40,0x20,0x3F,0xF8,0x24,0x48,0x24,0x48,0x24,0x48,0xFF,0xFE,0x00,0x00},/*"盘",1*/
            /* (16 X 16 , 宋体 )*/

            {0x08,0x40,0x08,0x40,0x08,0x40,0x17,0xFC,0x10,0x40,0x32,0x48,0x31,0x50,0x50,0x40,0x9F,0xFE,0x10,0xA0,0x10,0xA0,0x11,0x10,0x11,0x10,0x12,0x08,0x14,0x04,0x18,0x02},/*"侠",2*/
            /* (16 X 16 , 宋体 )*/

            {0x08,0x00,0x08,0x00,0x4B,0xFC,0x48,0x04,0x48,0x04,0x48,0x04,0x48,0x04,0x49,0xFC,0x48,0x04,0x48,0x04,0x48,0x04,0x08,0x04,0x10,0x04,0x13,0xFC,0x20,0x04,0x40,0x00},/*"归",3*/
            /* (16 X 16 , 宋体 )*/

            {0x01,0x00,0x01,0x00,0x01,0x00,0x7F,0xFC,0x01,0x00,0x11,0x10,0x09,0x10,0x09,0x20,0xFF,0xFE,0x03,0x80,0x05,0x40,0x09,0x20,0x31,0x18,0xC1,0x06,0x01,0x00,0x01,0x00},/*"来",4*/
            /* (16 X 16 , 宋体 )*/
            """;
            byte[] source = ViaUiConfiguration.ParseLcdText(export);
            var bitmap = ViaUiConfiguration.Import16PixelGlyphs(export);
            Check(bitmap.Width == 80 && bitmap.Height == 16 && bitmap.Pixels.Length == 160, "Five glyphs were forced into the old four-glyph canvas");
            for (int glyph = 0; glyph < 5; glyph++) for (int row = 0; row < 16; row++)
                Check(bitmap.Pixels.AsSpan(row * 10 + glyph * 2, 2).SequenceEqual(source.AsSpan(glyph * 32 + row * 2, 2)), "PCtoLCD per-glyph rows were transposed incorrectly");
            var config = Config();config.SetBitmap(false, bitmap.Width, bitmap.Height, bitmap.Pixels);config.Validate(Caps);
            Reject(() => ViaUiConfiguration.Import16PixelGlyphs(export, 64));
            Reject(() => ViaUiConfiguration.Import16PixelGlyphs("/* (8 X 16 , font)*/ {0x00}"));
            Reject(() => ViaUiConfiguration.Import16PixelGlyphs("{0x00,0x01}"));
            Reject(() => ViaUiConfiguration.Import16PixelGlyphs(string.Join(",", Enumerable.Repeat("0x00", 9 * 32))));
        });
        test("LCD text imports decode ANSI, UTF-8, UTF-16 with or without BOM and UTF-32", () =>
        {
            const string text = "/* title */ {0x08, 0X80, 0xA, 0xff}";
            var encodings = new Encoding[] { Encoding.Latin1, Encoding.UTF8, Encoding.Unicode, Encoding.BigEndianUnicode, Encoding.UTF32, new UTF32Encoding(true, true) };
            foreach (var encoding in encodings)
            {
                byte[] raw = encoding.GetBytes(text);
                var decoded = ViaUiConfiguration.DecodeLcdText(encoding.GetPreamble().Concat(raw).ToArray());
                Check(ViaUiConfiguration.ParseLcdText(decoded).SequenceEqual(new byte[] { 8, 128, 10, 255 }), "Encoded export lost bytes");
                if (encoding == Encoding.Unicode || encoding == Encoding.BigEndianUnicode)
                    Check(ViaUiConfiguration.ParseLcdText(ViaUiConfiguration.DecodeLcdText(raw)).Length == 4, "BOM-less UTF-16 was not detected");
            }
            byte[] plain = Encoding.UTF8.GetPreamble().Concat(Encoding.UTF8.GetBytes("08 80,FF;00")).ToArray();
            Check(ViaUiConfiguration.ParseLcdText(ViaUiConfiguration.DecodeLcdText(plain)).SequenceEqual(new byte[] { 8, 128, 255, 0 }), "Plain hex import failed");
            Reject(() => ViaUiConfiguration.ParseLcdText("no bitmap bytes"));
            Reject(() => ViaUiConfiguration.ParseLcdText(""));
            Reject(() => ViaUiConfiguration.ParseLcdText("{0x1234}"));
            Reject(() => ViaUiConfiguration.ParseLcdText("{8,128,40,136}"));
        });
        test("A rejected local import preserves the VIA session and permits a later upload", () =>
        {
            var device = new Device();var session = new ViaTransportSession(device);var client = new ViaUiClient(session);
            var config = client.ReadAsync(Caps, default).GetAwaiter().GetResult();var original = config.Copy();
            Reject(() => ViaUiConfiguration.ParseLcdText("a file without a glyph array"));
            Check(!session.IsFaulted && config.Bytes.SequenceEqual(original.Bytes) && device.Writes == 0, "Local validation invalidated the link or draft");
            config.SetBitmap(false, 16, 16, Enumerable.Repeat((byte)128, 32).ToArray());
            client.SaveAsync(config, Caps, default).GetAwaiter().GetResult();
            Check(!session.IsFaulted && device.Commits == 1, "A valid import could not be saved after a local error");
        });
        test("Actual exchange failures and stale replies invalidate VIA sessions", () =>
        {
            var session = new ViaTransportSession(new Device { FailChunk = 1 });
            Reject(() => new ViaUiClient(session).SaveAsync(Config(), Caps, default).GetAwaiter().GetResult());
            Check(session.IsFaulted, "Wire failure was treated as a local file error");
            var stale = new ViaTransportSession(new Device { BadReadOffset = true });
            Reject(() => new ViaUiClient(stale).ReadAsync(Caps, default).GetAwaiter().GetResult());
            Check(stale.IsFaulted, "Stale reply did not invalidate the connection");
            var unsupported = new ViaTransportSession(new Unsupported());
            Reject(() => new ViaUiClient(unsupported).CapabilitiesAsync(default).GetAwaiter().GetResult());
            Check(!unsupported.IsFaulted, "Completed unsupported-feature reply invalidated a working link");
        });
        test("OLED CRC matches IEEE vector, validates bounds and rejects unsupported lighting", () =>
        {
            Check(ViaUiConfiguration.Crc32(Encoding.ASCII.GetBytes("123456789")) == 0xCBF43926, "Wrong CRC32");
            var c = Config(); c.Validate(Caps); c.Bytes[16] = 128; c.Seal(); Reject(() => c.Validate(Caps));
            c = Config(); c.Bytes[48] = 2; c.Seal(); Reject(() => c.Validate(Caps));
            c = Config(); c.Bytes[ViaUiConfiguration.FontOffset] ^= 1; Reject(() => c.Validate(Caps));
            Reject(() => Config().SetBitmap(true, 65, 16, new byte[144]));
            Reject(() => Config().SetClockFont(new byte[32]));
        });
        test("PCtoLCD imports ignore comments and transpose per-character rows correctly", () =>
        {
            var bytes = ViaUiConfiguration.ParseLcdText("/* 0xFF */ {0x80, 0x01}, // 0xAB\n{0x02,0x04}");
            Check(bytes.SequenceEqual(new byte[] { 128, 1, 2, 4 }), "Comments entered bitmap");
            byte[] glyphs = Enumerable.Range(0, 64).Select(i => (byte)i).ToArray();
            var joined = ViaUiConfiguration.Join16PixelGlyphs(glyphs, 2);
            Check(joined.AsSpan(0, 8).SequenceEqual(new byte[] { 0, 1, 32, 33, 2, 3, 34, 35 }), "Glyph rows interleaved incorrectly");
        });
        test("Screen artwork backups preserve all glyphs and encoder actions and reject corruption", () =>
        {
            var c = Config(); c.SetBitmap(false, 16, 16, Enumerable.Repeat((byte)0x80, 32).ToArray());
            c.SetClockFont(Enumerable.Range(0, 672).Select(i => (byte)i).ToArray());
            var copy = ViaUiConfiguration.Import(ViaUiConfiguration.Export(c), Caps);
            Check(c.Bytes.SequenceEqual(copy.Bytes), "Lost artwork in backup");
            copy.Bytes[4] = 129; copy.Seal(); Reject(() => copy.Validate(Caps));
        });
        test("OLED preview clips a scaled logo at the bottom-right like firmware", () =>
        {
            var c = Config(); c.SetBitmap(true, 16, 16, Enumerable.Repeat((byte)255, 32).ToArray());
            c.Bytes[16] = 127; c.Bytes[17] = 31; c.Bytes[18] = 2; c.Bytes[19] = 1;
            var pixels = OledPreview.Render(c, Caps);
            Check(pixels.Count(p => p) == 1 && pixels[^1], "Preview failed pixel clipping");
        });
        test("VIA UI upload chunks commit atomically, verify readback and wait for Flash", () =>
        {
            var device = new Device(); var client = new ViaUiClient(device); var caps = client.CapabilitiesAsync(default).GetAwaiter().GetResult();
            var c = client.ReadAsync(caps, default).GetAwaiter().GetResult(); Check(device.Session == 0, "Read left staging locked");
            c.SetBitmap(false, 128, 32, Enumerable.Range(0, 512).Select(i => (byte)i).ToArray()); c.Bytes[48] = 0; c.Bytes[49] = 42; c.Seal();
            client.SaveAsync(c, caps, default).GetAwaiter().GetResult();
            Check(device.Commits == 1 && device.Writes == 78 && device.Polls == 3 && device.Active.Bytes.SequenceEqual(c.Bytes), "Incomplete transaction/readback/Flash polling");
            var defaults = client.DefaultsAsync(caps, default).GetAwaiter().GetResult();
            Check(defaults.Bytes.SequenceEqual(Config().Bytes) && device.Commits == 1 && device.Session == 0, "Reset defaults persisted before Save");
        });
        test("Aborted VIA UI upload releases session and retains the previous active config", () =>
        {
            var device = new Device { FailChunk = 4 }; var client = new ViaUiClient(device); var c = Config(); c.Bytes[2] = 1; c.Seal();
            Reject(() => client.SaveAsync(c, Caps, default).GetAwaiter().GetResult());
            Check(device.Session == 0 && device.Commits == 0 && device.Active.Bytes[2] == 0, "Partial upload affected live state or leaked a lock");
        });
        test("UI client rejects wrong offsets, mismatched commit data and Flash errors", () =>
        {
            var wrong = new Device { BadReadOffset = true }; Reject(() => new ViaUiClient(wrong).ReadAsync(Caps, default).GetAwaiter().GetResult()); Check(wrong.Session == 0, "Bad response leaked staging");
            var corrupt = new Device { CorruptCommit = true }; Reject(() => new ViaUiClient(corrupt).SaveAsync(Config(), Caps, default).GetAwaiter().GetResult());
            var flash = new Device { FlashError = true }; Reject(() => new ViaUiClient(flash).SaveAsync(Config(), Caps, default).GetAwaiter().GetResult());
        });
    }
    private sealed class Unsupported : IViaTransport
    {
        public Task<byte[]> ExchangeAsync(byte[] request, CancellationToken token)
        {
            byte[] reply = (byte[])request.Clone(); reply[0] = 0xFF; return Task.FromResult(reply);
        }
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
    }
    private sealed class Device : IViaTransport
    {
        public ViaUiConfiguration Active = Config(); private ViaUiConfiguration? draft; private byte[]? beforeStage;
        public ushort Session; private ushort next;
        public int Commits, Writes, Polls, FailChunk;
        public bool BadReadOffset, CorruptCommit, FlashError;
        public Task<byte[]> ExchangeAsync(byte[] request, CancellationToken token)
        {
            token.ThrowIfCancellationRequested(); Check(request.Length == 32, "Wrong request size"); byte[] r = (byte[])request.Clone();
            if (r[0] == 0x80) { r[1] = (byte)(Polls++ < 2 ? 1 : 0); if (FlashError) r[5] = 1; return Task.FromResult(r); }
            Check(r[1] == 0x71, "Wrong VIA custom channel"); ushort id = BinaryPrimitives.ReadUInt16LittleEndian(r.AsSpan(4)); int offset = BinaryPrimitives.ReadUInt16LittleEndian(r.AsSpan(6)), count = r[8];
            switch (r[2])
            {
                case 0: r[4] = 1; r[5] = 128; r[6] = 32; r[7] = 1; r[8] = 5; BinaryPrimitives.WriteUInt16LittleEndian(r.AsSpan(9), 1792); r[11] = 1; break;
                case 1: beforeStage = Active.Bytes.ToArray(); draft = r[4] == 1 ? Config() : Active.Copy(); Session = ++next; BinaryPrimitives.WriteUInt16LittleEndian(r.AsSpan(4), Session); break;
                case 2:
                    Check(Session == id && count is > 0 and <= 23 && offset + count <= 1792, "Invalid upload frame");
                    if (++Writes == FailChunk) throw new IOException("Disconnected during upload");
                    r.AsSpan(9, count).CopyTo(draft!.Bytes.AsSpan(offset)); Check(Active.Bytes.AsSpan().SequenceEqual(beforeStage), "Data applied before commit"); break;
                case 3:
                    Check(count is > 0 and <= 23 && offset + count <= 1792, "Invalid read frame");
                    (id == 0 ? Active : draft!).Bytes.AsSpan(offset, count).CopyTo(r.AsSpan(9)); if (BadReadOffset) r[6]++; break;
                case 4: Check(Session == id, "Stale commit"); draft!.Validate(Caps); Active = draft.Copy(); Session = 0; Commits++; if (CorruptCommit) Active.Bytes[2] ^= 1; break;
                case 5: Check(Session == id, "Stale abort"); Session = 0; draft = null; break;
                default: throw new Exception("Unknown command");
            }
            return Task.FromResult(r);
        }
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
    }
}
