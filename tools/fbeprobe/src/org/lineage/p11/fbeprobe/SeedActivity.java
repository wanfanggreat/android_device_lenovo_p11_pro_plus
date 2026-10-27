package org.lineage.p11.fbeprobe;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;

/** Writes the DE and CE probe files while the user is unlocked. */
public class SeedActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        TextView text = new TextView(this);
        String result;
        try {
            String stamp = String.valueOf(System.currentTimeMillis());
            Probe.write(Probe.deFile(this), "P2C1-DE-" + stamp);
            Probe.write(Probe.ceFile(this), "P2C1-CE-" + stamp);
            result = "seed de_sha=" + Probe.sha256(Probe.deFile(this))
                    + " ce_sha=" + Probe.sha256(Probe.ceFile(this));
        } catch (Exception e) {
            result = "seed ERROR " + e.getClass().getSimpleName();
        }
        Probe.logStatus(this, "seed");
        text.setText(result);
        setContentView(text);
    }
}
