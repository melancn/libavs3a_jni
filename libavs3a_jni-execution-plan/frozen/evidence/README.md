# 冻结证据说明（T02更新：2026-10-09）

原ARM64文本仍有效；旧ARMv7错误指令模式dump已被纠正，不得再使用旧dump。

新增证据：

- armv7.decoder.thumb.txt：GNU force-thumb下的函数指令；内嵌literal pool/跳转表和ARM veneer不是Thumb代码，不作为指令解读。
- armv7.apk.decoder-callbacks.txt：原APK的ARMv7分配/头部解析/Init/Decode/Reset/Destroy调用现场。
- *decoder.symbols-relocations.txt：符号尺寸、Thumb入口标记、调用重定位与ARM属性。
- tables.extracted.json：两ABI独立读取的9项采样率表、16项码率表、CRC表和channel-config表；表尺寸来自dynsym。
- table-and-vector-check.json：表值一致性及transport组合统计。
- handoff-tools-test-report.json：合同自检负例与PCM数值比较器自测；包含工具源码SHA，不是真实解码验收。
- reference-test-report.json：参考代码测试目标、工具版本、源文件SHA与结果摘要；不是设备报告。
- reference-test-result.txt：自研reference C在Windows x64执行的结果，不是vendor解码器测试。

所有文件校验值在上一级evidence-manifest.json。源文件中的原磁盘路径仅为来源标识，不要求独立仓库存在那些路径。

frozen/abi-contract.json与frame-dialect.json是当前支持范围内的静态合同；runtime/延迟/真实音频/授权仍单独处理。此证据默认不公开打包到SDK或Maven资产。
