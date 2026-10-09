# 可编译参考代码（不是完整 SDK）

此目录实现 T02 的布局断言、7 字节帧头解析、vendor CRC 和新 decoder 状态初始化字段写入。**不加载/调用 vendor，不生成声音，也不包含 JNI/session/framer/AudioTrack。**

## 本机复现

需要 Node.js 18+、Windows x64、JDK 17 和带 Windows LLVM 的 Android NDK。路径均可覆盖；NDK/JDK 路径默认值只是原分析机设置，不是 CI 工具链锁。

```powershell
node reference-code/verify-contracts.cjs
node reference-code/test-verify-contracts.cjs
node reference-code/test-compare-pcm.cjs
powershell -File reference-code/run-tests.ps1 `
  -NdkRoot C:\Android\Sdk\ndk\27.2.12479018 `
  -JdkRoot C:\Java\jdk-17 `
  -OutputDir C:\temp\avs3a-reference-test
```

从计划包根目录执行。默认输出到 `reference-code/.test-build`；不要将生成的 DLL/OBJ/LIB/CLASS/TSV 打包进 SDK。脚本先对两个 Android ABI 执行 C11/C++17 静态断言，再构建自研 C 的 Windows DLL，由 JDK17 FFM 调用。FFM 的 incubator warning 是预期提示。不能将此测试写成 Android vendor/device PASS。

最新实测：414 组、95,634 条断言；其中 2 组可解析但超过 signed16 SDK budget，初始化明确拒绝。CRC `123456789 -> a69d`，不是 CCITT-FALSE 的 `29b1`。测试向量由冻结表生成，不重新读 SO。

## 函数约定

- `avs3a_parse_header(bytes, size, out)`：只需 7 字节。成功表示当前范围头部语法成立，不表示 payload 完整/CRC 正确/配置已通过设备验收。
- `avs3a_validate_frame(bytes, size, out)`：验证第一个完整帧及 CRC；后面可有其他帧。调用方的 COMPLETE_SINGLE_FRAME 语义还必须检查 `size == out.frame_bytes`。
- `avs3a_crc16(bytes, size)`：调用方保证 size 字节可读；仅 size=0 可传 NULL。
- `avs3a_prepare_new_decoder(state, size, header)`：只在 `Avs3AllocDecoder` 成功后的新状态、Init 之前调用，size 必须等于当前目标布局大小。验证结构化参数并写入标量；不检验 payload 或模型，不清空 opaque/pointer 区，不写 firstFrame，不允许用它逐帧“重置”活跃 decoder。state 必须可写且不能与 header 重叠。
- 任意失败后都不应消费 out；validate_frame 返回 NEED_INPUT 时可能已经写入解析出的 header。
- 0 OK、1 NEED_INPUT、-1 INVALID、-2 UNSUPPORTED、-3 CRC_MISMATCH 是**本参考 C 的局部状态码**，不等于 frozen/jni-contract.json 的公开 JNI 状态值。集成时显式映射，不能直接透传。

布局是针对锁定二进制重建的兼容头，不是厂商官方头。生产访问器用边界校验后的 memcpy/字节写入；函数地址来自 dlsym，ARMv7 不清 Thumb bit0。Init/Decode 返回值忽略，不用寄存器判断错误。

## 生产集成还需要

- hash 锁、模型管理、严格 CRC、完整帧有界队列、runtime 配置白名单和会话状态机。
- signed16 budget 检查与样本数校验先于 vendor 调用；5120 是 transport 最大帧，不是可放行 decoder 最大帧（4096）。
- C/CPP 编译禁止 fast-math；长度公式依赖 binary32 中间舍入。非默认浮点舍入环境不是本合同支持的调用环境。
- 同配置后续帧仍检查 CRC/预算；seek 销毁并重建，不能依赖原 APK 空 flush。
- 真实样本与设备流程见 [T08_INPUTS_AND_ACCEPTANCE.md](../T08_INPUTS_AND_ACCEPTANCE.md)。随机 payload 绝不可送入 vendor。

`verify-contracts.cjs` 校验交接一致性与证据 hash，不替代生成器、人工审核、真实解码验收或授权判定。