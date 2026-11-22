"""Reads a recording (.mkv) made by Tools/trailer/shoot.ps1 and tells where the music starts and how late the picture is against the sound.

  python -I Tools/trailer/measure.py onset <take.mkv>                      where the music starts, in seconds on the recording's own timeline
  python -I Tools/trailer/measure.py calib <calibration.mkv> [--used S]    the calibration run (Tools/trailer/calibrate.csv: a white flash on each of the
                                                                           first four bar lines): the offset of the flashes against the beats, and the
                                                                           -ShiftTrailerLatency that cancels it (S = the latency the run was made with)

The last line of the output is machine readable: ONSET=<s> or LATENCY=<s>. Everything is read through ffmpeg pipes; nothing is written.
"""
import argparse
import re
import subprocess
import sys

import numpy as np

RATE = 48000
BAR = 4.0 * 60.0 / 112.5      # one bar of the music, 2.1333 s = exactly 128 frames at 60 fps
FRAME = 1.0 / 60.0
ONSET_LEVEL = 0.005           # -46 dBFS: the process capture is digital silence until the music starts


def stream_start(path, kind):
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", kind, "-show_entries", "stream=start_time", "-of", "default=nw=1:nk=1", path],
                         capture_output=True, text=True, check=True).stdout.strip()
    try:
        return float(out)
    except ValueError:
        return 0.0


def read_audio(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-vn", "-ac", "1", "-ar", str(RATE), "-f", "f32le", "-"], capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype="<f4")


def envelope_ms(samples):
    """The peak of every millisecond."""
    n = RATE // 1000
    m = len(samples) // n
    return np.abs(samples[: m * n]).reshape(m, n).max(axis=1)


def video_levels(path):
    """(time, mean luma) of every video frame."""
    flt = "scale=32:18:flags=area,format=gray,signalstats,metadata=mode=print:key=lavfi.signalstats.YAVG:file=-"
    # -copyts keeps the timestamps of the recording itself (the audio stream starts a little before the video), the same ones ffprobe reports
    out = subprocess.run(["ffmpeg", "-v", "error", "-copyts", "-i", path, "-an", "-vf", flt, "-f", "null", "-"], capture_output=True, text=True, check=True).stdout
    times, levels, t = [], [], None
    for line in out.splitlines():
        m = re.search(r"pts_time:([0-9.]+)", line)
        if m:
            t = float(m.group(1))
            continue
        m = re.search(r"YAVG=([0-9.]+)", line)
        if m and t is not None:
            times.append(t)
            levels.append(float(m.group(1)))
    return np.array(times), np.array(levels)


def attack_start(env, center_ms, before, after, floor_from_silence):
    """The foot of the attack near center_ms: where the 1 ms peak first passes 30% of the way from the floor to the window's peak."""
    lo = max(0, int(center_ms) - before)
    hi = min(len(env), int(center_ms) + after)
    seg = env[lo:hi]
    if len(seg) < 10:
        return None
    floor = 0.0 if floor_from_silence else float(np.median(seg[:max(5, before // 2)]))
    peak = float(seg.max())
    if peak <= floor * 1.5 + 1e-4:
        return None
    thr = floor + 0.3 * (peak - floor)
    idx = np.flatnonzero(seg > thr)
    return (lo + int(idx[0])) / 1000.0


def music_onset(path):
    """The time (recording timeline) of the music's first beat, and the audio start offset used."""
    audio = read_audio(path)
    if len(audio) == 0:
        raise SystemExit("the recording has no audio")
    start = stream_start(path, "a")
    env = envelope_ms(audio)
    idx = np.flatnonzero(env > ONSET_LEVEL)
    if len(idx) == 0:
        raise SystemExit("no sound in the recording (peak {:.1f} dBFS): is the game's audio captured?".format(20 * np.log10(max(float(env.max()), 1e-9))))
    # the music that counts is the last stretch of sound that starts after a silence of a second or more (the game's first music is held a few
    # frames after it began, which leaves a short blip long before the film's music)
    gaps = np.flatnonzero(np.diff(idx) > 1000)
    first = int(idx[gaps[-1] + 1]) if len(gaps) else int(idx[0])
    foot = attack_start(env, first, 5, 120, True)
    onset = start + (foot if foot is not None else first / 1000.0)
    return onset, start, env, audio


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["onset", "calib"])
    ap.add_argument("path")
    ap.add_argument("--used", type=float, default=0.0, help="the -ShiftTrailerLatency the calibration run was made with")
    args = ap.parse_args()

    onset, a_start, env, audio = music_onset(args.path)
    peak_db = 20 * np.log10(max(float(np.abs(audio).max()), 1e-9))
    print("music starts at {:.3f} s (audio stream starts at {:.3f} s), peak {:.1f} dBFS".format(onset, a_start, peak_db))
    if args.mode == "onset":
        print("ONSET={:.4f}".format(onset))
        return 0

    times, levels = video_levels(args.path)
    if len(times) == 0:
        raise SystemExit("no video frames read")
    lo, hi = float(levels.min()), float(levels.max())
    print("video: {} frames, mean luma {:.0f} .. {:.0f}".format(len(times), lo, hi))
    if hi - lo < 60:
        raise SystemExit("no flashes in the picture (luma range only {:.0f}): is the game window captured?".format(hi - lo))
    mid = lo + 0.45 * (hi - lo)
    edges = [float(times[i]) for i in range(1, len(times)) if levels[i] >= mid and levels[i - 1] < mid]

    offsets = []
    for k in range(4):
        expected_audio = onset + k * BAR
        # the beat of this bar line in the sound (the first one is the onset itself)
        if k == 0:
            a = onset
        else:
            foot = attack_start(env, (expected_audio - a_start) * 1000.0, 60, 120, False)
            a = None if foot is None else a_start + foot
        near = [e for e in edges if abs(e - expected_audio) < 0.5]
        if a is None or not near:
            print("bar line {}: not found (sound {}, flashes near {})".format(k, "yes" if a is not None else "no", len(near)))
            continue
        v = min(near, key=lambda e: abs(e - expected_audio))
        offsets.append(v - a)
        print("bar line {}: flash at {:.3f} s, beat at {:.3f} s -> the picture is {:+.0f} ms against the sound".format(k, v, a, (v - a) * 1000.0))
    if len(offsets) < 2:
        raise SystemExit("fewer than two bar lines could be matched: cannot calibrate")
    median = float(np.median(offsets))
    # a flash shows in the first frame that has it, on average half a frame after it happened: that is not an error of the run
    new = args.used - (median - 0.5 * FRAME)
    new = float(min(max(new, -0.2), 0.6))
    print("median {:+.0f} ms (spread {:.0f} ms), latency used {:.3f} s".format(median * 1000.0, (max(offsets) - min(offsets)) * 1000.0, args.used))
    print("LATENCY={:.3f}".format(new))
    return 0


if __name__ == "__main__":
    sys.exit(main())
