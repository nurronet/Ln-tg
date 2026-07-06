#!/usr/bin/env python3
from pathlib import Path
import re
import shutil
import sys

ROOT = Path.cwd()
SRC = ROOT / "src"
SELF = Path(__file__).resolve()
OVERLAY = SELF.parents[1]

def backup(path: Path):
    bak = path.with_suffix(path.suffix + ".lnbak")
    if not bak.exists():
        shutil.copy2(path, bak)

def copy_station_sources():
    for name in ["ln_station.c", "ln_station.h", "ln_station_panel.c", "ln_station_panel.h"]:
        src = OVERLAY / "src" / name
        dst = SRC / name
        shutil.copy2(src, dst)
        print(f"copied {dst}")

def patch_makefile():
    path = ROOT / "Makefile.am"
    text = path.read_text(encoding="utf-8")
    backup(path)

    needed = [
        "src/ln_station.c",
        "src/ln_station.h",
        "src/ln_station_panel.c",
        "src/ln_station_panel.h",
    ]

    if all(n in text for n in needed):
        print("Makefile.am already includes LN station sources")
        return

    insert = " \\\n " + " \\\n ".join(needed)
    text2 = re.sub(r"(src/serializer\.c\s*\\\s*)(src/tg\.h)", r"\1" + insert + r" \\\n \2", text, count=1)
    if text2 == text:
        raise RuntimeError("Could not patch Makefile.am. Please add LN station sources manually.")
    path.write_text(text2, encoding="utf-8")
    print("patched Makefile.am")

def patch_interface():
    path = SRC / "interface.c"
    text = path.read_text(encoding="utf-8")
    backup(path)

    if '#include "ln_station.h"' not in text:
        text = text.replace('#include "tg.h"', '#include "tg.h"\n#include "ln_station.h"\n#include "ln_station_panel.h"', 1)

    if "static LnStationContext ln_ctx;" not in text:
        anchor = "static const char *available_sample_rate_labels[]"
        idx = text.find(anchor)
        if idx == -1:
            raise RuntimeError("Could not find available_sample_rate_labels anchor in interface.c")
        end = text.find(";", idx)
        text = text[:end+1] + "\nstatic LnStationContext ln_ctx;\n" + text[end+1:]

    if "ln_station_timestamp_iso" not in text:
        func = r'''
static void ln_station_timestamp_iso(char *buf, size_t len) {
    time_t t = time(NULL);
    struct tm tmv;
#if defined(_WIN32)
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

static void save_ln_timing_json(GtkMenuItem *m, struct main_window *w) {
    UNUSED(m);

    struct snapshot *snapshot = w->active_snapshot;
    if (!snapshot || snapshot->calibrate || !snapshot->pb) {
        error("No stable timing result is available yet.");
        return;
    }

    if (!ln_ctx.identity_id[0]) {
        error("Scan or enter an LN identity before saving timing data.");
        return;
    }

    compute_results(snapshot);

    LnTimingResult result;
    memset(&result, 0, sizeof(result));
    result.rate_s_per_day = snapshot->rate;
    result.beat_error_ms = snapshot->be;
    result.amplitude_deg = snapshot->amp;
    result.beat_frequency_bph = snapshot->bph ? snapshot->bph : snapshot->guessed_bph;
    result.lift_angle_deg = snapshot->la;
    result.sample_rate_hz = snapshot->sample_rate;
    result.duration_seconds = 0.0;
    ln_station_timestamp_iso(result.timestamp_iso, sizeof(result.timestamp_iso));
    snprintf(result.notes, sizeof(result.notes), "Saved from Tg real-time display");

    char out_path[1024];
    int rc = ln_station_export_json(&ln_ctx, &result, out_path, sizeof(out_path));
    if (rc) {
        error("Unable to save LN timing JSON. Code %d", rc);
        return;
    }

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(w->window), 0, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
        "LN timing JSON saved:\\n%s", out_path);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}
'''
        anchor = "static void close_all(GtkMenuItem *m, struct main_window *w)"
        if anchor not in text:
            raise RuntimeError("Could not find close_all anchor in interface.c")
        text = text.replace(anchor, func + "\n" + anchor, 1)

    if "ln_station_panel_new(&ln_ctx)" not in text:
        anchor = "gtk_container_add(GTK_CONTAINER(w->window), vbox);"
        insert = '''
    ln_station_init(&ln_ctx);
    GtkWidget *ln_panel = ln_station_panel_new(&ln_ctx);
    gtk_box_pack_start(GTK_BOX(vbox), ln_panel, FALSE, FALSE, 0);
'''
        if anchor not in text:
            raise RuntimeError("Could not find vbox/container anchor in interface.c")
        text = text.replace(anchor, anchor + insert, 1)

    if 'Save LN Timing JSON' not in text:
        anchor = 'gtk_widget_set_sensitive(w->save_item, FALSE);'
        insert = '''
    GtkWidget *ln_save_item = gtk_menu_item_new_with_label("Save LN Timing JSON");
    gtk_menu_shell_append(GTK_MENU_SHELL(command_menu), ln_save_item);
    g_signal_connect(ln_save_item, "activate", G_CALLBACK(save_ln_timing_json), w);
'''
        if anchor not in text:
            raise RuntimeError("Could not find save menu anchor in interface.c")
        text = text.replace(anchor, anchor + insert, 1)

    path.write_text(text, encoding="utf-8")
    print("patched src/interface.c")

def main():
    if not (ROOT / "Makefile.am").exists() or not (SRC / "interface.c").exists():
        print("Run this script from the Ln-tg repository root.", file=sys.stderr)
        return 1
    copy_station_sources()
    patch_makefile()
    patch_interface()
    print("\nDone. Rebuild with: ./autogen.sh && ./configure && make")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
