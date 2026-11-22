// SPDX-License-Identifier: GPL-2.0-or-later
namespace Qmknrf5.Core;

// Local files and the wire protocol can both throw IOException. Only errors
// inside an exchange invalidate the connection (and may leave delayed replies).
public sealed class ViaTransportSession(IViaTransport inner) : IViaTransport
{
    public bool IsFaulted { get; private set; }

    public async Task<byte[]> ExchangeAsync(byte[] request, CancellationToken token)
    {
        if (request.Length != 32) throw new ArgumentException("VIA request must be 32 bytes.", nameof(request));
        try
        {
            byte[] reply = await inner.ExchangeAsync(request, token);
            if (reply is null || reply.Length != 32 || reply[0] != request[0] && reply[0] != 0xFF)
                throw new IOException("VIA 响应长度或命令不匹配，请重新连接。");
            // FF is a completed unsupported-command reply, not a broken link.
            if (reply[0] == 0xFF) return reply;
            bool buffer = request[0] is 0x0E or 0x0F or 0x12 or 0x13;
            bool key = request[0] is 4 or 5;
            if ((buffer || key) && !reply.AsSpan(1, 3).SequenceEqual(request.AsSpan(1, 3)))
                throw new IOException("VIA 缓冲区或键位响应不匹配，请重新连接。");
            if (request[0] is 7 or 8 or 9 && request[1] == 0x71)
            {
                if (reply[1] != request[1] || reply[2] != request[2])
                    throw new IOException("VIA 屏幕配置响应不匹配，请重新连接。");
                if (request[2] is 2 or 3 && !reply.AsSpan(4, 5).SequenceEqual(request.AsSpan(4, 5)))
                    throw new IOException("VIA 配置分包响应不匹配，请重新连接。");
            }
            return reply;
        }
        catch
        {
            IsFaulted = true;
            throw;
        }
    }

    public ValueTask DisposeAsync() => inner.DisposeAsync();
}
