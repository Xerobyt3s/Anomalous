import os
import sys

import numpy as np

from prepare_engine import load_wav, write_wav, resample

OUT_RATE = 44100
HOP_S = 0.02


def envelope(x, rate):
    hop = int(rate * HOP_S)
    n = len(x) // hop
    frames = x[:n * hop].reshape(n, hop)
    return np.sqrt(np.mean(frames * frames, axis=1))


def find_events(x, rate, threshold_ratio=0.15, min_gap_s=0.25):
    env = envelope(x, rate)
    threshold = env.max() * threshold_ratio
    events = []
    i = 0
    n = len(env)
    min_gap = int(min_gap_s / HOP_S)
    while i < n:
        if env[i] < threshold:
            i += 1
            continue
        j = i
        quiet = 0
        peak = 0.0
        while j < n and quiet < min_gap:
            quiet = quiet + 1 if env[j] < threshold * 0.5 else 0
            peak = max(peak, env[j])
            j += 1
        events.append((i * HOP_S, j * HOP_S, peak))
        i = j
    return events


def cmd_scan(path):
    x, rate = load_wav(path)
    print(f"{path}: {len(x) / rate:.1f}s at {rate} Hz")
    events = find_events(x, rate)
    print(f"events: {len(events)}")
    for k, (t0, t1, peak) in enumerate(events):
        print(f"  [{k:2d}] {t0:7.2f}s .. {t1:7.2f}s  len {t1 - t0:5.2f}s  peak {peak:.3f}")
    env = envelope(x, rate)
    win = int(1.0 / HOP_S)
    smooth = np.convolve(env, np.ones(win) / win, mode="same")
    best = int(np.argmax(smooth))
    print(f"strongest sustained second around {best * HOP_S:.1f}s")


def cmd_oneshot(path, out, event_index, pre_s=0.03, max_dur_s=1.6, gain=0.85):
    x, rate = load_wav(path)
    events = find_events(x, rate)
    t0, t1, peak = events[event_index]
    start = max(int((t0 - pre_s) * rate), 0)
    end = min(int(min(t1 + 0.1, t0 + max_dur_s) * rate), len(x))
    seg = x[start:end].copy()
    fade_in = int(rate * 0.004)
    fade_out = int(rate * 0.06)
    seg[:fade_in] *= np.linspace(0.0, 1.0, fade_in)
    seg[-fade_out:] *= np.linspace(1.0, 0.0, fade_out)
    seg = seg * (gain / max(np.abs(seg).max(), 1e-6))
    seg = resample(seg, rate, OUT_RATE)
    write_wav(out, seg, OUT_RATE)
    print(f"wrote {out}: event {event_index} at {t0:.2f}s, {len(seg) / OUT_RATE:.2f}s")


def fft_highpass(x, rate, cutoff_hz, width_hz=200.0):
    spectrum = np.fft.rfft(x)
    freqs = np.fft.rfftfreq(len(x), 1.0 / rate)
    gain = np.clip((freqs - (cutoff_hz - width_hz)) / width_hz, 0.0, 1.0)
    return np.fft.irfft(spectrum * gain, len(x))


def cmd_loop(path, out, center_s, dur_s=2.0, fade_s=0.30, rms=0.20, highpass_hz=0.0):
    x, rate = load_wav(path)
    n = int(dur_s * rate)
    fade = int(fade_s * rate)
    start = max(int(center_s * rate) - n // 2, 0)
    seg = x[start:start + n + fade].copy()
    if highpass_hz > 0.0:
        seg = fft_highpass(seg, rate, highpass_hz)
    for i in range(fade):
        t = i / fade
        seg[n - fade + i] = seg[n - fade + i] * (1.0 - t) + seg[i] * t
    loop = seg[:n]
    loop = loop * (rms / max(np.sqrt(np.mean(loop * loop)), 1e-6))
    loop = np.clip(loop, -1.0, 1.0)
    loop = resample(loop, rate, OUT_RATE)
    write_wav(out, loop, OUT_RATE)
    print(f"wrote {out}: loop {dur_s:.1f}s centered {center_s:.1f}s")


def main():
    cmd = sys.argv[1]
    if cmd == "scan":
        cmd_scan(sys.argv[2])
    elif cmd == "oneshot":
        gain = float(sys.argv[5]) if len(sys.argv) > 5 else 0.85
        cmd_oneshot(sys.argv[2], sys.argv[3], int(sys.argv[4]), gain=gain)
    elif cmd == "loop":
        dur = float(sys.argv[5]) if len(sys.argv) > 5 else 2.0
        hp = float(sys.argv[6]) if len(sys.argv) > 6 else 0.0
        cmd_loop(sys.argv[2], sys.argv[3], float(sys.argv[4]), dur_s=dur, highpass_hz=hp)
    else:
        print("usage: prepare_sound.py scan|oneshot|loop ...")


if __name__ == "__main__":
    main()
