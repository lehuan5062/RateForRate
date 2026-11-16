"""Level calibration from the in-graph meters (plain Python, no Unreal needed).

  python Tools\\py\\ms_calibrate.py [Saved\\run_last.log]

Reads the "AudioMeter: [PASS|FAIL] <sound> ... peak <x> dB" lines of a `-ShiftAudioMeter=all` run, and for each sound adds
(target - measured) dB to its trim in Tools\\py\\ms_trims.json. Rebuild with Tools\\build_audio.ps1 and measure again until every sound is
within 0.5 dB of its target (Tools\\verify.ps1 -Audio loops this).
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from ms_targets import MUSIC_TARGET_DB, SFX_TARGET_DB  # noqa: E402

TRIMS = os.path.join(HERE, "ms_trims.json")
LINE = re.compile(r"AudioMeter: \[(?:PASS|FAIL)\] (?P<label>.+?)\s+[\d.]+s\s+peak\s+(?P<peak>-?[\d.]+) dB")
MAX_TRIM = 45.0


def main():
    log_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "Saved", "run_last.log")
    trims = {}
    if os.path.exists(TRIMS):
        with open(TRIMS, "r", encoding="utf-8") as fh:
            trims = json.load(fh)
    measured = {}
    with open(log_path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = LINE.search(line)
            if m:
                measured[m.group("label")] = float(m.group("peak"))
    worst = 0.0
    changed = 0
    for label, peak in sorted(measured.items()):
        if peak <= -100.0:
            print("skip {}: no signal measured".format(label))
            continue
        if label.startswith("MS_Sfx_"):
            name = label[len("MS_Sfx_"):]
            if name not in SFX_TARGET_DB:
                continue
            key, target = label, float(SFX_TARGET_DB[name])
        elif label in MUSIC_TARGET_DB:
            key, target = MUSIC_TARGET_DB[label]
        else:
            continue
        error = target - peak
        worst = max(worst, abs(error))
        if abs(error) >= 0.3:
            new = max(-MAX_TRIM, min(MAX_TRIM, trims.get(key, 0.0) + error))
            trims[key] = round(new, 2)
            changed += 1
            print("{:<24} peak {:6.1f} dB  target {:6.1f}  -> trim {:+.2f} dB".format(label, peak, target, trims[key]))
    with open(TRIMS, "w", encoding="utf-8") as fh:
        json.dump(dict(sorted(trims.items())), fh, indent=2)
        fh.write("\n")
    print("{} trim(s) updated, worst error {:.1f} dB -> {}".format(changed, worst, TRIMS))
    return 0 if worst < 0.5 else 1


if __name__ == "__main__":
    sys.exit(main())
