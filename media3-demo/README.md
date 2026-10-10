# AVS3 Media3 Demo

独立 Android 应用，Java 17 / API 24+ / Media3 1.11.1，依赖本仓库 `:sdk`。

## 功能与范围

- 系统文件选择器读取 `content://`，无需存储全盘权限，不把 URI 强转为文件路径。
- 普通 MP4：保留 Media3 视频及常规音频解码；新增 `av3a` 音轨识别和 `dca3` 配置透传。
- AVS3 裸码流：分块解析、播放，时长未知、不提供猜测的随机定位。
- AVS3 JNI Renderer/Decoder：完整单帧输入；首版 Demo 接受单声道、双声道、5.1。
- PlayerView 播放/暂停、停止、重播、MP4 Seek、音轨手动选择。
- 每 500ms 更新实际选中音轨、音视频解码器、PCM 参数/计数/PTS、解码调用耗时、欠载、错误。
- 默认将 5.1 下混双声道。可取消下混进行多声道设备实验；输出路由支持仍由设备决定。
- 离开页面暂停并释放播放器；恢复页面不自动播放旧文件，选择新文件后准备播放。

## 构建

使用项目固定的 JDK、NDK、CMake 和 Python 3。Python 3 必须可被 CMake 找到，full 校验任务需要 `python3` 命令。
设置 `ANDROID_HOME` 或本地 `local.properties` 的 `sdk.dir`。

```powershell
.\gradlew.bat :media3-demo:assembleBridgeDebug
.\gradlew.bat :media3-demo:assembleFullDebug
.\gradlew.bat :sdk:testBridgeDebugUnitTest :media3-demo:testBridgeDebugUnitTest
```

- bridge：不含 vendor SO/model；可播放普通音轨和显示 MP4 的 AVS3 音轨信息，不能实际解码 AVS3。
- full：需先按 `vendor/manifest.lock.json` 校验并放置 vendor 输入；不要提交或擅自分发 vendor 文件。
- `-PsdkVersion=1.2.3` 同时设置 SDK 和 Demo `versionName`；`-PdemoVersionCode=123` 设置应用版本号。
- 两个 APK 的 applicationId 不同，可同时安装。构建包含 ARM64、ARMv7，不支持 x86 解码。

输出：

```text
build/outputs/apk/bridge/debug/media3-demo-bridge-debug.apk
build/outputs/apk/full/debug/media3-demo-full-debug.apk
```

## release-sdk.yml

SDK 发布构建同时生成 **bridgeDebug / fullDebug 两个可安装 APK**。它们使用 Android debug 签名，
不假定已有正式发布证书。不同 CI 运行可能使用不同 debug key，更新时可能需要先卸载旧包。

APK 经 `apksigner` 校验、双 ABI 检查、bridge 防 vendor 泄漏检查及 full vendor/model SHA 校验后，
放入 SDK 分发包 `dist/demo/`，并登记在 `sdk-manifest.json` 和 `SHA256SUMS`：

```text
avs3a-media3-demo-bridge-<SDK_VERSION>-debug.apk
avs3a-media3-demo-full-<SDK_VERSION>-debug.apk
```

手动运行从 `avs3a-full-<SDK_VERSION>` Actions artifact 下载；tag 发布通过授权检查后，
现有 `dist/**` 上传规则也会把 APK 附加到 GitHub Release。
**当前 lock 的 redistributionApproved=false，不允许创建包含 full AAR/APK 的公开 Release。**
构建和授权是两个独立条件；脚本不会把编译或打包通过写成设备解码通过。

## MP4 与输出边界

- MP4 sample 必须携带 SDK 可识别的完整 AVS3 帧头，并且每个 sample 为一帧。
  不猜测缺失帧头；多帧 sample、不同封装方言将明确报错。
- `dca3` 原样保留作为 initializationData，不宣称已完整解析所有配置语义。
- 非分片、未加密 MP4 是 AVS3 首版目标；fMP4/DRM/对象/HOA 尚未接入验收。
- MP4 解封装复用隔离的 Media3 源码，保留 co64、edit list、Seek 能力；见 `third_party/README.md`。
- 普通提供方不支持随机读取时 Seek 可能失败；当前不会无提示复制数 GB 文件到缓存。
- 默认 5.1 矩阵以 `L R C LFE Ls Rs` 为声道约定，中心/环绕 -3dB、忽略 LFE 并归一化。
  这是 Demo 下混，不是 Audio Vivid 空间/双耳渲染。vendor 声道顺序和声道内容需要参考 PCM/真机核验。
- 选中 AAC/E-AC-3 不算 AVS3 成功；面板区分选中 AVS3、解码器初始化、实际 PCM 计数。
- 视频平台 HEVC 支持不等于 HDR Vivid 动态元数据正确呈现。
- vendor 首尾延迟、Seek 预热长度、16KB 页兼容性、真实 PCM 正确性仍需设备验证。

## 真机验收清单

1. full APK 选择目标 4K MP4，确认音轨 ID 2 / av3a / 6ch / 48kHz / 384kbps。
2. 确认音频解码器显示 JNI 且 PCM 计数递增，不是备用轨出声。
3. 验证六声道映射与下混、暂停恢复、连续 Seek、切换音轨、切换文件、EOS。
4. 无模型/vendor、损坏帧、配置变化、不可读取 URI 要明确报错，不静默假报成功。
5. ARM64/ARMv7 分别记录设备、页大小、耗时、输出和参考 PCM 对比。
