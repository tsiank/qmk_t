// SPDX-License-Identifier: GPL-2.0-or-later
using System.Runtime.InteropServices;
using Qmknrf5.Core;
using Windows.Graphics.Imaging;
using Windows.Storage;

namespace Qmknrf5.App;

internal static class OledImageImport
{
    // Convert to row-major, horizontal MSB bytes. The threshold is user controlled.
    internal static byte[] Pack(byte[] bgra, int width, int height, int threshold, bool invert)
    {
        byte[] bytes = new byte[(width + 7) / 8 * height];
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++)
        {
            int p = (y * width + x) * 4;
            // Treat transparent pixels as white paper when importing a logo.
            int luma = (bgra[p] * 114 + bgra[p + 1] * 587 + bgra[p + 2] * 299) / 1000;
            luma = (luma * bgra[p + 3] + 255 * (255 - bgra[p + 3])) / 255;
            bool lit = luma < threshold; if (invert) lit = !lit;
            if (lit) bytes[y * ((width + 7) / 8) + x / 8] |= (byte)(0x80 >> (x % 8));
        }
        return bytes;
    }
    public static async Task<byte[]> ImageAsync(StorageFile file, int width, int height, int threshold, bool invert)
    {
        using var stream = await file.OpenReadAsync();
        var decoder = await BitmapDecoder.CreateAsync(stream);
        if (decoder.PixelWidth > 10000 || decoder.PixelHeight > 10000) throw new IOException("图片尺寸过大。");
        // Fit inside the selected canvas while preserving proportions.
        double factor = Math.Min((double)width / decoder.PixelWidth, (double)height / decoder.PixelHeight);
        int w = Math.Max(1, (int)Math.Round(decoder.PixelWidth * factor)), h = Math.Max(1, (int)Math.Round(decoder.PixelHeight * factor));
        var transform = new BitmapTransform { ScaledWidth = (uint)w, ScaledHeight = (uint)h };
        var data = await decoder.GetPixelDataAsync(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Straight, transform, ExifOrientationMode.IgnoreExifOrientation, ColorManagementMode.DoNotColorManage);
        byte[] source = Pack(data.DetachPixelData(), w, h, threshold, invert), result = new byte[(width + 7) / 8 * height];
        int left = (width - w) / 2, top = (height - h) / 2;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) if ((source[y * ((w + 7) / 8) + x / 8] & (0x80 >> (x % 8))) != 0) result[(y + top) * ((width + 7) / 8) + (x + left) / 8] |= (byte)(0x80 >> ((x + left) % 8));
        return result;
    }
    public static byte[] Text(string text, string font, int pixelHeight, int width, int height)
    {
        if (string.IsNullOrWhiteSpace(text) || text.Length > 128 || string.IsNullOrWhiteSpace(font) || font.Length > 100) throw new IOException("请输入标题文字和字体名称。");
        nint dc = CreateCompatibleDC(0), bitmap = 0, typeface = 0, previousBitmap = 0, previousFont = 0;
        if (dc == 0) throw new IOException("无法创建字模画布。");
        try
        {
            var info = new BitmapInfo { Size = 40, Width = width, Height = -height, Planes = 1, BitCount = 32, SizeImage = (uint)(width * height * 4) };
            bitmap = CreateDIBSection(dc, ref info, 0, out nint bits, 0, 0); if (bitmap == 0 || bits == 0) throw new IOException("无法创建字模位图。");
            previousBitmap = SelectObject(dc, bitmap);
            typeface = CreateFont(-pixelHeight, 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 3, 0, font); if (typeface == 0) throw new IOException("无法载入字体。");
            previousFont = SelectObject(dc, typeface);
            PatBlt(dc, 0, 0, width, height, 0x00FF0062); SetTextColor(dc, 0); SetBkColor(dc, 0x00FFFFFF);
            var rect = new Rect { Right = width, Bottom = height }; if (DrawText(dc, text, text.Length, ref rect, 0x100 | 0x800 | 0x20) == 0) throw new IOException("字模绘制失败。");
            byte[] b = new byte[width * height * 4]; Marshal.Copy(bits, b, 0, b.Length); for (int i = 3; i < b.Length; i += 4) b[i] = 255;
            return Pack(b, width, height, 128, false);
        }
        finally
        {
            if (previousFont != 0) SelectObject(dc, previousFont); if (previousBitmap != 0) SelectObject(dc, previousBitmap);
            if (typeface != 0) DeleteObject(typeface); if (bitmap != 0) DeleteObject(bitmap); DeleteDC(dc);
        }
    }
    [StructLayout(LayoutKind.Sequential)] private struct BitmapInfo { public uint Size; public int Width, Height; public ushort Planes, BitCount; public uint Compression, SizeImage; public int XPels, YPels; public uint Used, Important; public uint Color; }
    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("gdi32.dll")] private static extern nint CreateCompatibleDC(nint dc);
    [DllImport("gdi32.dll")] private static extern nint CreateDIBSection(nint dc, ref BitmapInfo info, uint usage, out nint bits, nint section, uint offset);
    [DllImport("gdi32.dll")] private static extern nint SelectObject(nint dc, nint obj);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(nint obj);
    [DllImport("gdi32.dll")] private static extern bool DeleteDC(nint dc);
    [DllImport("gdi32.dll", CharSet = CharSet.Unicode)] private static extern nint CreateFont(int h, int w, int e, int o, int weight, uint italic, uint underline, uint strike, uint charset, uint output, uint clip, uint quality, uint pitch, string face);
    [DllImport("gdi32.dll")] private static extern bool PatBlt(nint dc, int x, int y, int w, int h, uint op);
    [DllImport("gdi32.dll")] private static extern uint SetTextColor(nint dc, uint color);
    [DllImport("gdi32.dll")] private static extern uint SetBkColor(nint dc, uint color);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "DrawTextW")] private static extern int DrawText(nint dc, string text, int count, ref Rect rect, uint flags);
}
