#!/bin/bash
# P2-C1-Final image guard.
#
# Two modes:
#   MODE=build  (default)
#       Validate a new complete image set. vendor, boot first-stage and
#       recovery fstab must all match the device tree; the vendor image must
#       have the target logical size (sparse-aware), AVB hashtree and a clean
#       ext4 filesystem.
#   MODE=device
#       Validate the known-good current flash set without reflashing boot.
#       vendor and recovery must match pinned sha256 values and carry the FBE
#       fstab; boot must match its pinned baseline sha256, but its first-stage
#       fstab check is skipped because the current boot predates the FBE
#       ramdisk. RUNTIME=1 (default) also checks the live device state.
#
# Both modes scan the vendor image for software KeyMint binaries/APEX and the
# complete vendor VINTF manifest (manifest.xml plus every fragment), and
# optionally the system image for KeyMint declarations.
#
# Usage:
#   tools/check-fbe-images.sh [PRODUCT_OUT]
#
# Environment overrides:
#   MODE=build|device
#   PRODUCT_OUT, VENDOR_IMG, BOOT_IMG, RECOVERY_IMG, SYSTEM_IMG, FSTAB_SRC
#   EXPECTED_VENDOR_SIZE (default 888160256; 0 skips the size check)
#   CHECK_AVB=1, CHECK_FSCK=1, CHECK_VINTF=0
#   RUNTIME=1, ADB=...
#   PINNED_{VENDOR,BOOT,RECOVERY}_SHA256
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
DEVICE_DIR=$(cd "$HERE/.." && pwd)
TREE=$(cd "$DEVICE_DIR/../../.." && pwd)

MODE=${MODE:-build}
PRODUCT_OUT=${PRODUCT_OUT:-${1:-$TREE/out/target/product/p11_pro_plus}}
VENDOR_IMG=${VENDOR_IMG:-$PRODUCT_OUT/vendor.img}
BOOT_IMG=${BOOT_IMG:-$PRODUCT_OUT/boot.img}
RECOVERY_IMG=${RECOVERY_IMG:-$PRODUCT_OUT/recovery.img}
SYSTEM_IMG=${SYSTEM_IMG:-}
FSTAB_SRC=${FSTAB_SRC:-$DEVICE_DIR/rootdir/etc/fstab.qcom}
EXPECTED_VENDOR_SIZE=${EXPECTED_VENDOR_SIZE:-888160256}
CHECK_AVB=${CHECK_AVB:-1}
CHECK_FSCK=${CHECK_FSCK:-1}
CHECK_VINTF=${CHECK_VINTF:-0}
RUNTIME=${RUNTIME:-}
ADB=${ADB:-}

PINNED_VENDOR_SHA256=${PINNED_VENDOR_SHA256:-dcc47e82041b7a439e66d55769a5f2dedd5d788e7e10552a07971bf6a49bc8ab}
PINNED_BOOT_SHA256=${PINNED_BOOT_SHA256:-11ff8e0fadf9d0457da300a9c9f2e2bce0d35a1e9ea2ef9856e4a53fce691040}
PINNED_RECOVERY_SHA256=${PINNED_RECOVERY_SHA256:-e5c61a49e8d1dc62b8535f621a96c1935cdec35e8efdcf549a679d93b53dacd0}

S2I=$TREE/out/host/linux-x86/bin/simg2img
UNPACK=$TREE/out/host/linux-x86/bin/unpack_bootimg
LZ4=${LZ4:-$(command -v lz4 2>/dev/null || printf '%s' "$TREE/out/host/linux-x86/bin/lz4")}
AVBTOOL=${AVBTOOL:-$TREE/out/host/linux-x86/bin/avbtool}
CV=$TREE/out/host/linux-x86/bin/checkvintf
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
FAIL=0

case "$MODE" in
    build|device) ;;
    *) echo "FAIL: MODE must be 'build' or 'device' (got '$MODE')"; exit 2 ;;
esac

if [ -z "$RUNTIME" ]; then
    if [ "$MODE" = device ]; then RUNTIME=1; else RUNTIME=0; fi
fi
if [ "$MODE" = build ] && [ -z "$SYSTEM_IMG" ] && [ -f "$PRODUCT_OUT/system.img" ]; then
    SYSTEM_IMG=$PRODUCT_OUT/system.img
fi

pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*"; FAIL=1; }

require_tool() {
    if [ -x "$1" ]; then
        pass "tool $(basename "$1") present"
    else
        fail "tool $1 missing; build the host tools first"
        return 1
    fi
}

# Extract one file from a newc cpio archive. Decompression is done by the
# caller so this stays independent of gzip/lz4 availability in Python.
CPIO_EXTRACT='
import os, sys
src, dst, want = sys.argv[1], sys.argv[2], sys.argv[3]
data = open(src, "rb").read()
off = 0
while True:
    if data[off:off+6] not in (b"070701", b"070702"):
        sys.exit("bad cpio magic at %d" % off)
    f = [int(data[off+6+8*i:off+14+8*i], 16) for i in range(13)]
    filesize, namesize = f[6], f[11]
    name_start = off + 110
    name = data[name_start:name_start+namesize-1].decode("utf-8", "replace")
    data_start = (name_start + namesize + 3) & ~3
    if name == "TRAILER!!!":
        sys.exit("not found in ramdisk: " + want)
    if name == want:
        parent = os.path.dirname(dst)
        if parent:
            os.makedirs(parent, exist_ok=True)
        with open(dst, "wb") as out:
            out.write(data[data_start:data_start+filesize])
        sys.exit(0)
    off = (data_start + filesize + 3) & ~3
'

image_is_sparse() {
    [ "$(od -An -tx1 -N4 "$1" | tr -d ' \n')" = "3aff26ed" ]
}

sparse_logical_size() {
    python3 - "$1" <<'PY'
import struct, sys
with open(sys.argv[1], "rb") as f:
    h = f.read(28)
if len(h) < 28 or struct.unpack_from("<I", h, 0)[0] != 0xed26ff3a:
    sys.exit(1)
blk_sz = struct.unpack_from("<I", h, 12)[0]
total_blks = struct.unpack_from("<I", h, 16)[0]
if blk_sz == 0 or total_blks == 0:
    sys.exit(1)
print(blk_sz * total_blks)
PY
}

image_logical_size() {
    if image_is_sparse "$1"; then
        sparse_logical_size "$1"
    else
        stat -c%s "$1"
    fi
}

expand_image() { # image, raw destination; prints the raw path
    if image_is_sparse "$1"; then
        "$S2I" "$1" "$2" >/dev/null 2>&1 || return 1
        printf '%s' "$2"
    else
        printf '%s' "$1"
    fi
}

extract_ramdisk_file() { # image, outfile, inner path
    local img=$1 out=$2 inner=$3 ramdisk cpio
    rm -rf "$WORK/boot"
    mkdir -p "$WORK/boot"
    if ! "$UNPACK" --boot_img "$img" --out "$WORK/boot" >/dev/null 2>&1; then
        fail "$(basename "$img"): unpack_bootimg failed"
        return 1
    fi
    ramdisk=$WORK/boot/ramdisk
    cpio=$WORK/ramdisk.cpio
    case $(file -b "$ramdisk") in
        gzip*)  gzip -dc "$ramdisk" > "$cpio" ;;
        LZ4*|lz4*)
            if [ ! -x "$LZ4" ]; then
                fail "$(basename "$img"): lz4 ramdisk but no lz4 tool"
                return 1
            fi
            "$LZ4" -d -c "$ramdisk" > "$cpio" ;;
        *)      cp "$ramdisk" "$cpio" ;;
    esac || { fail "$(basename "$img"): cannot decompress ramdisk"; return 1; }
    if ! python3 -c "$CPIO_EXTRACT" "$cpio" "$out" "$inner"; then
        fail "$(basename "$img"): $inner not found in ramdisk"
        return 1
    fi
    pass "$(basename "$img"): extracted $inner"
}

check_pinned_sha() { # label, file, expected
    local label=$1 file=$2 expected=$3 actual
    actual=$(sha256sum "$file" | awk '{print $1}')
    if [ "$actual" = "$expected" ]; then
        pass "$label: pinned sha256 matches ($actual)"
    else
        fail "$label: sha256 $actual != pinned $expected"
    fi
}

check_fstab() { # label, file
    local label=$1 file=$2 line opts
    if [ ! -s "$file" ]; then
        fail "$label: missing or empty"
        return
    fi
    line=$(awk '$2 == "/data" { print; exit }' "$file")
    if [ -z "$line" ]; then
        fail "$label: no /data entry"
        return
    fi
    if cmp -s "$file" "$FSTAB_SRC"; then
        pass "$label: identical to device tree fstab.qcom"
    else
        fail "$label: differs from device tree fstab.qcom"
    fi
    opts=$(awk '$2 == "/data" { print $4; exit }' "$file")
    case ",$opts," in
        *,inlinecrypt,*) pass "$label: /data mount option has inlinecrypt" ;;
        *) fail "$label: /data mount option missing inlinecrypt" ;;
    esac
    for token in \
        'fileencryption=aes-256-xts:aes-256-cts:v2+inlinecrypt_optimized+wrappedkey_v0' \
        'metadata_encryption=aes-256-xts:wrappedkey_v0' \
        'keydirectory=/metadata/vold/metadata_encryption'; do
        if grep -qF -- "$token" <<<"$line"; then
            pass "$label: $token"
        else
            fail "$label: missing $token"
        fi
    done
}

image_file_exists() { # raw image, absolute path
    debugfs -R "stat $2" "$1" 2>/dev/null | grep -q 'Inode:'
}

scan_manifest_tree() { # raw image, base prefix ("" or "/system")
    local raw=$1 base=$2 all="" names n
    all+=$(debugfs -R "cat $base/etc/vintf/manifest.xml" "$raw" 2>/dev/null) || true
    names=$(debugfs -R "ls -l $base/etc/vintf/manifest" "$raw" 2>/dev/null |
            awk '{print $NF}' | grep '\.xml$' || true)
    for n in $names; do
        all+=$'\n'$(debugfs -R "cat $base/etc/vintf/manifest/$n" "$raw" 2>/dev/null) || true
    done
    printf '%s' "$all"
}

check_keymint_binaries() { # label, raw image, bin/hw path, apex path
    local label=$1 raw=$2 hw=$3 apex=$4
    if debugfs -R "ls -l $hw" "$raw" 2>/dev/null | grep -qi keymint; then
        fail "$label: keymint binary present in $hw"
    else
        pass "$label: no keymint binary in $hw"
    fi
    if debugfs -R "ls -l $apex" "$raw" 2>/dev/null | grep -qi keymint; then
        fail "$label: keymint APEX present in $apex"
    else
        pass "$label: no keymint APEX in $apex"
    fi
}

check_manifest_absent() { # label, manifest text, patterns...
    local label=$1 man=$2
    shift 2
    local pat
    for pat in "$@"; do
        if grep -q "$pat" <<<"$man"; then
            fail "$label: $pat declared"
        else
            pass "$label: no $pat"
        fi
    done
}

check_vendor_avb() { # label, raw
    local label=$1 raw=$2
    if [ "$CHECK_AVB" != 1 ]; then
        return
    fi
    if [ ! -x "$AVBTOOL" ]; then
        fail "$label: avbtool missing ($AVBTOOL)"
        return
    fi
    if ! "$AVBTOOL" info_image --image "$raw" >"$WORK/avb.txt" 2>&1; then
        fail "$label: avbtool info_image failed"
        return
    fi
    if [ "$EXPECTED_VENDOR_SIZE" -gt 0 ]; then
        if grep -q "Image size: *$EXPECTED_VENDOR_SIZE" "$WORK/avb.txt"; then
            pass "$label: AVB footer image size $EXPECTED_VENDOR_SIZE"
        else
            fail "$label: AVB footer image size != $EXPECTED_VENDOR_SIZE"
        fi
    fi
    if grep -q "Hashtree descriptor" "$WORK/avb.txt"; then
        pass "$label: AVB hashtree descriptor present"
    else
        fail "$label: AVB hashtree descriptor missing"
    fi
}

check_vendor_fsck() { # label, raw
    local label=$1 raw=$2
    if [ "$CHECK_FSCK" != 1 ]; then
        return
    fi
    if ! command -v e2fsck >/dev/null 2>&1; then
        fail "$label: e2fsck missing"
        return
    fi
    if timeout 120 e2fsck -fn "$raw" >"$WORK/e2fsck.log" 2>&1; then
        pass "$label: e2fsck -fn clean"
    else
        fail "$label: e2fsck -fn failed"
        tail -10 "$WORK/e2fsck.log"
    fi
}

runtime_checks() {
    local adb=$ADB
    if [ -z "$adb" ]; then
        if [ -x /mnt/d/Downloads/platform-tools-latest-windows/platform-tools/adb.exe ]; then
            adb=/mnt/d/Downloads/platform-tools-latest-windows/platform-tools/adb.exe
        else
            adb=adb
        fi
    fi
    if ! command -v "$adb" >/dev/null 2>&1 && [ ! -x "$adb" ]; then
        fail "runtime: adb not found ($adb)"
        return
    fi
    local values slot crypto meta boot data ustate
    if ! values=$("$adb" shell 'getprop ro.boot.slot_suffix; getprop ro.crypto.state; getprop ro.crypto.metadata.enabled; getprop sys.boot_completed' 2>/dev/null); then
        fail "runtime: adb shell failed"
        return
    fi
    slot=$(sed -n '1p' <<<"$values" | tr -d '\r')
    crypto=$(sed -n '2p' <<<"$values" | tr -d '\r')
    meta=$(sed -n '3p' <<<"$values" | tr -d '\r')
    boot=$(sed -n '4p' <<<"$values" | tr -d '\r')
    [ "$slot" = "_a" ] && pass "runtime: slot is _a" || fail "runtime: slot is '$slot', expected _a"
    [ "$crypto" = "encrypted" ] && pass "runtime: ro.crypto.state=encrypted" || fail "runtime: ro.crypto.state='$crypto'"
    [ "$meta" = "true" ] && pass "runtime: metadata encryption enabled" || fail "runtime: metadata encryption is '$meta'"
    [ "$boot" = "1" ] && pass "runtime: sys.boot_completed=1" || fail "runtime: sys.boot_completed='$boot'"
    data=$("$adb" shell 'mount | grep " /data "' 2>/dev/null | tr -d '\r')
    if grep -q '/dev/block/dm-' <<<"$data"; then
        pass "runtime: /data is a device-mapper device"
    else
        fail "runtime: /data is not dm-* ($data)"
    fi
    ustate=$("$adb" shell 'dumpsys user' 2>/dev/null | grep -m1 'State: RUNNING_' | tr -d '\r')
    if [ -n "$ustate" ]; then
        pass "runtime: user 0 $ustate"
    else
        fail "runtime: cannot read user 0 state"
    fi
}

echo "== inputs =="
echo "mode:     $MODE"
echo "device tree fstab: $FSTAB_SRC"
echo "vendor:   $VENDOR_IMG"
echo "boot:     $BOOT_IMG"
echo "recovery: $RECOVERY_IMG"
[ -n "$SYSTEM_IMG" ] && echo "system:   $SYSTEM_IMG"
for f in "$FSTAB_SRC" "$VENDOR_IMG" "$BOOT_IMG" "$RECOVERY_IMG"; do
    if [ -f "$f" ]; then
        pass "present: $f"
    else
        fail "missing: $f"
    fi
done

if [ "$MODE" = device ] && [ -f "$VENDOR_IMG" ] && [ -f "$BOOT_IMG" ] && [ -f "$RECOVERY_IMG" ]; then
    check_pinned_sha "vendor" "$VENDOR_IMG" "$PINNED_VENDOR_SHA256"
    check_pinned_sha "boot" "$BOOT_IMG" "$PINNED_BOOT_SHA256"
    check_pinned_sha "recovery" "$RECOVERY_IMG" "$PINNED_RECOVERY_SHA256"
fi

if [ -f "$VENDOR_IMG" ] && [ "$EXPECTED_VENDOR_SIZE" -gt 0 ]; then
    if image_is_sparse "$VENDOR_IMG"; then
        vtype=sparse
    else
        vtype=raw
    fi
    if ! logical=$(image_logical_size "$VENDOR_IMG"); then
        fail "vendor: cannot read $vtype logical size"
    elif [ "$logical" = "$EXPECTED_VENDOR_SIZE" ]; then
        pass "vendor: $vtype logical size $logical matches partition target"
    else
        fail "vendor: $vtype logical size $logical != target $EXPECTED_VENDOR_SIZE (fastboot would resize vendor_a)"
    fi
fi
require_tool "$S2I" || true
require_tool "$UNPACK" || true

if [ "$FAIL" -ne 0 ]; then
    echo
    echo "RESULT: FAIL (preflight: missing inputs/tools or vendor size mismatch)"
    exit 1
fi

# ---------- vendor fstab + security stack ----------
echo
echo "== vendor =="
VRAW=$(expand_image "$VENDOR_IMG" "$WORK/vendor.raw") || { fail "cannot expand vendor image"; VRAW=""; }
if [ -n "$VRAW" ]; then
    debugfs -R "cat /etc/fstab.qcom" "$VRAW" >"$WORK/vendor.fstab" 2>/dev/null || true
    check_fstab "vendor /etc/fstab.qcom" "$WORK/vendor.fstab"
    check_vendor_avb "vendor" "$VRAW"
    check_vendor_fsck "vendor" "$VRAW"

    check_keymint_binaries "vendor" "$VRAW" /bin/hw /apex
    VMAN=$(scan_manifest_tree "$VRAW" "")
    check_manifest_absent "vendor" "$VMAN" \
        'android.hardware.security.keymint' \
        'android.hardware.security.secureclock' \
        'android.hardware.security.sharedsecret' \
        'IRemotelyProvisionedComponent'
    if grep -q '@4.1::IKeymasterDevice/default' <<<"$VMAN"; then
        pass "vendor: HIDL Keymaster 4.1/default declared"
    else
        fail "vendor: HIDL Keymaster 4.1/default missing"
    fi
    if grep -q '<name>android.hardware.power</name>' <<<"$VMAN" &&
       grep -q 'IPower/default' <<<"$VMAN"; then
        pass "vendor: AIDL Power/default declared"
    else
        fail "vendor: AIDL Power/default missing"
    fi
    if debugfs -R "ls -l /bin/hw" "$VRAW" 2>/dev/null | grep -q 'android.hardware.power-service-qti$'; then
        pass "vendor: QTI AIDL Power HAL binary present"
    else
        fail "vendor: QTI AIDL Power HAL binary missing"
    fi
    if debugfs -R "ls -l /bin/hw" "$VRAW" 2>/dev/null | grep -q 'android.hardware.keymaster@4.1-service-qti$'; then
        pass "vendor: HIDL Keymaster 4.1 service present"
    else
        fail "vendor: HIDL Keymaster 4.1 service missing"
    fi
fi

# ---------- boot first-stage fstab ----------
echo
echo "== boot =="
if [ "$MODE" = device ]; then
    pass "boot: device mode uses the pinned baseline boot; first-stage FBE fstab check skipped"
else
    if extract_ramdisk_file "$BOOT_IMG" "$WORK/boot.fstab" first_stage_ramdisk/fstab.qcom; then
        check_fstab "boot first_stage_ramdisk/fstab.qcom" "$WORK/boot.fstab"
    fi
fi

# ---------- recovery fstab ----------
echo
echo "== recovery =="
if extract_ramdisk_file "$RECOVERY_IMG" "$WORK/recovery.fstab" system/etc/recovery.fstab; then
    check_fstab "recovery system/etc/recovery.fstab" "$WORK/recovery.fstab"
fi

# ---------- optional system image ----------
if [ -n "$SYSTEM_IMG" ]; then
    echo
    echo "== system =="
    if [ ! -f "$SYSTEM_IMG" ]; then
        fail "system image missing: $SYSTEM_IMG"
    else
        SRAW=$(expand_image "$SYSTEM_IMG" "$WORK/system.raw") || { fail "cannot expand system image"; SRAW=""; }
        if [ -n "$SRAW" ]; then
            check_keymint_binaries "system" "$SRAW" /system/bin/hw /system/apex
            SMAN=$(scan_manifest_tree "$SRAW" /system)
            check_manifest_absent "system" "$SMAN" \
                'android.hardware.security.keymint' \
                'android.hardware.security.secureclock' \
                'android.hardware.security.sharedsecret' \
                'IRemotelyProvisionedComponent'
            if image_file_exists "$SRAW" /system/bin/keystore2 ||
               image_file_exists "$SRAW" /bin/keystore2; then
                pass "system: keystore2 present"
            else
                fail "system: keystore2 missing"
            fi
            if image_file_exists "$SRAW" /system/lib64/libkm_compat.so ||
               image_file_exists "$SRAW" /lib64/libkm_compat.so; then
                pass "system: libkm_compat.so present"
            else
                fail "system: libkm_compat.so missing"
            fi
        fi
    fi
fi

# ---------- optional merged VINTF compatibility ----------
if [ "$CHECK_VINTF" = 1 ]; then
    echo
    echo "== checkvintf =="
    if [ "$MODE" != build ]; then
        echo "INFO: CHECK_VINTF is only meaningful in MODE=build; skipped"
    elif [ ! -x "$CV" ]; then
        fail "checkvintf host tool missing"
    else
        APEX=$PRODUCT_OUT/obj/PACKAGING/check_vintf_all_intermediates/apex
        if "$CV" --check-compat \
            --dirmap /system:"$PRODUCT_OUT/system" \
            --dirmap /vendor:"$PRODUCT_OUT/vendor" \
            --dirmap /system_ext:"$PRODUCT_OUT/system_ext" \
            --dirmap /product:"$PRODUCT_OUT/product" \
            --dirmap /odm:"$PRODUCT_OUT/odm" \
            --dirmap /apex:"$APEX" \
            -D ro.product.first_api_level=30 \
            -D ro.board.api_level=202604 \
            -D ro.vendor.api_level=30 \
            -D ro.boot.product.hardware.sku= \
            >"$WORK/checkvintf.log" 2>&1; then
            pass "checkvintf COMPATIBLE"
        else
            fail "checkvintf failed"
            tail -20 "$WORK/checkvintf.log"
        fi
    fi
fi

# ---------- runtime ----------
if [ "$RUNTIME" = 1 ]; then
    echo
    echo "== runtime =="
    runtime_checks
fi

echo
if [ "$FAIL" -ne 0 ]; then
    echo "RESULT: FAIL"
    exit 1
fi
echo "RESULT: PASS"
