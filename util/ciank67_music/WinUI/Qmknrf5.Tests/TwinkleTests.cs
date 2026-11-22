// SPDX-License-Identifier: GPL-2.0-or-later
using System.Buffers.Binary;
using System.Text;
using Qmknrf5.Core;

internal static class TwinkleTests
{
    public static void Run(Action<string,Action> test)
    {
        test("Twinkle includes all six phrases, home-row melody and lower accompaniment",()=>{
            var song=TwinkleSong.Create();var melody=song.Events.Where(e=>e.Channel==0&&e.IsNoteOn).ToArray();
            if(melody.Length!=42||melody[0].Tick!=4*480||melody[0].Data1!=61||melody[^1].Data1!=61)throw new Exception("Incomplete melody");
            byte[] first=[61,61,68,68,70,70,68,66,66,65,65,63,63,61];
            if(!melody.Take(first.Length).Select(e=>e.Data1).SequenceEqual(first))throw new Exception("Wrong melody notes");
            if(song.Events.Where(e=>e.Channel==1&&e.IsNoteOn).Count()<90||!song.Events.Any(e=>e.IsNoteOn&&e.Data1==49))throw new Exception("Missing accompaniment");
            if(melody.Any(e=>e.Data2>=100)||song.TimeAt(TwinkleSong.EndTick).TotalSeconds is <36 or >37)throw new Exception("Wrong dynamics or tempo");
        });
        test("Twinkle closes every held note and orders release before retrigger",()=>{
            var song=TwinkleSong.Create(10);HashSet<(int,int)> held=[];int previous=-1;
            foreach(var e in song.Events)
            {
                if(e.Tick<previous||e.Tick>TwinkleSong.EndTick||e.Data1>127||e.Data2>127)throw new Exception("Invalid event");previous=e.Tick;
                var key=(e.Channel,(int)e.Data1);
                if(e.IsNoteOn&&!held.Add(key))throw new Exception("Overlapping same-channel note");
                if((e.Status&0xf0)==0x80&&!held.Remove(key))throw new Exception("Unmatched NoteOff");
            }
            if(held.Count!=0||!song.Events.Any(e=>e.Status==0xc0&&e.Data1==10)||!song.Events.Any(e=>e.Status==0xc1&&e.Data1==24))throw new Exception("Wrong voices or stuck notes");
        });
        test("Exported MIDI has three complete bounded tracks and a standard tempo",()=>{
            byte[] bytes=TwinkleSong.Create().ToMidiFile();
            if(Encoding.ASCII.GetString(bytes,0,4)!="MThd"||BinaryPrimitives.ReadUInt16BigEndian(bytes.AsSpan(8))!=1||BinaryPrimitives.ReadUInt16BigEndian(bytes.AsSpan(10))!=3||BinaryPrimitives.ReadUInt16BigEndian(bytes.AsSpan(12))!=480)throw new Exception("Wrong MIDI header");
            int pos=14;for(int i=0;i<3;i++){
                if(Encoding.ASCII.GetString(bytes,pos,4)!="MTrk")throw new Exception("Missing track");
                int length=BinaryPrimitives.ReadInt32BigEndian(bytes.AsSpan(pos+4));pos+=8+length;
                if(pos>bytes.Length||!bytes.AsSpan(pos-3,3).SequenceEqual(new byte[]{0xff,0x2f,0}))throw new Exception("Broken track ending");
            }
            if(pos!=bytes.Length||!bytes.AsSpan().Contains((byte)0x51))throw new Exception("Trailing data or missing tempo");
        });
    }
}
