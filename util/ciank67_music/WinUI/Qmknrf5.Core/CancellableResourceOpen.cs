// SPDX-License-Identifier: GPL-2.0-or-later
namespace Qmknrf5.Core;

// Some Windows device-open operations cannot be cancelled natively. Release
// the UI promptly, but still own and dispose any resource they return later.
public static class CancellableResourceOpen
{
    public static async Task<T?> OpenAsync<T>(Func<Task<T?>> open, Action<T> close, CancellationToken token) where T : class
    {
        token.ThrowIfCancellationRequested();
        Task<T?> pending = open();
        try
        {
            T? value = await pending.WaitAsync(token).ConfigureAwait(false);
            token.ThrowIfCancellationRequested();
            return value;
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested)
        {
            _ = CloseLateAsync(pending, close);
            throw;
        }
    }
    private static async Task CloseLateAsync<T>(Task<T?> pending, Action<T> close) where T : class
    {
        try { if (await pending.ConfigureAwait(false) is { } value) close(value); }
        catch (Exception) { /* Observe a late open/dispose failure after cancellation. */ }
    }
}
