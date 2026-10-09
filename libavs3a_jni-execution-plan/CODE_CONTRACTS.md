# 代码合同：libavs3a_jni v1

本文件与 [README.md](README.md) 一起执行。这里冻结新的独立SDK v1接口；早期讨论中的方法名/参数只是草图，以本文为准。

**2026-10-09补充：[T02静态复核](T02_STATIC_REVIEW.md)、[ABI合同](frozen/abi-contract.json)、[帧格式合同](frozen/frame-dialect.json)、[参考C](reference-code/avs3a_reference.c)。ABI不再空缺；下文对unverified的失败规则用于防止未来版本/范围错误。**

## 1. 公开Java API

包名固定 `com.inlz.avs3a`，Java 17源码，不公开JNI指针、Media3/FFmpeg类型或协程接口。

```java
public final class Avs3Sdk {
    public static final int API_CONTRACT_VERSION = 1;
    public static final long TIME_UNSET = Long.MIN_VALUE;
    public static final int INPUT_COMPLETE_SINGLE_FRAME = 1;

    // 显式调用，可能执行首次库加载/包内文件校验；UI不得直接阻塞调用。
    public static Avs3Capabilities capabilities(Context context) throws Avs3Exception;
    public static VerifiedModel prepareBundledModel(Context context) throws Avs3Exception;
    public static VerifiedModel prepareModel(Context context, ModelSource source) throws Avs3Exception;
    public static Avs3Session open(VerifiedModel model, long epoch) throws Avs3Exception;
    public static Avs3FrameParser newFrameParser(long epoch) throws Avs3Exception;
}

@FunctionalInterface
public interface ModelSource {
    InputStream open() throws IOException; // SDK关闭每次open返回的流
}

public final class Avs3Session implements AutoCloseable {
    public synchronized QueueResult queueInput(
        byte[] input, int offset, int length, long ptsUs, long epoch, int flags
    ) throws Avs3Exception;
    public synchronized PcmResult receivePcm(byte[] output, int offset, int capacity)
        throws Avs3Exception;
    public synchronized void signalEndOfInput() throws Avs3Exception;
    public synchronized void flush(long newEpoch) throws Avs3Exception;
    @Override public synchronized void close();
}

public final class Avs3FrameParser implements AutoCloseable {
    public synchronized QueueResult queueInput(
        byte[] input, int offset, int length, long ptsUs, long epoch, int flags
    ) throws Avs3Exception;
    public synchronized FrameResult receiveFrame(byte[] output, int offset, int capacity)
        throws Avs3Exception;
    public synchronized void signalEndOfInput() throws Avs3Exception;
    public synchronized void flush(long newEpoch) throws Avs3Exception;
    @Override public synchronized void close();
}
```

**输入显式携带epoch**：flush后，即使调用方误提交旧包，也应返回STALE_EPOCH，不把旧数据当新会话。epoch非负，flush必须严格增加；状态失败后必须close并重新open。

### 1.1 DTO定义

- `QueueResult` enum：`ACCEPTED`、`BACKPRESSURE`，不是消费字节数。
- `PcmResult.Kind` / `FrameResult.Kind`：`NEED_INPUT`、`READY`、`END_OF_STREAM`、`OUTPUT_TOO_SMALL`。
- PcmResult含可空PcmInfo、requiredBytes。READY必须有PcmInfo，NEED_INPUT/EOS不使用旧info。
- PcmInfo不可变：ptsUs、sampleRateHz、channels、samplesPerChannel、byteCount、layout（MONO/STEREO）、flags、epoch。
- FrameResult含不可变EncodedFrameInfo：以上源格式信息，以及frameBytes、payloadOffset、payloadBytes、bitrateBps、channelMode、epoch。
- VerifiedModel不可由用户随意new：包级构造，保存applicationContext、私有文件位置、固定vendorId和已核验SHA。对外不将完整私有路径放入toString/异常。
- Avs3Capabilities不可变：bridgeLoaded、nativeContractVersion、processAbi、parserReady、vendorPresent、vendorFingerprintMatched、vendorAbiVerified、decodeImplementationReady。**不把model已准备当成进程全局布尔值**。

输出byte[]由调用方所有；一次receive只写有效范围，元数据不可变。SDK不会保留Java数组地址。调用方必须在下一次receive复用同一数组前消费/拷贝当前内容。

### 1.2 Java类的隐藏边界

为真正使用包级可见性，NativeBridge、BridgeLoader、NativeCalls、JniCalls、ModelIo、RuntimeVendorVerifier均放在**同一个 `com.inlz.avs3a` 包中，类不声明public**。目录清单中的“internal”是职责分类，不应为了子包访问把JNI类暴露为公共API。

NativeCalls为包级接口，可供同包JUnit测试注入；生产仅使用JniCalls。不得增加公开的setFakeBackend、环境变量Fake开关或发布构建测试后端。

## 2. 固定状态码与JNI签名

### 2.1 状态/错误

```text
Queue:   0 ACCEPTED, 1 BACKPRESSURE
Receive: 0 NEED_INPUT, 1 READY, 2 EOS, 3 OUTPUT_TOO_SMALL
End/Flush: 0 OK
Handle: >0有效；<0固定错误码；0不作为成功

-1000 INVALID_ARGUMENT
-1001 BRIDGE_UNAVAILABLE
-1002 VENDOR_UNAVAILABLE
-1003 VENDOR_SYMBOL_MISSING
-1004 VENDOR_ABI_NOT_READY
-1005 NATIVE_CONTRACT_MISMATCH
-1010 MODEL_MISSING
-1011 MODEL_CORRUPT
-1012 MODEL_IO_FAILED
-1020 UNSUPPORTED_MODE
-1021 INVALID_HEADER
-1022 INPUT_TOO_LARGE
-1023 TRUNCATED_FRAME
-1024 UNSUPPORTED_CONFIG_CHANGE
-1025 FRAME_DIALECT_NOT_READY
-1026 RESYNC_LIMIT
-1030 CLOSED_OR_INVALID_HANDLE
-1031 STALE_EPOCH
-1032 INVALID_STATE
-1033 COMPLETE_SAMPLE_CONTRACT
-1034 HANDLE_KIND_MISMATCH
-1090 NO_MEMORY
-1091 INTERNAL
```

这是自研SDK的命名空间，不是vendor函数的返回码。C++ `status.h` 与Java `Avs3Error`必须生成/对照同一表，不复制后任其漂移。

### 2.2 NativeBridge

```java
final class NativeBridge { // 无静态loadLibrary块
    static native int nContractVersion();
    static native int nProcessAbi(); // 1=arm64-v8a, 2=armeabi-v7a，其它明确不支持
    static native long nCapabilities(); // 编译/合同能力，不做模型准备
    static native String nBuildInfo(); // SDK版本/sourceCommit/合同与编译ABI标识
    static native long nCreate(String modelPath, String verifiedVendorPath, long epoch);
    static native int nQueue(long h, byte[] in, int off, int len, long ptsUs, long epoch, int flags);
    static native int nReceive(long h, byte[] out, int off, int cap, long[] info);
    static native int nEnd(long h);
    static native int nFlush(long h, long epoch);
    static native void nRelease(long h);

    static native long nParserCreate(long epoch);
    static native int nParserQueue(long h, byte[] in, int off, int len, long ptsUs, long epoch, int flags);
    static native int nParserReceive(long h, byte[] out, int off, int cap, long[] info);
    static native int nParserEnd(long h);
    static native int nParserFlush(long h, long epoch);
    static native void nParserRelease(long h);
}
```

RegisterNatives冻结描述符：

| 方法 | descriptor |
| --- | --- |
| nContractVersion/nProcessAbi | `()I` |
| nCapabilities | `()J` |
| nBuildInfo | `()Ljava/lang/String;` |
| nCreate | `(Ljava/lang/String;Ljava/lang/String;J)J` |
| nQueue/nParserQueue | `(J[BIIJJI)I` |
| nReceive/nParserReceive | `(J[BII[J)I` |
| nEnd/nParserEnd | `(J)I` |
| nFlush/nParserFlush | `(JJ)I` |
| nRelease/nParserRelease | `(J)V` |
| nParserCreate | `(J)J` |

Native清单与错误码同时固定在 [frozen/jni-contract.json](frozen/jni-contract.json)。生成/校验代码以该文件为单一来源。

`nBuildInfo`返回固定格式的ASCII key=value行：sdkVersion、sourceCommit、apiContract、jniContract、processAbi、abiVerified、dialectVerified、vendorId。SDK加载时比对Java BuildConfig与native标识；C++在 `.avs3a.build` 段保存同一常量，供CI检查新JNI而无需执行ARM代码。

`JNI_OnLoad`只注册 `com/inlz/avs3a/NativeBridge`，返回JNI_VERSION_1_6；禁止在此加载vendor/读取model/创建decoder。

PCM info固定8个long：PTS、rate、channels、samplesPerChannel、bytes/requiredBytes、layoutId、flags、epoch。

EncodedFrame info固定10个long：PTS、rate、channels、samplesPerChannel、frameBytes/requiredBytes、payloadOffset、payloadBytes、bitrateBps、channelMode、epoch。

layoutId只定义1=MONO、2=STEREO。未支持模式不能借用该枚举伪装。UNKNOWN PTS统一为Long.MIN_VALUE，调用方自行转换框架sentinel。

## 3. Java实现骨架

### 3.1 加载与会话

```java
final class BridgeLoader {
    private static boolean loaded;
    static synchronized void ensureLoaded() throws Avs3Exception {
        if (loaded) return;
        try {
            System.loadLibrary("avs3a_jni");
            if (NativeBridge.nContractVersion() != 1)
                throw new Avs3Exception(Avs3Error.NATIVE_CONTRACT_MISMATCH);
            loaded = true;
        } catch (UnsatisfiedLinkError e) {
            throw Avs3Exception.fromSanitizedFailure(Avs3Error.BRIDGE_UNAVAILABLE, e.getClass().getSimpleName());
        }
    }
}

// Avs3Sdk.open的关键次序
requireVerifiedModel(model);
BridgeLoader.ensureLoaded();
int abi = NativeBridge.nProcessAbi();
VerifiedVendor vendor = RuntimeVendorVerifier.verifyApplicationVendor(model.appContext(), abi);
long result = calls.create(model.internalPath(), vendor.loadPath(), epoch);
if (result <= 0) throw Avs3Exception.fromNative(result);
return new Avs3Session(result, epoch, calls);
```

默认公开异常只保留固定码和受控cause类型，不直接透出含路径/任意输入的Throwable.message；详细诊断另走明确的受控机制。

RuntimeVendorVerifier实现要求：

1. 使用native当前进程ABI，不用SUPPORTED_ABIS[0]代替进程位数。
2. 匹配app nativeLibraryDir，或base/split APK中的 `lib/<abi>/libavs3a_decoder.so`，逐字节SHA与lock比对。
3. Android直接从APK加载时，不能假定nativeLibraryDir中一定有解压文件。接受验证过的APK库路径，但不接受任意用户URI/外部网络路径。
4. native dlopen只使用已验证候选；加载后核对符号归属/加载来源，避免只凭同名SONAME选择错误版本。
5. 缓存只在当前application/source版本/进程ABI范围内有效。构造SDK类不做扫描，调用方显式在后台准备。

### 3.2 接收

```java
public synchronized PcmResult receivePcm(byte[] out, int off, int cap) throws Avs3Exception {
    requireOpen();
    checkRange(out.length, off, cap);
    Arrays.fill(infoScratch, 0L); // 不让NEED_INPUT携带上一次的元信息
    int r = calls.receive(handle, out, off, cap, infoScratch);
    if (r < 0) throw Avs3Exception.fromNative(r);
    switch (r) {
        case 0: return PcmResult.needInput();
        case 1:
            PcmInfo info = validateAndCopyInfo(infoScratch, epoch, cap);
            return PcmResult.ready(info);
        case 2: return PcmResult.endOfStream();
        case 3:
            int need = checkedPcmCapacity(infoScratch[4]);
            return PcmResult.outputTooSmall(need);
        default: throw new Avs3Exception(Avs3Error.INTERNAL);
    }
}
```

validateAndCopyInfo校验：epoch相等、rate有效、mode/layout与channels一致、samples>0、S16有效bytes精确等于samples×channels×2且不溢出；不能只相信native元数组内容。

queueInput：检查range/flags/epoch，返回ACCEPTED或BACKPRESSURE，负数抛固定异常。0长度片段可以作为无副作用no-op，但不追加时间锚；结束必须显式signalEndOfInput。

close：幂等，Java对象标为关闭并且释放唯一handle；后续操作明确CLOSED，不自动重开。会话方法synchronized，native仍有第二层互斥保护。

## 4. 模型管理代码任务

`Avs3ModelStore`只负责调用Context获得applicationContext与私有根；实际IO放到纯Java ModelIo，便于JVM测试。

```text
root = applicationContext.noBackupFilesDir / "avs3a" / vendorId / modelSha
lock(root的固定锁文件)
  target存在 → 检查regular file、大小、SHA；损坏则报错，不覆盖在用版本
  target不存在 → 同目录unique临时文件
    ModelSource.open → 有长度上限复制 → flush+fd.sync → 关闭
    verify size=79930 & SHA
    原子发布为model.bin（锁内无覆盖）
  返回VerifiedModel(applicationContext, target, vendorId, sha)
finally: 仅删除本次未发布临时文件；释放锁
```

- prepareBundledModel的source为assets/avs3a/model.bin，bridge缺少该资源返回MODEL_MISSING。
- prepareModel允许外部ModelSource，但仍复制到SDK私有稳定位置并匹配锁定SHA，不直接让vendor读取会被外部覆盖的临时文件。
- 不使用cacheDir作为长期模型位置，不向stdout打印绝对私有路径。
- 不在close时删除model；不同session可能共享该只读文件。

## 5. C++纯core接口

```cpp
struct FrameConfig {
    int sample_rate;
    int channels;
    int samples_per_channel;
    int bitrate;
    ChannelMode mode;
    // vendor原始配置字段采用T02确认的结构，不在这里猜布局。
    VerifiedHeaderFields vendor_fields;
};

struct EncodedFrame {
    std::vector<uint8_t> bytes;
    FrameConfig config;
    size_t payload_offset;
    size_t payload_size;
    int64_t pts_us;
};
struct OwnedPcm {
    std::vector<int16_t> samples;
    FrameConfig config;
    int64_t pts_us;
    int64_t epoch;
};

class DecoderBackend {
public:
    virtual Status initialize(const FrameConfig&, const VerifiedModelPath&) = 0;
    virtual Status decode(const EncodedFrame&, OwnedPcm&) = 0;
    virtual void destroy() noexcept = 0;
    virtual ~DecoderBackend() = default;
};
```

FakeBackend只在CTest executable中实现。生产Session由vendor backend factory创建，ABI未验证时返回VENDOR_ABI_NOT_READY，不替换为Fake。

`HeaderParser`接口：

```cpp
HeaderResult parse_header(ByteSpan bytes);
// NeedMore(requiredMinimumBytes)、Invalid(reason)、Ready(validatedHeader)
```

Ready必须给出准确frameBytes、payload区间与FrameConfig。只匹配FFF2不能Ready；查表值/CRC未冻结时整个相关dialect不可声明ready。

## 6. Framer、时间锚与背压

建议预算：MAX_INPUT_CHUNK=256KiB，QUEUE_LIMIT=512KiB，MAX_PENDING_PCM=4096字节（首期1024×2×2），每次最多保留一个pending输出。MAX_FRAME_BYTES来自确认的dialect，且必须≤QUEUE_LIMIT−MAX_INPUT_CHUNK。

BitReader：所有读取先检查位数与剩余bit；返回NeedMore/Invalid，不做未检查移位/整数溢出。

Framer输入提交：

1. 先验证range、flags、epoch和状态。
2. 若chunk本身大于MAX_INPUT_CHUNK，返回INPUT_TOO_LARGE，不能永远BACKPRESSURE。
3. pending输出存在/队列无法整包容纳时返回BACKPRESSURE，**0字节消费，时间锚也不消费**。
4. 拷贝为native自有内存，成功后一次性提交。
5. COMPLETE_SINGLE_FRAME要求队列无残留，整个输入恰好一个完整帧；不满足则COMPLETE_SAMPLE_CONTRACT。

Framer读出：

```text
寻找已确认同步模式（搜索有字节预算）
  → HeaderParser
    NeedMore：保留候选；等待输入
    Invalid：按受限重同步策略移动；超过预算报RESYNC_LIMIT
    Ready：检查frameBytes/区间/容量
      不足完整帧：保留
      足够：取出完整EncodedFrame，时间锚只应用一次
```

Timeline：

- queueInput的PTS关联**在该输入区间开始或之后启动的第一个access unit**；不能用后续片段的PTS覆盖已经开始的半帧。
- 后续帧使用anchorPts+累计样本×1e6/rate，使用checked rational算术，不逐帧加21ms。
- 没有锚点可返回TIME_UNSET，SDK不猜TS/HLS起点；裸流调用方可显式传0。
- EOS剩余不完整合法帧为TRUNCATED_FRAME；只忽略规范确认的padding，不任意吞垃圾。

## 7. Session/Registry具体逻辑

状态：WAITING_HEADER→READY→DRAINING→ENDED；错误→FAILED；release→CLOSED。flush允许WAITING_HEADER/READY/ENDED，epoch必须增加，FAILED必须close重开。

Registry：正数单调ID、类型标记Decoder/Parser、shared_ptr；查找/删除只在registry短锁内操作，实际调用持session锁；ID溢出明确失败，不复用旧ID。

```cpp
Status Session::receive(MutableByteSpan output, PcmMetadata& meta) {
    lock_guard lock(mutex_);
    RETURN_IF_FAILED_OR_CLOSED();
    if (!pending_) {
        auto next = framer_.next();
        if (next.invalid()) return fail(next.error());
        if (next.need_more()) {
            if (!input_ended_) return NEED_INPUT;
            if (framer_.has_nonpadding_tail()) return fail(TRUNCATED_FRAME);
            state_ = ENDED;
            return END_OF_STREAM;
        }
        auto frame = next.take();
        if (!is_channel_based_mono_or_stereo(frame.config)) return fail(UNSUPPORTED_MODE);
        RETURN_IF_ERROR(validate_crc_and_sdk_budget(frame));
        // 正式发布支持范围取T08双ABI通过的精确配置白名单，不是所有静态索引组合。
        if (!config_) {
            auto r = backend_->initialize(frame.config, model_);
            if (r.is_error()) return fail(r);
            config_ = frame.config;
        } else if (!same_codec_configuration(*config_, frame.config)) {
            return fail(UNSUPPORTED_CONFIG_CHANGE); // 首期不偷偷复用旧SDK状态
        }
        OwnedPcm pcm;
        auto r = backend_->decode(frame, pcm);
        if (r.is_error()) return fail(r);
        validate_pcm_shape(pcm, frame.config);
        pcm.epoch = epoch_;
        pending_ = std::move(pcm);
    }
    meta = describe(*pending_);
    if (output.size() < meta.bytes) return OUTPUT_TOO_SMALL;
    copy_exact(output, pending_->samples);
    pending_.reset();
    return FRAME_READY;
}
```

OUTPUT_TOO_SMALL重试不得重新decode/推进PTS。公开Parser也保留pending EncodedFrame直到完整复制，不能扩容重试时丢帧。

flush：destroy backend状态、清framer/anchors/pending、inputEnded=false、更新epoch、回WAITING_HEADER。

release：registry先remove拒绝新调用，已有调用持strong ref不悬空；等session锁后destroy。Java调用者仍须在自己的worker停止后close；锁不解决SDK永久卡死/exit。

## 8. Vendor backend：静态合同与运行范围分别门控

```cpp
Status VendorBackend::initialize(const FrameConfig& cfg, const VerifiedModelPath& model) {
    if (!compiled_contract_.ready_for(process_abi())) return VENDOR_ABI_NOT_READY;
    if (!compiled_dialect_.verified()) return FRAME_DIALECT_NOT_READY;
    RETURN_IF_ERROR(validate_supported_config(cfg));
    RETURN_IF_ERROR(model.check_still_readable());
    RETURN_IF_ERROR(loader_.open_verified_library());
    RETURN_IF_ERROR(loader_.resolve_required_functions());
    handle_ = api_.allocate();
    if (!handle_) return NO_MEMORY;
    RETURN_IF_ERROR(abi_.write_verified_initial_fields(handle_, cfg));
    api_.initialize(handle_, model.absolute_path());
    // 未确认错误返回，不能把返回寄存器当status；SO可能exit。
    config_ = cfg;
    return OK;
}

Status VendorBackend::decode(const EncodedFrame& frame, OwnedPcm& output) {
    RETURN_IF_ERROR(validate_crc_and_sdk_budget(frame));
    auto bs = abi_.verified_bitstream_span(handle_);
    auto payload = checked_payload(frame);
    if (payload.size() > bs.size()) return INVALID_HEADER;
    output.samples.resize(checked_samples(config_));
    memcpy(bs.data(), payload.data(), payload.size());
    api_.decode(handle_, output.samples.data());
    api_.reset_bitstream(bs.data());
    output.config = config_;
    output.pts_us = frame.pts_us;
    return OK; // 表示调用流程完成，不宣称SDK能返回所有内部错误
}

void VendorBackend::destroy() noexcept {
    if (handle_) api_.destroy(handle_);
    handle_ = nullptr; // 不再free，Destroy已负责顶层分配
}
```

直接复用reference-code时，参考C局部状态码必须映射到JNI状态码；validate_frame通过后仍检查完整帧边界、signed16 budget和发布支持配置。CRC默认严格，不因原APK漏检而默默跳过。模型/Init风险及T08门禁见T08_INPUTS_AND_ACCEPTANCE.md。

`abi_access`用已确认大小/offset + memcpy读写标量/指针，检查offset+sizeof不溢出且在分配范围内，避免对齐/strict-aliasing问题。未经verified字段绝不能解引用。

函数入口封装只使用冻结的参数/调用约定，Init/Decode的源码返回类型若未由头文件证明，不伪装成可恢复错误协议。对应限制要留在abi-contract和文档中。

loader的库生命周期至少覆盖所有session；不在某个session close时直接dlclose共享库，不尝试运行中热卸载。无SDK函数调用发生在未验证ABI下。

## 9. JNI边界具体要求

- 所有数组先检查null、signed offset/len、`len <= arrayLength - offset`，不要用可能溢出的off+len做唯一判断。
- epoch/flags/range检查先于native内存修改。
- GetByteArrayRegion复制后即拥有独立输入；不跨调用保存JNI指针。
- receive先产出native pending，再SetByteArrayRegion；有Java exception pending时立即退出，不继续调用JNI。
- metadata长度严格PCM=8/Frame=10；OUTPUT_TOO_SMALL填requiredBytes和当前元数据，不写出半帧。
- catch bad_alloc→NO_MEMORY，其他可处理C++异常→INTERNAL；不得让C++异常穿过JNI，也不得宣称捕获SIGSEGV/exit。
- nCreate错误为负数，不用全局lastError；handle0永不分配。void型release的无效/错误kind请求必须不释放其它对象，可记录固定诊断；其余入口返回HANDLE_KIND_MISMATCH。
- JNI错误信息只包含固定码/受控枚举，不输出用户数据/完整路径。

## 10. 必须实现的测试案例

### Java/JUnit（NativeCalls替身）

- public API range/flags/epoch检查、关闭后操作、重复close只有一次release。
- queue backpressure不被当成消费字节数，异常码映射不读取上次结果。
- receive元数据不可变，NEED_INPUT不带旧frame；OUTPUT_TOO_SMALL合法且不越界。
- ModelIo正确复制、短读/超长/坏SHA、并发锁、损坏已发布版本不覆盖、只删自己临时文件。
- 不触发静态loadLibrary，不需要MediaHub或Android播放器依赖。

### CTest

- 每次1字节输入、头跨包、payload跨包、一包多帧、false sync、EOS半帧。
- frameCount/consumeCount、BACKPRESSURE事务性、buffer-too-small重试decodeCount=1。
- 最大chunk/超chunk、队列预算、重同步预算、整数字段溢出。
- epoch递增/旧输入拒绝、flush状态清空、失败态拒绝继续、wrong handle kind。
- shared_ptr/session mutex下release不UAF；测试backend析构次数等于创建次数。
- Timeline长时间累计、多个时间锚、半帧PTS不被后片段覆盖。
- 未确认ABI访问器不能读/写offset0假字段，生产factory永不选择FakeBackend。

### ARM对发布AAR的测试

- 记录实际加载JNI和vendor SHA、sourceCommit、进程ABI，匹配待发布AAR。
- mono/stereo、参考PCM、rate/samples、正确端序/交织、首尾延迟。
- signalEOS、flush/重开、重复创建释放、缺模型/错模型在SDK调用前失败。
- decode-only/Exo逻辑不属于本仓库，SDK只验证PCM和epoch合同。
- 第三方exit风险真实记录；未通过不得由host测试代替。
