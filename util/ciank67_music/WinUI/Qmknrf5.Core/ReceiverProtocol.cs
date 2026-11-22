// SPDX-License-Identifier: GPL-2.0-or-later
namespace Qmknrf5.Core;

public static class ReceiverProtocol
{
    public static byte[] WrapMusic(ReadOnlySpan<byte> packet)
    {
        if(packet.Length!=20||packet[0]!=0xC7||packet[1] is <1 or >6)
            throw new ArgumentException("接收器音乐或控制数据格式错误。");
        byte[] request=new byte[32];packet.CopyTo(request);return request;
    }
    public static KeyboardControlState ParseControl(ReadOnlySpan<byte> response,byte sequence)
    {
        if(response.Length!=32)throw new IOException("2.4G 控制响应长度错误。");
        if(response[0]==0xFF)throw new IOException("接收器不支持远程控制，请给键盘和接收器都刷入 R13 或更新固件。");
        if(response[0]==0xC7&&response[1]==0xFF&&response[2]==sequence)
            throw new IOException("未能通过 2.4G 控制键盘，请确认配对成功且键盘处于 2.4G 模式。");
        foreach(byte value in response[20..])if(value!=0)throw new IOException("2.4G 控制响应包含无效数据。");
        return KeyboardControlProtocol.Parse(response[..20],sequence)
            ?? throw new IOException("2.4G 控制响应无效或已过期，请确认两端使用配套固件。");
    }
}
