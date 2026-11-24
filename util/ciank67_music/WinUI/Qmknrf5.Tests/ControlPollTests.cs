// SPDX-License-Identifier: GPL-2.0-or-later
using Qmknrf5.Core;
internal static class ControlPollTests
{
    public static void Run(Action<string,Action> test)
    {
        test("Control polling slows in background and refreshes on foreground return", () => {
            long now = 0; var schedule = new ControlPollSchedule(() => now);
            if (!schedule.IsDue) throw new Exception("First read missing");
            schedule.ReadCompleted(); now = 1999;
            if (schedule.IsDue) throw new Exception("Foreground polls too often");
            now++; if (!schedule.IsDue) throw new Exception("Foreground refresh missing");
            schedule.ReadCompleted(); schedule.SetForeground(false); now += 29999;
            if (schedule.IsDue) throw new Exception("Background polls too often");
            now++; if (!schedule.IsDue) throw new Exception("Background refresh missing");
            schedule.ReadCompleted(); schedule.SetForeground(true);
            if (!schedule.IsDue) throw new Exception("Returning window did not refresh");
            schedule.ReadCompleted(); schedule.SetForeground(true);
            if (schedule.IsDue) throw new Exception("Duplicate activation caused another read");
        });
        test("Successful user commands reset polling without background catch-up bursts", () => {
            long now = 0; var schedule = new ControlPollSchedule(() => now);
            schedule.ReadCompleted(); schedule.SetForeground(false); now = 120000;
            if (!schedule.IsDue) throw new Exception("Long idle lost refresh");
            schedule.ReadCompleted();
            if (schedule.IsDue) throw new Exception("Missed polls accumulated");
            now += 29000; schedule.ReadCompleted(); now += 1000;
            if (schedule.IsDue) throw new Exception("User command caused redundant poll");
            schedule.SetForeground(true); schedule.ReadCompleted(); now += 2000;
            if (!schedule.IsDue) throw new Exception("Foreground cadence not restored");
        });
    }
}
