// SPDX-License-Identifier: GPL-2.0-or-later
using System.Buffers.Binary;
using Qmknrf5.Core;

internal static class WirelessTests
{
    public static void Run(Action<string, Action> test)
    {
        test("Receiver DFU uses only its management command and never provisions a key", () => {
            var rx = new Device(2, 22) { ProtocolVersion = 2 };
            new WirelessClient(rx).EnterDfuAsync(default).GetAwaiter().GetResult();
            if (!rx.Requests.Select(r => r[1]).SequenceEqual(new byte[] { 0, 7 }) || rx.Writes != 0)
                throw new Exception("Unexpected DFU request flow");
            var keyboard = new Device(1, 11) { ProtocolVersion = 2 };
            Throws<IOException>(() => new WirelessClient(keyboard).EnterDfuAsync(default).GetAwaiter().GetResult());
            if (keyboard.Requests.Any(r => r[1] == 7)) throw new Exception("DFU reached a keyboard");
        });
        test("Wireless pairing needs only the receiver and does not provision a USB key", () => {
            var receiver = new Device(2, 22) { ProtocolVersion = 2, Paired = true, PairId = 17 };
            var client = new WirelessClient(receiver);
            var start = client.StartPairingAsync(default).GetAwaiter().GetResult();
            if (start.PairingState != 1 || start.PairingSeconds != 60 || !start.Paired || start.PairId != 17)
                throw new Exception("Opening a window modified the existing binding");
            receiver.PairingState = 3;
            var candidate = client.ReadAsync(default).GetAwaiter().GetResult();
            if (candidate.Fingerprint != 0x123456) throw new Exception("Candidate fingerprint decode failed");
            var saving = client.ConfirmPairingAsync(default).GetAwaiter().GetResult();
            if (saving.PairingState != 4) throw new Exception("Confirmation did not start saving");
            Throws<IOException>(() => client.CancelPairingAsync(default).GetAwaiter().GetResult());
            if (receiver.Writes != 0 || receiver.Requests.Any(r => r[1] == 2 || r.AsSpan(4, 26).ToArray().Any(b => b != 0)))
                throw new Exception("Wireless setup sent a USB provisioning key");
        });
        test("Wireless cancellation preserves pairing and legacy/keyboard targets cannot start a window", () => {
            var rx = new Device(2, 22) { ProtocolVersion = 2, Paired = true, PairId = 17 };
            var client = new WirelessClient(rx); client.StartPairingAsync(default).GetAwaiter().GetResult();
            var cancelled = client.CancelPairingAsync(default).GetAwaiter().GetResult();
            if (cancelled.PairingState != 8 || !cancelled.Paired || cancelled.PairId != 17) throw new Exception("Cancel replaced pairing");
            var legacy = new Device(2, 22);
            Throws<NotSupportedException>(() => new WirelessClient(legacy).StartPairingAsync(default).GetAwaiter().GetResult());
            if (legacy.Requests.Any(r => r[1] != 0)) throw new Exception("Legacy firmware received a new command");
            var keyboard = new Device(1, 11) { ProtocolVersion = 2 };
            Throws<IOException>(() => new WirelessClient(keyboard).StartPairingAsync(default).GetAwaiter().GetResult());
        });
        test("Wireless state rejects invalid pairing states and countdowns", () => {
            var rx = new Device(2, 22) { ProtocolVersion = 2, PairingState = 250 };
            Throws<IOException>(() => new WirelessClient(rx).ReadAsync(default).GetAwaiter().GetResult());
            rx.PairingState = 1; rx.PairingSeconds = 61;
            Throws<IOException>(() => new WirelessClient(rx).ReadAsync(default).GetAwaiter().GetResult());
        });
        test("USB pairing validates roles, shares a fresh key and saves both devices", () => {
            var tx = new Device(1, 11); var rx = new Device(2, 22);
            var result = WirelessClient.PairAsync(new(tx), new(rx), default).GetAwaiter().GetResult();
            if (!result.Keyboard.Paired || !result.Receiver.Paired || tx.PairId != rx.PairId || !tx.Key.SequenceEqual(rx.Key) || tx.Key.All(b => b == 0))
                throw new Exception("Pairing did not save identical independent keys");
            byte[] first = tx.Key.ToArray();
            WirelessClient.PairAsync(new(tx), new(rx), default).GetAwaiter().GetResult();
            if (tx.Key.SequenceEqual(first)) throw new Exception("Re-pair reused a key");
            if (tx.Requests.Any(r => r.Length != 32 || r[0] != 0x84 || r[2] != 1)) throw new Exception("Bad request schema");
        });
        test("Pairing rejects the same physical device and reversed roles before writes", () => {
            var tx = new Device(1, 11); var wrong = new Device(2, 11);
            Throws<IOException>(() => WirelessClient.PairAsync(new(tx), new(wrong), default).GetAwaiter().GetResult());
            Throws<IOException>(() => WirelessClient.PairAsync(new(wrong), new(tx), default).GetAwaiter().GetResult());
            if (tx.Writes != 0 || wrong.Writes != 0) throw new Exception("Bad selection wrote keys");
        });
        test("A partial pairing failure explains how to recover without exposing keys", () => {
            var tx = new Device(1, 11) { FailWrite = true }; var rx = new Device(2, 22);
            try { WirelessClient.PairAsync(new(tx), new(rx), default).GetAwaiter().GetResult(); throw new Exception("Expected failure"); }
            catch (IOException error) {
                if (!error.Message.Contains("接收器已保存") || !error.Message.Contains("重新配对") || !rx.Paired)
                    throw new Exception("Partial save was hidden");
            }
        });
        test("Output mode persists and clearing pairing retains no active pair", () => {
            var tx = new Device(1, 11);
            var result = new WirelessClient(tx).SelectModeAsync(WirelessMode.Bluetooth, default).GetAwaiter().GetResult();
            if (result.Preferred != WirelessMode.Bluetooth || result.Current != WirelessMode.Bluetooth) throw new Exception("Mode mismatch");
            tx.Paired = true;
            if (new WirelessClient(tx).ClearAsync(default).GetAwaiter().GetResult().Paired) throw new Exception("Clear failed");
        });
        test("Old firmware, stale responses and cancelled requests fail explicitly", () => {
            Throws<NotSupportedException>(() => new WirelessClient(new Device(1, 11) { Unsupported = true }).ReadAsync(default).GetAwaiter().GetResult());
            Throws<IOException>(() => new WirelessClient(new Device(1, 11) { Stale = true }).ReadAsync(default).GetAwaiter().GetResult());
            using var cancellation = new CancellationTokenSource(); cancellation.Cancel();
            Throws<OperationCanceledException>(() => new WirelessClient(new Device(1, 11)).ReadAsync(cancellation.Token).GetAwaiter().GetResult());
        });
    }
    private static void Throws<T>(Action action) where T : Exception {
        try { action(); } catch (T) { return; } throw new Exception("Expected " + typeof(T).Name);
    }
    private sealed class Device(byte role, ulong id) : IViaTransport
    {
        public bool FailWrite, Unsupported, Stale, Paired;
        public uint PairId;
        public byte[] Key = new byte[16];
        public int Writes;
        public byte ProtocolVersion = 1, PairingState, PairingSeconds = 60;
        public readonly List<byte[]> Requests = [];
        private byte mode;
        public Task<byte[]> ExchangeAsync(byte[] request, CancellationToken token) {
            token.ThrowIfCancellationRequested(); Requests.Add(request.ToArray());
            if (Unsupported) return Task.FromResult(new byte[] { 255 }.Concat(new byte[31]).ToArray());
            if (request[1] == 2) {
                if (FailWrite) throw new IOException("Test write failure");
                Writes++; Key = request[4..20]; PairId = BinaryPrimitives.ReadUInt32BigEndian(request.AsSpan(20)); Paired = true;
            }
            if (request[1] == 1) mode = request[3];
            if (request[1] == 3) Paired = false;
            byte error = 0;
            if (request[1] == 4) PairingState = 1;
            if (request[1] == 5) { if (PairingState == 3) PairingState = 4; else error = 3; }
            if (request[1] == 6) { if (PairingState is >= 1 and <= 3) PairingState = 8; else error = 3; }
            byte[] r = new byte[32]; r[0] = 0x84; r[1] = ProtocolVersion; r[2] = error; r[3] = role; r[4] = r[5] = mode;
            r[6] = (byte)(Paired ? 1 : 0); r[7] = 1;
            BinaryPrimitives.WriteUInt32BigEndian(r.AsSpan(9), PairId); BinaryPrimitives.WriteUInt64BigEndian(r.AsSpan(13), id);
            r[25] = request[30]; r[26] = Stale ? (byte)(request[31] ^ 1) : request[31];
            if (ProtocolVersion == 2) { r[27] = PairingState; r[28] = PairingSeconds; r[29] = 0x12; r[30] = 0x34; r[31] = 0x56; }
            return Task.FromResult(r);
        }
        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
    }
}
