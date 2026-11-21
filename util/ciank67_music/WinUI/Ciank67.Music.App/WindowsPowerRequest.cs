// Copyright 2026 QMK Contributors
// SPDX-License-Identifier: GPL-2.0-or-later
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Ciank67.Music.App;

// Power requests belong to the session handle, not to a thread: music awaits
// resume on different thread-pool threads. Never request DisplayRequired.
internal sealed class WindowsPowerRequest : IDisposable
{
    private SafeFileHandle? handle;
    private const int SystemRequired = 1, ExecutionRequired = 3;

    public WindowsPowerRequest(string description = "Qmknrf5配置工具音乐律动正在采集声音并向键盘发送数据（允许熄屏）")
    {
        IntPtr reason = Marshal.StringToHGlobalUni(description);
        try
        {
            var context = new ReasonContext { Flags = 1, SimpleReason = reason };
            handle = PowerCreateRequest(ref context);
            if (handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
            if (!PowerSetRequest(handle, SystemRequired)) throw new Win32Exception(Marshal.GetLastWin32Error());
            if (!PowerSetRequest(handle, ExecutionRequired)) throw new Win32Exception(Marshal.GetLastWin32Error());
        }
        catch
        {
            // Closing the request also releases a partially acquired request.
            handle?.Dispose(); handle = null; throw;
        }
        finally { Marshal.FreeHGlobal(reason); }
    }

    public void Dispose()
    {
        var active = Interlocked.Exchange(ref handle, null);
        if (active is null) return;
        try
        {
            PowerClearRequest(active, ExecutionRequired);
            PowerClearRequest(active, SystemRequired);
        }
        finally { active.Dispose(); }
    }

    // REASON_CONTEXT contains a union whose detailed member occupies three
    // pointer-sized fields on x64. Reserve the full union even in simple mode.
    [StructLayout(LayoutKind.Sequential)]
    private struct ReasonContext
    {
        public uint Version, Flags;
        public IntPtr SimpleReason, Reserved1, Reserved2;
    }
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern SafeFileHandle PowerCreateRequest(ref ReasonContext context);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PowerSetRequest(SafeFileHandle handle, int type);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PowerClearRequest(SafeFileHandle handle, int type);
}
