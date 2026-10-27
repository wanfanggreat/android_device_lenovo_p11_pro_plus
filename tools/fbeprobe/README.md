# FbeProbe

Diagnostic app for the P2-C1 PIN cold-boot / DE-CE isolation test. It is
**not** part of any image (not listed in `PRODUCT_PACKAGES`); build it with
`m FbeProbe` and install with `adb install -r`.

## Usage

```bash
cd /home/gaoming/android/lineage-24
unset -f grep; source build/envsetup.sh
lunch lineage_p11_pro_plus cp2a userdebug
m FbeProbe

ADB=/mnt/d/Downloads/platform-tools-latest-windows/platform-tools/adb.exe
APK=$(find out/target/product/p11_pro_plus -name FbeProbe.apk | head -1)
"$ADB" install -r "$APK"

# Write DE/CE probes while unlocked and log their SHA-256.
"$ADB" shell am start -n org.lineage.p11.fbeprobe/.SeedActivity
"$ADB" logcat -d -s FBE_PROBE

# On-demand re-check (works unlocked; use --receiver-foreground so the
# manifest receiver is not deferred as a background broadcast).
"$ADB" shell am broadcast --receiver-foreground \
    -a org.lineage.p11.fbeprobe.VERIFY -p org.lineage.p11.fbeprobe
"$ADB" logcat -d -s FBE_PROBE
```

`LOCKED_BOOT_COMPLETED` and `BOOT_COMPLETED` are logged automatically at
boot. Do not use root/shell to read `/data/user/0` or `/data/user_de/0`;
the whole point is to exercise the normal storage API boundary.

Log tag: `FBE_PROBE`. The payload strings contain no sensitive data.
