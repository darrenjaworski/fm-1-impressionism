#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Checks a host render (build/host/impressionism.wav, written by tests/run_host.sh) for what an ear would catch:
clipping, silence, and a window that should be quieter than another. Standard library only.

  python3 tests/wav_check.py WAV [--min-rms N] [--drop S0:S1 R0:R1 DB ...]

Always checked: the file is 16-bit PCM at 44100 Hz, no sample is at full scale (|x| >= 32767: the app clamps there, so
one of those means it clipped), and the overall RMS is at least --min-rms (default 100, about -50 dBFS; the renders
measure -19 to -25 dBFS, and the reverb's own DC offset alone is about 18).
--drop: the AC level (DC removed per channel) of the window S0:S1 seconds must be at least DB decibels below that of the
window R0:R1 seconds. Repeatable. Exit 0 when every check passes, 1 when one fails, 2 for an unreadable file or bad options.
"""
import argparse
import array
import math
import sys
import wave

FULL_SCALE = 32767


def parse_window(text):
    try:
        a, b = text.split(":")
        t0, t1 = float(a), float(b)
    except ValueError:
        raise argparse.ArgumentTypeError(f"window must be START:END in seconds, got {text!r}")
    if not (0 <= t0 < t1):
        raise argparse.ArgumentTypeError(f"window {text!r} must have 0 <= START < END")
    return t0, t1


def load(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise ValueError(f"{path}: needs 16-bit samples, got {8 * w.getsampwidth()}-bit")
        if w.getframerate() != 44100:
            raise ValueError(f"{path}: needs 44100 Hz, got {w.getframerate()}")
        channels = w.getnchannels()
        samples = array.array("h", w.readframes(w.getnframes()))
    return channels, samples


def ac_rms(samples, channels, rate, t0, t1, n_frames):
    """RMS over the window, DC removed per channel, both channels together."""
    a = int(round(t0 * rate))
    b = int(round(t1 * rate))
    if b > n_frames or b <= a:
        raise ValueError(f"window {t0}:{t1} s is outside the file ({n_frames / rate:.2f} s)")
    total = 0.0
    count = 0
    for c in range(channels):
        seg = samples[a * channels + c:b * channels:channels]
        mean = sum(seg) / len(seg)
        total += sum((x - mean) ** 2 for x in seg)
        count += len(seg)
    return math.sqrt(total / count)


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("wav")
    p.add_argument("--min-rms", type=float, default=100.0)
    p.add_argument("--drop", nargs=3, action="append", metavar=("S0:S1", "R0:R1", "DB"), default=[])
    args = p.parse_args()

    try:
        channels, samples = load(args.wav)
    except (OSError, ValueError, wave.Error) as e:
        print(f"wav_check: {e}")
        return 2
    rate = 44100
    n_frames = len(samples) // channels
    failed = False

    full = sum(1 for x in samples if x >= FULL_SCALE or x <= -FULL_SCALE)
    ok = full == 0
    failed |= not ok
    print(f"wav_check: no sample at full scale: {full} found in {len(samples)} samples {'ok' if ok else 'FAIL'}")

    mean_sq = sum(x * x for x in samples) / len(samples)
    rms = math.sqrt(mean_sq)
    ok = rms >= args.min_rms
    failed |= not ok
    print(f"wav_check: overall RMS {rms:.1f} (need >= {args.min_rms:g}) {'ok' if ok else 'FAIL'}")

    for s_text, r_text, db_text in args.drop:
        try:
            s0, s1 = parse_window(s_text)
            r0, r1 = parse_window(r_text)
            db = float(db_text)
        except (argparse.ArgumentTypeError, ValueError) as e:
            print(f"wav_check: bad --drop: {e}")
            return 2
        try:
            quiet = ac_rms(samples, channels, rate, s0, s1, n_frames)
            ref = ac_rms(samples, channels, rate, r0, r1, n_frames)
        except ValueError as e:
            print(f"wav_check: {e}")
            return 2
        drop = 20 * math.log10(ref / quiet) if quiet > 0 else float("inf")
        ok = drop >= db
        failed |= not ok
        print(f"wav_check: {s_text} AC RMS {quiet:.2f} is {drop:.1f} dB below {r_text} AC RMS {ref:.2f} "
              f"(need >= {db:g} dB) {'ok' if ok else 'FAIL'}")

    print("wav_check: " + ("FAILED" if failed else "all ok"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
