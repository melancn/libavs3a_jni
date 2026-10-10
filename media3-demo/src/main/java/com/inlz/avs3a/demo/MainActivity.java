package com.inlz.avs3a.demo;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.*;
import android.provider.OpenableColumns;
import android.view.View;
import android.widget.*;
import androidx.media3.common.*;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.exoplayer.analytics.AnalyticsListener;
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory;
import androidx.media3.datasource.DefaultDataSource;
import androidx.media3.ui.PlayerView;
import com.inlz.avs3a.*;
import com.inlz.avs3a.demo.diagnostics.PlaybackDiagnostics;
import com.inlz.avs3a.demo.media3.Avs3Format;
import com.inlz.avs3a.demo.player.*;
import java.util.*;
import java.util.concurrent.*;

@UnstableApi
public final class MainActivity extends Activity {
    private static final int OPEN_FILE = 20;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private final PlaybackDiagnostics diagnostics = new PlaybackDiagnostics();
    private ExoPlayer player;
    private PlayerView playerView;
    private TextView fileView, statusView, detailsView;
    private CheckBox downmixView;
    private Uri uri;
    private long position;
    private VerifiedModel model;
    private boolean prepared, started, autoplayRequested;
    private int fileGeneration;
    private String sdkStatus = "正在检查 JNI / vendor / model…";
    private String error = "", audioDecoder = "未初始化", videoDecoder = "未初始化";
    private int underruns;
    private final Runnable refresh = new Runnable() {
        @Override public void run() { updateDetails(); if (started) main.postDelayed(this, 500); }
    };
    @Override public void onCreate(Bundle savedState) {
        super.onCreate(savedState);
        setContentView(R.layout.activity_main);
        View root = findViewById(R.id.root);
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            return insets;
        });
        playerView = findViewById(R.id.player_view);
        fileView = findViewById(R.id.file_info); statusView = findViewById(R.id.status); detailsView = findViewById(R.id.details);
        downmixView = findViewById(R.id.downmix);
        downmixView.setChecked(savedState == null || savedState.getBoolean("downmix", true));
        downmixView.setOnCheckedChangeListener((button, checked) -> {
            savePositionAndRelease(); if (started && prepared && uri != null) startPlayer(false);
        });
        findViewById(R.id.open_file).setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*")
                    .addCategory(Intent.CATEGORY_OPENABLE)
                    .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            try { startActivityForResult(intent, OPEN_FILE); }
            catch (RuntimeException e) { showError("无法打开系统文件选择器：" + e.getMessage()); }
        });
        findViewById(R.id.audio_tracks).setOnClickListener(v -> showAudioTracks());
        findViewById(R.id.stop).setOnClickListener(v -> { if (player != null) { player.pause(); player.stop(); } });
        findViewById(R.id.replay).setOnClickListener(v -> {
            position = 0;
            if (player != null) { player.seekToDefaultPosition(); player.prepare(); player.play(); }
            else if (prepared && uri != null) startPlayer(true);
        });
        if (savedState != null && savedState.getString("uri") != null) {
            uri = Uri.parse(savedState.getString("uri")); position = savedState.getLong("position");
            fileView.setText(savedState.getString("file", uri.toString()));
        }
        io.execute(() -> {
            VerifiedModel loadedModel = null;
            String text;
            try {
                Avs3Capabilities caps = Avs3Sdk.capabilities(getApplicationContext());
                text = "SDK " + com.inlz.avs3a.BuildConfig.SDK_VERSION + " / API " + Avs3Sdk.API_CONTRACT_VERSION + " / JNI " + caps.bridgeLoaded() + " / ABI " + Arrays.toString(Build.SUPPORTED_ABIS)
                        + " / vendor " + caps.vendorPresent() + " / SHA匹配 " + caps.vendorFingerprintMatched();
                if (caps.decodeImplementationReady()) {
                    loadedModel = Avs3Sdk.prepareBundledModel(getApplicationContext());
                    text += " / 模型已校验（不代表实播通过）";
                } else text += "\nAVS3 解码不可用；可以查看音轨或选择普通音轨。";
            } catch (Exception | LinkageError e) {
                text = "AVS3 初始化不可用：" + e.getMessage() + "\n普通 Media3 音视频仍可播放。";
            }
            VerifiedModel finalModel = loadedModel; String finalText = text;
            main.post(() -> {
                if (isDestroyed()) return;
                model = finalModel; sdkStatus = finalText; prepared = true;
                if (started && uri != null && player == null) startPlayer(autoplayRequested);
                updateDetails();
            });
        });
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != OPEN_FILE || result != RESULT_OK || data == null || data.getData() == null) return;
        savePositionAndRelease(); uri = data.getData(); autoplayRequested = true; position = 0; error = ""; diagnostics.reset();
        int flags = data.getFlags();
        if ((flags & Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION) != 0) {
            try { getContentResolver().takePersistableUriPermission(uri, flags & Intent.FLAG_GRANT_READ_URI_PERMISSION); }
            catch (SecurityException ignored) { /* Some providers only grant this session. */ }
        }
        Uri selected = uri; int generation = ++fileGeneration;
        fileView.setText(selected.toString());
        io.execute(() -> {
            String description = selected.toString();
            try (Cursor cursor = getContentResolver().query(selected,
                    new String[]{OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE}, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) {
                    String name = cursor.getString(0);
                    description = name + (cursor.isNull(1) ? " / 大小未知" : " / " + cursor.getLong(1) + " bytes");
                }
            } catch (RuntimeException ignored) { /* Playback reports actionable provider/read errors. */ }
            String label = description;
            main.post(() -> { if (!isDestroyed() && generation == fileGeneration) fileView.setText(label); });
        });
        if (started && prepared) startPlayer(true);
    }
    private void startPlayer(boolean autoplay) {
        if (uri == null || player != null) return;
        autoplayRequested = false;
        audioDecoder = videoDecoder = "未初始化"; error = ""; underruns = 0;
        DemoRenderersFactory renderers = new DemoRenderersFactory(this, model, downmixView.isChecked(), diagnostics);
        DefaultMediaSourceFactory sources = new DefaultMediaSourceFactory(
                new DefaultDataSource.Factory(this), new DemoExtractorsFactory());
        player = new ExoPlayer.Builder(this, renderers).setMediaSourceFactory(sources).build();
        player.setAudioAttributes(new AudioAttributes.Builder().setUsage(C.USAGE_MEDIA)
                .setContentType(C.AUDIO_CONTENT_TYPE_MOVIE).build(), true);
        player.setHandleAudioBecomingNoisy(true);
        player.setTrackSelectionParameters(player.getTrackSelectionParameters().buildUpon()
                .setPreferredAudioMimeTypes(Avs3Format.MIME_TYPE).build());
        player.addListener(new Player.Listener() {
            @Override public void onPlayerError(PlaybackException e) { showError(e.getErrorCodeName() + ": " + causeText(e)); }
            @Override public void onTracksChanged(Tracks tracks) { updateDetails(); }
            @Override public void onPlaybackStateChanged(int state) { updateDetails(); }
        });
        player.addAnalyticsListener(new AnalyticsListener() {
            @Override public void onAudioDecoderInitialized(EventTime time, String name, long at, long duration) { audioDecoder = name; }
            @Override public void onVideoDecoderInitialized(EventTime time, String name, long at, long duration) { videoDecoder = name; }
            @Override public void onAudioUnderrun(EventTime time, int bytes, long ms, long sinceFeed) { underruns++; }
        });
        playerView.setPlayer(player);
        player.setMediaItem(MediaItem.fromUri(uri), position);
        player.prepare(); player.setPlayWhenReady(autoplay);
    }
    private void showAudioTracks() {
        if (player == null) return;
        List<String> labels = new ArrayList<>(); List<TrackSelectionOverride> choices = new ArrayList<>();
        List<Boolean> support = new ArrayList<>(); int selected = -1;
        for (Tracks.Group group : player.getCurrentTracks().getGroups()) {
            if (group.getType() != C.TRACK_TYPE_AUDIO) continue;
            for (int i = 0; i < group.length; i++) {
                Format f = group.getTrackFormat(i);
                if (group.isTrackSelected(i)) selected = labels.size();
                boolean supported = group.isTrackSupported(i);
                labels.add(trackText(f) + (supported ? "" : " [当前不可解码]"));
                choices.add(new TrackSelectionOverride(group.getMediaTrackGroup(), i)); support.add(supported);
            }
        }
        if (labels.isEmpty()) { showError("尚未识别到音轨"); return; }
        new AlertDialog.Builder(this).setTitle("选择音轨（备用轨不代表 AVS3 验证成功）")
                .setSingleChoiceItems(labels.toArray(new String[0]), selected, (dialog, which) -> {
                    if (player == null) return;
                    if (!support.get(which)) { showError("该音轨当前不可解码：" + labels.get(which)); return; }
                    error = ""; diagnostics.reset(); audioDecoder = "切换中";
                    player.setTrackSelectionParameters(player.getTrackSelectionParameters().buildUpon()
                            .clearOverridesOfType(C.TRACK_TYPE_AUDIO).addOverride(choices.get(which)).build());
                    if (player.getPlaybackState() == Player.STATE_IDLE) player.prepare();
                    dialog.dismiss();
                }).setNegativeButton("取消", null).show();
    }
    private void updateDetails() {
        StringBuilder text = new StringBuilder(sdkStatus).append("\n");
        String state = "请选择 MP4 或 AVS3 裸码流";
        boolean avsSelected = false;
        if (player != null) {
            state = switch (player.getPlaybackState()) {
                case Player.STATE_BUFFERING -> "缓冲中"; case Player.STATE_READY -> player.isPlaying() ? "播放中" : "已暂停";
                case Player.STATE_ENDED -> "播放结束"; default -> "已停止 / 准备中";
            };
            state += " | " + clock(player.getCurrentPosition()) + " / " + clock(player.getDuration());
            state += player.isCurrentMediaItemSeekable() ? " | 可 Seek" : " | 不支持随机定位";
            for (Tracks.Group group : player.getCurrentTracks().getGroups()) {
                if (group.getType() != C.TRACK_TYPE_AUDIO && group.getType() != C.TRACK_TYPE_VIDEO) continue;
                for (int i = 0; i < group.length; i++) {
                    Format f = group.getTrackFormat(i);
                    text.append(group.isTrackSelected(i) ? "\n▶ " : "\n  ").append(trackText(f));
                    if (group.isTrackSelected(i) && Avs3Format.MIME_TYPE.equals(f.sampleMimeType)) avsSelected = true;
                }
            }
        }
        text.append("\n\n音频解码器：").append(audioDecoder).append("\n视频解码器：").append(videoDecoder);
        text.append(avsSelected ? "\n当前选择 AVS3 音轨；是否产生 PCM 见下方计数。" : "\n当前未选择 AVS3 音轨，不计为 AVS3 实播验证。");
        text.append("\n音频欠载次数：").append(underruns).append("\n\n").append(diagnostics.snapshot());
        text.append("\n\n提示：HDR Vivid 呈现、声道映射及算法首尾延迟尚待真机验证。\n5.1 下混采用 L R C LFE Ls Rs 约定；不是空间音频渲染。");
        if (!error.isEmpty()) text.append("\n\n错误：").append(error);
        statusView.setText(state); detailsView.setText(text);
    }
    private static String trackText(Format f) {
        return "ID=" + f.id + " " + f.sampleMimeType + " / " + (f.language == null ? "und" : f.language)
                + (f.width > 0 ? " / " + f.width + "×" + f.height + " / " + f.frameRate + " fps"
                : " / " + f.channelCount + " ch / " + f.sampleRate + " Hz / " + f.averageBitrate + " bps")
                + (f.label == null ? "" : " / " + f.label);
    }
    private static String clock(long ms) { return ms < 0 || ms == C.TIME_UNSET ? "未知" : String.format(Locale.ROOT, "%d:%02d", ms / 60000, ms / 1000 % 60); }
    private static String causeText(Throwable e) {
        StringBuilder text = new StringBuilder();
        for (int i = 0; e != null && i < 5; i++, e = e.getCause()) { if (i > 0) text.append(" → "); text.append(e.getClass().getSimpleName()).append(": ").append(e.getMessage()); }
        return text.toString();
    }
    private void showError(String message) { error = message; updateDetails(); }
    private void savePositionAndRelease() {
        if (player != null) { position = player.getCurrentPosition(); playerView.setPlayer(null); player.release(); player = null; }
    }
    @Override protected void onStart() {
        super.onStart(); started = true; main.post(refresh);
        if (prepared && uri != null && player == null) startPlayer(autoplayRequested);
    }
    @Override protected void onStop() { started = false; main.removeCallbacks(refresh); savePositionAndRelease(); super.onStop(); }
    @Override protected void onSaveInstanceState(Bundle state) {
        if (uri != null) state.putString("uri", uri.toString());
        state.putLong("position", player == null ? position : player.getCurrentPosition());
        state.putString("file", fileView.getText().toString()); state.putBoolean("downmix", downmixView.isChecked());
        super.onSaveInstanceState(state);
    }
    @Override protected void onDestroy() { main.removeCallbacks(refresh); savePositionAndRelease(); io.shutdownNow(); super.onDestroy(); }
}
