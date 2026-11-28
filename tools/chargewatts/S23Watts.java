package com.android.systemui.statusbar;

import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.TextUtils;
import java.io.BufferedReader;
import java.io.FileReader;
import java.util.Locale;

/** s23-ultra-tweaks (chargewatts): add the power going into the battery to the charging line (lock screen,
 * Always On Display, the charging notification), the most this charging step allows, and what holds it back:
 * "• 27.1 / 27 W (drops at 59%)", "• 11.4 / 27 W (phone warm)". */
public final class S23Watts {
    private static final String B = "/sys/class/power_supply/battery/";
    /** the direct-charging step table, copied here from the device tree by the module (SystemUI can't read
     * the device tree): line 1 the battery levels each step ends at, line 2 each step's battery current (mA) */
    private static final String STEPS = "/data/user_de/0/com.android.systemui/files/s23tweaks_dcsteps";

    private static String read(String name) throws Exception {
        try (BufferedReader r = new BufferedReader(new FileReader(B + name))) {
            String s = r.readLine();
            return s == null ? "" : s.trim();
        }
    }

    private static String readAll(String path) throws Exception {
        StringBuilder b = new StringBuilder();
        try (BufferedReader r = new BufferedReader(new FileReader(path))) {
            for (String l; (l = r.readLine()) != null; ) b.append(l).append('\n');
        }
        return b.toString();
    }

    /** where a line starting with key is in voter_status, or -1 ("FV: ..." must not match inside "DCFV: ...") */
    private static int line(String votes, String key) {
        if (votes.startsWith(key)) return 0;
        int i = votes.indexOf("\n" + key);
        return i < 0 ? -1 : i + 1;
    }

    /** the voter that sets a limit in voter_status ("FCC: voter=VOTER_LRP_TEMP type=Min v=3400"), or "" */
    private static String winner(String votes, String limit) {
        int i = line(votes, limit + ": voter=");
        if (i < 0) return "";
        int a = i + limit.length() + 8, e = a;
        while (e < votes.length() && votes.charAt(e) > ' ') e++;
        return votes.substring(a, e);
    }

    private static int number(String s, int a) {
        int e = a;
        while (e < s.length() && Character.isDigit(s.charAt(e))) e++;
        try { return Integer.parseInt(s.substring(a, e)); } catch (Throwable t) { return -1; }
    }

    /** the limit's winning value in mV/mA ("... type=Min v=4285"), or -1 */
    private static int value(String votes, String limit) {
        int i = line(votes, limit + ": voter=");
        if (i < 0) return -1;
        int a = votes.indexOf(" v=", i);
        return a < 0 ? -1 : number(votes, a + 3);
    }

    /** one voter's own vote on a limit, if it's enabled ("FCC: VOTER_CABLE:   en=1 v=6510 p=0"), or -1 */
    private static int vote(String votes, String limit, String voter) {
        int i = line(votes, limit + ": " + voter + ":");
        if (i < 0) return -1;
        int e = votes.indexOf('\n', i); if (e < 0) e = votes.length();
        String l = votes.substring(i, e);
        if (!l.contains("en=1")) return -1;
        int a = l.indexOf(" v=");
        return a < 0 ? -1 : number(l, a + 3);
    }

    /** direct charging? direct_charging_status is system-only (0660), so SystemUI falls back on voter_status,
     * where the direct charger's mode votes on the input voltage while it runs */
    private static boolean dc(String votes) {
        try { return "1".equals(read("direct_charging_status")); }
        catch (Throwable t) { return vote(votes, "IV", "VOTER_DC_MODE") > 0; }
    }

    /** Turkish (#56) and Czech (#58); other languages get English. The rest of the line is Samsung's, already
     * translated. */
    private static String lang() { return Locale.getDefault().getLanguage(); }
    private static boolean tr() { return "tr".equals(lang()); }
    private static boolean cs() { return "cs".equals(lang()); }

    private static String t(String en) {
        if (cs()) switch (en) {   // by soukup96-eng (#58)
            case "phone warm": return "telefon se zahřívá";
            case "phone in use": return "telefon se používá";
            case "battery temperature": return "teplota baterie";
            case "charger chip warm": return "hřeje nabíjecí čip";
            case "tapering": return "zpomalování";
            case "battery protection": return "ochrana baterie";
            case "25 W charger": return "25W nabíječka";
            case "15 W charger": return "15W nabíječka";
            case "slow charger": return "pomalá nabíječka";
            default: return en;
        }
        if (!tr()) return en;
        switch (en) {
            case "phone warm": return "telefon sıcak";
            case "phone in use": return "telefon kullanımda";
            case "battery temperature": return "pil sıcaklığı";
            case "charger chip warm": return "şarj yongası sıcak";
            case "tapering": return "yavaşlıyor";
            case "battery protection": return "pil koruması";
            case "25 W charger": return "25 W şarj cihazı";
            case "15 W charger": return "15 W şarj cihazı";
            case "slow charger": return "yavaş şarj cihazı";
            default: return en;
        }
    }

    /** Turkish locative after a number, by vowel harmony and the last spoken word: %80'de, %45'te, %59'da */
    private static String trAt(int n) {
        String[] ones = {"", "de", "de", "te", "te", "te", "da", "de", "de", "da"};   // bir iki üç dört beş altı yedi sekiz dokuz
        String[] tens = {"", "da", "de", "da", "ta", "de", "ta", "te", "de", "da"};   // on yirmi otuz kırk elli altmış yetmiş seksen doksan
        String s = n % 10 != 0 ? ones[n % 10] : (n % 100 != 0 ? tens[(n / 10) % 10] : "de");   // yüz
        return "%" + n + "'" + s;
    }

    private static String label(String v) {
        switch (v) {
            case "VOTER_LRP_TEMP": return t("phone warm");
            case "VOTER_SIOP": return t("phone in use");
            case "VOTER_SWELLING": return t("battery temperature");
            case "VOTER_CHG_TEMP": case "VOTER_DCHG_TEMP": case "VOTER_DC_TEMP": return t("charger chip warm");
            case "VOTER_STEP": case "VOTER_DC_STEP_CHARGE": case "VOTER_AGING_STEP": case "VOTER_FULL_CHARGE": return t("tapering");
            case "VOTER_CHG_LIMIT": case "VOTER_SLEEP_MODE": case "VOTER_USER": case "VOTER_SLATE": case "VOTER_SMART_SLATE": return t("battery protection");
            // VOTER_CABLE carries the charger type's current and, while direct charging, the step's: shown as the max
            case "": case "None": case "INIT": case "VOTER_CABLE": case "VOTER_SELECT_PDO": case "VOTER_DC_MODE": return "";
            default: return v.replace("VOTER_", "").replace('_', ' ').toLowerCase(Locale.ROOT);
        }
    }

    /** what holds it under the max: the kernel's winning current limit, or the battery nearing its float voltage */
    private static String limiter(String votes) {
        try {
            String why = label(winner(votes, "FCC"));             // battery current
            if (why.isEmpty()) why = label(winner(votes, "ICL")); // charger input current
            if (why.isEmpty()) {
                // no current limit wins: the battery is near the voltage the charger holds it at (direct
                // charging steps it up with the level, normal charging at the end), so the current tapers off
                int mv = (int) (Long.parseLong(read("voltage_now")) / 1000);
                int dc = dc(votes) ? value(votes, "DCFV") : -1;
                int fv = dc > 0 ? dc : value(votes, "FV");
                if (fv > 0 && mv >= fv - 100) why = tr() || cs() ? t("tapering") : "tapering at " + read("capacity") + "%";   // tr, cs: the level is on the line already
            }
            return why;
        } catch (Throwable t) { return ""; }
    }

    /** a charger below the 45 W class sets the max itself */
    private static String charger() {
        try {   // 4 = 45 W (Super fast charging 2.0), 3 = 25 W, 1-2 = 15/20 W, 0 = normal
            int hv = Integer.parseInt(read("hv_charger_status"));
            return t(hv == 3 ? "25 W charger" : hv == 1 || hv == 2 ? "15 W charger" : hv == 0 ? "slow charger" : "");
        } catch (Throwable t) { return ""; }
    }

    /** while direct charging at a step's current: the battery level where the next lower step starts, or "" */
    private static String dropsAt(String votes, int cap) {
        try {
            if (!dc(votes)) return "";
            String[] l = readAll(STEPS).split("\n");
            String[] soc = l[0].trim().split("\\s+"), iout = l[1].trim().split("\\s+");
            int n = Math.min(soc.length, iout.length), i = 0;
            while (i < n && Integer.parseInt(iout[i]) != cap) i++;
            for (int j = i + 1; j < n; j++)
                if (Integer.parseInt(iout[j]) < cap) {
                    int at = Integer.parseInt(soc[j - 1]), now = Integer.parseInt(read("capacity"));
                    return at > now && at < 100 ? (tr() ? trAt(at) + " düşer" : cs() ? "při " + at + " % klesá" : "drops at " + at + "%") : "";
                }
        } catch (Throwable ignored) { }
        return "";
    }

    private static String join(String a, String b) {
        return a.isEmpty() ? b : b.isEmpty() ? a : a + ", " + b;
    }

    /** text with " • 11.4 / 27 W (phone warm)" after its first line while charging; unchanged on anything unexpected */
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
            String votes = "";
            try { votes = readAll(B + "voter_status"); } catch (Throwable ignored) { }
            int cap = vote(votes, "FCC", "VOTER_CABLE");                   // the charger's / step's battery current
            double max = cap > 0 ? volts * cap / 1000 : -1;
            StringBuilder w = new StringBuilder(" • ").append(String.format(Locale.getDefault(), "%.1f", watts));
            String why;
            if (max > 1) {
                w.append(String.format(Locale.getDefault(), " / %.0f W", max));
                why = watts >= 0.9 * max ? join(dropsAt(votes, cap), charger()) : join(limiter(votes), charger());
            } else {
                w.append(" W");
                why = watts < 40 ? join(limiter(votes), charger()) : "";
            }
            if (!why.isEmpty()) w.append(" (").append(why).append(')');
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

    /** the charging notification's text, which is a String */
    public static String text(String text) {
        CharSequence r = add(text);
        return r == null ? null : r.toString();
    }
}
