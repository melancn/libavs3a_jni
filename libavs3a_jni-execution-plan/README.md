# libavs3a_jni：可独立执行的项目实施计划

状态：已补齐当前范围T02静态合同及参考C代码；SDK工程/真实解码/CI尚未完成。更新：2026-10-09。

**T02补齐结果：[T02_STATIC_REVIEW.md](T02_STATIC_REVIEW.md)**。两个ABI与支持范围内的帧头/CRC已静态闭合；真实PCM、首尾延迟、设备与授权门禁不变。

## 0. 给执行者的指令

在一个**新的独立Git仓库 `libavs3a_jni`** 中实现本计划。可以只复制本目录作为交接包；不需要MediaHub源码、历史对话或原APK工程。

按以下顺序阅读并执行：

1. 本文件：范围、输入、目录、任务和完成条件。
2. [CODE_CONTRACTS.md](CODE_CONTRACTS.md)：固定Java/JNI/C++合同与关键算法。
3. [BUILD_AND_CI.md](BUILD_AND_CI.md)：Gradle/CMake/脚本/Actions的确定配置。
4. [frozen/vendor.lock.json](frozen/vendor.lock.json)、[frozen/abi-facts.json](frozen/abi-facts.json)、[frozen/jni-contract.json](frozen/jni-contract.json)：冻结输入、已知/未知边界与协议单一来源。
5. [reference-code/README.md](reference-code/README.md)：可编译实现、合同自检与测试复现。
6. [T08_INPUTS_AND_ACCEPTANCE.md](T08_INPUTS_AND_ACCEPTANCE.md)：真实fixture、延迟、双ABI报告和授权输入合同。
7. `frozen/evidence/`：已完成分析的文本快照，仅用于落实ABI/头部合同，默认不作为公开源码或SDK发布内容。

用户已于2026-10-09授权并完成本次定向复核。当前锁定SHA的静态合同与新证据已经携带，正常实施无需重复扫描；这不是永久禁止纠正错误或对新版本另行授权分析。输入SHA校验、新产物ELF检查和ARM运行测试照常执行。

禁止：

- 修改MediaHub、FFmpeg、Media3，或把它们添加为本项目依赖。
- 把Test/Fake decoder编入发布库，或用生成的静音PCM伪装解码成功。
- 猜测未确认的结构偏移、表长度、头部字段或算法延迟。
- 仅因构建通过就宣称真实解码通过；仅因JNI是16KB对齐就宣称vendor也支持。
- 未获授权公开vendor SO、model、真实媒体样本或私有证据。

## 1. 项目成果

独立Android SDK完成以下链路：

```text
调用方提供AV3A elementary bytes + PTS/epoch
  → Java Avs3Session
  → JNI事务输入/有界framer/安全状态机
  → 经过验证的vendor ABI适配
  → libavs3a_decoder.so + model.bin
  → S16 little-endian交织PCM + 元信息
```

不包含网络、TS/MP4/HLS整体解复用、AudioTrack、Renderer、UI或播放器时钟。头部/framer输出可以供外部提取器使用，但不执行真实解码。

产物：

- `com.inlz.avs3a:avs3a-sdk-bridge:<version>`：Java API + 双ABI自研JNI，不含vendor SO/model。
- `com.inlz.avs3a:avs3a-sdk:<version>`：通过输入、ABI、授权和ARM门禁后的full AAR。
- Maven目录zip、JNI符号包、`sdk-manifest.json`、`SHA256SUMS`。
- 独立测试app消费**发布AAR**，不是依赖同仓库debug源码产物冒充release实测。

首期真实解码只支持确认过的channel-based mono/stereo，输出按源采样率保留，**不重采样、不下混、不作对象/HOA渲染**。mono/stereo不等于所有profile/位深/码率均支持，仅放行T02与真实样本确认过的dialect。

## 2. 冻结输入与限制

### 2.1 二进制输入

本包不包含vendor二进制；本次定向复核只读分析了锁定输入，没有修改它们。使用者另行提供三个文件，其逻辑位置和SHA已列在vendor.lock：

```text
vendor-inputs/arm64-v8a/libavs3a_decoder.so
vendor-inputs/armeabi-v7a/libavs3a_decoder.so
vendor-inputs/model.bin
```

模型79,930字节；decoder分别259,992和194,176字节。以lock逐项核验，不依据文件名信任输入。

### 2.2 可直接采用的事实

- 两ABI目标为Android ARM；内部算法包含神经codec，model是必需参数。
- Init参数为decoder和模型绝对路径；Decode参数为decoder和PCM缓冲。
- 当前观察的每帧样本数为1024/声道，合成输出为交织S16。
- Destroy包含释放顶层decoder，不能再free。
- SDK的Init/Decode未建立可靠错误返回合同，不能读返回寄存器判断成功。
- 模型打不开/部分异常路径可能exit进程，Java/C++ catch均不能兜住。
- ARM64顶层分配264字节、bitstream指针偏移80、metadata指针偏移248是已知证据。
- 两ABI bitstream为12300字节payload数组 + offset12300的int32 cursor，总大小12304；容量已通过分开的清零逻辑和reader访问确认。
- ARMv7顶层164字节，bitstream/metadata/modelFile指针分别在72/156/160；通过Thumb代码和原APK调用交叉核对。
- vendor LOAD对齐为4KB，16KB页兼容未验证。

### 2.3 已闭合与仍需动态验收的边界

`frozen/abi-contract.json`、`frozen/frame-dialect.json`及templates副本已经静态ready/verified，字段覆盖整个外层状态，指针目标为SDK管理的opaque结构。当前格式范围：profile0、mono/stereo、16-bit source precision、NN type0/1，超出范围明确拒绝。

旧ARMv7错误模式dump已由新Thumb证据替换。C布局经过两个Android目标编译断言；参考C的414组transport测试通过。它们**不是**真实音频解码测试。

仍需T08：真实fixture/独立PCM、算法priming/tail、设备上的实际解码与支持矩阵。授权仍未批准，16KB vendor兼容仍未验证。

表中每个rate/bitrate组合不等于都可实播；特别是signed16位预算安全限制排除了stereo 8000Hz/320000bps。详见静态复核报告。
## 3. 目标目录与文件职责

```text
libavs3a_jni/
  .github/workflows/android-ci.yml
  .github/workflows/release-sdk.yml
  settings.gradle.kts / build.gradle.kts / gradle.properties
  gradle/wrapper/* / gradlew / gradlew.bat
  native/
    CMakeLists.txt
    include/avs3a/{status.h,types.h,backend.h,header_parser.h,framer.h,timeline.h,session.h,registry.h}
    src/{header_parser.cpp,framer.cpp,timeline.cpp,session.cpp,registry.cpp}
    src/{abi_access.cpp,vendor_backend.cpp,vendor_loader_android.cpp,jni_bridge.cpp,build_info.cpp}
    avs3a_jni.map
    generated/                      构建目录生成，不手工填magic number
    vendor/abi-contract.json        复制本包已完成的scoped static contract
    vendor/frame-dialect.json       复制本包已完成的profile0 mono/stereo合同
    tests/{test_main.cpp,*_test.cpp}
    tests/support/fake_backend.*    仅CTest可执行程序使用
  sdk/
    build.gradle.kts / consumer-rules.pro
    src/main/AndroidManifest.xml
    src/main/java/com/inlz/avs3a/
      Avs3Sdk.java / Avs3Session.java / Avs3FrameParser.java
      VerifiedModel.java / Avs3ModelStore.java / Avs3Capabilities.java
      Avs3Exception.java / Avs3Error.java / QueueResult.java
      PcmResult.java / PcmInfo.java / FrameResult.java / EncodedFrameInfo.java
      NativeBridge.java / BridgeLoader.java / NativeCalls.java / JniCalls.java
      ModelIo.java / RuntimeVendorVerifier.java  （以上隐藏实现为同包package-private）
    src/full/jniLibs/{abi}/libavs3a_decoder.so
    src/full/assets/avs3a/model.bin
    src/test/java/*
  smoke-test/
    build.gradle.kts
    src/main/AndroidManifest.xml
    src/main/java/*                  最小诊断页面，不绑定播放器框架
    src/androidTest/java/*           针对发布AAR的ARM测试
    src/androidTest/assets/fixtures/ 受控真实样本/参考PCM，不默认公开
  ci/
    verify-wrapper-and-locks.py
    verify-vendor-inputs.py
    generate-contracts.py
    verify-aar.py / make-ci-dist.py
    verify-device-evidence.py
  vendor/manifest.lock.json          从frozen/vendor.lock复制
  protocol/jni-contract.json         从frozen/jni-contract复制
  reference/*                       从本包frozen复制，发布包不带这些证据
  docs/{api.md,abi.md,errors.md,compatibility.md,ABI_GAPS.md}
  EXECUTION_STATUS.md
  THIRD_PARTY_NOTICES.md / README.md
```

Native纯core依赖标准C++；Android JNI/loader和vendor backend为平台层。Java公开API不引入Media3/FFmpeg/协程/MediaHub，使用普通DTO和同步方法。

## 4. 任务表：逐项执行并记录

先复制 [执行状态模板](templates/EXECUTION_STATUS.md)。每项完成后更新 `EXECUTION_STATUS.md`：状态、改动文件、执行命令、实际结果、剩余限制。未跑的命令写NOT_RUN，不能写“预计通过”。

### T00 输入/工作区准备

- 建立独立仓库目录；复制本计划、frozen和workflow模板；不提交vendor数据。
- 添加ignore：vendor-inputs、sdk/src/full/jniLibs、full模型、fixtures、私有reference、build、local.properties。
- 固定版本0.1.0-SNAPSHOT起步；api/jni contract均为1。
- 验收：仓库中无SO/model/密钥；所需参考文本可以本地读取，不引用MediaHub路径。

### T01 Gradle/CMake骨架

实现BUILD_AND_CI中的root、sdk、smoke-test、CMake与publication。

- 宿主构建只运行纯C++测试；Android构建目标为两ABI的libavs3a_jni.so。
- JNI内部类不能在静态初始化自动loadLibrary。
- bridge/full源集严格分离。
- 验收：配置任务/host-test目标可运行；不需要vendor二进制即可编译bridge。

### T02 冻结ABI与frame-dialect（当前范围静态完成）

直接采用本包frozen和templates中已更新的合同、reference-code及T02_STATIC_REVIEW；不要重新把已确认字段填unknown，也不要扩大到未支持profile。

- 将abi-contract/frame-dialect复制为独立仓库的native/vendor输入。
- 生成器仍需验证字段/指针宽度/表边界/完整覆盖，并保留未来错误合同的拒绝测试。
- 参考header/parser/CRC代码可以复用，但不是完整JNI/SDK实现。
- 保持runtimeValidation=NOT_RUN；算法延迟、真实PCM、授权另交T08/T09，不能因静态ready而提前发布。
### T03 Java公开API与模型管理

按CODE_CONTRACTS的签名和语义实现，NativeCalls可注入测试替身；真实JniCalls不依赖任何播放器。

- offset/length、epoch、model、结果元数据检查。
- 模型发布不可变，已存在损坏版本不能在活跃会话中覆盖。
- 验收：本地JUnit覆盖异常、幂等close、线程串行、buffer语义和模型文件测试，无需加载ARM库。

### T04 Native纯core

实现BitReader、Timeline、BoundedFramer、Session和Registry，使用测试专属FakeBackend验证状态机。

- 事务queue、pending PCM、buffer-too-small、输入结束、flush、句柄生命周期。
- 必须能证明半帧+背压不会形成零进展死循环。
- 验收：CTest覆盖CODE_CONTRACTS中的核心断言；FakeBackend不进入Android库源列表。

### T05 真实header/framer

将T02的frame-dialect实现为真实HeaderParser，保留NeedMore与Invalid的区别。

- 禁止永久返回NeedMore、只搜sync就放行或用测试parser替代真实实现。
- 未闭合的dialect明确parserReady=false；不是支持任何AV3A。
- 验收：冻结header用例与分片/多帧/时间锚边界通过，格式变化/对象/HOA按策略拒绝。

### T06 Android loader、vendor backend、JNI

- RuntimeVendorVerifier确认进程ABI与锁定输入；不要只信SONAME。
- 只有abiReady和dialectReady满足时才允许执行vendor函数。
- JNI全部边界、异常、数组和handle语义按CODE_CONTRACTS实现。
- 验收：双ABI bridge编译，JNI签名与Java匹配；未就绪返回真实原因，不走Fake。

### T07 分发与校验脚本

真实实现verify/make-dist/generate等脚本，不保留空返回0。

- bridge AAR严格不带vendor/model；full必须完整且hash正确。
- POM/module/版本/双ABI齐全，manifest区分构建与设备验证。
- 验收：对故意删ABI、改model、混入renderer、伪造报告等负例脚本退出非0。

### T08 ARM发布二进制测试

在T02/T05/T06闭合后执行，不能用x86 JVM代替。具体输入字段、来源审核、样本比较和授权流程见 [T08_INPUTS_AND_ACCEPTANCE.md](T08_INPUTS_AND_ACCEPTANCE.md)；空模板为阻塞状态，不是通过报告。

- 先生成并发布fullRelease到本地CI Maven仓库。
- smoke-test以Maven坐标消费这个fullRelease AAR，不用project(":sdk")。
- 分别过滤安装ARM64/ARMv7，测试报告必须记录实际native进程ABI；64位设备不支持32位时不得把两次64位运行算两ABI。
- 先验证测试APK中的JNI/vendor SHA与full AAR完全一致，再比较PCM/样本数/延迟与参考。
- 真实golden需有独立来源，不用新实现输出自身生成expected来“验证”自己。
- 验收：两ABI匹配报告；模型坏/错误epoch/释放等正常可控失败通过；exit风险按独立测试进程策略报告。

### T09 GitHub Actions与发布

实现模板引用的所有任务/脚本，再安装workflow。未获用户明确授权不创建远程仓库、不push、不上传vendor。

- PR/main常规CI仅bridge与host/JVM测试。
- full只在受保护的发布工作流中恢复vendor、校验授权/ABI/设备证据。
- 不在不可信PR/自托管设备runner暴露vendor、签名、发布凭据。
- 验收：bridge CI真实通过；full release必须满足所有门禁，否则明确失败。

## 5. 完成矩阵

| 项目 | bridge交付必需 | full正式交付必需 |
| --- | --- | --- |
| Java/JNI API、状态机、buffer安全 | 是 | 是 |
| 双ABI JNI编译与打包校验 | 是 | 是 |
| 完整ABI与header规则 | 可以未完成，但能力必须false | 是 |
| vendor/model输入与授权 | 不包含 | 是 |
| 两ABI真实PCM/生命周期验证 | 不宣称已做 | 是 |
| 不修改/依赖MediaHub、FFmpeg、Media3 | 是 | 是 |
| private证据/凭据不进分发 | 是 | 是 |

当前范围的静态字段/格式事实已经补齐，可以继续工程/API/CI；**真实PCM、延迟、双ABI运行及授权事实仍未具备**。不得为了让计划“全绿”而把ABI_NOT_READY改成成功。

## 6. 交接与提交粒度

建议提交：`build skeleton`、`api/model`、`core state machine`、`verified contract`、`vendor backend/JNI`、`packaging checks`、`device harness`、`actions/release gates`。

提交前附实际命令输出摘要，列出哪些测试因为SDK实现、设备、真实媒体或授权输入缺失未运行。最终README提供最短Java使用示例、bridge/full区别、API表、错误码、线程与buffer所有权、16KB/exit限制。
