// User-provided logo: uugai.com-1588335396760.png (red emblem only).
// Aspect-preserving 16x9 emblem centered in a 16x16 monochrome cell.
// Red-pixel area coverage threshold 25%; excludes the source image caption.
// Row-major, MSB-left. Original SHA256: bdb63dd9a1ad0e8f07e60b8f22b7ebd8fd79825ef2236a596f3677473f6d9f00
#pragma once
#define NRF5_OLED_TITLE_PREFIX_BITMAP_WIDTH 16
#define NRF5_OLED_TITLE_PREFIX_BITMAP_HEIGHT 16
static const uint8_t nrf5_oled_title_prefix_bitmap[] = {
    0x00,0x00,
    0x00,0x00,
    0x00,0x00,
    0xE1,0x87,
    0x7A,0x5E,
    0x1D,0xB8,
    0x1F,0xF8,
    0x0F,0xF0,
    0x0F,0xF0,
    0x0F,0xF0,
    0x06,0x60,
    0x01,0x80,
    0x00,0x00,
    0x00,0x00,
    0x00,0x00,
    0x00,0x00,
};
