package com.jianwei.vibro;

import android.app.Activity;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.graphics.Color;
import android.net.Uri;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.MediaStore;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InterfaceAddress;
import java.net.NetworkInterface;
import java.net.SocketTimeoutException;
import java.nio.charset.Charset;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

import org.json.JSONArray;
import org.json.JSONObject;

/**
 * 见微测振: hosts the app (assets/web) in a full-screen WebView and gives it
 * the few things a page cannot do itself (window.VibmonNative): UDP discovery
 * of devices on the LAN or the gateway's access point, haptics, sharing,
 * saving exports to Downloads and status bar styling.
 */
public class MainActivity extends Activity {
    private static final int DISCOVERY_PORT = 48300;
    private static final Charset UTF8 = Charset.forName("UTF-8");

    private WebView web;
    private float density = 1f;
    private int insetTop, insetBottom;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        density = getResources().getDisplayMetrics().density;
        Window w = getWindow();
        w.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
        w.setStatusBarColor(Color.TRANSPARENT);
        web = new WebView(this);
        web.setBackgroundColor(Color.parseColor("#F2F3F6"));
        web.setOverScrollMode(View.OVER_SCROLL_NEVER);
        web.setVerticalScrollBarEnabled(false);
        web.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setAllowFileAccess(true);
        s.setAllowFileAccessFromFileURLs(true);
        s.setAllowUniversalAccessFromFileURLs(true);  // the page talks to devices on the LAN
        s.setMixedContentMode(WebSettings.MIXED_CONTENT_ALWAYS_ALLOW);
        s.setTextZoom(100);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setCacheMode(WebSettings.LOAD_NO_CACHE);
        web.addJavascriptInterface(new Bridge(), "VibmonNative");
        web.setWebChromeClient(new WebChromeClient());
        web.setWebViewClient(new WebViewClient() {
            @Override
            public void onPageFinished(WebView view, String url) {
                pushInsets();
            }

            @Override
            public boolean shouldOverrideUrlLoading(WebView view, String url) {
                return !url.startsWith("file:");
            }
        });
        web.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
                insetTop = insets.getSystemWindowInsetTop();
                insetBottom = insets.getSystemWindowInsetBottom();
                pushInsets();
                return insets.consumeSystemWindowInsets();
            }
        });
        setContentView(web);
        web.loadUrl("file:///android_asset/web/index.html");
    }

    // Safe areas as CSS pixels (the page lays out under the system bars).
    private void pushInsets() {
        if (web == null) return;
        String js = "document.documentElement.style.setProperty('--sat','" + Math.round(insetTop / density) + "px');"
                + "document.documentElement.style.setProperty('--sab','" + Math.round(insetBottom / density) + "px');";
        web.evaluateJavascript(js, null);
    }

    @Override
    public void onBackPressed() {
        web.evaluateJavascript("window.__jwBack ? window.__jwBack() : false", new ValueCallback<String>() {
            @Override
            public void onReceiveValue(String handled) {
                if (!"true".equals(handled)) moveTaskToBack(true);
            }
        });
    }

    @Override
    protected void onDestroy() {
        if (web != null) web.destroy();
        super.onDestroy();
    }

    private void callJs(final String fn, final String jsonArg) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                web.evaluateJavascript("window.__jw && window.__jw['" + fn + "'] && window.__jw['" + fn + "']("
                        + JSONObject.quote(jsonArg) + ")", null);
            }
        });
    }

    /** Broadcasts VIBMON_DISCOVER and collects the JSON answers of every device. */
    private JSONArray discover(int timeoutMs) {
        JSONArray found = new JSONArray();
        WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        WifiManager.MulticastLock lock = wifi != null ? wifi.createMulticastLock("vibmon-discovery") : null;
        DatagramSocket sock = null;
        try {
            if (lock != null) lock.acquire();
            sock = new DatagramSocket();
            sock.setBroadcast(true);
            sock.setSoTimeout(250);
            byte[] probe = "VIBMON_DISCOVER".getBytes(UTF8);
            Set<String> targets = new HashSet<String>();
            targets.add("255.255.255.255");
            targets.add("192.168.4.1");  // the gateway's own access point
            for (NetworkInterface ni : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!ni.isUp() || ni.isLoopback()) continue;
                for (InterfaceAddress a : ni.getInterfaceAddresses()) {
                    if (a.getBroadcast() != null) targets.add(a.getBroadcast().getHostAddress());
                }
            }
            long end = System.currentTimeMillis() + Math.max(400, Math.min(6000, timeoutMs));
            long nextProbe = 0;
            Set<String> seen = new HashSet<String>();
            byte[] buf = new byte[2048];
            while (System.currentTimeMillis() < end) {
                if (System.currentTimeMillis() >= nextProbe) {  // a few rounds: UDP may drop one
                    for (String t : targets) {
                        try {
                            sock.send(new DatagramPacket(probe, probe.length, InetAddress.getByName(t), DISCOVERY_PORT));
                        } catch (Exception ignored) {
                            // unreachable interface
                        }
                    }
                    nextProbe = System.currentTimeMillis() + 600;
                }
                DatagramPacket p = new DatagramPacket(buf, buf.length);
                try {
                    sock.receive(p);
                } catch (SocketTimeoutException e) {
                    continue;
                }
                String ip = p.getAddress().getHostAddress();
                try {
                    JSONObject o = new JSONObject(new String(p.getData(), 0, p.getLength(), UTF8));
                    String key = ip + ":" + o.optInt("http_port", 80);
                    if (!"vibmon".equals(o.optString("service")) || !seen.add(key)) continue;
                    o.put("ip", ip);
                    found.put(o);
                } catch (Exception ignored) {
                    // not one of ours
                }
            }
        } catch (Exception ignored) {
            // no network
        } finally {
            if (sock != null) sock.close();
            if (lock != null && lock.isHeld()) lock.release();
        }
        return found;
    }

    private class Bridge {
        @JavascriptInterface
        public void discover(final int timeoutMs, final String callback) {
            new Thread(new Runnable() {
                @Override
                public void run() {
                    callJs(callback, MainActivity.this.discover(timeoutMs).toString());
                }
            }, "discovery").start();
        }

        @JavascriptInterface
        public void vibrate(int ms) {
            Vibrator v = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
            if (v == null || !v.hasVibrator()) return;
            int d = Math.max(5, Math.min(600, ms));
            if (Build.VERSION.SDK_INT >= 26) v.vibrate(VibrationEffect.createOneShot(d, d < 30 ? 60 : VibrationEffect.DEFAULT_AMPLITUDE));
            else v.vibrate(d);
        }

        @JavascriptInterface
        public void shareText(String title, String text) {
            Intent i = new Intent(Intent.ACTION_SEND);
            i.setType("text/plain");
            i.putExtra(Intent.EXTRA_SUBJECT, title);
            i.putExtra(Intent.EXTRA_TEXT, text);
            startActivity(Intent.createChooser(i, title));
        }

        /** Saves an export to Downloads/见微测振; returns true on success. */
        @JavascriptInterface
        public boolean saveFile(String name, String mime, String text) {
            String safe = name.replaceAll("[^A-Za-z0-9._-]", "_");
            byte[] data = text.getBytes(UTF8);
            try {
                if (Build.VERSION.SDK_INT >= 29) {
                    ContentValues cv = new ContentValues();
                    cv.put(MediaStore.MediaColumns.DISPLAY_NAME, safe);
                    cv.put(MediaStore.MediaColumns.MIME_TYPE, mime);
                    cv.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/见微测振");
                    Uri uri = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv);
                    if (uri == null) return false;
                    OutputStream os = getContentResolver().openOutputStream(uri);
                    if (os == null) return false;
                    os.write(data);
                    os.close();
                } else {
                    File dir = new File(getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS), "见微测振");
                    if (!dir.isDirectory() && !dir.mkdirs()) return false;
                    FileOutputStream fo = new FileOutputStream(new File(dir, safe));
                    fo.write(data);
                    fo.close();
                }
                return true;
            } catch (Exception e) {
                return false;
            }
        }

        @JavascriptInterface
        public void setTheme(final boolean dark) {
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    int bg = Color.parseColor(dark ? "#090B10" : "#F2F3F6");
                    web.setBackgroundColor(bg);
                    getWindow().setNavigationBarColor(bg);
                    int flags = View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_STABLE;
                    if (!dark) {
                        flags |= View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR;
                        if (Build.VERSION.SDK_INT >= 26) flags |= View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
                    }
                    web.setSystemUiVisibility(flags);
                }
            });
        }

        @JavascriptInterface
        public void keepScreenOn(final boolean on) {
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    if (on) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                    else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                }
            });
        }

        @JavascriptInterface
        public String appVersion() {
            try {
                PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
                return pi.versionName;
            } catch (Exception e) {
                return "";
            }
        }

        @JavascriptInterface
        public String deviceName() {
            return Build.MANUFACTURER + " " + Build.MODEL;
        }
    }
}
