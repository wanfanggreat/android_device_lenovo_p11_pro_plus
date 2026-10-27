package org.lineage.p11.fbeprobe;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * Runs at LOCKED_BOOT_COMPLETED (direct boot, user still locked) and at
 * BOOT_COMPLETED (after first unlock). It never bypasses the storage API:
 * CE access is attempted exactly as a normal app would.
 */
public class ProbeReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        String action = intent.getAction() == null ? "unknown" : intent.getAction();
        Probe.logStatus(context, action);
    }
}
