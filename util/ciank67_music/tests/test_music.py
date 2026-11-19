"""DSP, wire format and host lifecycle tests; no sound device or keyboard needed."""
import argparse
import asyncio
import contextlib
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import AsyncMock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import music
import protocol
import numpy as np
from spectrum import Spectrum


class ProtocolTests(unittest.TestCase):
    def test_selected_rgb_command_and_hex_input(self):
        self.assertEqual(protocol.color_packet((255, 128, 64)), bytes([0xC7, 2, 255, 128, 64]) + bytes(15))
        self.assertEqual(len(protocol.usb_report(protocol.color_packet((0, 0, 0)))), 21)
        self.assertEqual(music.hex_color('#FF8040'), (255, 128, 64))
        for invalid in ['#123', 'GG0000', 'ff ff ', '1234567']:
            with self.assertRaises(argparse.ArgumentTypeError):
                music.hex_color(invalid)
        for invalid in [(0, 0), (256, 0, 0), (-1, 0, 0)]:
            with self.assertRaises(ValueError):
                protocol.color_packet(invalid)

    def test_layout_stop_and_sequence_wrap(self):
        levels = list(range(16))
        data = protocol.packet(256, levels, 'underglow')
        self.assertEqual(data, bytes([0xC7, 1, 0, 2, *levels]))
        self.assertEqual(protocol.usb_report(data), bytes([4]) + data)
        self.assertEqual(protocol.packet(3, levels, stop=True)[3], 0)
        rays = protocol.packet(7, levels, effect='dual-dance')
        self.assertEqual(rays, bytes([0xC7, 1, 7, 0x13, *levels]))
        self.assertEqual(len(protocol.usb_report(rays)), 21)
        self.assertEqual(rays, protocol.packet(7, levels, effect='dual-rays'))
        self.assertEqual(protocol.packet(7, levels, effect='dual-dance', stop=True)[3], 0)
        self.assertEqual(protocol.packet(7, levels, color_mode='solid')[3], 0x23)
        self.assertEqual(protocol.packet(7, levels, effect='dual-dance', color_mode='solid')[3], 0x33)
        self.assertEqual(protocol.packet(7, levels, color_mode='solid', stop=True)[3], 0)

    def test_invalid_lengths_and_ranges(self):
        for levels in [[0] * 15, [0] * 17, [256] * 16, [-1] * 16]:
            with self.assertRaises(ValueError):
                protocol.packet(0, levels)
        with self.assertRaises(ValueError):
            protocol.usb_report(bytes(19))

    def test_usb_descriptor_and_ble_uuid_match_host(self):
        root = Path(__file__).resolve().parents[3]
        source = (root / 'tmk_core/protocol/nrf5/usb/nrf5_usb.c').read_text()
        self.assertIn('0x06, 0x67, 0xFF, 0x09, 0x67', source)
        # BLE UUID base is little-endian, with the service/characteristic at bytes 12:14.
        import re
        import uuid
        ble = (root / 'tmk_core/protocol/nrf5/ble/music_gatt.c').read_text()
        initial = ble.split('const ble_uuid128_t base = {{', 1)[1].split('}}', 1)[0]
        base = bytearray(int(value, 16) for value in re.findall(r'0x[0-9a-f]+', initial))
        for value, expected in [(1, protocol.SERVICE_UUID), (2, protocol.RX_UUID)]:
            base[12:14] = value.to_bytes(2, 'little')
            self.assertEqual(str(uuid.UUID(bytes=bytes(base[::-1]))), expected)


class SpectrumTests(unittest.TestCase):
    def test_silence_and_dc_are_black(self):
        for samples in [np.zeros(2048), np.ones(2048), np.zeros((2048, 2))]:
            self.assertEqual(Spectrum(48000).analyze(samples), [0] * 16)

    def test_frequency_moves_between_low_and_high_bands(self):
        t = np.arange(2048) / 48000
        low = Spectrum(48000).analyze(0.5 * np.sin(2 * np.pi * 100 * t))
        high = Spectrum(48000).analyze(0.5 * np.sin(2 * np.pi * 6000 * t))
        self.assertLess(np.argmax(low), 5)
        self.assertGreater(np.argmax(high), 10)
        self.assertGreater(max(low), 100)
        self.assertGreater(max(high), 100)

    def test_stereo_phase_does_not_cancel_and_release_decays(self):
        t = np.arange(2048) / 48000
        tone = 0.5 * np.sin(2 * np.pi * 440 * t)
        mono = Spectrum(48000).analyze(tone)
        analyzer = Spectrum(48000)
        self.assertEqual(mono, analyzer.analyze(np.column_stack([tone, -tone])))
        last = max(analyzer.levels)
        for _ in range(40):
            levels = analyzer.analyze(np.zeros(2048))
            self.assertLessEqual(max(analyzer.levels), last)
            last = max(analyzer.levels)
        self.assertEqual(levels, [0] * 16)

    def test_short_input_and_nonfinite_data(self):
        self.assertEqual(len(Spectrum(48000).analyze(np.zeros(10))), 16)
        for samples in [[float('nan')], [float('inf')], np.zeros((5, 0))]:
            with self.assertRaises(ValueError):
                Spectrum(48000).analyze(samples)

    def test_fast_spectrum_catches_a_short_beat_and_releases_in_three_frames(self):
        t = np.arange(2048) / 48000
        tone = .5 * np.sin(2 * np.pi * 440 * t)
        fast, original = Spectrum(48000, fast_response=True), Spectrum(48000)
        fast_attack, original_attack = max(fast.analyze(tone)), max(original.analyze(tone))
        self.assertGreater(fast_attack, 200)
        self.assertGreater(fast_attack, original_attack)
        for _ in range(3):
            fast_tail, original_tail = max(fast.analyze(np.zeros(2048))), max(original.analyze(np.zeros(2048)))
        self.assertLess(fast_tail, 15)
        self.assertGreater(original_tail, 60)


class LifecycleTests(unittest.TestCase):
    def test_usb_finds_legacy_identity_with_nordic_product_name(self):
        import types
        calls = []
        def enumerate_devices(vid, pid):
            calls.append((vid, pid))
            if (vid, pid) != (0x1915, 0x520B):
                return []
            return [
                {'path': b'keyboard', 'usage_page': 1, 'usage': 6},
                {'path': b'music', 'usage_page': protocol.USAGE_PAGE, 'usage': protocol.USAGE,
                 'product_string': 'nRF52 USB Product'},
            ]
        with patch.dict(sys.modules, hid=types.SimpleNamespace(enumerate=enumerate_devices)):
            found = music.usb_devices(0xFEED, 0xC475)
        self.assertEqual(calls, [(0xFEED, 0xC475), (0x1915, 0x520B)])
        self.assertEqual([item['path'] for item in found], [b'music'])

    def test_windows_paired_ble_does_not_require_advertising(self):
        import types
        paired = types.SimpleNamespace(address='AA:BB:CC:DD:EE:FF', name='ciank67')
        args = argparse.Namespace(ble_address=paired.address, ble_name=None)
        clients = []
        for missing_service in [False, True]:
            class Client:
                def __init__(self, device, **kwargs):
                    self.is_connected = False
                    self.frames = []
                    self.services = types.SimpleNamespace(get_characteristic=lambda uuid: None if missing_service else 'rx')
                    self.disconnected = False
                    self.kwargs = kwargs
                    self.device = device
                    clients.append(self)
                async def connect(self):
                    self.is_connected = True
                async def disconnect(self):
                    self.disconnected = True
                async def write_gatt_char(self, characteristic, data, response):
                    self.frames.append(data)
                    self.characteristic, self.response = characteristic, response

            async def exercise():
                if missing_service:
                    with self.assertRaises(RuntimeError):
                        await music.open_ble(args)
                else:
                    ble = await music.open_ble(args)
                    await ble.send(protocol.packet(0, [0] * 16))
                    await ble.close()

            bleak = types.SimpleNamespace(BleakClient=Client, BleakScanner=object())
            with patch.dict(sys.modules, bleak=bleak), patch.object(music.sys, 'platform', 'win32'), \
                    patch.object(music, 'paired_ble_devices', new=AsyncMock(return_value=[paired])):
                asyncio.run(exercise())
            client = clients[-1]
            self.assertTrue(client.disconnected)
            self.assertIs(client.device, paired)
            self.assertEqual(client.kwargs['winrt'], {'use_cached_services': False})
            if not missing_service:
                self.assertFalse(client.response)
                self.assertEqual(len(client.frames[0]), 20)

    def test_wasapi_callback_keeps_recent_audio_and_cleans_up(self):
        import types
        stream = types.SimpleNamespace(stop_stream=lambda: None, close=lambda: None)
        class Audio:
            def __init__(self):
                self.terminated = False
            def get_default_wasapi_loopback(self):
                return {'isLoopbackDevice': True, 'maxInputChannels': 2,
                        'defaultSampleRate': 48000, 'index': 7, 'name': 'speakers'}
            def open(self, **kwargs):
                self.options = kwargs
                return stream
            def terminate(self):
                self.terminated = True
        audio = Audio()
        module = types.SimpleNamespace(PyAudio=lambda: audio, paInt16=8, paContinue=0)
        with patch.dict(sys.modules, pyaudiowpatch=module), patch.object(music.sys, 'platform', 'win32'), \
                contextlib.redirect_stdout(io.StringIO()):
            capture = music.AudioCapture()
            values = np.arange(4096, dtype='<i2').reshape(2048, 2)
            audio.options['stream_callback'](values.tobytes(), 2048, {}, 0)
            self.assertTrue(np.allclose(capture.samples(), values / 32768))
            self.assertEqual(audio.options['input_device_index'], 7)
            with patch.object(music.time, 'monotonic', return_value=music.time.monotonic() + 1):
                self.assertFalse(capture.samples().any())
            capture.close()
            self.assertTrue(audio.terminated)

    def args(self):
        return argparse.Namespace(scan_ble=False, list_paired_ble=False, list_usb=False, list_audio=False,
                                  transport='usb', demo=True, seconds=0.01, fps=25, target='both', effect='dual-dance', color_mode='solid', solid_color=None)

    def test_demo_and_stop_restore_on_exit_and_write_failure(self):
        for fail in [False, True]:
            class FakeUSB:
                def __init__(self, args):
                    self.frames = []
                    self.closed = False
                async def send(self, data):
                    self.frames.append(data)
                    if fail and len(self.frames) == 1:
                        raise OSError('unplugged')
                async def close(self):
                    self.closed = True
            device = FakeUSB(None)
            with patch.object(music, 'USB', return_value=device), contextlib.redirect_stdout(io.StringIO()):
                if fail:
                    with self.assertRaises(OSError):
                        asyncio.run(music.run(self.args()))
                else:
                    asyncio.run(music.run(self.args()))
            self.assertTrue(device.closed)
            self.assertEqual(device.frames[0][3], 0x33)
            self.assertEqual(device.frames[-1][3], 0)
            self.assertTrue(all(len(frame) == 20 for frame in device.frames))

    def test_usb_uses_explicit_report_id_and_rejects_ambiguous_devices(self):
        import types
        device = types.SimpleNamespace(open_path=lambda path: None, write=lambda data: len(data), close=lambda: None)
        hid = types.SimpleNamespace(device=lambda: device, enumerate=lambda vid, pid: [
            {'path': b'a', 'usage_page': protocol.USAGE_PAGE, 'usage': protocol.USAGE}])
        args = argparse.Namespace(vid=0xFEED, pid=0xC475, serial=None)
        with patch.dict(sys.modules, hid=hid):
            usb = music.USB(args)
            asyncio.run(usb.send(protocol.packet(0, [0] * 16)))
            device.write = lambda data: 0
            with self.assertRaises(RuntimeError):
                asyncio.run(usb.send(protocol.packet(0, [0] * 16)))
            hid.enumerate = lambda vid, pid: []
            with self.assertRaises(RuntimeError):
                music.USB(args)


if __name__ == '__main__':
    unittest.main()
