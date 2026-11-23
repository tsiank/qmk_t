// SPDX-License-Identifier: GPL-2.0-or-later
using Qmknrf5.Core;
internal static class BatteryTests
{
    public static void Run(Action<string,Action> test)
    {
        test("Battery zero, unknown and USB-powered cached samples are distinct", () => {
            var q=KeyboardBatteryProtocol.Request();var p=(byte[])q.Clone();
            p[2]=1;var unknown=KeyboardBatteryProtocol.Parse(p,q)!;
            if(unknown.HasSample||!unknown.Supported||unknown.Description.Contains("0%"))throw new Exception("Unknown shown as empty battery");
            p[2]=3;p[4]=12;p[5]=128;
            var empty=KeyboardBatteryProtocol.Parse(p,q)!;
            if(!empty.HasSample||empty.Percent!=0||empty.Millivolts!=3200)throw new Exception("Valid zero lost");
            p[2]=7;p[3]=99;p[4]=16;p[5]=4;
            var usb=KeyboardBatteryProtocol.Parse(p,q)!;
            if(!usb.UsbPowered||usb.Millivolts!=4100||!usb.Description.Contains("上次采样")||usb.Description.Contains("充电"))throw new Exception("Supply misreported as charging");
        });
        test("Battery rejects stale replies, impossible flags and invalid readings", () => {
            var q=KeyboardBatteryProtocol.Request();
            void Reject(Action<byte[]> corrupt){var p=(byte[])q.Clone();p[2]=3;p[3]=50;p[4]=15;p[5]=160;corrupt(p);
                try{KeyboardBatteryProtocol.Parse(p,q);throw new Exception("Invalid battery accepted");}catch(IOException){}}
            Reject(p=>p[31]^=1);Reject(p=>p[2]=8);Reject(p=>p[2]=2);Reject(p=>p[2]=1);
            Reject(p=>p[3]=101);Reject(p=>{p[4]=0;p[5]=0;});Reject(p=>p[4]=255);Reject(p=>p[27]=1);
            Reject(p=>p[1]=2);
        });
        test("Battery read is compatible with old firmware and cancellation", () => {
            var legacy=new FakeBattery(true);
            if(KeyboardBatteryProtocol.ReadAsync(legacy,default).GetAwaiter().GetResult() is not null)throw new Exception("Old firmware did not degrade gracefully");
            var device=new FakeBattery(false);var result=KeyboardBatteryProtocol.ReadAsync(device,default).GetAwaiter().GetResult();
            if(result is null||!result.Supported||result.HasSample)throw new Exception("Read did not parse reply");
            using var cancel=new CancellationTokenSource();cancel.Cancel();
            try{KeyboardBatteryProtocol.ReadAsync(device,cancel.Token).GetAwaiter().GetResult();throw new Exception("Cancellation ignored");}catch(OperationCanceledException){}
            if(device.Requests!=1)throw new Exception("Cancelled read reached device");
        });
    }
    private sealed class FakeBattery(bool legacy):IViaTransport
    {
        public int Requests;
        public Task<byte[]> ExchangeAsync(byte[] q,CancellationToken token){Requests++;var p=(byte[])q.Clone();p[2]=1;if(legacy)p[0]=0xFF;return Task.FromResult(p);}
        public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
    }
}
