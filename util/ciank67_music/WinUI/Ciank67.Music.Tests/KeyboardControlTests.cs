// SPDX-License-Identifier: GPL-2.0-or-later
using Ciank67.Music.Core;

internal static class KeyboardControlTests
{
    public static void Run(Action<string,Action> test)
    {
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
