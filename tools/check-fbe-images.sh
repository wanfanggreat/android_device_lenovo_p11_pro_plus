#!/bin/bash
# P2-C1-Final image guard.
#
# The validated wrappedkey_v0 fstab is installed in three places and all three
# must match rootdir/etc/fstab.qcom byte for byte:
#   vendor.img   /etc/fstab.qcom
#   boot.img     first_stage_ramdisk/fstab.qcom
#   recovery.img system/etc/recovery.fstab
#
# This script also verifies that the vendor image still carries the QTI AIDL
# Power HAL and the HIDL Keymaster 4.1 service/manifest, and that no nonsecure
# KeyMint stack was pulled in.
#
# Usage:
#   tools/check-fbe-images.sh [PRODUCT_OUT]
#
# Environment overrides:
#   PRODUCT_OUT, VENDOR_IMG, BOOT_IMG, RECOVERY_IMG, FSTAB_SRC
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
DEVICE_DIR=$(cd "$HERE/.." && pwd)
TREE=$(cd "$DEVICE_DIR/../../.." && pwd)

PRODUCT_OUT=${PRODUCT_OUT:-${1:-$TREE/out/target/product/p11_pro_plus}}
VENDOR_IMG=${VENDOR_IMG:-$PRODUCT_OUT/vendor.img}
BOOT_IMG=${BOOT_IMG:-$PRODUCT_OUT/boot.img}
RECOVERY_IMG=${RECOVERY_IMG:-$PRODUCT_OUT/recovery.img}
FSTAB_SRC=${FSTAB_SRC:-$DEVICE_DIR/rootdir/etc/fstab.qcom}

S2I=$TREE/out/host/linux-x86/bin/simg2img
UNPACK=$TREE/out/host/linux-x86/bin/unpack_bootimg
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
FAIL=0

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
        LZ4*|lz4*) lz4 -d -c "$ramdisk" > "$cpio" ;;
        *)      cp "$ramdisk" "$cpio" ;;
    esac || { fail "$(basename "$img"): cannot decompress ramdisk"; return 1; }
    if ! python3 -c "$CPIO_EXTRACT" "$cpio" "$out" "$inner"; then
        fail "$(basename "$img"): $inner not found in ramdisk"
        return 1
    fi
    pass "$(basename "$img"): extracted $inner"
}

vendor_raw() { # image -> prints raw path
    local img=$1 raw=$WORK/vendor.raw
    if [ "$(od -An -tx1 -N4 "$img" | tr -d ' \n')" = "3aff26ed" ]; then
        "$S2I" "$img" "$raw" >/dev/null 2>&1 || return 1
        printf '%s\n' "$raw"
    else
        printf '%s\n' "$img"
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

scan_manifest_tree() { # raw image
    local raw=$1 all="" names n
    all+=$(debugfs -R "cat /etc/vintf/manifest.xml" "$raw" 2>/dev/null) || true
    names=$(debugfs -R "ls -l /etc/vintf/manifest" "$raw" 2>/dev/null |
            awk '{print $NF}' | grep '\.xml$' || true)
    for n in $names; do
        all+=$'\n'$(debugfs -R "cat /etc/vintf/manifest/$n" "$raw" 2>/dev/null) || true
    done
    printf '%s' "$all"
}

echo "== inputs =="
echo "device tree fstab: $FSTAB_SRC"
echo "vendor:   $VENDOR_IMG"
echo "boot:     $BOOT_IMG"
echo "recovery: $RECOVERY_IMG"
for f in "$FSTAB_SRC" "$VENDOR_IMG" "$BOOT_IMG" "$RECOVERY_IMG"; do
    if [ -f "$f" ]; then
        pass "present: $f"
    else
        fail "missing: $f"
    fi
done
require_tool "$S2I" || true
require_tool "$UNPACK" || true

if [ "$FAIL" -ne 0 ]; then
    echo
    echo "RESULT: FAIL (missing inputs or tools)"
    exit 1
fi

# ---------- vendor fstab + security stack ----------
echo
echo "== vendor =="
VRAW=$(vendor_raw "$VENDOR_IMG") || { fail "cannot unsparse vendor image"; VRAW=""; }
if [ -n "$VRAW" ]; then
    debugfs -R "cat /etc/fstab.qcom" "$VRAW" >"$WORK/vendor.fstab" 2>/dev/null || true
    check_fstab "vendor /etc/fstab.qcom" "$WORK/vendor.fstab"

    VLS() { debugfs -R "ls -l $1" "$VRAW" 2>/dev/null; }
    if VLS /bin/hw | grep -q 'android.hardware.power-service-qti$'; then
        pass "vendor: QTI AIDL Power HAL binary present"
    else
        fail "vendor: QTI AIDL Power HAL binary missing"
    fi
    if VLS /bin/hw | grep -q 'android.hardware.keymaster@4.1-service-qti$'; then
        pass "vendor: HIDL Keymaster 4.1 service present"
    else
        fail "vendor: HIDL Keymaster 4.1 service missing"
    fi

    VMAN=$(scan_manifest_tree "$VRAW")
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
    for pat in \
        'android.hardware.security.keymint' \
        'android.hardware.security.secureclock' \
        'android.hardware.security.sharedsecret' \
        'IRemotelyProvisionedComponent'; do
        if grep -q "$pat" <<<"$VMAN"; then
            fail "vendor: $pat declared"
        else
            pass "vendor: no $pat"
        fi
    done
fi

# ---------- boot first-stage fstab ----------
echo
echo "== boot first-stage =="
if extract_ramdisk_file "$BOOT_IMG" "$WORK/boot.fstab" first_stage_ramdisk/fstab.qcom; then
    check_fstab "boot first_stage_ramdisk/fstab.qcom" "$WORK/boot.fstab"
fi

# ---------- recovery fstab ----------
echo
echo "== recovery =="
if extract_ramdisk_file "$RECOVERY_IMG" "$WORK/recovery.fstab" system/etc/recovery.fstab; then
    check_fstab "recovery system/etc/recovery.fstab" "$WORK/recovery.fstab"
fi

echo
if [ "$FAIL" -ne 0 ]; then
    echo "RESULT: FAIL"
    exit 1
fi
echo "RESULT: PASS"
