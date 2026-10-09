import java.lang.invoke.*;
import java.nio.*;
import java.nio.file.*;
import java.util.*;
import jdk.incubator.foreign.*;

public final class ReferenceCheck {
 static void check(boolean b,String msg){if(!b)throw new AssertionError(msg);}
 static void bits(byte[] b,int start,int width,int value){for(int i=0;i<width;i++){int pos=start+i,mask=1<<(7-pos%8);b[pos/8]=(byte)((b[pos/8]&~mask)|(((value>>>(width-1-i))&1)*mask));}}
 public static void main(String[] args)throws Throwable{
  System.load(Path.of(args[0]).toAbsolutePath().toString());
  var linker=CLinker.getInstance();var lookup=SymbolLookup.loaderLookup();
  var shape=MethodType.methodType(int.class,MemoryAddress.class,long.class,MemoryAddress.class);
  var desc=FunctionDescriptor.of(CLinker.C_INT,CLinker.C_POINTER,CLinker.C_LONG_LONG,CLinker.C_POINTER);
  var parse=linker.downcallHandle(lookup.lookup("avs3a_parse_header").orElseThrow(),shape,desc);
  var validate=linker.downcallHandle(lookup.lookup("avs3a_validate_frame").orElseThrow(),shape,desc);
  var prepare=linker.downcallHandle(lookup.lookup("avs3a_prepare_new_decoder").orElseThrow(),shape,desc);
  var crc=linker.downcallHandle(lookup.lookup("avs3a_crc16").orElseThrow(),MethodType.methodType(short.class,MemoryAddress.class,long.class),FunctionDescriptor.of(CLinker.C_SHORT,CLinker.C_POINTER,CLinker.C_LONG_LONG));
  int cases=0,checks=0;
  try(var scope=ResourceScope.newConfinedScope()){
   var input=MemorySegment.allocateNative(16384,8,scope);var output=MemorySegment.allocateNative(48,8,scope);var state=MemorySegment.allocateNative(264,8,scope);
   for(String line:Files.readAllLines(Path.of(args[1]))){
    String[] v=line.split("\t");int seed=Integer.parseInt(v[1]);int[] exp=new int[12];for(int i=0;i<12;i++)exp[i]=Integer.parseInt(v[i+2]);
    byte[] frame=new byte[exp[10]];System.arraycopy(HexFormat.of().parseHex(v[0]),0,frame,0,7);
    for(int i=7;i<frame.length;i++)frame[i]=(byte)((i-7)*73+seed);
    input.asByteBuffer().put(frame);int status=(int)parse.invokeExact(input.address(),7L,output.address());check(status==0,"header "+cases);checks++;
    var fields=output.asByteBuffer().order(ByteOrder.LITTLE_ENDIAN);for(int i=0;i<12;i++){check(fields.getInt(i*4)==exp[i],"field "+i+" case "+cases);checks++;}
    status=(int)validate.invokeExact(input.address(),(long)frame.length,output.address());check(status==0,"crc "+cases);checks++;
    for(long n=0;n<7;n++){status=(int)parse.invokeExact(input.address(),n,output.address());check(status==1,"short header");checks++;}
    status=(int)validate.invokeExact(input.address(),(long)frame.length-1,output.address());check(status==1,"short payload");checks++;
    input.asByteBuffer().put(frame.length-1,(byte)(frame[frame.length-1]^1));
    status=(int)validate.invokeExact(input.address(),(long)frame.length,output.address());check(status==-3,"corrupted payload");checks++;
    input.asByteBuffer().put(frame);state.fill((byte)0xa5);
    status=(int)prepare.invokeExact(state.address(),264L,output.address());check(status==(exp[8]>32767?-2:0),"prepare policy");checks++;
    if(exp[8]>32767){cases++;continue;}
    var st=state.asByteBuffer().order(ByteOrder.LITTLE_ENDIAN);
    check(st.getInt(4)==exp[0]&&st.getInt(12)==exp[1]&&st.getShort(24)==exp[2]&&st.getShort(42)==exp[4]&&st.getInt(52)==exp[8]&&st.getInt(56)==exp[3]&&st.getInt(60)==1,"state fields");checks++;
    for(int i=64;i<264;i++)check(st.get(i)==(byte)0xa5,"managed pointer overwritten");checks+=200;
    check(st.getInt(0)==0xa5a5a5a5&&st.getShort(10)==(short)0xa5a5&&st.getShort(50)==(short)0xa5a5,"opaque/first-frame modified");checks++;
    byte[] bad=frame.clone();bits(bad,23,4,15);input.asByteBuffer().put(bad);status=(int)parse.invokeExact(input.address(),7L,output.address());check(status==-1,"reserved samplerate");checks++;
    bad=frame.clone();bits(bad,44,4,15);input.asByteBuffer().put(bad);status=(int)parse.invokeExact(input.address(),7L,output.address());check(status==-1,"zero bitrate");checks++;
    for(int profile:new int[]{1,2,7}){bad=frame.clone();bits(bad,20,3,profile);input.asByteBuffer().put(bad);status=(int)parse.invokeExact(input.address(),7L,output.address());check(status==-2,"unsupported profile");checks++;}
    bad=frame.clone();bits(bad,17,3,2);input.asByteBuffer().put(bad);status=(int)parse.invokeExact(input.address(),7L,output.address());check(status==-2,"unsupported NN");checks++;
    cases++;
   }
   input.asByteBuffer().put("123456789".getBytes(java.nio.charset.StandardCharsets.US_ASCII));
   int c=((short)crc.invokeExact(input.address(),9L))&65535;check(c==0xa69d,"CRC check value");checks++;
   c=((short)crc.invokeExact(input.address(),0L))&65535;check(c==0xffff,"empty CRC");checks++;
  }
  System.out.println("PASS C_REFERENCE cases="+cases+" assertions="+checks+"; vendor decoder NOT executed");
 }
}
