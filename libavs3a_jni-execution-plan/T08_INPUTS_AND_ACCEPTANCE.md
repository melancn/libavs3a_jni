# T08 真实输入、设备与授权：可执行交接合同

更新：2026-10-09。**此文件补齐采集/验收方案，不代表真实媒体、参考 PCM、设备或授权已具备。** 当前所有运行时门禁仍关闭。T02 合成随机 payload 严禁交给 vendor。

## 1. 分开三道门禁，避免循环依赖

1. **静态构建输入**：锁定三个 vendor 文件 + 当前双 ABI/格式静态合同，可以开展受控本地 full 测试构建（测试方须自行确认使用权）。不用先有设备 PASS 才允许生成待测 AAR。
2. **真实解码验证**：设备测试使用唯一候选 fullRelease AAR；限定候选配置集运行，然后以通过的配置生成发布支持白名单。静态表存在不自动获得支持资格。
3. **正式分发**：同一 AAR 的双 ABI 报告、来源可审核的参考 PCM、配置白名单、授权证据全部满足才可发布；重新编译、换 model/vendor/JNI 后必须重新核对测试绑定，不能挪用旧 PASS。

受控候选测试与正式可播放白名单分离，不能为了绕过白名单在发布库放 `allowAllConfigs` 开关。没有运行验收的版本只能标记 build-only，不能称正式 full。

## 2. 输入目录与来源

```text
private-validation/                       # 不提交，不上传公开 Actions artifact
  fixture-manifest.json
  media/<id>.av3a                         # 真正 elementary stream，不是随机 payload
  reference/<id>.s16le                    # 无 WAV 头，交织、小端、原采样率
  provenance/<id>.json                   # 来源、命令、版本、哈希、使用权依据
  delay/<id>.json                        # 独立延迟测量/规范证据
  devices/arm64-v8a/report.json
  devices/armeabi-v7a/report.json
  devices/<abi>/<id>.raw.s16le            # 不预先剪掉头尾，保留诊断原始输出
  legal/authorization.json
  legal/<authorization-document>         # 权利人许可凭据，非自己声明
```

可用参考来源：供应方 conformance vector + PCM；或来源明确的独立实现/官方解码工具及完整命令、版本、二进制 hash。编码前的原 PCM **不是有损解码的逐样本 golden**；只有新 JNI 输出、两个 ABI 互比、复制同一输出改名，都不构成独立参考。原 APK 可作路径差分补充，但相同 vendor 的 APK 输出不能单独充当独立算法正确性的证明。

受保护流程中人工审核来源真实性/使用权，并记录审核人、时间和证据 SHA。JSON 和 hash 只能保证绑定/完整性，不能证明来源陈述为真。

## 3. fixture manifest 必填字段

初始文件见 `templates/fixture-manifest.json`。空 fixtures/requiredConfigurations、null、`NOT_READY` 均须失败，绝不能空循环通过。

每条 `requiredConfigurations` 必须精确到：

```json
{"profile":0,"channelConfig":1,"sourceBits":16,"neuralType":0,"sampleRateHz":48000,"bitrateBps":128000}
```

上述只是**格式示例，不是已经支持的配置**。首批目标至少覆盖 mono、stereo；若宣称同时支持 neural0/1，则每个承诺模式都需真实样本。发布白名单取本 manifest 中两 ABI 均通过的精确配置，不做笛卡尔积扩张。每配置至少一个非全静音样本，stereo 必须左右可区分以发现交织/交换错误；加入起止瞬态、短文件和多帧音频以暴露首尾处理问题。静音只作补充。

每条 `fixtures` 记录：

| 字段 | 规则 |
| --- | --- |
| id、configuration | 唯一 id；完整配置等于 requiredConfigurations 中的一项 |
| encoded.path / sha256 / bytes / frameCount | 私有目录内的实际 elementary 数据、完整 SHA、正数大小/帧数 |
| encoded.framePolicy | 首期 constant-config，所有头/长度/CRC 检查通过；不静默跳坏帧 |
| reference.path / sha256 / bytes | 独立来源原始 S16LE，samples×channels×2 必须等于 bytes |
| reference.samplesPerChannel / sampleRateHz / channels | 与配置一致，正整数；不重采样/下混 |
| provenance.path / sha256 / reviewId | 来源命令、工具 hash、源样本许可与人工审核证据；不得是 DUT 自产 expected |
| delay.primingSamples / trailingSamples / evidencePath / evidenceSha256 | 非负整数，按每声道样本数；未知必须 null 并阻塞，不默认 0/1024 |
| expectedDecodedSamplesPerChannel | 首期完整帧调用通常为 frameCount×1024；设备实测必须核对，不以公式代替实测 |
| comparison.maxAbsErrorLsb / rmsErrorLsb | 默认均 0，即逐样本相等；非零需预先批准及原因，不按失败后结果放宽 |
| comparison.policyReviewId | 固定比较阈值/延迟规则的审核编号 |

PCM 裁剪规则固定为：设备原始输出去掉 `primingSamples` 与 `trailingSamples` 后，与独立 reference **全部样本**逐一比较。必须满足 `decoded - priming - trailing == reference.samplesPerChannel`。如果 reference 带自身 padding，先离线、可复现地规范化并记录来源转换；禁止比较工具“自动找最佳对齐”、只比共同前缀或忽略结尾。

延迟可以通过供应方明确定义或独立来源测试序列/对齐测量取得；仅仅找到让 DUT 与自身输出匹配的偏移不是证据。报告保留测量输入、工具、原始相关性结果和稳定性检查。SDK 先报告原始 PCM 与 delayUnknown，不硬编码剪掉首帧，不添加静音或凭空补帧 flush。

## 4. 设备报告和候选 AAR 的绑定

初始报告结构见 `templates/device-report.json`，必须由 harness/受保护流程填充，不能直接将 status 改 PASS。报告至少包含：

- schemaVersion、runId、UTC 时间、受信任 runner/人工执行人标识与日志 hash。
- AAR SHA、SDK version、sourceCommit、API/JNI contract version；model SHA；实际 APK 中 JNI/vendor bytes 的 SHA 与 Build ID。APK 对比 AAR，运行加载来源还要对应 APK，不能仅抄 sdk-manifest。
- 进程 `sizeof(void*)`、native 编译 ABI 字符串、实际 pageSize、Android API、设备型号/build fingerprint。`Build.SUPPORTED_ABIS`/`adb getprop` 只是能力信息，不能证明当前测试进程为 ARMv7。
- 一个 ARM64 报告和一个 ARMv7 报告，实际 pointerBytes 分别 8/4。设备可以是一台兼容双 ABI 设备或两台；仅支持 64 位的手机不能承担 ARMv7 项。
- 每 fixture 的输入/参考/原始输出 SHA、实际声道/采样率、解码帧数、每声道样本数、priming/tail、逐声道 maxAbsErrorLsb 与 RMS、结果及日志。
- EOS、1-byte/分片输入、单包多帧、OUTPUT_TOO_SMALL 重试不重复 decode、epoch/flush/reopen、重复 create/close、模型缺失/坏 hash 在 vendor 调用前拒绝。
- 生命周期/内存趋势的循环次数、RSS/耗时原始记录。异常 vendor/exit/损坏码流测试在独立进程，报告超时/退出码；不能承诺 catch 能兜住 exit。

参考命令（SDK/harness 实现后才可执行）：

```powershell
# 两次 install/run 必须串行，报告按 ABI 分目录；核对是同一候选 fullRelease。
./gradlew :sdk:publishFullReleasePublicationToCiRepository -PsdkVersion=<candidate>
./gradlew :smoke-test:connectedDebugAndroidTest -PsdkVersion=<candidate> -PtestAbi=arm64-v8a
./gradlew :smoke-test:connectedDebugAndroidTest -PsdkVersion=<candidate> -PtestAbi=armeabi-v7a
```

测试壳可以是 debug APK，但其消费的 JNI/vendor 必须是候选 **release AAR** 原样 bytes。adb 没设备时记录 NOT_RUN，不能以模拟器 JVM/FFM 测试顶替。

4KB 页设备通过不能证明 16KB 页支持。当前 vendor 对齐限制在兼容性声明中保留；未证实前不能宣称支持 16KB 页环境。

## 5. 可立即复用的 PCM 比较器

`reference-code/compare-pcm.cjs` 不调用 vendor。它读取 spec，校验参考 SHA、原始样本数、显式裁剪和逐声道误差；支持标准 Node.js，无第三方依赖。

```json
{
  "actualPcm":"devices/arm64-v8a/example.raw.s16le",
  "referencePcm":"reference/example.s16le",
  "referenceSha256":null,
  "channels":2,
  "sampleRateHz":48000,
  "decodedSamplesPerChannel":null,
  "referenceSamplesPerChannel":null,
  "primingSamples":null,
  "trailingSamples":null,
  "maxAbsErrorLsb":0,
  "rmsErrorLsb":0
}
```

示例未填，必须拒绝。实际路径限 spec 所在目录及其子目录；把 spec 放 private-validation 根部，不允许绝对路径/`..`/symlink 逃逸。

```powershell
node reference-code/compare-pcm.cjs private-validation/comparison-spec.json
node reference-code/test-compare-pcm.cjs
```

退出码 0 只表示本次 PCM 数值比较通过；非 0 表示输入无效或比较不一致。**不表示媒体来源、设备运行或授权通过**。自测只测试比较器，用合成 PCM，不产生 T08 PASS。

## 6. 授权交接，不代签/不伪造

初始 `templates/authorization-record.json` 保留 `NOT_PROVIDED`。合法授权证据至少明确：权利人、被授权主体、许可文本/编号/签署时间、适用 vendor/model 及其 SHA、允许的使用/修改/再分发方式、目标渠道（Maven/GitHub/应用）、商业范围/地域/期限、notice/署名要求、是否允许公开传播本次分析文本。源码/JNI 自研部分和 vendor/model 的权利分别处理。

不因“APK 可下载/二进制已持有/hash 正确”推断许可。`redistributionApproved=true` 只能由有权限人员在审核真实证据后变更，并绑定许可文档 SHA 与审核记录；发布脚本同时验证授权状态/范围/有效期及候选 artifact 绑定。无证据继续保持 false，bridge 开发不受影响，full 正式分发阻塞。

## 7. 当前输入台账

| 项目 | 当前状态 | 谁/什么能补齐 |
| --- | --- | --- |
| T02 双 ABI/格式静态合同 | STATIC_DONE_FOR_SCOPE | 已在本包 |
| 独立参考 PCM + 真媒体 | NOT_PROVIDED | 供应方/有权使用的标准向量/独立实现 |
| 首尾延迟依据 | NOT_PROVIDED | 规范或独立测量证据 |
| 双 ABI release AAR 运行报告 | NOT_RUN | 实现 SDK/harness 后的真实 ARM 进程 |
| 分发授权 | NOT_PROVIDED / false | 权利人许可与授权审核 |

本文件、模板和比较器已经交付，但上表后三类外部事实不能由计划文本生成。具体发布门禁实现仍是 T07/T09 的工程任务。