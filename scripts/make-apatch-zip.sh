#!/bin/bash
# APatch variant builder for NoVA kernel zips.
#
# Takes an existing AK3 kernel zip (default: latest NoVA-KSU-*.zip built by
# nova-build.sh), kernel-patches the contained Image.gz with KernelPatch
# (kptools + kpimg), and writes NoVA-KSU-APatch-*.zip.
#
# Requires python3 with zipfile (Arch Linux default) and unzip.
#
# Usage:
#   scripts/make-apatch-zip.sh [kernel.zip] [superkey]
#
# superkey protects KernelPatch SuperCall ops; 16+ [A-Za-z0-9] chars.
set -euo pipefail

SRC_ZIP="${1:-}"
KEY="${2:-}"
TOOLS_DIR="$(cd "$(dirname "$0")" && pwd)/apatch-tools"
KTOOLS="$TOOLS_DIR/kptools-linux"
KPIMG="$TOOLS_DIR/kpimg-android"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

if [ -z "$SRC_ZIP" ]; then
    SRC_ZIP="$(ls -t NoVA-KSU-*.zip 2>/dev/null | head -1)"
fi
if [ -z "$SRC_ZIP" ] || [ ! -f "$SRC_ZIP" ]; then
    echo "No source kernel zip found. Build one first (./nova-build.sh -k) or pass a zip path." >&2
    exit 1
fi
if [ -z "$KEY" ]; then
    echo "No superkey given; using default 'opencode'. Set one for real APatch security." >&2
    KEY="opencode"
fi

echo "== Source: $SRC_ZIP"
echo "== Superkey: $KEY"

cd "$WORK"
unzip -q "$OLDPWD/$SRC_ZIP"
[ -f Image.gz ] || { echo "Image.gz missing in zip" >&2; exit 1; }

echo "== split kernel gz / dtb"
python3 - <<'PY'
import os
data=open("Image.gz","rb").read()            # this entry has gz only (dtb is a separate entry)
open("kernel.gz","wb").write(data)
PY
[ -f dtb ] || { echo "dtb entry missing in zip" >&2; exit 1; }

echo "== decompress kernel"
gunzip -kf Image.gz

echo "== kernel-patch with kptools"
if "$KTOOLS" -h 2>&1 | grep -q -- "--kpimg"; then
    # legacy KernelPatch CLI (0.7.x-era): -p <img> --kpimg --skey --out
    "$KTOOLS" -p Image --kpimg "$KPIMG" --skey "$KEY" --out Image-patched
else
    "$KTOOLS" -p -i Image -k "$KPIMG" -o Image-patched -s "$KEY"
fi
"$KTOOLS" -l -i Image-patched 2>/dev/null | sed -n '1,12p' || true

echo "== repack gz + dtb, rebuild zip"
gzip -9 -c Image-patched > Image.gz
rm -f Image Image-patched

BASE="$(basename "$SRC_ZIP" .zip)"
case "$BASE" in
    NoVA-KSU-*) OUT="NoVA-KSU-APatch-${BASE#NoVA-KSU-}.zip" ;;
    NoVA-*)     OUT="NoVA-APatch-${BASE#NoVA-}.zip" ;;
    *)          OUT="APatch-${BASE}.zip" ;;
esac
python3 - <<PY
import zipfile
OUT="$OUT"
zin=zipfile.ZipFile("$OLDPWD/$SRC_ZIP")
zout=zipfile.ZipFile(OUT,"w",zipfile.ZIP_DEFLATED)
newgz=open("Image.gz","rb").read()
for i in zin.infolist():
    data=zin.read(i.filename)
    if i.filename=="Image.gz":
        data=newgz
    zout.writestr(i,data)
zout.close()
print("zz wrote", OUT)
PY
rm -f Image.gz

echo "== differential (only Image.gz may differ)"
python3 - <<PY
import zipfile,hashlib
A=zipfile.ZipFile("$OLDPWD/$SRC_ZIP"); B=zipfile.ZipFile("$WORK/$OUT")
diff=[k for k in A.infolist() if A.read(k.filename)!=B.read(k.filename)]
print("differing:", [d.filename for d in diff])
assert [d.filename for d in diff]==["Image.gz"], "UNEXPECTED extra changes!"
print("APatch variant OK")
PY

cp "$WORK/$OUT" "$OLDPWD/$OUT"
echo "== Done: $OLDPWD/$OUT"