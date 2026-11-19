#!/usr/bin/env python3
"""Windows system-audio visualizer for ciank67 USB and Bluetooth firmware."""
import argparse
import asyncio
import math
import sys
import threading
import time

from protocol import BANDS, RX_UUID, TARGETS, USAGE, USAGE_PAGE, packet, usb_report


class AudioCapture:
    def __init__(self, device_index=None):
        if sys.platform != 'win32':
            raise RuntimeError('System audio capture requires Windows; use --demo for a transport test')
        import pyaudiowpatch as pa
        self.pa = pa.PyAudio()
        self.stream = None
        self.lock = threading.Lock()
        self.data = b''
        self.timestamp = 0.0
        try:
            device = self.pa.get_default_wasapi_loopback() if device_index is None else self.pa.get_device_info_by_index(device_index)
            if not device.get('isLoopbackDevice'):
                raise RuntimeError('Select a WASAPI loopback device, not a microphone')
            self.channels = int(device['maxInputChannels'])
            self.rate = int(device['defaultSampleRate'])
            self.capacity = 2048 * self.channels * 2
            print(f"Capturing: {device['name']} ({self.rate} Hz, {self.channels} channels)")

            def callback(data, count, timing, status):
                with self.lock:
                    self.data = (self.data + data)[-self.capacity:]
                    self.timestamp = time.monotonic()
                return (None, pa.paContinue)

            self.stream = self.pa.open(format=pa.paInt16, channels=self.channels, rate=self.rate,
                                       input=True, input_device_index=int(device['index']),
                                       frames_per_buffer=1024, stream_callback=callback)
        except BaseException:
            self.close()
            raise

    def samples(self):
        import numpy as np
        with self.lock:
            data, timestamp = self.data, self.timestamp
        if time.monotonic() - timestamp > 0.25 or not data:
            return np.zeros((2048, self.channels))
        return np.frombuffer(data, dtype='<i2').reshape(-1, self.channels).astype(np.float64) / 32768

    def close(self):
        if self.stream is not None:
            self.stream.stop_stream()
            self.stream.close()
        self.pa.terminate()


def usb_devices(vid, pid):
    import hid
    identities = [(vid, pid)]
    if (vid, pid) == (0xFEED, 0xC475):
        identities.append((0x1915, 0x520B))
    found = {}
    for vendor, product in identities:
        for device in hid.enumerate(vendor, product):
            if device.get('usage_page') == USAGE_PAGE and device.get('usage') == USAGE:
                found[device['path']] = device
    return list(found.values())


class USB:
    def __init__(self, args):
        import hid
        matches = usb_devices(args.vid, args.pid)
        if args.serial:
            matches = [device for device in matches if device.get('serial_number') == args.serial]
        if len(matches) != 1:
            raise RuntimeError(f'Found {len(matches)} music HID interfaces; use --list-usb and --serial, and flash the music firmware')
        self.device = hid.device()
        try:
            self.device.open_path(matches[0]['path'])
        except BaseException:
            self.device.close()
            raise

    async def send(self, data):
        report = usb_report(data)
        sent = await asyncio.to_thread(self.device.write, report)
        if sent != len(report):
            raise RuntimeError(f'Short USB write: {sent}/{len(report)}')

    async def close(self):
        self.device.close()


async def paired_ble_devices():
    """Enumerate paired Windows devices, including keyboards no longer advertising."""
    from bleak.backends.device import BLEDevice
    from winrt.windows.devices.bluetooth import BluetoothLEDevice
    from winrt.windows.devices.enumeration import DeviceInformation
    selector = BluetoothLEDevice.get_device_selector_from_pairing_state(True)
    result = []
    for information in await DeviceInformation.find_all_async_aqs_filter(selector):
        device = await BluetoothLEDevice.from_id_async(information.id)
        if device is not None:
            try:
                address = f'{device.bluetooth_address:012X}'
                address = ':'.join(address[i:i + 2] for i in range(0, 12, 2))
                result.append(BLEDevice(address, information.name, None))
            finally:
                device.close()
    return result


async def open_ble(args):
    from bleak import BleakClient, BleakScanner
    device = None
    if sys.platform == 'win32':
        paired = await paired_ble_devices()
        matches = [item for item in paired if
                   (args.ble_address and item.address.casefold() == args.ble_address.replace('-', ':').casefold()) or
                   (not args.ble_address and args.ble_name and item.name == args.ble_name)]
        if len(matches) > 1:
            raise RuntimeError('Multiple paired keyboards have this name; use --ble-address')
        if matches:
            device = matches[0]
    if device is None and args.ble_address:
        device = await BleakScanner.find_device_by_address(args.ble_address, timeout=15)
    elif device is None and args.ble_name:
        device = await BleakScanner.find_device_by_name(args.ble_name, timeout=15)
    elif device is None:
        raise RuntimeError('BLE requires --ble-address or --ble-name; use --scan-ble to find the keyboard')
    if device is None:
        raise RuntimeError('Keyboard not found; enable keyboard Bluetooth advertising')
    client = BleakClient(device, pair=True, timeout=30, winrt={'use_cached_services': False})
    try:
        await client.connect()
        characteristic = client.services.get_characteristic(RX_UUID)
        if characteristic is None:
            raise RuntimeError('Music service not found; flash the music firmware and remove/re-pair the keyboard if Windows cached old services')
    except BaseException:
        if client.is_connected:
            await client.disconnect()
        raise

    class BLE:
        async def send(self, data):
            await client.write_gatt_char(characteristic, data, response=False)

        async def close(self):
            await client.disconnect()

    return BLE()


async def run(args):
    if args.list_paired_ble:
        if sys.platform != 'win32':
            raise RuntimeError('Paired device listing requires Windows')
        for device in await paired_ble_devices():
            print(device.address, device.name or '(unnamed)')
        return
    if args.scan_ble:
        from bleak import BleakScanner
        for device in await BleakScanner.discover(timeout=8):
            print(device.address, device.name or '(unnamed)')
        return
    if args.list_usb:
        for device in usb_devices(args.vid, args.pid):
            print(device.get('product_string'), 'serial=', device.get('serial_number'), 'path=', device['path'])
        return
    if args.list_audio:
        if sys.platform != 'win32':
            raise RuntimeError('WASAPI device listing requires Windows')
        import pyaudiowpatch as pa
        with pa.PyAudio() as audio:
            for device in audio.get_loopback_device_info_generator():
                print(int(device['index']), device['name'])
        return

    capture = None
    transport = None
    sequence = 0
    try:
        transport = USB(args) if args.transport == 'usb' else await open_ble(args)
        if not args.demo:
            from spectrum import Spectrum
            capture = AudioCapture(args.audio_device)
            analyzer = Spectrum(capture.rate, gain_db=args.gain_db)
        print(f'Music connected via {args.transport}, target={args.target}, {args.fps:g} fps. Ctrl+C to restore normal lighting.')
        started = deadline = time.monotonic()
        while args.seconds is None or time.monotonic() - started < args.seconds:
            if args.demo:
                t = time.monotonic() - started
                bands = [round(255 * (0.5 + 0.5 * math.sin(t * 3 - i * 0.45))) for i in range(BANDS)]
            else:
                bands = analyzer.analyze(capture.samples())
            await transport.send(packet(sequence, bands, args.target))
            sequence = (sequence + 1) & 255
            # Do not queue delayed frames after a slow Bluetooth write.
            deadline = max(deadline + 1 / args.fps, time.monotonic())
            await asyncio.sleep(max(0, deadline - time.monotonic()))
    finally:
        if capture is not None:
            capture.close()
        if transport is not None:
            try:
                await transport.send(packet(sequence, [0] * BANDS, stop=True))
            except Exception:
                pass  # Firmware also restores normal lighting after 750 ms.
            await transport.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--transport', choices=['usb', 'ble'], default='usb')
    parser.add_argument('--target', choices=TARGETS, default='both')
    parser.add_argument('--fps', type=float, default=25)
    parser.add_argument('--gain-db', type=float, default=0)
    parser.add_argument('--vid', type=lambda value: int(value, 0), default=0xFEED)
    parser.add_argument('--pid', type=lambda value: int(value, 0), default=0xC475)
    parser.add_argument('--serial')
    parser.add_argument('--ble-address')
    parser.add_argument('--ble-name')
    parser.add_argument('--audio-device', type=int)
    parser.add_argument('--seconds', type=float)
    parser.add_argument('--demo', action='store_true', help='Send a synthetic spectrum without capturing audio')
    parser.add_argument('--list-usb', action='store_true')
    parser.add_argument('--scan-ble', action='store_true')
    parser.add_argument('--list-paired-ble', action='store_true')
    parser.add_argument('--list-audio', action='store_true')
    args = parser.parse_args()
    if not math.isfinite(args.fps) or not 1 <= args.fps <= 30:
        parser.error('--fps must be between 1 and 30')
    if not math.isfinite(args.gain_db):
        parser.error('--gain-db must be finite')
    if args.seconds is not None and (not math.isfinite(args.seconds) or args.seconds <= 0):
        parser.error('--seconds must be positive')
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass
    except Exception as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
