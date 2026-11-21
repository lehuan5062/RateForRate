"""Peak levels (dBFS) the audio is calibrated to. Plain Python (no unreal import) so Tools/py/ms_calibrate.py can use it with any interpreter.

These are the levels the original offline mix normalised to (loud confirmations near -5 dB, small UI ticks much quieter). The offline
normalisation is gone, so Tools/py/ms_calibrate.py measures every sound with the in-graph meters (Tools/verify.ps1 -Audio) and writes the
trims, in dB, that bring each one to its target into Tools/py/ms_trims.json; Tools/py/ms_sfx.py and ms_music.py apply them.
"""

# round 14: the playtester found the sound of a successful flip a bit loud: Flip -5 -> -9 dB, Sock -6 -> -8 dB (they sound dozens of times a jam; Win stays at -5, it sounds once)
SFX_TARGET_DB = {
    "Click": -7, "Hover": -17, "Slider": -15, "Upload": -6, "Transition": -12, "Tip": -12, "Plug": -6, "Flip": -9, "Reinforce": -10,
    "Weaken": -10, "Gated": -7, "Locked": -10, "NoPosts": -9, "NightOn": -5, "NightOff": -7, "Sock": -8, "Heartbeat": -6, "ModWarn": -10,
    "ModSweep": -10, "Strike": -5, "Shadowban": -9, "Blame": -6, "Day": -22, "Win": -5, "Lose": -8, "DQ": -6,
}

# meter scenario label -> (trim key, target peak dB). Each stem alone, one whole loop.
MUSIC_TARGET_DB = {
    "music day": ("music_day", -5.0),
    "music night": ("music_night", -8.0),
    "music tension only": ("music_tension", -7.0),
}
