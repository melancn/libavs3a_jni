package com.inlz.avs3a.demo;

import androidx.media3.common.*;
import androidx.media3.common.util.ParsableByteArray;
import com.inlz.avs3a.demo.media3.Avs3Format;
import com.inlz.avs3a.demo.media3.mp4.BoxParser;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;
import java.lang.reflect.*;
import java.nio.*;
import static org.junit.Assert.*;

@RunWith(RobolectricTestRunner.class)
@Config(sdk=28, manifest=Config.NONE)
public class Avs3Mp4BoxTest {
    // Synthetic CONTAINER metadata only. Never passed to the vendor decoder.
    private static Format parse(int configLength) throws Exception {
        byte[] config = {0x22,0x20,0x04,0x01,(byte)0x80,0x40,0x00};
        ByteBuffer b = ByteBuffer.allocate(36 + 8 + configLength + 20).order(ByteOrder.BIG_ENDIAN);
        b.putInt(b.capacity()).putInt(Avs3Format.TYPE_AV3A).put(new byte[6]).putShort((short)1);
        b.putLong(0).putShort((short)6).putShort((short)16).putInt(0).putInt(48000 << 16);
        b.putInt(8+configLength).putInt(Avs3Format.TYPE_DCA3).put(config,0,configLength);
        b.putInt(20).putInt(0x62747274).putInt(0).putInt(384000).putInt(384000);
        Class<?> dataClass = Class.forName(BoxParser.class.getName() + "$StsdData");
        Constructor<?> ctor = dataClass.getDeclaredConstructor(int.class); ctor.setAccessible(true);
        Object out = ctor.newInstance(1);
        Method method = BoxParser.class.getDeclaredMethod("parseAudioSampleEntry", ParsableByteArray.class,
                int.class,int.class,int.class,int.class,String.class,boolean.class,DrmInitData.class,dataClass,int.class);
        method.setAccessible(true);
        try { method.invoke(null,new ParsableByteArray(b.array()),Avs3Format.TYPE_AV3A,0,b.capacity(),2,"zho",false,null,out,0); }
        catch (InvocationTargetException e) { throw (Exception)e.getCause(); }
        Field field = dataClass.getDeclaredField("format"); field.setAccessible(true);
        return (Format)field.get(out);
    }
    @Test public void exposesAv3aTrackAndPreservesDca3() throws Exception {
        Format f = parse(7);
        assertEquals(Avs3Format.MIME_TYPE,f.sampleMimeType);
        assertEquals(6,f.channelCount); assertEquals(48000,f.sampleRate);
        assertEquals(384000,f.averageBitrate); assertEquals("av3a",f.codecs);
        assertEquals(7,f.initializationData.get(0).length);
    }
    @Test(expected=ParserException.class) public void rejectsTruncatedDca3() throws Exception { parse(6); }
}
