package com.a730.mesa.egl263.ubwc;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
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
 * Gallium-EXP 插件的调试开关界面。
 *
 * 驱动在 EGL 初始化时会读取 /sdcard/FCL/fcl_ab.txt（每行 KEY=VALUE），
 * 这里就是把那套开关做成界面，省得每次 adb push。
 * 改完必须 force-stop FCL（或杀进程）再启动游戏才生效。
 */
public class MainActivity extends Activity {
    private static final String DIR = "/sdcard/FCL";
    private static final String FILE = DIR + "/fcl_ab.txt";

    private CheckBox cbFlush, cbNotile, cbNoubwc, cbNoFastClear, cbSysmem, cbNobin;
    private TextView status;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(16);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setTextSize(16f);
        title.setText("Gallium-EXP · 调试开关\n"
                + "写入 " + FILE + "\n"
                + "改完要 force-stop FCL，再启动游戏");
        root.addView(title);

        cbFlush = addCheck(root, "flush —— 每次 draw 后强制 flush（排查同步问题）");
        cbNotile = addCheck(root, "notile —— 禁用 tiling");
        cbNoubwc = addCheck(root, "noubwc —— 禁用 UBWC");
        cbNoFastClear = addCheck(root, "FCL_NO_FAST_CLEAR —— 禁用 GMEM fast clear");
        cbSysmem = addCheck(root, "sysmem —— 全部走 sysmem 渲染");
        cbNobin = addCheck(root, "nobin —— 禁用硬件 binning");

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

        if (!hasAllFilesAccess()) {
            Button perm = new Button(this);
            perm.setText("授予「所有文件访问」权限");
            perm.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) { requestAllFilesAccess(); }
            });
            root.addView(perm);
        }

        status = new TextView(this);
        status.setTextSize(12f);
        status.setPadding(0, dp(12), 0, 0);
        root.addView(status);

        ScrollView sv = new ScrollView(this);
        sv.addView(root);
        setContentView(sv);

        if (Build.VERSION.SDK_INT < 30
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

    private boolean hasAllFilesAccess() {
        if (Build.VERSION.SDK_INT < 30)
            return true;
        try {
            Object r = Environment.class.getMethod("isExternalStorageManager").invoke(null);
            return r instanceof Boolean && (Boolean) r;
        } catch (Throwable t) {
            return true;
        }
    }

    private void requestAllFilesAccess() {
        try {
            Intent i = new Intent("android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION");
            i.setData(Uri.parse("package:" + getPackageName()));
            startActivity(i);
        } catch (Throwable t) {
            try {
                startActivity(new Intent("android.settings.MANAGE_ALL_FILES_ACCESS_PERMISSION"));
            } catch (Throwable t2) {
                toast("打不开系统设置页，请手动到「所有文件访问」里授权");
            }
        }
    }

    private void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_LONG).show();
    }

    private String readFile() {
        StringBuilder sb = new StringBuilder();
        try (BufferedReader r = new BufferedReader(
                new InputStreamReader(new FileInputStream(FILE), "UTF-8"))) {
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
        boolean noFc = false;
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
            }
        }

        cbFlush.setChecked(flush);
        cbNotile.setChecked(notile);
        cbNoubwc.setChecked(noubwc);
        cbNoFastClear.setChecked(noFc);
        cbSysmem.setChecked(sysmem);
        cbNobin.setChecked(nobin);

        updateStatus(content);
    }

    private void updateStatus(String content) {
        StringBuilder sb = new StringBuilder();
        sb.append("权限：").append(hasAllFilesAccess() ? "可写 /sdcard" : "缺「所有文件访问」").append('\n');
        sb.append("当前 ").append(FILE).append("：\n");
        sb.append(content.isEmpty() ? "(空/不存在)" : content);
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
        out.append("# Gallium-EXP 插件设置，由 MainActivity 写入\n");
        out.append("# force-stop FCL 后生效\n");
        if (dbg.length() > 0) {
            dbg.setLength(dbg.length() - 1);
            out.append("FD_MESA_DEBUG=").append(dbg).append('\n');
        }
        if (cbNoFastClear.isChecked())
            out.append("FCL_NO_FAST_CLEAR=1\n");

        try {
            File dir = new File(DIR);
            if (!dir.exists() && !dir.mkdirs())
                throw new Exception("mkdirs failed: " + DIR);
            try (OutputStreamWriter w = new OutputStreamWriter(
                    new FileOutputStream(FILE), "UTF-8")) {
                w.write(out.toString());
            }
            toast("已写入 " + FILE);
        } catch (Throwable t) {
            toast("写入失败：" + t.getMessage());
        }
        updateStatus(readFile());
    }

    private void clear() {
        File f = new File(FILE);
        boolean ok = !f.exists() || f.delete();
        toast(ok ? "已删除 " + FILE : "删除失败：" + FILE);
        cbFlush.setChecked(false);
        cbNotile.setChecked(false);
        cbNoubwc.setChecked(false);
        cbNoFastClear.setChecked(false);
        cbSysmem.setChecked(false);
        cbNobin.setChecked(false);
        updateStatus(readFile());
    }
}
