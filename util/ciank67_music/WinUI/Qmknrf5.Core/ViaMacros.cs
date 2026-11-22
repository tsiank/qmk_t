// SPDX-License-Identifier: GPL-2.0-or-later
namespace Qmknrf5.Core;

// Edit text macros without losing binary key sequences in other macro slots.
public static class ViaMacros
{
    public static byte[][] Split(byte[] buffer, int count)
    {
        var result = new byte[count][]; int offset = 0;
        for (int i = 0; i < count; i++)
        {
            int end = Array.IndexOf(buffer, (byte)0, offset);
            if (end < 0) throw new IOException("宏缓冲区不完整，请重新读取。");
            result[i] = buffer[offset..end]; offset = end + 1;
        }
        return result;
    }
    public static byte[] Replace(byte[] buffer, int count, int slot, string text, bool hex)
    {
        if (slot < 0 || slot >= count) throw new ArgumentOutOfRangeException(nameof(slot));
        byte[] data;
        if (hex) data = Convert.FromHexString(string.Concat(text.Where(c => !char.IsWhiteSpace(c))));
        else
        {
            if (text.Any(c => c != '\n' && c != '\r' && c != '\t' && (c < ' ' || c > '~'))) throw new FormatException("文字宏支持英文、数字、标点、换行和 Tab；组合键请使用 VIA 宏字节。中文需由输入法转换。");
            data = System.Text.Encoding.ASCII.GetBytes(text.Replace("\r\n", "\n"));
        }
        if (data.Contains((byte)0)) throw new FormatException("宏内容不能含 00 结束符。");
        var slots = Split(buffer, count); slots[slot] = data;
        if (slots.Sum(s => s.Length + 1) >= buffer.Length) throw new IOException("宏内容超过键盘容量，末尾须保留有效标记。");
        byte[] packed = new byte[buffer.Length]; int offset = 0;
        foreach (var item in slots) { item.CopyTo(packed, offset); offset += item.Length + 1; }
        return packed;
    }
}
