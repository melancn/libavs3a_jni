package com.inlz.avs3a.demo;

import androidx.media3.common.*;
import androidx.media3.common.util.ParsableByteArray;
import androidx.media3.extractor.*;
import com.inlz.avs3a.demo.media3.Avs3Format;
import com.inlz.avs3a.demo.media3.mp4.Mp4Extractor;
import org.junit.*;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;
import java.io.*;
import java.util.*;
import static org.junit.Assert.*;

/** Opt-in private sample test. Container extraction only; never invokes the vendor. */
@RunWith(RobolectricTestRunner.class)
@Config(sdk=28, manifest=Config.NONE)
public class RealMp4ExtractionTest {
    private static final class Track implements TrackOutput {
        Format format;
        int samples;
        long lastTimeUs;
        final ByteArrayOutputStream packet = new ByteArrayOutputStream();
        final byte[] scratch = new byte[16384];
        @Override public void format(Format f) { format=f; }
        private boolean avs3() { return format!=null && Avs3Format.MIME_TYPE.equals(format.sampleMimeType); }
        @Override public int sampleData(DataReader input,int length,boolean allowEnd,int part) throws IOException {
            int n=input.read(scratch,0,Math.min(length,scratch.length));
            if (n<0 && !allowEnd) throw new EOFException();
            if(n>0 && avs3()) packet.write(scratch,0,n);
            return n;
        }
        @Override public void sampleData(ParsableByteArray data,int length,int part) {
            if(avs3()) packet.write(data.getData(),data.getPosition(),length);
            data.skipBytes(length);
        }
        @Override public void sampleMetadata(long timeUs,int flags,int size,int offset,CryptoData crypto) {
            samples++; lastTimeUs=timeUs;
            if(avs3()) {
                byte[] bytes=packet.toByteArray();
                assertEquals(size,bytes.length); assertEquals(0,offset); assertNull(crypto);
                assertEquals(0xff,bytes[0]&255); assertEquals(0xf2,bytes[1]&255);
                assertEquals(1024,size); packet.reset();
            }
        }
    }
    private static final class Output implements ExtractorOutput {
        final Map<Integer,Track> tracks=new LinkedHashMap<>();
        SeekMap seek;
        @Override public TrackOutput track(int id,int type) { return tracks.computeIfAbsent(id,k->new Track()); }
        @Override public void endTracks() {}
        @Override public void seekMap(SeekMap value) { seek=value; }
        Track avs3() { return tracks.values().stream().filter(Track::avs3).findFirst().orElse(null); }
    }
    private static DefaultExtractorInput input(RandomAccessFile f) throws IOException {
        return new DefaultExtractorInput((b,o,n)->f.read(b,o,n),f.getFilePointer(),f.length());
    }
    private static void readSamples(Mp4Extractor extractor,Output out,RandomAccessFile file,int target) throws IOException {
        DefaultExtractorInput in=input(file); PositionHolder seek=new PositionHolder();
        for(int i=0;i<10000;i++) {
            int result=extractor.read(in,seek);
            if(result==Extractor.RESULT_SEEK) { file.seek(seek.position); in=input(file); }
            if(out.avs3()!=null && out.avs3().samples>=target) return;
            if(result==Extractor.RESULT_END_OF_INPUT) break;
        }
        fail("AVS3 samples were not emitted");
    }
    @Test public void extractsTargetAvs3AndSeeksBeyondFourGiB() throws Exception {
        String path=System.getProperty("avs3.test.mp4");
        Assume.assumeTrue("Supply -Pavs3TestMp4=<private target MP4>",path!=null);
        try(RandomAccessFile file=new RandomAccessFile(path,"r")) {
            assertTrue(file.length()>0x1_0000_0000L);
            Mp4Extractor extractor=new Mp4Extractor(); Output output=new Output();
            assertTrue(extractor.sniff(input(file))); file.seek(0); extractor.init(output);
            try {
                readSamples(extractor,output,file,8);
                Track avs=output.avs3(); assertNotNull(avs);
                assertEquals(6,avs.format.channelCount); assertEquals(48000,avs.format.sampleRate);
                assertEquals(384000,avs.format.averageBitrate); assertFalse(avs.format.initializationData.isEmpty());
                assertTrue(output.seek.isSeekable()); assertTrue(output.tracks.size()>=4);
                SeekPoint point=output.seek.getSeekPoints(output.seek.getDurationUs()*9/10).first;
                assertTrue("co64 offset must survive beyond 4 GiB",point.position>0xffff_ffffL);
                int before=avs.samples;
                extractor.seek(point.position,point.timeUs); file.seek(point.position);
                readSamples(extractor,output,file,before+8);
                assertTrue(avs.lastTimeUs>output.seek.getDurationUs()*8/10);
            } finally { extractor.release(); }
        }
    }
}
