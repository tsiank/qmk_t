// SPDX-License-Identifier: GPL-2.0-or-later
using System.Text;

namespace Qmknrf5.Core;

public readonly record struct SongMidiEvent(int Tick,byte Status,byte Data1,byte Data2=0)
{
    public int Channel=>Status&15;
    public bool IsNoteOn=>(Status&0xf0)==0x90&&Data2>0;
}

// One shared score drives both the in-app audition and the exported MIDI file.
public sealed class TwinkleSong
{
    public const int Division=480;
    public const int BeatsPerMinute=92;
    public const int TempoMicroseconds=652174;
    public const int EndTick=56*Division;
    public IReadOnlyList<SongMidiEvent> Events {get;}
    private TwinkleSong(List<SongMidiEvent> events) => Events=events.OrderBy(e=>e.Tick)
        .ThenBy(e=>(e.Status&0xf0)==0x80?0:e.IsNoteOn?2:1).ToArray();
    public TimeSpan TimeAt(int tick)=>TimeSpan.FromTicks((long)tick*TempoMicroseconds*10/Division);

    public static TwinkleSong Create(byte melodyProgram=0)
    {
        if(melodyProgram>127)throw new ArgumentOutOfRangeException(nameof(melodyProgram));
        List<SongMidiEvent> events=[];
        void Add(int tick,byte status,byte a,byte b=0)=>events.Add(new(tick,status,a,b));
        void Note(int channel,int note,double beat,double length,int velocity)
        {
            int start=(int)Math.Round(beat*Division),end=(int)Math.Round((beat+length)*Division);
            Add(start,(byte)(0x90|channel),(byte)note,(byte)velocity);
            Add(end,(byte)(0x80|channel),(byte)note,0);
        }
        void Setup(byte channel,byte program,byte volume,byte pan)
        {
            Add(0,(byte)(0xc0|channel),program);
            Add(0,(byte)(0xb0|channel),7,volume);
            Add(0,(byte)(0xb0|channel),10,pan);
            Add(0,(byte)(0xb0|channel),11,110);
            Add(0,(byte)(0xb0|channel),91,28);
            Add(0,(byte)(0xb0|channel),93,0);
        }
        Setup(0,melodyProgram,100,60);Setup(1,24,82,72); // piano/music-box + nylon guitar
        int[][] chords=[[49,56,61,65],[54,61,58,66],[56,63,60,68]];
        void Arpeggio(double beat,int chord,int velocity=52)
        {
            for(int i=0;i<4;i++)Note(1,chords[chord][i],beat+i*.5,i==0?1.8:.42,velocity-(i==0?0:5));
        }
        Arpeggio(0,0,46);Arpeggio(2,0,49); // four-beat introduction
        int[] scale=[61,63,65,66,68,70];
        int[][] melody=[
            [1,1,5,5],[6,6,5,0],[4,4,3,3],[2,2,1,0],
            [5,5,4,4],[3,3,2,0],[5,5,4,4],[3,3,2,0],
            [1,1,5,5],[6,6,5,0],[4,4,3,3],[2,2,1,0]];
        int[][] harmony=[[0,0],[1,0],[1,0],[2,0],[0,1],[0,2],[0,1],[0,2],[0,0],[1,0],[1,0],[2,0]];
        for(int bar=0;bar<melody.Length;bar++)
        {
            int start=4+bar*4;
            for(int beat=0;beat<4;beat++)
            {
                int degree=melody[bar][beat];if(degree==0)continue;
                bool held=beat==2&&melody[bar][3]==0;
                int velocity=beat==0?88:beat==2?84:79;
                if(bar>=10)velocity-=4;
                Note(0,scale[degree-1],start+beat,held?1.84:.86,velocity);
            }
            Arpeggio(start,harmony[bar][0]);Arpeggio(start+2,harmony[bar][1]);
        }
        foreach(int note in new[]{49,61,65,68})Note(1,note,52+(note-49)*.008,3.45,49);
        for(int beat=0;beat<4;beat++)Add((52+beat)*Division,0xb1,11,(byte)(100-beat*22));
        for(byte channel=0;channel<2;channel++)
        {
            Add(EndTick,(byte)(0xb0|channel),64,0);Add(EndTick,(byte)(0xb0|channel),123,0);
        }
        return new(events);
    }

    // Standard MIDI File, format 1: conductor, melody and accompaniment tracks.
    public byte[] ToMidiFile()
    {
        using var file=new MemoryStream();
        WriteAscii(file,"MThd");WriteInt(file,6);WriteShort(file,1);WriteShort(file,3);WriteShort(file,Division);
        using(var conductor=new MemoryStream())
        {
            Meta(conductor,0,3,Encoding.UTF8.GetBytes("Twinkle - soft piano and guitar"));
            Meta(conductor,0,0x51,[(byte)(TempoMicroseconds>>16),(byte)((TempoMicroseconds>>8)&255),(byte)(TempoMicroseconds&255)]);
            Meta(conductor,0,0x58,[4,2,24,8]);Meta(conductor,0,0x59,[0xfb,0]); // D-flat major
            Meta(conductor,EndTick,0x2f,[]);Track(file,conductor);
        }
        for(int channel=0;channel<2;channel++)
        {
            using var track=new MemoryStream();
            Meta(track,0,3,Encoding.UTF8.GetBytes(channel==0?"Melody - home row":"Accompaniment - lower register"));
            int previous=0;
            foreach(var e in Events.Where(e=>e.Channel==channel))
            {
                Variable(track,e.Tick-previous);previous=e.Tick;
                track.WriteByte(e.Status);track.WriteByte(e.Data1);
                if((e.Status&0xf0) is not (0xc0 or 0xd0))track.WriteByte(e.Data2);
            }
            Meta(track,EndTick-previous,0x2f,[]);Track(file,track);
        }
        return file.ToArray();
    }
    private static void Track(Stream output,MemoryStream track){WriteAscii(output,"MTrk");WriteInt(output,(int)track.Length);track.Position=0;track.CopyTo(output);}
    private static void Meta(Stream output,int delta,byte type,byte[] bytes){Variable(output,delta);output.WriteByte(0xff);output.WriteByte(type);Variable(output,bytes.Length);output.Write(bytes);}
    private static void WriteAscii(Stream output,string value)=>output.Write(Encoding.ASCII.GetBytes(value));
    private static void WriteShort(Stream output,int value){output.WriteByte((byte)(value>>8));output.WriteByte((byte)value);}
    private static void WriteInt(Stream output,int value){for(int shift=24;shift>=0;shift-=8)output.WriteByte((byte)(value>>shift));}
    private static void Variable(Stream output,int value)
    {
        if(value<0||value>0x0fffffff)throw new ArgumentOutOfRangeException(nameof(value));
        Span<byte> bytes=stackalloc byte[4];int index=3;bytes[index]=(byte)(value&127);
        while((value>>=7)>0)bytes[--index]=(byte)((value&127)|128);
        output.Write(bytes[index..]);
    }
}
