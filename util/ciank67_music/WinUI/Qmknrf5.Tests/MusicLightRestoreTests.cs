// SPDX-License-Identifier: GPL-2.0-or-later
using Qmknrf5.Core;

internal static class MusicLightRestoreTests
{
    public static void Run(Action<string,Action> test)
    {
        test("Music Stop restores all initial light combinations after sending STOP", () => {
            for(byte initial=0;initial<4;initial++)
            {
                using var cancel=new CancellationTokenSource();
                var device=new Device((byte)(initial|12)) { CancelOnFrame=cancel };
                try { KeyboardControlProtocol.RunWithMusicLightsAsync(device,LightTarget.Both,
                    token=>MusicSession.RunAsync(device,null,()=>new MusicSettings(25,0,LightTarget.Both,true),_=>{},token),cancel.Token).GetAwaiter().GetResult();
                    throw new Exception("Cancellation lost"); } catch(OperationCanceledException) { }
                if(device.Flags!=(initial|12)||device.Mode!=KeyboardMusicMode.MidiTyping||device.Calls.Count!=3||
                   device.Calls[0].Mask!=0||device.Calls[1]!=(3,3)||device.Calls[2]!=(3,initial)||
                   !device.StoppedBeforeRestore||device.Packets.Count!=2)
                    throw new Exception("Stop order or original light state was not restored");
            }
        });
        test("Music restoration only owns selected groups and preserves audio/MIDI changes", () => {
            var device=new Device(14);
            KeyboardControlProtocol.RunWithMusicLightsAsync(device,LightTarget.Matrix,_=>{
                if(device.Flags!=15)throw new Exception("Selected matrix was not enabled");
                device.Flags=9;device.Mode=KeyboardMusicMode.Midi;return Task.CompletedTask;
            },default).GetAwaiter().GetResult();
            if(device.Flags!=8||device.Mode!=KeyboardMusicMode.Midi||device.Calls[^1]!=(1,0))
                throw new Exception("Restoration overwrote unrelated group, audio or MIDI");
        });
        test("Startup failure after an applied write still restores the captured state", () => {
            var device=new Device(12){LoseEnableAck=true};
            try{KeyboardControlProtocol.RunWithMusicLightsAsync(device,LightTarget.Both,_=>throw new Exception("Music should not start"),default).GetAwaiter().GetResult();throw new Exception("Failure ignored");}
            catch(IOException ex)when(ex.Message=="lost enable ACK"){}
            if(device.Flags!=12||device.Calls.Count!=3||device.Calls[^1]!=(3,0))throw new Exception("Partial startup left lights on");
        });
        test("Capture failure and pre-cancellation never modify light power", () => {
            using var cancel=new CancellationTokenSource();cancel.Cancel();var device=new Device(12);
            try{KeyboardControlProtocol.RunWithMusicLightsAsync(device,LightTarget.Both,_=>Task.CompletedTask,cancel.Token).GetAwaiter().GetResult();throw new Exception("Cancellation ignored");}catch(OperationCanceledException){}
            if(device.Calls.Count!=0)throw new Exception("Cancelled start reached keyboard");
            device.FailRead=true;
            try{KeyboardControlProtocol.RunWithMusicLightsAsync(device,LightTarget.Both,_=>Task.CompletedTask,default).GetAwaiter().GetResult();throw new Exception("Read failure ignored");}catch(IOException){}
            if(device.Flags!=12||device.Calls.Count!=1)throw new Exception("Missing snapshot still modified keyboard");
        });
        test("Restoration failure is explicit and retains the original session error", () => {
            foreach(bool reject in new[]{false,true})
            {
            var device=new Device(12){FailRestore=!reject,RejectRestore=reject};
            try{KeyboardControlProtocol.RunWithMusicLightsAsync(device,LightTarget.Both,_=>throw new InvalidOperationException("capture failed"),default).GetAwaiter().GetResult();throw new Exception("Restore failure ignored");}
            catch(IOException ex){if(!ex.Message.Contains("恢复原灯光开关失败")||ex.InnerException is not AggregateException aggregate||!aggregate.InnerExceptions.Any(e=>e.Message=="capture failed"))throw new Exception("Failure evidence lost");}
            }
        });
        test("No target leaves switches untouched and absent groups are ignored", () => {
            var device=new Device(12);bool ran=false;
            KeyboardControlProtocol.RunWithMusicLightsAsync(device,LightTarget.None,_=>{ran=true;return Task.CompletedTask;},default).GetAwaiter().GetResult();
            if(!ran||device.Calls.Count!=0)throw new Exception("No target touched power");
            device=new Device(0){Capabilities=18};
            KeyboardControlProtocol.RunWithMusicLightsAsync(device,LightTarget.Both,_=>Task.CompletedTask,default).GetAwaiter().GetResult();
            if(device.Calls[1]!=(2,2)||device.Calls[^1]!=(2,0))throw new Exception("Absent matrix was requested");
        });
    }
    private sealed class Device(byte flags):IKeyboardControlTransport
    {
        public byte Flags=flags, Capabilities=31;
        public KeyboardMusicMode Mode=KeyboardMusicMode.MidiTyping;
        public bool LoseEnableAck, FailRead, FailRestore, RejectRestore, StoppedBeforeRestore;
        public CancellationTokenSource? CancelOnFrame;
        public List<(byte Mask,byte Values)> Calls=[];
        public List<byte[]> Packets=[];
        public Task<KeyboardControlState> ControlAsync(byte mask,byte values,KeyboardMusicMode requested,CancellationToken token)
        {
            token.ThrowIfCancellationRequested();Calls.Add((mask,values));
            if(mask==0&&FailRead)throw new IOException("read failed");
            if(Calls.Count==3){if(FailRestore)throw new IOException("device disconnected");StoppedBeforeRestore=Packets.LastOrDefault() is byte[] last&&last[1]==1&&last[3]==0;}
            if(!(Calls.Count==3&&RejectRestore))Flags=(byte)((Flags&~mask)|(values&mask));
            if((mask&16)!=0)Mode=requested;
            if(Calls.Count==2&&LoseEnableAck)throw new IOException("lost enable ACK");
            return Task.FromResult(new KeyboardControlState(Flags,Mode,Capabilities));
        }
        public ValueTask SendAsync(ReadOnlyMemory<byte> packet,CancellationToken token)
        {token.ThrowIfCancellationRequested();Packets.Add(packet.ToArray());if(packet.Span[1]==1&&packet.Span[3]!=0)CancelOnFrame?.Cancel();return ValueTask.CompletedTask;}
        public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
    }
}
