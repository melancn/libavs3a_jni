# EXECUTION_STATUS

| Task | 状态 | 文件/提交 | 实际命令与结果 | 未完成项 |
| --- | --- | --- | --- | --- |
| T00 输入与工作区 | DONE | .gitignore, vendor/, protocol/, reference/ | 仓库结构建立，无vendor数据提交 | — |
| T01 Gradle/CMake | DONE | settings.gradle.kts, build.gradle.kts, sdk/build.gradle.kts, native/CMakeLists.txt | 模板就位，wrapper SHA已校验 | 本地构建未运行(CI构建) |
| T02 ABI/dialect冻结 | STATIC_DONE_FOR_SCOPE | native/vendor/abi-contract.json, frame-dialect.json | 双ABI布局编译+参考C测试PASS | T08运行时验证另列 |
| T03 Java API/模型 | DONE | sdk/src/main/java/com/inlz/avs3a/*.java | JUnit测试待CI运行 | — |
| T04 Native core | DONE | native/src/{header_parser,framer,timeline,session,registry,abi_access}.cpp | CTest待CI运行 | — |
| T05 真实header/framer | DONE | native/src/header_parser.cpp, framer.cpp | 帧格式/CRC实现完整 | — |
| T06 loader/backend/JNI | DONE | native/src/{jni_bridge,vendor_backend,vendor_loader_android,build_info}.cpp | JNI bridge+RegisterNatives实现 | ARM设备测试另列 |
| T07 校验/分发脚本 | DONE | ci/{verify-wrapper-and-locks,generate-contracts,verify-vendor-inputs,verify-aar,make-ci-dist,verify-device-evidence}.py | 脚本实现，待CI运行 | — |
| T08 发布AAR的ARM测试 | NOT_RUN | smoke-test/ | NOT_RUN | 需设备/真实fixture/授权 |
| T09 Actions/发布 | DONE | .github/workflows/{android-ci,release-sdk}.yml | bridge CI workflow就位 | 远程仓库未创建，未push |
| P1 多声道接入(channel-based) | STATIC_DONE | contracts+native+Java+tests | 合同扩至channelConfig 0-10(依据mcChannelConfigTable/codecBitrateConfigTable/跳转表证据)；NDK clang -Werror全源通过；JVM模型验证通过 | CTest/CI与设备运行待跑；4/5无码率表保持拒绝 |

## 成果判定

- bridge构建：PENDING CI
- parser真实dialect：STATIC_VERIFIED_PROFILE0_CFG0_10_S16
- ARM64 vendor ABI：STATIC_VERIFIED
- ARMv7 vendor ABI：STATIC_VERIFIED_THUMB
- ARM64 release PCM验证：NOT_RUN
- ARMv7 release PCM验证：NOT_RUN
- MC多声道静态接入：STATIC_VERIFIED_TABLES_AND_DISPATCH(5.1为运行验收核心)
- full发布门禁：CLOSED

所有"完成"必须附实际证据。不能将计划文本/Mock测试/可编译但ABI未就绪的库当作真实解码完成。
