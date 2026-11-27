#!/bin/bash
# SPDX-License-Identifier: BSD-2-Clause-Patent
# Build ZizhanInstaller.app v2 (4K system app), ZizhanDemoApp-16k.ipa, ZizhanCompatSuite-16k.ipa
# (16K page-signed decrypted 8-subsystem IPA), and FairPlayEncryptedSample.ipa on macOS.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="${1:-/tmp/zizhan-apps}"
SDK="$(xcrun --sdk iphoneos --show-sdk-path)"
CC="$(xcrun --sdk iphoneos -f clang)"
CODESIGN="$(xcrun -f codesign)"

mkdir -p "$OUT_DIR"

python3 - "$OUT_DIR" << 'PY'
import pathlib, struct, sys, zlib

out = pathlib.Path(sys.argv[1])

def make_icon(path: pathlib.Path, top_rgb, bot_rgb, glyph_rgb):
    w, h = 180, 180
    rows = []
    for y in range(h):
        t = y / (h - 1)
        br = int(top_rgb[0] * (1 - t) + bot_rgb[0] * t)
        bg = int(top_rgb[1] * (1 - t) + bot_rgb[1] * t)
        bb = int(top_rgb[2] * (1 - t) + bot_rgb[2] * t)
        row = bytearray([0])
        for x in range(w):
            in_box = (46 <= x < 134 and 92 <= y < 144)
            in_stem = (78 <= x < 102 and 34 <= y < 96)
            if in_box or in_stem:
                row.extend((glyph_rgb[0], glyph_rgb[1], glyph_rgb[2], 255))
            else:
                row.extend((br, bg, bb, 255))
        rows.append(bytes(row))
    raw = b"".join(rows)
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )
    path.write_bytes(png)

def write_plist(app_dir: pathlib.Path, disp: str, exe: str, bid: str):
    (app_dir / "Info.plist").write_text(f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDisplayName</key><string>{disp}</string>
  <key>CFBundleExecutable</key><string>{exe}</string>
  <key>CFBundleIconFiles</key><array><string>AppIcon60x60</string></array>
  <key>CFBundleIdentifier</key><string>{bid}</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>{exe}</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>2.0</string>
  <key>CFBundleVersion</key><string>2</string>
  <key>LSRequiresIPhoneOS</key><true/>
  <key>NSCameraUsageDescription</key><string>验证 MIX Fold 2 系统级三摄预览、对焦与扫码</string>
  <key>NSLocationWhenInUseUsageDescription</key><string>验证 MIX Fold 2 CoreLocation GNSS 定位</string>
  <key>UIDeviceFamily</key><array><integer>1</integer><integer>2</integer></array>
  <key>UISupportedInterfaceOrientations</key>
  <array>
    <string>UIInterfaceOrientationPortrait</string>
    <string>UIInterfaceOrientationLandscapeLeft</string>
    <string>UIInterfaceOrientationLandscapeRight</string>
  </array>
  <key>UISupportedInterfaceOrientations~ipad</key>
  <array>
    <string>UIInterfaceOrientationPortrait</string>
    <string>UIInterfaceOrientationPortraitUpsideDown</string>
    <string>UIInterfaceOrientationLandscapeLeft</string>
    <string>UIInterfaceOrientationLandscapeRight</string>
  </array>
</dict>
</plist>
""")

inst_app = out / "ZizhanInstaller.app"
inst_app.mkdir(parents=True, exist_ok=True)
make_icon(inst_app / "AppIcon60x60@3x.png", (30, 118, 232), (18, 64, 156), (255, 255, 255))
write_plist(inst_app, "IPA 安装器", "ZizhanInstaller", "com.zizhan.installer")

demo_app = out / "demo_stage/Payload/ZizhanDemoApp.app"
demo_app.mkdir(parents=True, exist_ok=True)
make_icon(demo_app / "AppIcon60x60@3x.png", (22, 163, 106), (13, 92, 68), (255, 255, 255))
write_plist(demo_app, "扫码相机验证", "ZizhanDemoApp", "com.zizhan.demoapp")

suite_app = out / "suite_stage/Payload/ZizhanCompatSuite.app"
suite_app.mkdir(parents=True, exist_ok=True)
make_icon(suite_app / "AppIcon60x60@3x.png", (139, 92, 246), (76, 29, 149), (255, 255, 255))
write_plist(suite_app, "兼容套件验证", "ZizhanCompatSuite", "com.zizhan.compatsuite")
PY

# 1. Build & 4K-sign ZizhanInstaller.app v2 (/Applications/ZizhanInstaller.app)
"$CC" -arch arm64 -isysroot "$SDK" -miphoneos-version-min=18.0 -fobjc-arc -O2 \
  -framework Foundation -framework UIKit -framework CoreGraphics \
  "$SCRIPT_DIR/ZizhanInstaller.m" -o "$OUT_DIR/ZizhanInstaller.app/ZizhanInstaller"
cp "$SCRIPT_DIR/installer-entitlements.plist" "$OUT_DIR/installer-ent.plist"
"$CODESIGN" -f -s - --pagesize 4096 --entitlements "$OUT_DIR/installer-ent.plist" "$OUT_DIR/ZizhanInstaller.app"

# 2. Build & 16K-sign bare third-party ZizhanDemoApp-16k.ipa
"$CC" -arch arm64 -isysroot "$SDK" -miphoneos-version-min=18.0 -fobjc-arc -O2 \
  -framework Foundation -framework UIKit -framework AVFoundation -framework CoreLocation -framework QuartzCore \
  "$SCRIPT_DIR/ZizhanDemoApp.m" -o "$OUT_DIR/demo_stage/Payload/ZizhanDemoApp.app/ZizhanDemoApp"
"$CODESIGN" -f -s - --pagesize 16384 "$OUT_DIR/demo_stage/Payload/ZizhanDemoApp.app"
rm -f "$OUT_DIR/ZizhanDemoApp-16k.ipa"
(cd "$OUT_DIR/demo_stage" && /usr/bin/zip -qry "$OUT_DIR/ZizhanDemoApp-16k.ipa" Payload)

# 3. Build & 16K-sign bare third-party ZizhanCompatSuite-16k.ipa (8-subsystem test: Metal/Keychain/AppGroup/SQLite/WebKit/Periph/Loc/3-Cam)
"$CC" -arch arm64 -isysroot "$SDK" -miphoneos-version-min=18.0 -fobjc-arc -O2 \
  -framework Foundation -framework UIKit -framework CoreGraphics -framework Metal -framework Security -framework WebKit \
  -framework AVFoundation -framework AudioToolbox -framework CoreLocation -framework QuartzCore -lsqlite3 \
  "$SCRIPT_DIR/ZizhanCompatSuite.m" -o "$OUT_DIR/suite_stage/Payload/ZizhanCompatSuite.app/ZizhanCompatSuite"
"$CODESIGN" -f -s - --pagesize 16384 "$OUT_DIR/suite_stage/Payload/ZizhanCompatSuite.app"
rm -f "$OUT_DIR/ZizhanCompatSuite-16k.ipa"
(cd "$OUT_DIR/suite_stage" && /usr/bin/zip -qry "$OUT_DIR/ZizhanCompatSuite-16k.ipa" Payload)

# 4. Synthesize FairPlay-encrypted negative test IPA (LC_ENCRYPTION_INFO_64 cryptid=1)
python3 - "$OUT_DIR" << 'PY'
import pathlib, shutil, struct, subprocess, sys

out = pathlib.Path(sys.argv[1])
enc_app = out / "enc_stage/Payload/FairPlaySample.app"
if enc_app.parent.exists():
    shutil.rmtree(enc_app.parent)
enc_app.mkdir(parents=True)
(enc_app / "Info.plist").write_text("""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>FairPlaySample</string>
  <key>CFBundleIdentifier</key><string>com.zizhan.encrypted.sample</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleVersion</key><string>1</string>
</dict>
</plist>
""")
lc_enc = struct.pack("<IIIIII", 0x2C, 24, 4096, 4096, 1, 0)
hdr = struct.pack("<IIIIIIII", 0xFEEDFACF, 0x0100000C, 0, 2, 1, len(lc_enc), 0, 0)
buf = bytearray(8192)
buf[:len(hdr)] = hdr
buf[len(hdr):len(hdr)+len(lc_enc)] = lc_enc
(enc_app / "FairPlaySample").write_bytes(bytes(buf))
ipa = out / "FairPlayEncryptedSample.ipa"
if ipa.exists():
    ipa.unlink()
subprocess.run(["/usr/bin/zip", "-qry", str(ipa), "Payload"], cwd=str(out / "enc_stage"), check=True)
PY

cat > "$OUT_DIR/install-snippet.sh" << 'SNIP'
#!/bin/bash
set -euo pipefail
rm -rf "$M/Applications/ZizhanInstaller.app" "$M/pong/ipas"
cp -R /tmp/zizhan-apps/ZizhanInstaller.app "$M/Applications/ZizhanInstaller.app"
chown -R 0:0 "$M/Applications/ZizhanInstaller.app"
chmod -R 755 "$M/Applications/ZizhanInstaller.app"
chmod 644 "$M/Applications/ZizhanInstaller.app/Info.plist"
mkdir -p "$M/pong/ipas"
cp /tmp/zizhan-apps/ZizhanDemoApp-16k.ipa "$M/pong/ipas/ZizhanDemoApp-16k.ipa"
cp /tmp/zizhan-apps/ZizhanCompatSuite-16k.ipa "$M/pong/ipas/ZizhanCompatSuite-16k.ipa"
cp /tmp/zizhan-apps/FairPlayEncryptedSample.ipa "$M/pong/ipas/FairPlayEncryptedSample.ipa"
chown -R 0:0 "$M/pong/ipas"
chmod 755 "$M/pong/ipas"
chmod 644 "$M/pong/ipas"/*.ipa
SNIP
chmod +x "$OUT_DIR/install-snippet.sh"
echo "BUILT_ZIZHAN_APPS_V2_OK: $OUT_DIR"
