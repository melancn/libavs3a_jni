# T02 静态合同补齐记录（2026-10-09）

## 结论与边界

用户已授权定向复核。本次补齐的是**当前锁定二进制、profile=0、channelConfig=0/1、16-bit source precision、neuralType=0/1** 的静态适配合同。不是整个AVS3标准或所有Audio Vivid模式。

- `frozen/abi-contract.json` 与 `templates/abi-contract.json`：两个ABI均 `ready=true`，字段/容量/初始化映射已给出。
- `frozen/frame-dialect.json` 与对应template：`verified=true`，完整支持范围内的帧头、索引表、长度和CRC已给出。
- ready/verified均标记为**静态证据 + reference C验证**，不表示vendor实际音频解码/设备/授权已通过。
- `vendor.lock.json.redistributionApproved`保持false。真实fixture、独立PCM、双ABI设备及算法首尾延迟仍属T08/发布门禁。

## 1. 如何修正ARMv7误判

旧dump把Thumb代码按ARM指令解释，因此无效。此次使用GNU objdump针对.text的Thumb模式，并交叉核对原APK的32位调用点：

```text
Avs3AllocDecoder: 0x6014，mov r0,1; mov r1,164; branch calloc veneer
Avs3InitDecoder:  0x601c，model FILE存到 +160；bitstream最终存到 +72
Avs3Decode:       0x57b4，r0=decoder，r1=PCM；读channels +24、samples +48
Avs3DecoderDestroy: 0x6608，metadata +156，模型64/68，bitstream +72，最后free顶层
```

原APK ARMv7：init `0xb4440`、decode `0xb4480`、close `0xb4a24`、flush `0xb4a68`；实际调用Init在`0xb490c`、Decode在`0xb4956`、Reset在`0xb49b8`。

函数dynsym的低位标记Thumb，生产dlsym函数指针**不得清bit0**。强制Thumb文本中的literal pool/跳转表仍是数据，不把它们当指令推断。

## 2. Decoder外层内存视图

| 区域 | ARM64 | ARMv7 |
| --- | --- | --- |
| 顶层分配字节数 | 264 | 164 |
| 公共标量前缀 | 0..63 | 0..63 |
| base/hyper模型指针 | 64 / 72 | 64 / 68 |
| bitstream指针 | 80 | 72 |
| HOA / MC / stereo / monoAux | 88 / 96 / 104 / 112 | 76 / 80 / 84 / 88 |
| 16个core指针数组 | 120..247 | 92..155 |
| metadata指针 | 248 | 156 |
| model FILE指针 | 256 | 160 |

公共字段偏移：firstFrame 0；sampleRate 4；sourceBits 8；bitrate 12；bitrateCopy 16；channelConfig 20；channels 24；objects 26；object/bed bitrate 28/32；mixedType/mixedFlag 36/38；LFE 40；decoderFormat 42；option44 44；HOA order 46；frameSamples 48；payloadBits 52；neuralType 56；modelType 60。

2..3、10..11、50..51保留分配器零值，不宣称这些字节的厂商语义。option44只确认原调用方写0，不臆造名称。嵌套私有结构为opaque，桥不访问。

`reference-code/avs3a_vendor_layout.h`给出完整外层覆盖和offsetof/sizeof断言；实际桥推荐memcpy/字节访问，不把这份逆向视图说成厂商源码头文件。

已用NDK clang分别以 `aarch64-linux-android24`、`armv7a-linux-androideabi24` 编译检查这些断言。

## 3. Bitstream容量

ARMv7 ResetBitstream `0x46c4`明确：

1. 循环清零起始的**12300字节**；
2. 向`base+12300`存一个零int32。

两ABI的side-bit reader持续从`+12300`读写next-bit cursor；Init分配总计12304字节。因此确认：

```c
struct { uint8_t payload[12300]; int32_t next_bit; }; // size 12304
```

这不是把sizeof盲当payload容量。支持语法的最大transport frame为5120字节，payload最大5113，仍低于物理容量。

## 4. 精确帧头（当前范围）

MSB-first，bit offset从sync首bit起算：

| 字段 | offset | width | 约束 |
| --- | --- | --- | --- |
| sync | 0 | 12 | 0xfff |
| codec id | 12 | 4 | 2 |
| reserved flag | 16 | 1 | 0 |
| neural type | 17 | 3 | 0=Hyper，1=HyperLc；其它拒绝 |
| profile | 20 | 3 | 0；mixed/object/HOA不在本合同 |
| sample-rate index | 23 | 4 | 0..8 |
| CRC high | 27 | 8 | 校验值高8位 |
| channel config | 35 | 7 | 0=mono，1=stereo；其它拒绝 |
| precision index | 42 | 2 | 1=16bit；0/2未纳入，3保留 |
| bitrate index | 44 | 4 | 对应表必须非0 |
| CRC low | 48 | 8 | 校验值低8位 |

总计56bit=7字节。原SDK FILE接口会先读9字节后回退文件位置，但内存parser只需7字节头，不能误把9当payload offset。

原APK在ARM64的`0x135090..0x13528c`和ARMv7的`0xb449e..0xb4636`给出了取位宽度和字节长度计算。

采样率表9项（两ABI逐项相同）：

```text
192000,96000,48000,44100,32000,24000,22050,16000,8000
```

Mono表16项：

```text
16000,32000,44000,56000,64000,72000,80000,96000,
128000,144000,164000,192000,0,0,0,0
```

注意第10项是**164000，不是160000**。

Stereo表16项：

```text
24000,32000,48000,64000,80000,96000,128000,144000,
192000,256000,320000,0,0,0,0,0
```

表大小来自ELF dynsym st_size：采样率36字节，mono/stereo各64字节，而非猜表长度。

## 5. 长度与初始化

```text
frameBits   = trunc(f32(f32(bitrate/sampleRate) * 1024))
payloadBits = frameBits - 56
payloadBytes= (payloadBits + 7) / 8    // 整数除法
frameBytes  = 7 + payloadBytes
```

必须保留binary32计算/截断规则，不把payloadBits替换成payloadBytes×8。余下的低位padding不计入payloadBits，但整字节计入CRC。

`avs3a_prepare_new_decoder`给出fresh calloc后、Init之前的字段写入参考。所有managed指针、opaque bytes及firstFrame均不由该函数覆盖；Init设firstFrame=1，Decode清成0。modelType由SDK强制1。每帧后仅ResetBitstream，seek/配置变化再重建整个SDK。

Stereo bitrate≤32000时SDK初始化自己的MCR分支标志，不要由桥强制改成普通StereoDec。

## 6. CRC不是CCITT-FALSE

两ABI完整256项表一致，可逐项由poly0x1021生成。精确迭代：

```c
crc = 0xffff;
for each payload byte:
    crc = ((crc << 8) ^ table[(crc >> 8) & 255] ^ byte) & 0xffff;
```

不是 `table[(crc>>8)^byte]`！初值ffff、非反射、不追加零字节、xorout=0，结果取低16位。

- 空串：ffff。
- ASCII `123456789`：**a69d**。
- coverage：仅payload的ceil字节，包括最后字节的未使用低位；不包括7字节头。
- stored CRC：`getBits(27,8)<<8 | getBits(48,8)`。

SDK ReadBitstream在ARM64 `0x6604..0x6674`、ARMv7 `0x4bc2..0x4c2e`验证此比较。原APK自有decode回调读取CRC但不检查；本参考validator选择严格校验。若未来需要兼容错误CRC源，必须显式定义策略，不能静默跳过。

## 7. 新增的安全限制

`GetAvailableBits`的返回值在Mono/MCR及`StereoBitsAllocation`中被按signed16使用（例如ARM64 `0x14560 sxth w8,w0`）。因此初始化参考保守拒绝payloadBits>32767。

- Stereo 8000 Hz / 320000 bps得到40904 payload bits：语法可解析，但**不放行SDK初始化**。
- 该组合的两个neuralType均在测试里明确拒绝。
- 解码政策最大frame为4096字节；transport parser仍能识别5120字节以正确报告不支持。
- 表值存在不等于每个组合都可安全实播，最终支持矩阵仍需T08。

## 8. 本次实际验证

- 直接提取并对比两ABI的全部采样率/码率/CRC表。
- CRC256项与poly1021逐项一致。
- 原SDK、两个ABI的APK调用点交叉核对字段与header。
- 兼容布局头通过两个Android目标的C11/C++17编译/静态断言。
- 将**自研参考C**编为Windows x64 DLL，经JDK17 FFM运行：**414组transport组合、95634条断言通过**，覆盖头部字段、CRC、截断/损坏、初始化字段和指针区不覆盖；其中2组超过SDK signed16预算被拒绝。

没有执行vendor解码器。合成payload是用于帧格式测试的随机字节，**不是音频fixture，禁止喂给Avs3Decode**。它们不能代替独立参考PCM或双ABI设备验证。

复现命令、函数前置条件及返回码见 [reference-code/README.md](reference-code/README.md)。全部文本证据已纳入 evidence-manifest；使用 verify-contracts.cjs 校验交接一致性。

## 9. 仍未完成但不再混入静态ABI阻塞的事项

- 实际mono/stereo音频fixture及独立参考PCM。
- SDK首帧priming、尾部PCM/延迟、异常码流和内存/性能动态验证。
- 真实ARM64/ARMv7进程的发布AAR报告。
- 分发授权，仍为未批准；16KB vendor兼容性仍未验证。

真实输入/延迟/报告与授权的收集、验收步骤已细化为 [T08_INPUTS_AND_ACCEPTANCE.md](T08_INPUTS_AND_ACCEPTANCE.md)，附空模板及可运行PCM比较器，不代表真实输入已提供。

**当前可用上述合同继续实现T03—T07并准备full本地测试构建；不能据此正式发布full或声称声音已验证。**
