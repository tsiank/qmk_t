namespace Qmknrf5.Core;

// All packets set state, so retrying the same packet is safe. Callers serialize
// writes and await completion/cancellation before sending the restore command.
public sealed class UsbOutputFallback(
    Func<byte[], CancellationToken, ValueTask> interrupt,
    Func<byte[], CancellationToken, ValueTask> control)
{
    public bool UsesControl { get; private set; }

    public async ValueTask SendAsync(byte[] report, CancellationToken token)
    {
        token.ThrowIfCancellationRequested();
        if (UsesControl) { await control(report, token).ConfigureAwait(false); return; }
        try { await interrupt(report, token).ConfigureAwait(false); }
        catch (TimeoutException timeout)
        {
            UsesControl = true;
            token.ThrowIfCancellationRequested();
            try { await control(report, token).ConfigureAwait(false); }
            catch (OperationCanceledException) when (token.IsCancellationRequested) { throw; }
            catch (Exception failure)
            {
                throw new IOException("USB 中断写入超时，备用控制传输也未成功。请重新拔插 USB 并刷新设备。" +
                    $" 控制传输：{failure.Message}", new AggregateException(timeout, failure));
            }
        }
    }
}
