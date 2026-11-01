#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Measures a host render against ROADMAP.md's "Measurements" table. Needs numpy (a dev tool, not part of the build).

  python3 tools/measure_render.py WAV      # a 16-bit 44.1 kHz stereo render, e.g. build/host/impressionism.wav
  python3 tools/measure_render.py --selftest

Definitions (the first SKIP_S seconds are dropped, DC is removed per channel, bands are on the mono mix):
  envelope range   p95 minus p10 of the 1 s RMS windows, in dB
  centroid         power-weighted mean frequency of the mean power spectrum (8192-point Hann frames), in Hz
  band             power in the band as dB below the total power
  L/R correlation  Pearson correlation of the two channels
  clipped          samples at full scale (|x| >= 32767), counted over the whole file
"""
import sys
import wave

import numpy as np

SKIP_S = 15.0
FRAME = 8192


def load(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2 or w.getnchannels() != 2:
            raise ValueError(f"{path}: needs 16-bit stereo")
        sr = w.getframerate()
        data = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2).astype(np.float64)
    return sr, data


def measure(sr, data):
    clipped = int(np.sum(np.abs(data) >= 32767))
    x = data[int(SKIP_S * sr):]
    x = x - x.mean(axis=0)
    mono = x.mean(axis=1)

    k = len(mono) // sr
    rms = np.sqrt((mono[: k * sr].reshape(k, sr) ** 2).mean(axis=1))
    db = 20 * np.log10(np.maximum(rms, 1e-9) / 32768)
    env_range = np.percentile(db, 95) - np.percentile(db, 10)

    win = np.hanning(FRAME)
    power = np.zeros(FRAME // 2 + 1)
    for i in range(len(mono) // FRAME):
        power += np.abs(np.fft.rfft(mono[i * FRAME:(i + 1) * FRAME] * win)) ** 2
    f = np.fft.rfftfreq(FRAME, 1 / sr)
    total = power[f > 0].sum()

    def band(lo, hi):
        sel = (f >= lo) & (f < hi)
        return 10 * np.log10(max(power[sel].sum() / total, 1e-12))

    return {
        "envelope range (dB)": env_range,
        "centroid (Hz)": (f * power).sum() / total,
        "20-100 Hz (dB)": band(20, 100),
        "1.6-3.2 kHz (dB)": band(1600, 3200),
        "L/R correlation": np.corrcoef(x[:, 0], x[:, 1])[0, 1],
        "clipped samples": clipped,
    }


def selftest():
    sr = 44100
    t = np.arange(sr * 40) / sr
    ok = True

    def check(name, got, want, tol):
        nonlocal ok
        good = abs(got - want) <= tol
        ok &= good
        print(f"{'ok  ' if good else 'FAIL'} {name}: got {got:.2f}, want {want:.2f} +/- {tol}")

    s = 8000 * np.sin(2 * np.pi * 440 * t)
    m = measure(sr, np.stack([s, s], 1))
    check("440 Hz sine: centroid", m["centroid (Hz)"], 440, 2)
    check("440 Hz sine, L = R: correlation", m["L/R correlation"], 1.0, 1e-6)
    check("constant level: envelope range", m["envelope range (dB)"], 0.0, 0.01)

    s = 6000 * np.sin(2 * np.pi * 50 * t) + 6000 * np.sin(2 * np.pi * 2000 * t)
    m = measure(sr, np.stack([s, s], 1))
    check("50 Hz + 2 kHz, equal power: centroid", m["centroid (Hz)"], 1025, 5)
    check("50 Hz + 2 kHz: 20-100 Hz band", m["20-100 Hz (dB)"], -3.01, 0.1)
    check("50 Hz + 2 kHz: 1.6-3.2 kHz band", m["1.6-3.2 kHz (dB)"], -3.01, 0.1)
    m = measure(sr, np.stack([s, -s], 1))
    check("L = -R: correlation", m["L/R correlation"], -1.0, 1e-6)

    # -2 dB per second. The kept 25 s span 24 dB per 12 s of drop; the percentiles of a
    # uniform ramp over 2 * 24 = 48 dB are 0.85 * 48 apart.
    amp = np.repeat(10 ** (-np.arange(40) * 2 / 20), sr)
    s = 20000 * amp * np.sin(2 * np.pi * 440 * t)
    m = measure(sr, np.stack([s, s], 1))
    check("-2 dB/s ramp: envelope range", m["envelope range (dB)"], 0.85 * 2 * 24, 1.0)

    full = np.random.default_rng(1).normal(0, 100, (sr * 20, 2))
    full[100, 0] = 32767
    full[200, 1] = -32768
    check("two full-scale samples: clipped", measure(sr, full)["clipped samples"], 2, 0)
    return ok


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(0 if selftest() else 1)
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    for name, value in measure(*load(sys.argv[1])).items():
        print(f"{name:22s} {value:.2f}")
