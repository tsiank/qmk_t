// SPDX-License-Identifier: GPL-2.0-or-later
namespace Qmknrf5.Core;

// A single physical interface owns its I/O state. A client releasing its lease
// must not close the interface while another client is still streaming.
public sealed class SharedDevicePool<T>(Func<string,T> open, Action<T> close) where T : class
{
    private readonly object gate = new();
    private readonly Dictionary<string,Entry> entries = new(StringComparer.OrdinalIgnoreCase);
    private sealed class Entry(T value) { public T Value = value; public int Count; }
    public sealed class Lease : IDisposable
    {
        private readonly SharedDevicePool<T> pool;
        private readonly string key;
        private T? value;
        internal Lease(SharedDevicePool<T> pool,string key,T value) { this.pool=pool;this.key=key;this.value=value; }
        public T Value => Volatile.Read(ref value) ?? throw new ObjectDisposedException(nameof(Lease));
        public void Dispose() { if (Interlocked.Exchange(ref value,null) is not null) pool.Release(key); }
    }
    public Lease Acquire(string key)
    {
        lock(gate)
        {
            if (!entries.TryGetValue(key,out var entry)) { entry=new(open(key));entries.Add(key,entry); }
            entry.Count++;
            return new Lease(this,key,entry.Value);
        }
    }
    private void Release(string key)
    {
        lock(gate)
        {
            var entry=entries[key];
            if (--entry.Count==0) { entries.Remove(key);close(entry.Value); }
        }
    }
}
