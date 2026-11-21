// SPDX-License-Identifier: GPL-2.0-or-later
using Ciank67.Music.Core;

internal static class KeyboardControlTests
{
    public static void Run(Action<string,Action> test)
    {
        test("Screen commands and acknowledgements preserve independent clock and light state",()=>
        {
            foreach(var mode in new[]{ScreenPowerMode.Automatic,ScreenPowerMode.AlwaysOn,ScreenPowerMode.Off})
            {
                var command=KeyboardControlProtocol.ScreenCommand(42,mode);
                if(command.Length!=20||command[0]!=0xc7||command[1]!=6||command[2]!=42||command[3]!=(byte)mode||command.Skip(4).Any(x=>x!=0))throw new Exception("Wrong screen command");
                byte[] reply=new byte[20];reply[0]=0xc7;reply[1]=0x83;reply[2]=42;reply[3]=2;reply[5]=3;reply[6]=18;reply[7]=6;reply[8]=3;reply[9]=1;reply[10]=(byte)mode;reply[11]=mode==ScreenPowerMode.Off?(byte)0:(byte)1;
                var state=KeyboardControlProtocol.Parse(reply,42);
                if(state is not {SupportsScreen:true,SupportsClock:true,ClockSynchronized:true}||state.ScreenMode!=mode||!state.Underglow||state.ScreenOn!=(mode!=ScreenPowerMode.Off))throw new Exception("Wrong extended screen state");
                reply[10]=3;if(KeyboardControlProtocol.Parse(reply,42) is not null)throw new Exception("Invalid screen mode accepted");reply[10]=0;
                reply[11]=2;if(KeyboardControlProtocol.Parse(reply,42) is not null)throw new Exception("Invalid screen status accepted");
            }
            try{KeyboardControlProtocol.ScreenCommand(0,(ScreenPowerMode)3);throw new Exception("Invalid screen command accepted");}catch(ArgumentOutOfRangeException){}
        });
        test("Clock sync uses local wall time, millisecond precision and zero reserved bytes",()=>
        {
            var packet=KeyboardControlProtocol.ClockCommand(255,new DateTime(2026,11,21,23,45,12,999,DateTimeKind.Local));
            if(packet.Length!=20||packet[0]!=0xc7||packet[1]!=5||packet[2]!=255||packet[3]!=23||packet[4]!=45||packet[5]!=12||packet[6]!=0xe7||packet[7]!=3||packet.Skip(8).Any(x=>x!=0))throw new Exception("Clock packet mismatch");
        });
        test("Clock capability v3 accepts synchronized state and rejects invalid flags",()=>
        {
            byte[] packet=new byte[20];packet[0]=0xc7;packet[1]=0x83;packet[2]=7;packet[5]=3;packet[6]=18;packet[7]=6;packet[8]=1;
            var state=KeyboardControlProtocol.Parse(packet,7);
            if(state is not {SupportsClock:true,ClockSynchronized:false})throw new Exception("Clock capability missing");
            packet[9]=1;if(KeyboardControlProtocol.Parse(packet,7) is not {ClockSynchronized:true})throw new Exception("Clock acknowledgement missing");
            packet[9]=2;if(KeyboardControlProtocol.Parse(packet,7) is not null)throw new Exception("Invalid sync state");packet[9]=0;
            packet[8]=4;if(KeyboardControlProtocol.Parse(packet,7) is not null)throw new Exception("Unknown clock capabilities");packet[8]=1;
            for(int i=10;i<20;i++){packet[i]=1;if(KeyboardControlProtocol.Parse(packet,7) is not null)throw new Exception("Reserved byte accepted");packet[i]=0;}
        });
        test("Music start powers only selected light groups while preserving MIDI and audio state",()=>
        {
            foreach(var target in new[]{LightTarget.Matrix,LightTarget.Underglow,LightTarget.Both})
            {
                var device=new ControlDevice();
                var state=KeyboardControlProtocol.EnableMusicLightsAsync(device,target,default).GetAwaiter().GetResult()!;
                if(state.Mode!=KeyboardMusicMode.MidiTyping||!state.Audio||!state.Clicky||(state.Flags&3)!=(byte)target||device.Requests!=1)
                    throw new Exception("Music startup changed unrelated keyboard state or wrong lights");
            }
        });
        test("Music startup on an underglow-only pad ignores absent matrix hardware",()=>
        {
            var device=new ControlDevice();
            var state=KeyboardControlProtocol.EnableMusicLightsAsync(device,LightTarget.Both,default,18).GetAwaiter().GetResult()!;
            if(!state.Underglow||state.Matrix||!state.Audio||state.Mode!=KeyboardMusicMode.MidiTyping)throw new Exception("Pad startup tried unsupported matrix hardware");
        });
        test("Music start without a selected light group leaves keyboard state untouched",()=>
        {
            var device=new ControlDevice();var state=KeyboardControlProtocol.EnableMusicLightsAsync(device,LightTarget.None,default).GetAwaiter().GetResult();
            if(state is not null||device.Requests!=0)throw new Exception("An unselected light group was powered");
        });
        test("Music startup reports rejected light changes and respects cancellation before writing",()=>
        {
            var device=new ControlDevice { Reject=true };
            try{KeyboardControlProtocol.EnableMusicLightsAsync(device,LightTarget.Both,default).GetAwaiter().GetResult();throw new Exception("Rejected power command accepted");}catch(IOException){}
            using var cancel=new CancellationTokenSource();cancel.Cancel();device=new();
            try{KeyboardControlProtocol.EnableMusicLightsAsync(device,LightTarget.Both,cancel.Token).GetAwaiter().GetResult();throw new Exception("Canceled startup accepted");}catch(OperationCanceledException){}
            if(device.Requests!=0)throw new Exception("Canceled startup wrote to keyboard");
        });
    }
    private sealed class ControlDevice : IKeyboardControlTransport
    {
        public int Requests;
        public bool Reject;
        private byte flags=12;
        private KeyboardMusicMode mode=KeyboardMusicMode.MidiTyping;
        public Task<KeyboardControlState> ControlAsync(byte mask,byte values,KeyboardMusicMode requested,CancellationToken token)
        {
            token.ThrowIfCancellationRequested();Requests++;
            if(!Reject)flags=(byte)((flags&~mask)|(values&mask));
            if((mask&16)!=0)mode=requested;
            return Task.FromResult(new KeyboardControlState(flags,mode));
        }
        public ValueTask SendAsync(ReadOnlyMemory<byte> packet,CancellationToken token)=>ValueTask.CompletedTask;
        public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
    }
}
