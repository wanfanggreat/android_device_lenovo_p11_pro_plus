// P2-D0-S: verify AIDL HAL declared instances exactly as keystore2's
// get_declared_instances() does (via servicemanager VINTF lookup).
// Build with `m declared_probe`, push from the intermediates dir.
// installable: false -> never enters system/vendor staging or an image.
#include <android/binder_manager.h>

#include <cstdio>

int main() {
    const char* names[] = {
            "android.hardware.security.keymint.IKeyMintDevice/default",
            "android.hardware.security.keymint.IKeyMintDevice/nonsecure",
            "android.hardware.security.keymint.IKeyMintDevice/strongbox",
            "android.hardware.security.secureclock.ISecureClock/default",
            "android.hardware.security.sharedsecret.ISharedSecret/default",
            "android.hardware.security.keymint.IRemotelyProvisionedComponent/default",
    };

    for (const char* name : names) {
        std::printf("%-88s %s\n", name,
                    AServiceManager_isDeclared(name) ? "DECLARED" : "NOT_DECLARED");
    }
    return 0;
}
