#!/usr/bin/env python3
"""Sets every Slapper preset's "trimDb" so its funk riff plays at -14 dB
K-weighted (the default preset's level) at VOL 0 dB, whatever its engine.
Run from the repo after building sl_render:
    cmake --build build --target sl_render && python3 scripts/calibrate_slapper_trims.py
(two passes: the trim is applied after the amp, the second only checks)."""
import glob, json, re, subprocess, sys, tempfile

TARGET = -14.0
RENDER = "build/tests/slapper/sl_render_artefacts/Release/sl_render"

def levels():
    out = subprocess.run([RENDER, tempfile.mkdtemp()], capture_output=True, text=True, check=True).stdout
    return {m.group(1): float(m.group(2)) for m in re.finditer(r"^\s*(.+): (-?[\d.]+) dB \(K-weighted\)", out, re.M)}

for rebuild in (True, False):
    lv = levels()
    worst = 0.0
    for f in sorted(glob.glob("plugins/slapper/Presets/*.json")):
        d = json.load(open(f))
        err = TARGET - lv[d["name"]]
        worst = max(worst, abs(err))
        if rebuild:
            d["trimDb"] = round(max(-6.0, min(6.0, d.get("trimDb", 0.0) + err)), 1)
            open(f, "w").write(json.dumps(d, indent=2) + "\n")
    print(("set, " if rebuild else "checked, ") + "worst error before: %.2f dB" % worst)
    if rebuild:
        subprocess.run(["cmake", "--build", "build", "--target", "sl_render"], check=True, capture_output=True)
