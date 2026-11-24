// SPDX-License-Identifier: GPL-2.0-or-later
namespace Qmknrf5.Core;

// UI-thread policy. A monotonic clock keeps wall-clock corrections from
// producing a polling burst or delaying refresh indefinitely.
public sealed class ControlPollSchedule
{
    private readonly Func<long> milliseconds;
    private long? lastRead;
    private bool foreground = true, refreshRequested;
    public ControlPollSchedule(Func<long>? milliseconds = null)
        => this.milliseconds = milliseconds ?? (() => Environment.TickCount64);
    public bool IsDue => refreshRequested || lastRead is null ||
        milliseconds() - lastRead.Value >= (foreground ? 2000 : 30000);
    public void ReadCompleted() { lastRead = milliseconds(); refreshRequested = false; }
    public void SetForeground(bool value)
    {
        if (value && !foreground) refreshRequested = true;
        foreground = value;
    }
}
