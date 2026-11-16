"""Compares octave-band energy of a recorded playback with an offline stem (QA for the audio pipeline).
   python Tools/py/compare_spectra.py Saved/Audio/rec_day.wav Audio/Music_Day.wav
"""
import sys
import wave

import numpy as np


def load(path):
    with wave.open(path, "rb") as w:
        n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    if sw == 2:
        d = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif sw == 4:
        d = np.frombuffer(raw, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise RuntimeError("sample width {}".format(sw))
    return d.reshape(-1, ch).mean(axis=1), sr


def bands(mono, sr):
    win = 16384
    acc = None
    count = 0
    for start in range(0, len(mono) - win, win // 2):
        seg = mono[start:start + win] * np.hanning(win)
        spec = np.abs(np.fft.rfft(seg)) ** 2
        acc = spec if acc is None else acc + spec
        count += 1
    spec = acc / max(count, 1)
    freqs = np.fft.rfftfreq(win, 1.0 / sr)
    edges = [20, 40, 80, 160, 315, 630, 1250, 2500, 5000, 10000, 20000]
    out = []
    for a, b in zip(edges[:-1], edges[1:]):
        idx = (freqs >= a) & (freqs < b)
        out.append(10 * np.log10(max(spec[idx].sum(), 1e-20)))
    return edges, out


def main():
    rec, sr = load(sys.argv[1])
    ref, sr2 = load(sys.argv[2])
    scale = float(sys.argv[3]) if len(sys.argv) > 3 else 10.0
    rec = rec * scale
    e, a = bands(rec, sr)
    _, b = bands(ref, sr2)
    print("band Hz        recorded  offline   diff")
    for i in range(len(a)):
        print("{:5d}-{:5d}   {:7.1f}  {:7.1f}  {:6.1f}".format(e[i], e[i + 1], a[i], b[i], a[i] - b[i]))


main()
