# EXECUTION_STATUS（复制到独立仓库后维护）

本文件为状态模板。T02已完成当前范围的静态复核；不代表SDK工程、vendor实播或发布完成。

| Task | 状态 | 文件/提交 | 实际命令与结果 | 未完成项 |
| --- | --- | --- | --- | --- |
| T00 输入与工作区 | TODO | — | NOT_RUN | — |
| T01 Gradle/CMake | TODO | — | NOT_RUN | — |
| T02 ABI/dialect冻结 | STATIC_DONE_FOR_SCOPE | 见T02_STATIC_REVIEW | 双ABI布局编译+参考C测试PASS | T08运行时验证另列 |
| T03 Java API/模型 | TODO | — | NOT_RUN | — |
| T04 Native core | TODO | — | NOT_RUN | — |
| T05 真实header/framer | TODO | — | NOT_RUN | — |
| T06 loader/backend/JNI | TODO | — | NOT_RUN | — |
| T07 校验/分发脚本 | TODO | — | NOT_RUN | — |
| T08 发布AAR的ARM测试 | TODO | — | NOT_RUN | 输入/报告模板与PCM比较器已提供；仍需设备、SDK与独立golden |
| T09 Actions/发布 | TODO | — | NOT_RUN | 远程仓库/授权/发布均未执行 |

## 成果判定

- bridge构建：NOT_RUN
- parser真实dialect：STATIC_VERIFIED_PROFILE0_MONO_STEREO_S16
- ARM64 vendor ABI：STATIC_VERIFIED
- ARMv7 vendor ABI：STATIC_VERIFIED_THUMB
- ARM64 release PCM验证：NOT_RUN
- ARMv7 release PCM验证：NOT_RUN
- full发布门禁：CLOSED

所有“完成”必须附实际证据。不能将计划文本/Mock测试/可编译但ABI未就绪的库当作真实解码完成。
