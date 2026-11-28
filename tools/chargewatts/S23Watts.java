package com.android.systemui.statusbar;

import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.TextUtils;
import java.io.BufferedReader;
import java.io.FileReader;
import java.util.Locale;

/** s23-ultra-tweaks (chargewatts): add the power going into the battery to the lock screen's charging line,
 * and what holds it back when it's well under the 45 W top speed. */
public final class S23Watts {
    private static final String B = "/sys/class/power_supply/battery/";

    private static String read(String name) throws Exception {
        try (BufferedReader r = new BufferedReader(new FileReader(B + name))) {
            String s = r.readLine();
            return s == null ? "" : s.trim();
        }
    }

    private static String readAll(String name) throws Exception {
        StringBuilder b = new StringBuilder();
        try (BufferedReader r = new BufferedReader(new FileReader(B + name))) {
            for (String l; (l = r.readLine()) != null; ) b.append(l).append('\n');
        }
        return b.toString();
    }

    /** the voter that sets a limit in voter_status ("FCC: voter=VOTER_LRP_TEMP type=Min v=3400"), or "" */
    private static String winner(String votes, String limit) {
        int i = votes.indexOf(limit + ": voter=");
        if (i < 0) return "";
        int a = i + limit.length() + 8, e = a;
        while (e < votes.length() && votes.charAt(e) > ' ') e++;
        return votes.substring(a, e);
    }

    /** the limit's value in mV/mA ("... type=Min v=4285"), or -1 */
    private static int value(String votes, String limit) {
        int i = votes.indexOf(limit + ": voter=");
        if (i < 0) return -1;
        int a = votes.indexOf(" v=", i), e = a + 3;
        if (a < 0) return -1;
        while (e < votes.length() && Character.isDigit(votes.charAt(e))) e++;
        try { return Integer.parseInt(votes.substring(a + 3, e)); } catch (Throwable t) { return -1; }
    }

    private static String label(String v) {
        switch (v) {
            case "VOTER_LRP_TEMP": return "phone warm";
            case "VOTER_SIOP": return "phone in use";
            case "VOTER_SWELLING": return "battery temperature";
            case "VOTER_CHG_TEMP": case "VOTER_DCHG_TEMP": case "VOTER_DC_TEMP": return "charger chip warm";
            case "VOTER_STEP": case "VOTER_DC_STEP_CHARGE": case "VOTER_AGING_STEP": case "VOTER_FULL_CHARGE": return "tapering";
            case "VOTER_CHG_LIMIT": case "VOTER_SLEEP_MODE": case "VOTER_USER": case "VOTER_SLATE": case "VOTER_SMART_SLATE": return "battery protection";
            case "": case "None": case "INIT": case "VOTER_CABLE": case "VOTER_SELECT_PDO": case "VOTER_DC_MODE": return "";
            default: return v.replace("VOTER_", "").replace('_', ' ').toLowerCase(Locale.ROOT);
        }
    }

    /** why it's charging slower than 45 W: the kernel's winning current limit, and the charger's class */
    static String reason() {
        StringBuilder r = new StringBuilder();
        try {
            String votes = readAll("voter_status");
            String why = label(winner(votes, "FCC"));             // battery current
            if (why.isEmpty()) why = label(winner(votes, "ICL")); // charger input current
            if (why.isEmpty()) {
                // no current limit wins: the battery is near the voltage the charger holds it at (direct
                // charging steps it up with the level, normal charging at the end), so the current tapers off
                int mv = (int) (Long.parseLong(read("voltage_now")) / 1000);
                int dc = "1".equals(read("direct_charging_status")) ? value(votes, "DCFV") : -1;
                int fv = dc > 0 ? dc : value(votes, "FV");
                if (fv > 0 && mv >= fv - 100) why = "tapering at " + read("capacity") + "%";
            }
            r.append(why);
        } catch (Throwable ignored) { }
        try {   // 4 = 45 W (Super fast charging 2.0), 3 = 25 W, 1-2 = 15/20 W, 0 = normal
            int hv = Integer.parseInt(read("hv_charger_status"));
            String c = hv == 3 ? "25 W charger" : hv == 1 || hv == 2 ? "15 W charger" : hv == 0 ? "slow charger" : "";
            if (!c.isEmpty()) r.append(r.length() > 0 ? ", " : "").append(c);
        } catch (Throwable ignored) { }
        return r.toString();
    }

    /** text with " • 12.8 W" after its first line while charging; text unchanged on anything unexpected. */
    public static CharSequence add(CharSequence text) {
        try {
            if (TextUtils.isEmpty(text) || !"Charging".equals(read("status"))) return text;
            double volts = Long.parseLong(read("voltage_now")) / 1e6;      // microvolts
            long cur;                                                      // Samsung: milliamps
            try { cur = Long.parseLong(read("current_avg")); }             // the fuel gauge's average: one
            catch (Throwable e) { cur = Long.parseLong(read("current_now")); } // dip doesn't stick on screen
            double amps = Math.abs(cur) > 100000 ? cur / 1e6 : cur / 1e3;  // other kernels: microamps
            double watts = volts * amps;
            if (!(watts > 0.05 && watts < 200)) return text;
            String w = " • " + String.format(Locale.getDefault(), "%.1f W", watts);
            if (watts < 40) {
                String why = reason();
                if (!why.isEmpty()) w += " (" + why + ")";
            }
            int nl = TextUtils.indexOf(text, '\n');
            int at = nl < 0 ? text.length() : nl;
            if (text instanceof Spanned) {
                SpannableStringBuilder b = new SpannableStringBuilder(text);
                b.insert(at, w);
                return b;
            }
            String s = text.toString();
            return s.substring(0, at) + w + s.substring(at);
        } catch (Throwable t) {
            return text;
        }
    }
}
