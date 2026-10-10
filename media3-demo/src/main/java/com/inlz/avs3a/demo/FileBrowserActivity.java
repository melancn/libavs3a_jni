package com.inlz.avs3a.demo;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.widget.ArrayAdapter;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;
import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;

/** 遥控器可用的应用内文件浏览器：方向键选择，OK 进入目录或返回所选文件，返回键回上一级。
 *  直接遍历外部存储 / U 盘，以 file:// URI 回传 MainActivity；需要存储读取权限（见 MainActivity 授权流程）。 */
public final class FileBrowserActivity extends Activity {
    private static final Comparator<File> DIR_FIRST = (a, b) -> {
        if (a.isDirectory() != b.isDirectory()) return a.isDirectory() ? -1 : 1;
        return a.getName().compareToIgnoreCase(b.getName());
    };
    private TextView pathView;
    private ArrayAdapter<String> adapter;
    private final List<File> entries = new ArrayList<>(); // 与列表项一一对应；null 表示 ".."（上一级）
    private File current; // null 表示存储卷根列表

    @Override protected void onCreate(Bundle savedState) {
        super.onCreate(savedState);
        setContentView(R.layout.activity_file_browser);
        pathView = findViewById(R.id.path);
        ListView list = findViewById(R.id.list);
        adapter = new ArrayAdapter<>(this, R.layout.file_browser_row);
        list.setAdapter(adapter);
        list.setOnItemClickListener((parent, view, which, id) -> onEntry(which));
        list.requestFocus();
        if (savedState != null && savedState.getString("current") != null) current = new File(savedState.getString("current"));
        render();
    }

    private void onEntry(int which) {
        if (which < 0 || which >= entries.size()) return;
        File target = entries.get(which);
        if (target == null) { goUp(); return; }
        if (target.isDirectory()) {
            if (!target.canRead()) { toast("无法读取该目录：" + target.getName()); return; }
            current = target; render(); return;
        }
        setResult(RESULT_OK, new Intent().setData(Uri.fromFile(target)));
        finish();
    }

    private void goUp() {
        if (current == null) { finish(); return; }
        if (isVolumeRoot(current)) { current = null; render(); return; }
        File parent = current.getParentFile();
        current = (parent == null || !parent.canRead()) ? null : parent;
        render();
    }

    @Override public void onBackPressed() { goUp(); }

    private void render() {
        entries.clear(); adapter.clear();
        List<String> labels = new ArrayList<>();
        if (current == null) {
            pathView.setText("存储设备");
            for (File root : volumeRoots()) { entries.add(root); labels.add("📁 " + volumeLabel(root)); }
            if (entries.isEmpty()) labels.add("未找到可读取的存储卷，请确认已授予存储访问权限");
        } else {
            pathView.setText(current.getAbsolutePath());
            entries.add(null); labels.add("⬆  ..（上一级）");
            File[] children = current.listFiles();
            if (children == null) {
                labels.add("无法列出目录内容，可能缺少存储访问权限");
            } else {
                List<File> sorted = new ArrayList<>(Arrays.asList(children));
                sorted.sort(DIR_FIRST);
                for (File f : sorted) {
                    if (f.getName().startsWith(".")) continue; // 隐藏点文件/点目录
                    entries.add(f);
                    labels.add(f.isDirectory() ? "📁 " + f.getName()
                            : "📄 " + f.getName() + "  (" + sizeText(f.length()) + ")");
                }
            }
        }
        adapter.addAll(labels);
        adapter.notifyDataSetChanged();
    }

    private List<File> volumeRoots() {
        List<File> roots = new ArrayList<>();
        File primary = Environment.getExternalStorageDirectory();
        if (primary != null && primary.canRead()) roots.add(primary);
        File[] mounts = new File("/storage").listFiles();
        if (mounts != null) for (File m : mounts) {
            String name = m.getName();
            if (name.equals("self") || name.equals("emulated")) continue; // self=当前进程视图，emulated=内部存储已含
            if (m.isDirectory() && m.canRead() && !containsPath(roots, m)) roots.add(m);
        }
        return roots;
    }

    private boolean isVolumeRoot(File dir) {
        for (File root : volumeRoots()) if (root.getAbsolutePath().equals(dir.getAbsolutePath())) return true;
        return false;
    }

    private static boolean containsPath(List<File> list, File f) {
        for (File e : list) if (e.getAbsolutePath().equals(f.getAbsolutePath())) return true;
        return false;
    }

    private static String volumeLabel(File root) {
        File primary = Environment.getExternalStorageDirectory();
        if (primary != null && primary.getAbsolutePath().equals(root.getAbsolutePath()))
            return "内部存储 (" + root.getAbsolutePath() + ")";
        return "存储卷 " + root.getName() + " (" + root.getAbsolutePath() + ")";
    }

    private static String sizeText(long bytes) {
        if (bytes < 1024) return bytes + " B";
        if (bytes < 1024 * 1024) return bytes / 1024 + " KB";
        if (bytes < 1024L * 1024 * 1024) return bytes / (1024 * 1024) + " MB";
        return String.format(Locale.ROOT, "%.1f GB", bytes / (1024.0 * 1024 * 1024));
    }

    private void toast(String message) { Toast.makeText(this, message, Toast.LENGTH_SHORT).show(); }

    @Override protected void onSaveInstanceState(Bundle state) {
        if (current != null) state.putString("current", current.getAbsolutePath());
        super.onSaveInstanceState(state);
    }
}
