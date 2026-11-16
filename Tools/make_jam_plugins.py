"""Writes the three jam pack Game Feature plugins' descriptors (Plugins/GameFeatures/ShiftJam_*/ShiftJam_*.uplugin) and the per-jam entry CSVs
(Data/Packs/Entries_<Key>.csv, split from Data/Entries.csv). Plain Python, run before Tools/py/build_jam_packs.py:  python Tools/make_jam_plugins.py
"""
import csv
import json
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

PACKS = (
    ("ShiftJam_TinyJam7", "TinyJam7", "Tiny Jam #7"),
    ("ShiftJam_Rumble48", "Rumble48", "48h Pixel Rumble"),
    ("ShiftJam_GameOff2026", "GameOff2026", "GAME OFF 2026"),
)


def main():
    rows = list(csv.DictReader(open(os.path.join(ROOT, "Data", "Entries.csv"), encoding="utf-8", newline="")))
    fields = list(rows[0].keys())
    packs_dir = os.path.join(ROOT, "Data", "Packs")
    os.makedirs(packs_dir, exist_ok=True)
    for plugin, key, title in PACKS:
        folder = os.path.join(ROOT, "Plugins", "GameFeatures", plugin)
        os.makedirs(os.path.join(folder, "Content"), exist_ok=True)
        descriptor = {
            "FileVersion": 3,
            "Version": 1,
            "VersionName": "1.0",
            "FriendlyName": "Shift Jam Pack: " + title,
            "Description": "Game Feature plugin: the {} jam of Shift Happens (card text, rival and other entries).".format(title),
            "Category": "Game Features",
            "CreatedBy": "Shift Happens",
            "CanContainContent": True,
            "IsExperimentalVersion": False,
            "Installed": False,
            "ExplicitlyLoaded": True,
            "EnabledByDefault": True,
            "BuiltInInitialFeatureState": "Active",
            "Plugins": [
                {"Name": "GameFeatures", "Enabled": True},
                {"Name": "ModularGameplay", "Enabled": True},
            ],
        }
        with open(os.path.join(folder, plugin + ".uplugin"), "w", encoding="utf-8", newline="\n") as fh:
            json.dump(descriptor, fh, indent="\t")
            fh.write("\n")
        subset = [r for r in rows if r["Jam"] == key]
        with open(os.path.join(packs_dir, "Entries_{}.csv".format(key)), "w", encoding="utf-8", newline="") as fh:
            writer = csv.DictWriter(fh, fieldnames=fields)
            writer.writeheader()
            writer.writerows(subset)
        print("{}: {} entry rows".format(plugin, len(subset)))


main()
