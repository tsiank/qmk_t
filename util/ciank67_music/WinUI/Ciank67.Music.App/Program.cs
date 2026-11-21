using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Ciank67.Music.App;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        Trace("Main");
        WinRT.ComWrappersSupport.InitializeComWrappers();
        Trace("ComWrappers initialized");
        Application.Start(parameters =>
        {
            Trace("Application.Start callback");
            var queue = Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread();
            SynchronizationContext.SetSynchronizationContext(
                new Microsoft.UI.Dispatching.DispatcherQueueSynchronizationContext(queue));
            _ = new MusicApplication();
            Trace("Application constructed");
        });
    }

    internal static void Trace(string message)
    {
        if (Environment.GetEnvironmentVariable("QMKNRF5_DIAGNOSTICS") != "1" && Environment.GetEnvironmentVariable("CIANK67_DIAGNOSTICS") != "1") return;
        try { File.AppendAllText(Path.Combine(AppContext.BaseDirectory, "startup-trace.log"), message + Environment.NewLine); }
        catch (Exception) { }
    }
}

public sealed partial class MusicApplication : Application
{
    private Window? window;

    public MusicApplication()
    {
        Program.Trace("Application constructor");
        UnhandledException += (_, args) =>
        {
            try { File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "startup-error.log"), args.Message + Environment.NewLine + args.Exception); }
            catch (Exception) { }
        };
        InitializeComponent();
        Program.Trace("Control resources initialized");
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Program.Trace("OnLaunched");
        window = new MainWindow();
        Program.Trace("Window constructed");
        window.Activate();
        Program.Trace("Window activated");
    }
}
