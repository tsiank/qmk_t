"""Shared 20-byte music packet; USB adds HID report ID 4."""
BANDS = 16
REPORT_ID = 4
USAGE_PAGE = 0xFF67
USAGE = 0x67
SERVICE_UUID = 'c1a00001-7d9b-4c7d-a568-7294e2166700'
RX_UUID = 'c1a00002-7d9b-4c7d-a568-7294e2166700'
TARGETS = {'matrix': 1, 'underglow': 2, 'both': 3}


def packet(sequence, bands, target='both', *, stop=False):
    levels = bytes(bands)
    if len(levels) != BANDS:
        raise ValueError('Exactly 16 spectrum levels are required')
    return bytes((0xC7, 1, sequence & 255, 0 if stop else TARGETS[target])) + levels


def usb_report(data):
    if len(data) != 20:
        raise ValueError('Music payload must contain 20 bytes')
    return bytes((REPORT_ID,)) + data
