using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Qmknrf5.App;

// The nRF SDK already accepts SET_REPORT(Output) on EP0. Use the overlapped
// equivalent of HidD_SetOutputReport so timeout/cancellation can be drained.
internal sealed class WindowsHidOutput : IDisposable
{
    private readonly SafeFileHandle handle;
    private const uint SetOutputReport = (11u << 16) | (101u << 2) | 1u; // HID_IN_CTL_CODE(101)

    public WindowsHidOutput(string path)
    {
        handle = CreateFileW(path, 0xC0000000, 3, IntPtr.Zero, 3, 0x40000000, IntPtr.Zero);
        if (handle.IsInvalid)
        {
            int error = Marshal.GetLastWin32Error(); handle.Dispose();
            throw new IOException("无法打开 USB 控制传输接口。", new Win32Exception(error));
        }
    }

    public void Write(byte[] report, CancellationToken token)
    {
        token.ThrowIfCancellationRequested();
        using var completed = new EventWaitHandle(false, EventResetMode.ManualReset);
        IntPtr buffer = Marshal.AllocHGlobal(report.Length);
        IntPtr operation = Marshal.AllocHGlobal(Marshal.SizeOf<Overlapped>());
        try
        {
            Marshal.Copy(report, 0, buffer, report.Length);
            Marshal.StructureToPtr(new Overlapped { Event = completed.SafeWaitHandle.DangerousGetHandle() }, operation, false);
            bool immediate = DeviceIoControl(handle, SetOutputReport, buffer, (uint)report.Length,
                IntPtr.Zero, 0, out _, operation);
            int error = Marshal.GetLastWin32Error();
            if (!immediate && error != 997) throw Failure(error);
            if (!immediate && WaitHandle.WaitAny([completed, token.WaitHandle], 1500) != 0)
            {
                CancelIoEx(handle, operation);
                // Never free the native buffers or send another report while
                // Windows might still be using this operation.
                GetOverlappedResult(handle, operation, out _, true);
                token.ThrowIfCancellationRequested();
                throw new TimeoutException("USB 控制传输超时（1500 ms）。");
            }
            if (!GetOverlappedResult(handle, operation, out _, false)) throw Failure(Marshal.GetLastWin32Error());
        }
        finally { Marshal.FreeHGlobal(operation); Marshal.FreeHGlobal(buffer); }
    }

    private static IOException Failure(int error) => new($"USB 控制传输失败（Win32 {error}）。", new Win32Exception(error));
    public void Dispose() => handle.Dispose();

    [StructLayout(LayoutKind.Sequential)]
    private struct Overlapped
    {
        public UIntPtr Internal, InternalHigh;
        public uint Offset, OffsetHigh;
        public IntPtr Event;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
    private static extern SafeFileHandle CreateFileW(string path, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DeviceIoControl(SafeFileHandle handle, uint code, IntPtr input, uint inputSize, IntPtr output, uint outputSize, out uint transferred, IntPtr overlapped);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetOverlappedResult(SafeFileHandle handle, IntPtr overlapped, out uint transferred, [MarshalAs(UnmanagedType.Bool)] bool wait);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CancelIoEx(SafeFileHandle handle, IntPtr overlapped);
}
