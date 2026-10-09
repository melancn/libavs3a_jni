# 构建、脚本与Actions实施细则

依赖 [README.md](README.md) 和 [CODE_CONTRACTS.md](CODE_CONTRACTS.md)，不依赖MediaHub源码。本包templates只作为新独立仓库的初始文件，不在当前工作区执行构建。

## 1. 固定工具链与模板复制表

JDK17、Gradle9.4.0、AGP9.1.0、compileSdk36、minSdk24、NDK27.2.12479018、CMake3.22.1、C++17，Android ABI为arm64-v8a/armeabi-v7a。

| 本包模板 | 独立仓库目标 |
| --- | --- |
| templates/settings.gradle.kts | settings.gradle.kts |
| templates/root.build.gradle.kts | build.gradle.kts |
| templates/gradle.properties | gradle.properties |
| templates/sdk.build.gradle.kts | sdk/build.gradle.kts |
| templates/native.CMakeLists.txt | native/CMakeLists.txt |
| templates/avs3a_jni.map | native/avs3a_jni.map |
| templates/abi-contract.json | native/vendor/abi-contract.json（已静态ready，受scope限制） |
| templates/frame-dialect.json | native/vendor/frame-dialect.json（已静态verified，受scope限制） |
| templates/consumer-rules.pro | sdk/consumer-rules.pro |
| templates/android-ci.yml | .github/workflows/android-ci.yml |
| frozen/vendor.lock.json | vendor/manifest.lock.json |
| frozen/jni-contract.json | protocol/jni-contract.json |
| templates/EXECUTION_STATUS.md | EXECUTION_STATUS.md |
| templates/fixture-manifest.json | private-validation/fixture-manifest.json（空输入，不能验收） |
| templates/device-report.json | private-validation/devices/<abi>/report.json（NOT_RUN骨架） |
| templates/authorization-record.json | private-validation/legal/authorization.json（未提供授权） |

源文件必须按CODE_CONTRACTS实现，不能为了让模板链接通过用返回0的空文件代替。

Wrapper由可信的固定Gradle版本生成，分发URL为 `https://services.gradle.org/distributions/gradle-9.4.0-bin.zip`；从官方SHA发布获取校验值，写入distributionSha256Sum。不要从聊天推测SHA，也不要先执行未验证下载内容。

AGP 9.1可能自带Kotlin能力，但本SDK公开/实现优先Java，不依赖协程或播放器框架；若构建系统引入额外依赖，其元数据必须正确发布，不能靠本地AAR丢弃POM掩盖。

## 2. SDK Gradle模板必须保持的行为

- `bridge`、`full`分发flavor，debug/release为正常build type。
- 两ABI都通过externalNativeBuild编译新JNI；不链接vendor、不引用FFmpeg/Media3。
- sdkVersion/sourceCommit同时传给Java BuildConfig和C++，CLI值必须验证字符范围，不能注入任意编译器参数。
- full源集只来自 `sdk/src/full/...`。执行bridge任务不得触发vendor下载/校验或full预构建。
- 请求full build必须依赖verifyFullInputs，缺vendor、错hash、ABI未ready或dialect未verified均失败。
- 两个MavenPublication：bridgeRelease→avs3a-sdk-bridge；fullRelease→avs3a-sdk。
- publication输出到root/build/ci-maven，包含AAR、POM、module metadata和source jar。
- sources jar只包含SDK自有源码，不能把reference、vendor-inputs、完整仓库或秘密配置打进去。

所有variant都可以注册任务，但不要因为full输入缺失就让配置阶段失败，以免无vendor的bridge CI也无法运行。门禁挂在full任务依赖上。

模板任务名固定：

```text
:sdk:testBridgeDebugUnitTest
:sdk:assembleBridgeRelease
:sdk:publishBridgeReleasePublicationToCiRepository
:sdk:verifyFullInputs
:sdk:assembleFullRelease
:sdk:publishFullReleasePublicationToCiRepository
```

## 3. Native CMake与导出

模板将纯core与Android层分开：host配置只构建CTest，Android配置不允许测试后端。

必须补充 `native/avs3a_jni.map`：

```text
AVS3A_JNI_1 {
  global: JNI_OnLoad;
  local: *;
};
```

Android target使用version-script，LINK_DEPENDS记录该文件；纯core也用hidden visibility。Native `build_info.cpp`的 `.avs3a.build` 段与nBuildInfo返回相同常量，CI比对其版本/sourceCommit/合同，不靠推测或运行ARM二进制。JNI_OnLoad需JNIEXPORT/JNICALL，其他方法通过RegisterNatives绑定，不依赖Java_*动态导出。

- `--no-undefined`：链接缺依赖立即失败。
- C++静态runtime；不增加额外libc++_shared依赖。
- arm64 bridge `max-page-size=16384`，新库ELF验证与vendor限制分开。
- CMake在POST_BUILD将未strip bridge复制到确定的build/unstripped/<distribution>/<CMAKE_BUILD_TYPE>/<abi>/路径，后续符号包与AAR中对应库按Build ID关联。
- CI只允许本次distribution/version的符号目录进入dist，按AAR Build ID选择匹配的unstripped配置；同ABI不同build type不得互相覆盖，不能从多个flavor/旧路径随便选第一个文件。
- ABI/dialect生成器可以生成安全的ready=false合同以构建bridge，不能把unknown字段填0后标ready。

## 4. 脚本逐个实现，不留空壳

统一Python3标准库实现即可；不通过shell拼接未知输入路径/命令。参数错误或校验失败exit!=0，成功exit0；输出固定JSON/受控摘要，不打印token。

### 4.1 verify-wrapper-and-locks.py

输入：当前仓库配置与env工具链固定值。

检查：wrapper版本/SHA存在且为64hex、AGP/compileSdk/minSdk/NDK/CMake锁定一致、发布版本格式、无MediaHub路径/includeBuild/禁止框架依赖。不能仅检查文件存在。

测试：缺SHA、动态版本、修改NDK、篡改vendor.lock格式均失败。

### 4.2 generate-contracts.py

```text
--abi <arm64-v8a|armeabi-v7a|host-test>
--abi-contract native/vendor/abi-contract.json
--dialect-contract native/vendor/frame-dialect.json
--output <generated-dir>
```

实现步骤：

1. 读取并校验JSON schemaVersion。
2. 从protocol/jni-contract.json取JNI方法/元数据长度/状态码，生成/验证对应常量；校验所有声明verified字段的类型、offset/size/指针宽度、表长度/范围。
3. ready=true必须意味着全部required字段已verified、有非空证据引用。
4. 未完成合同生成ready=false；未知offset用明确invalid sentinel/optional表示，不能用0冒充。
5. 生成 `status_generated.h`、`vendor_contract_generated.h`、`frame_dialect_generated.h` 和机器可读 `contract-report.json`。
6. 生成操作写临时文件后替换，输出不因时间戳随机变化，便于diff/repro。

测试：ARMv7不能继承ARM64字段；offset溢出/容量未确认/表空而ready=true必须失败。host-test不能生成真实vendor-runtime-ready声明。当前锁定双ABI已经静态ready，不应因host未运行vendor而回退静态合同；它也不提升设备验证状态。

本交接包可先运行 `node reference-code/verify-contracts.cjs` 校验字段完整覆盖、指针宽度、表/模板一致及全部证据SHA；这是输入自检，不替代待实现的生产代码生成器。

### 4.3 verify-vendor-inputs.py

支持两种明确模式：

```text
# 验证外部输入并复制到白名单full源集（须获准执行）
--lock vendor/manifest.lock.json --input-root vendor-inputs --stage-root .

# Gradle full任务：只校验已stage内容+合同，不下载
--lock vendor/manifest.lock.json --staged-root .
--require-abi-ready native/vendor/abi-contract.json
--require-dialect-ready native/vendor/frame-dialect.json
```

核心代码骨架：

```python
for entry in lock["files"]:
    source = checked_descendant(input_root, entry["source"])
    data = read_with_size_limit(source, entry["size"])
    require(len(data) == entry["size"])
    require(hashlib.sha256(data).hexdigest() == entry["sha256"])
    destination = checked_descendant(project_root, entry["destination"])
    # 只有stage模式执行，路径必须落在明确full目录白名单内。
    atomic_write_if_staging(destination, data)
```

不允许输入路径逃逸、跟随未验证symlink、复制renderer/IJK/任意多余文件。校验vendor只做锁定输入验证，不重新反汇编/调研。

**允许本地full测试构建不等于允许发布。** 是否有再分发授权由发布门禁另查，不能把lock中的redistributionApproved随便改true就当法律依据。

### 4.4 verify-aar.py

参数与模板一致：aar、distribution、expected-abis、ndk；建议可选输出JSON路径。

检查项目：

1. ZIP成员路径合法，无重复冲突/穿越；只按白名单解到临时目录。
2. classes.jar存在；用本机javap检查NativeBridge签名与protocol/jni-contract.json完全一致。
3. JNI bridge每ABI均存在且无多余ABI；新bridge导出/NEEDED符合协议，不依赖FFmpeg/Media3/IJK/renderer或动态libc++。
4. arm64新bridge的LOAD alignment符合16KB；不能据此把vendor标成16KB compatible。
5. bridge分发中没有decoder/model或full/vendor测试资产。
6. full分发中两个decoder/model大小/hash严格匹配lock，不接受只存在文件名。
7. 不存在MediaHub业务class、测试FakeBackend、reference/私有fixture/key数据。
8. SDK版本、JNI版本、sourceCommit标识与构建请求/manifest一致。

对新JNI产物可运行NDK readelf/nm验证，这不是重查现有SO。vendor结构/符号事实由lock/冻结证据提供，不在该脚本做逆向分析。

测试：缺ABI、改model1字节、加入renderer、classes.jar签名不匹配、伪造ready标志均失败。

### 4.5 make-ci-dist.py

固定参数：

```text
--aar ... --maven-dir build/ci-maven --version VERSION
--source-commit SHA --distribution bridge|full --output dist
```

要求：

- bridge仅选 `com/inlz/avs3a/avs3a-sdk-bridge/VERSION/`；full仅选 `.../avs3a-sdk/VERSION/`。
- **不能将整个build/ci-maven打包**：同目录可能残留旧full产物，导致bridge泄露vendor。
- 校验Maven坐标/POM和AAR一致，只生成该版本所需目录/metadata。
- 输出本版本AAR、Maven zip、对应unstripped bridge符号包、sdk-manifest.json、SHA256SUMS。
- 每个.so记录ABI、hash、Build ID；符号包Build ID须匹配AAR里的bridge。
- deviceValidation默认NOT_RUN，不得根据编译/host测试设PASS。
- 源文件hash/新产物hash使用完整SHA256，临时目录/整个workspace不上传。

示意核心：

```python
artifact_id = "avs3a-sdk-bridge" if mode == "bridge" else "avs3a-sdk"
version_dir = repo / "com/inlz/avs3a" / artifact_id / validated_version
require(version_dir.is_dir())
require(aar_hash(version_dir) == sha256(requested_aar))
package_only_current_coordinate(version_dir, output)
write_manifest(actual_files, api=1, jni=1, device_validation="NOT_RUN")
write_sha256sums(output)
```

### 4.6 verify-device-evidence.py

输入：待发布full AAR、sdk-manifest、ABI报告及fixture manifest。字段及来源/延迟/比较规则固定在 [T08_INPUTS_AND_ACCEPTANCE.md](T08_INPUTS_AND_ACCEPTANCE.md)。参考数值比较器已提供，仍需实现受保护的报告采集与聚合校验。

必须比对：发布JNI/vendor/model的hash、SDK版本/sourceCommit/API/JNI合同、实际测试进程ABI、测试项结果、参考PCM来源。report缺项/ABI重复/来自debug JNI或其他commit则失败。

不能通过命令行一个 `--passed=true` 生成报告。测试报告来自设备测试导出，并由受保护发布流程核对。

## 5. 独立测试App必须消费发布AAR

`smoke-test/build.gradle.kts`关键部分：

```kotlin
plugins { id("com.android.application") }
val sdkVersion = providers.gradleProperty("sdkVersion").getOrElse("0.1.0-SNAPSHOT")
val testAbi = providers.gradleProperty("testAbi").getOrElse("arm64-v8a")
require(testAbi in setOf("arm64-v8a", "armeabi-v7a"))
android {
    namespace = "com.inlz.avs3a.smoke"
    compileSdk = 36
    defaultConfig {
        applicationId = "com.inlz.avs3a.smoke"
        minSdk = 24
        targetSdk = 36
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += testAbi }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    packaging {
        jniLibs.keepDebugSymbols += setOf("**/libavs3a_jni.so", "**/libavs3a_decoder.so")
    }
}
dependencies {
    implementation("com.inlz.avs3a:avs3a-sdk:$sdkVersion") // 不是project(":sdk")
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
}
```

- fullPublication生成后再构建/运行smoke-test；仅bridge CI不解析该full依赖。
- 测试App可显示诊断或写PCM文件，不需要Media3/FFmpeg。SDK库中仍不得出现AudioTrack。
- 测试前检查APK中bridge未被二次strip导致hash变化；与AAR不一致则不认可测试报告。
- 每次测试记录native进程ABI；testAbi过滤的是已发布AAR中对应库，不是重编译JNI。

## 6. Actions模板补全与安全

模板 `templates/android-ci.yml` 复制后需完成其调用的脚本/任务。常规CI：

- PR/main/manual，contents只读，无vendor/发布secret。
- 运行固定工具链、host+JUnit、双ABIbridge、verify-aar、当前coordinate-only dist。
- 正式落仓前把action major tag锁定为审核过的commit SHA。
- Android SDK license需由仓库维护者按组织政策确认；不因为CI交互失败就忽略sdkmanager退出码。

受保护release-sdk.yml实现：

1. 仅保护tag/受信任dispatch，版本格式与tag/commit相符。
2. release环境审批，检查有权使用/分发vendor的证据和目标公开范围。
3. vendor下载凭据只在恢复输入步骤出现，禁止共享会泄露full数据的cache。
4. 校验ABI/dialect、full AAR、两ABI release二进制设备证据。
5. publish job才具有contents/packages写权限，构建job不持有发布权限。
6. 发布Release/Maven包并提供SHA/来源记录；不自动触发MediaHub改动或升级。

GitHub hosted x86 runner不能宣称已跑ARM decoder。自托管设备runner不运行不可信fork PR，不携带额外签名/管理员凭据。

## 7. 从零执行命令

命令示例用于独立仓库，Windows用gradlew.bat；host CTest环境建议CI Linux。执行者记录实际结果，不在当前MediaHub运行这些任务。

```bash
# 配置/纯core测试（在生成器与源文件已经实现后）
cmake -S native -B build/host -DAVS3A_BUILD_HOST_TESTS=ON
cmake --build build/host --parallel 2
ctest --test-dir build/host --output-on-failure

# 无vendor的bridge闭环
./gradlew :sdk:testBridgeDebugUnitTest :sdk:assembleBridgeRelease \
  :sdk:publishBridgeReleasePublicationToCiRepository \
  -PsdkVersion=0.1.0-ci.1 -PsourceCommit="$(git rev-parse HEAD)"
python3 ci/verify-aar.py --aar sdk/build/outputs/aar/sdk-bridge-release.aar \
  --distribution bridge --expected-abis arm64-v8a armeabi-v7a --ndk "$ANDROID_NDK_ROOT"

# 只有输入/ABI/dialect完整后才执行full
python3 ci/verify-vendor-inputs.py --lock vendor/manifest.lock.json \
  --input-root vendor-inputs --stage-root .
./gradlew :sdk:assembleFullRelease :sdk:publishFullReleasePublicationToCiRepository \
  -PsdkVersion=0.1.0-rc.1 -PsourceCommit="$(git rev-parse HEAD)"

# 在实际对应ABI设备上，测试已经发布的release AAR
./gradlew :smoke-test:connectedDebugAndroidTest -PsdkVersion=0.1.0-rc.1 -PtestAbi=arm64-v8a
./gradlew :smoke-test:connectedDebugAndroidTest -PsdkVersion=0.1.0-rc.1 -PtestAbi=armeabi-v7a
```

并发连接多个设备时显式选择目标serial/受控runner，不能混淆报告所属ABI。没有对应设备或golden时记录NOT_RUN并保持full发布门禁关闭。

## 8. 最终产物审核

交付前运行：

- public API/JNI descriptor对比；
- Maven coordinate唯一性（不同时打包bridge/full两个SDK）；
- bridge无vendor、full全输入hash锁定；
- production target无测试Fake；
- SDK无MediaHub/Media3/FFmpeg/AudioTrack依赖；
- API/native/source/version/测试证据对应同一产物；
- README正确区分已完成的静态scope与RUNTIME_NOT_VALIDATED，标注16KB限制、vendor exit风险和分发权限；对未来不匹配二进制保留ABI_GAP拒绝。

不能用“需要时再补脚本”结束任务。脚本、构建门禁和负例测试是本独立项目代码的一部分。
