#!/bin/bash
# Windows x64 build: windows/main.cpp (Edge WebView2 window + native bridge)
# cross-compiled with MinGW-w64, packed with the web folder into
# dist/JianweiVibro-<ver>-windows-x64.zip. Runs on Linux; needs
# g++-mingw-w64-x86-64, python3-pil, git, curl, unzip.
#
# Third-party headers (fetched once, pinned by checksum):
#   webview 0.12.0 (MIT)        github.com/webview/webview
#   WebView2.h 1.0.1150.38 (Microsoft, BSD-style), as vendored in
#   github.com/webview/webview_go (Go module proxy). Its 133 interface IDs were
#   cross-checked against the windows-rs webview2-com-sys bindings.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(sed -n "s/^VERSION_NAME=//p" tools/build_apk.sh)
CACHE="${JW_WIN_CACHE:-$HOME/.cache/jw_win}"
mkdir -p "$CACHE" build/windows dist
WV_SHA=b1ff6e11431d031e5f7917e5301f4bcdf3910a2763a0720abfd291994eee7f7b
WV2_SHA=557d5c751148732242b6ced2dbf99d35bed4c4cb471fa50ccf2069a342244cf4
if [ ! -f "$CACHE/wv/core/include/webview/webview.h" ]; then
  rm -rf "$CACHE/wv"
  git clone -q --depth 1 --branch 0.12.0 https://github.com/webview/webview.git "$CACHE/wv"
fi
if [ ! -f "$CACHE/wv2inc/WebView2.h" ]; then
  GOV=v0.0.0-20240831120633-6173450d4dd6
  curl -fsSL -o "$CACHE/wgo.zip" "https://proxy.golang.org/github.com/webview/webview_go/@v/$GOV.zip"
  mkdir -p "$CACHE/wv2inc"
  unzip -o -j -q "$CACHE/wgo.zip" '*/libs/mswebview2/include/WebView2.h' -d "$CACHE/wv2inc"
fi
echo "$WV_SHA  $CACHE/wv/core/include/webview/webview.h" | sha256sum -c --quiet
echo "$WV2_SHA  $CACHE/wv2inc/WebView2.h" | sha256sum -c --quiet

IFS=. read -r MA MI PA <<<"$VERSION"
printf '#define VER_MAJOR %s\n#define VER_MINOR %s\n#define VER_PATCH %s\n#define VER_STRING "%s"\n' "$MA" "$MI" "$PA" "$VERSION" > windows/version.h
mkdir -p windows/build
python3 - <<'PY'
from PIL import Image, ImageDraw
# The store icon is a full square; desktop icons get rounded corners.
im = Image.open('docs/icon_512.png').convert('RGBA')
m = Image.new('L', (2048, 2048), 0)
ImageDraw.Draw(m).rounded_rectangle((0, 0, 2047, 2047), radius=460, fill=255)
im.putalpha(m.resize((512, 512), Image.LANCZOS))
im.save('windows/build/app.ico', sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
PY
x86_64-w64-mingw32-windres -I windows windows/app.rc -O coff -o build/windows/app.res.o
x86_64-w64-mingw32-g++ -std=c++17 -O2 -s -mwindows -static \
  -DAPP_VERSION="\"$VERSION\"" -I windows/compat -I "$CACHE/wv/core/include" -I "$CACHE/wv2inc" \
  windows/main.cpp build/windows/app.res.o -o build/windows/JianweiVibro.exe \
  -ladvapi32 -lole32 -lshell32 -lshlwapi -luser32 -lversion -lws2_32 -liphlpapi -ldwmapi -luuid
STAGE=build/windows/JianweiVibro
rm -rf "$STAGE" && mkdir -p "$STAGE"
cp build/windows/JianweiVibro.exe "$STAGE/"
cp -r web "$STAGE/web"
cat > "$STAGE/使用说明.txt" <<TXT
见微测振 $VERSION（Windows x64）

解压后双击 JianweiVibro.exe 运行（web 文件夹须与程序放在一起）。
需要 Microsoft Edge WebView2 运行时：Windows 11 与较新的 Windows 10 已自带；
若提示缺少，请从微软官网安装“WebView2 Runtime”。
电脑与测振仪须在同一局域网，或连接测振仪热点 VIBMON-xxxx（密码 vibmon2026）。
首次搜索设备时 Windows 防火墙可能询问是否允许网络访问，请允许“专用网络”。
导出的文件保存在“下载”文件夹；“分享”会把摘要复制到剪贴板。
TXT
OUT="dist/JianweiVibro-$VERSION-windows-x64.zip"
rm -f dist/JianweiVibro-*-windows-x64.zip
(cd build/windows && python3 -c "
import os, sys, zipfile
z = zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED, compresslevel=9)
for r, _, fs in os.walk('JianweiVibro'):
    for f in sorted(fs):
        p = os.path.join(r, f); z.write(p, p)
z.close()" "../../$OUT")
echo "built $OUT ($(du -k "$OUT" | cut -f1)K)"
