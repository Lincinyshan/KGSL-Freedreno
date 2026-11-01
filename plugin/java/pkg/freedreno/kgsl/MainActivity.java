package pkg.freedreno.kgsl;

import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;

/**
 * KGSL-Freedreno（OpenGL 4.6）插件的调试开关界面。
 *
 * 调试开关：FD_MESA_DEBUG 各项、fast clear、GL profile（core/compat 二选一）。
 * 设置写到本应用自己的 Android/media 目录（跨应用可读、写入不需要
 * 「所有文件访问」权限），驱动在 EGL 初始化时读同一个文件：
 *   /storage/emulated/0/Android/media/<包名>/fcl_env.txt
 * 改完必须 force-stop FCL（或杀进程）再启动游戏才生效。
 */
public class MainActivity extends Activity {
    private static final String FILE_NAME = "fcl_env.txt";

    private CheckBox cbFlush, cbNotile, cbNoubwc, cbNoFastClear, cbSysmem, cbNobin;
    private CheckBox cbForceCore, cbForceCompat;
    private TextView status;

    private File envFile() {
        File[] dirs = getExternalMediaDirs();
        File dir = (dirs != null && dirs.length > 0 && dirs[0] != null)
                ? dirs[0] : getExternalFilesDir(null);
        return new File(dir, FILE_NAME);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(16);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setTextSize(16f);
        title.setText("KGSL-Freedreno（OpenGL 4.6）· 调试开关\n"
                + "写入：" + envFile().getAbsolutePath() + "\n"
                + "改完要 force-stop FCL，再启动游戏");
        root.addView(title);

        cbFlush = addCheck(root, "flush —— 每次 draw 后强制 flush（排查同步问题）");
        cbNotile = addCheck(root, "notile —— 禁用 tiling");
        cbNoubwc = addCheck(root, "noubwc —— 禁用 UBWC");
        cbNoFastClear = addCheck(root, "FCL_NO_FAST_CLEAR —— 禁用 GMEM fast clear");
        cbSysmem = addCheck(root, "sysmem —— 全部走 sysmem 渲染");
        cbNobin = addCheck(root, "nobin —— 禁用硬件 binning");

        // GL profile 二选一（都不选 = 按游戏的请求走）
        cbForceCore = addCheck(root, "强制 Core Profile —— 严格现代 GL（Iris shader pack 会编译失败）");
        cbForceCompat = addCheck(root, "强制 Compatibility Profile —— 保留旧特性（MC 26.3 的 140 着色器与 Iris 都需要）");
        cbForceCore.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                if (cbForceCore.isChecked()) cbForceCompat.setChecked(false);
            }
        });
        cbForceCompat.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                if (cbForceCompat.isChecked()) cbForceCore.setChecked(false);
            }
        });

        Button save = new Button(this);
        save.setText("保存");
        save.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { save(); }
        });
        root.addView(save);

        Button clear = new Button(this);
        clear.setText("清空（删除配置文件）");
        clear.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { clear(); }
        });
        root.addView(clear);

        status = new TextView(this);
        status.setTextSize(12f);
        status.setPadding(0, dp(12), 0, 0);
        root.addView(status);

        ScrollView sv = new ScrollView(this);
        sv.addView(root);
        setContentView(sv);

        if (Build.VERSION.SDK_INT < 29
                && checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
                   != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] { android.Manifest.permission.WRITE_EXTERNAL_STORAGE }, 1);
        }

        load();
    }

    private CheckBox addCheck(LinearLayout root, String text) {
        CheckBox cb = new CheckBox(this);
        cb.setText(text);
        cb.setTextSize(14f);
        root.addView(cb);
        return cb;
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density + 0.5f);
    }

    private void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_LONG).show();
    }

    private String readFile() {
        StringBuilder sb = new StringBuilder();
        File f = envFile();
        if (f == null || !f.exists())
            return "";
        try (BufferedReader r = new BufferedReader(
                new InputStreamReader(new FileInputStream(f), "UTF-8"))) {
            String line;
            while ((line = r.readLine()) != null) {
                sb.append(line).append('\n');
            }
        } catch (Throwable ignored) {
        }
        return sb.toString();
    }

    private void load() {
        String content = readFile();

        boolean flush = false, notile = false, noubwc = false, sysmem = false, nobin = false;
        boolean noFc = false, forceCore = false, forceCompat = false;
        for (String line : content.split("\n")) {
            line = line.trim();
            if (line.startsWith("FD_MESA_DEBUG=")) {
                for (String f : line.substring("FD_MESA_DEBUG=".length()).split(",")) {
                    f = f.trim();
                    if (f.equals("flush")) flush = true;
                    else if (f.equals("notile")) notile = true;
                    else if (f.equals("noubwc")) noubwc = true;
                    else if (f.equals("sysmem")) sysmem = true;
                    else if (f.equals("nobin")) nobin = true;
                }
            } else if (line.startsWith("FCL_NO_FAST_CLEAR=")) {
                String v = line.substring("FCL_NO_FAST_CLEAR=".length()).trim();
                noFc = !v.isEmpty() && !v.equals("0");
            } else if (line.startsWith("FCL_GL_FORCE_CORE=")) {
                String v = line.substring("FCL_GL_FORCE_CORE=".length()).trim();
                forceCore = !v.isEmpty() && !v.equals("0");
            } else if (line.startsWith("FCL_GL_FORCE_COMPAT=")) {
                String v = line.substring("FCL_GL_FORCE_COMPAT=".length()).trim();
                forceCompat = !v.isEmpty() && !v.equals("0");
            }
        }

        boolean profileConflict = forceCore && forceCompat;
        if (profileConflict) {
            forceCore = false;
            forceCompat = false;
        }

        cbFlush.setChecked(flush);
        cbNotile.setChecked(notile);
        cbNoubwc.setChecked(noubwc);
        cbNoFastClear.setChecked(noFc);
        cbSysmem.setChecked(sysmem);
        cbNobin.setChecked(nobin);
        cbForceCore.setChecked(forceCore);
        cbForceCompat.setChecked(forceCompat);

        updateStatus(profileConflict
                ? content + "\n# 注意：FCL_GL_FORCE_CORE 与 FCL_GL_FORCE_COMPAT 同时存在，两者都不生效\n"
                : content);
    }

    private void updateStatus(String content) {
        StringBuilder sb = new StringBuilder();
        File f = envFile();
        sb.append("路径：").append(f.getAbsolutePath()).append('\n');
        sb.append("可写：").append(f.getParentFile() != null && f.getParentFile().canWrite() ? "是" : "否").append('\n');
        sb.append("当前内容：\n").append(content.isEmpty() ? "(空/不存在)" : content);
        status.setText(sb.toString());
    }

    private void save() {
        StringBuilder dbg = new StringBuilder();
        if (cbFlush.isChecked()) dbg.append("flush,");
        if (cbNotile.isChecked()) dbg.append("notile,");
        if (cbNoubwc.isChecked()) dbg.append("noubwc,");
        if (cbSysmem.isChecked()) dbg.append("sysmem,");
        if (cbNobin.isChecked()) dbg.append("nobin,");

        StringBuilder out = new StringBuilder();
        out.append("# KGSL-Freedreno（OpenGL 4.6）插件设置，由 MainActivity 写入\n");
        out.append("# force-stop FCL 后生效\n");
        if (dbg.length() > 0) {
            dbg.setLength(dbg.length() - 1);
            out.append("FD_MESA_DEBUG=").append(dbg).append('\n');
        }
        if (cbNoFastClear.isChecked())
            out.append("FCL_NO_FAST_CLEAR=1\n");
        // 二选一：万一两个都被勾上，则都不写（与驱动的冲突处理一致）
        if (cbForceCore.isChecked() != cbForceCompat.isChecked()) {
            if (cbForceCore.isChecked())
                out.append("FCL_GL_FORCE_CORE=1\n");
            else
                out.append("FCL_GL_FORCE_COMPAT=1\n");
        }

        File f = envFile();
        try {
            File dir = f.getParentFile();
            if (dir != null && !dir.exists() && !dir.mkdirs())
                throw new Exception("mkdirs failed: " + dir);
            try (OutputStreamWriter w = new OutputStreamWriter(
                    new FileOutputStream(f), "UTF-8")) {
                w.write(out.toString());
            }
            toast("已写入 " + f.getAbsolutePath());
        } catch (Throwable t) {
            toast("写入失败：" + t.getMessage());
        }
        updateStatus(readFile());
    }

    private void clear() {
        File f = envFile();
        boolean ok = !f.exists() || f.delete();
        toast(ok ? "已删除 " + f.getAbsolutePath() : "删除失败：" + f.getAbsolutePath());
        cbFlush.setChecked(false);
        cbNotile.setChecked(false);
        cbNoubwc.setChecked(false);
        cbNoFastClear.setChecked(false);
        cbSysmem.setChecked(false);
        cbNobin.setChecked(false);
        cbForceCore.setChecked(false);
        cbForceCompat.setChecked(false);
        updateStatus(readFile());
    }
}
