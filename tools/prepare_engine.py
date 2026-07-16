import os
import struct
import sys

import numpy as np

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "audio")
OUT_RATE = 44100
FIRING_PER_REV = 2.0
MIN_HZ = 22.0
MAX_HZ = 300.0
WINDOW_S = 0.25
HOP_S = 0.10
STABLE_TOL = 0.06
MIN_STABLE_S = 1.0
TARGET_LEVELS = 7
LOOP_S = 1.8


def load_wav(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE"
    pos = 12
    fmt = None
    raw = None
    while pos + 8 <= len(data):
        tag = data[pos:pos + 4]
        size = struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if tag == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif tag == b"data":
            raw = body
        pos += 8 + size + (size & 1)
    audio_format, channels, rate, _, block_align, bits = fmt
    if bits == 24:
        as_bytes = np.frombuffer(raw, dtype=np.uint8)
        n = len(as_bytes) // 3
        as_bytes = as_bytes[:n * 3].reshape(n, 3)
        samples = (as_bytes[:, 0].astype(np.int32)
                   | (as_bytes[:, 1].astype(np.int32) << 8)
                   | (as_bytes[:, 2].astype(np.int32) << 16))
        samples = np.where(samples >= 1 << 23, samples - (1 << 24), samples)
        x = samples.astype(np.float64) / float(1 << 23)
    elif bits == 16:
        x = np.frombuffer(raw, dtype=np.int16).astype(np.float64) / 32768.0
    elif bits == 32 and audio_format == 3:
        x = np.frombuffer(raw, dtype=np.float32).astype(np.float64)
    else:
        raise RuntimeError(f"unsupported wav: format {audio_format}, {bits} bits")
    if channels > 1:
        x = x[:len(x) // channels * channels].reshape(-1, channels).mean(axis=1)
    return x, rate


def pitch_track(x, rate):
    win = int(rate * WINDOW_S)
    hop = int(rate * HOP_S)
    lag_min = int(rate / MAX_HZ)
    lag_max = int(rate / MIN_HZ)
    times = []
    pitches = []
    powers = []
    for start in range(0, len(x) - win - lag_max, hop):
        seg = x[start:start + win + lag_max]
        seg = seg - seg.mean()
        base = seg[:win]
        power = float(np.sqrt(np.mean(base * base)))
        corr = np.correlate(seg, base, mode="valid")
        corr0 = corr[0] if corr[0] > 1e-9 else 1e-9
        best = None
        window = corr[lag_min:lag_max]
        peak = int(np.argmax(window)) + lag_min
        while peak * 2 < lag_max:
            lo2 = int(peak * 1.9)
            hi2 = min(int(peak * 2.1) + 1, lag_max)
            peak2 = int(np.argmax(corr[lo2:hi2])) + lo2
            if corr[peak2] > 0.82 * corr[peak]:
                peak = peak2
            else:
                break
        if lag_min + 1 < peak < lag_max - 2 and corr[peak] / corr0 > 0.35:
            best = rate / peak
        times.append(start / rate)
        pitches.append(best if best else 0.0)
        powers.append(power)
    return np.array(times), np.array(pitches), np.array(powers)


def find_steady(times, pitches, powers):
    segments = []
    i = 0
    n = len(times)
    while i < n:
        if pitches[i] <= 0.0 or powers[i] < 0.005:
            i += 1
            continue
        j = i
        ref = pitches[i]
        while j + 1 < n and pitches[j + 1] > 0.0 \
                and abs(pitches[j + 1] - ref) / ref < STABLE_TOL \
                and powers[j + 1] >= 0.005:
            j += 1
            ref = 0.85 * ref + 0.15 * pitches[j]
        length = times[j] - times[i]
        if length >= MIN_STABLE_S:
            segment_pitch = float(np.median(pitches[i:j + 1]))
            segments.append((times[i], times[j], segment_pitch,
                             float(np.median(powers[i:j + 1]))))
        i = j + 1
    return segments


def pick_levels(segments):
    segments = sorted(segments, key=lambda s: s[2])
    if not segments:
        return []
    lo = segments[0][2]
    hi = segments[-1][2]
    picked = []
    for k in range(TARGET_LEVELS):
        want = lo * (hi / lo) ** (k / max(TARGET_LEVELS - 1, 1))
        best = min(segments, key=lambda s: abs(s[2] - want))
        if not picked or abs(best[2] - picked[-1][2]) / picked[-1][2] > 0.10:
            picked.append(best)
    return picked


def refine_period(seg, rate, approx_hz):
    lag0 = int(rate / approx_hz)
    lo = max(int(lag0 * 0.9), 8)
    hi = int(lag0 * 1.1)
    base = seg[:lag0 * 6]
    base = base - base.mean()
    corr = np.correlate(seg[:lag0 * 6 + hi] - seg[:lag0 * 6 + hi].mean(), base, mode="valid")
    peak = int(np.argmax(corr[lo:hi])) + lo
    return peak


def cut_loop(x, rate, t0, t1, hz):
    mid = int((t0 + t1) * 0.5 * rate)
    period = refine_period(x[mid:mid + int(rate * 1.0)], rate, hz)
    periods = max(int(LOOP_S * rate / period), 8)
    n = periods * period
    start = mid - n // 2
    seg = x[start:start + n + period].copy()
    fade = period
    for i in range(fade):
        t = i / fade
        seg[n - fade + i] = seg[n - fade + i] * (1.0 - t) + seg[i] * t
    loop = seg[:n]
    rms = np.sqrt(np.mean(loop * loop))
    loop = loop * (0.22 / max(rms, 1e-6))
    return loop, rate / period


def resample(x, rate_in, rate_out):
    n_out = int(len(x) * rate_out / rate_in)
    t_out = np.arange(n_out) * (len(x) - 1) / max(n_out - 1, 1)
    return np.interp(t_out, np.arange(len(x)), x)


def write_wav(path, x, rate):
    x = np.clip(x, -1.0, 1.0)
    pcm = (x * 32767.0).astype(np.int16).tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(pcm)))
        f.write(pcm)


def main():
    src = sys.argv[1]
    x, rate = load_wav(src)
    print(f"loaded {src}: {len(x) / rate:.1f}s at {rate} Hz")
    times, pitches, powers = pitch_track(x, rate)
    segments = find_steady(times, pitches, powers)
    print(f"steady segments found: {len(segments)}")
    for t0, t1, hz, power in segments:
        print(f"  {t0:7.1f}s..{t1:7.1f}s  {hz:6.1f} Hz (~{hz * 60 / FIRING_PER_REV:5.0f} rpm)"
              f"  len {t1 - t0:5.1f}s  rms {power:.3f}")
    picked = pick_levels(segments)
    print(f"picked {len(picked)} levels")
    os.makedirs(OUT_DIR, exist_ok=True)
    manifest = []
    for idx, (t0, t1, hz, _) in enumerate(picked):
        loop, exact_hz = cut_loop(x, rate, t0, t1, hz)
        loop = resample(loop, rate, OUT_RATE)
        name = f"engine_r{idx}.wav"
        write_wav(os.path.join(OUT_DIR, name), loop, OUT_RATE)
        rpm = exact_hz * 60.0 / FIRING_PER_REV
        manifest.append((name, exact_hz, rpm))
        print(f"wrote {name}: {exact_hz:.1f} Hz (~{rpm:.0f} rpm), {len(loop) / OUT_RATE:.2f}s")
    with open(os.path.join(OUT_DIR, "engine_set.cfg"), "w") as f:
        f.write("[engine_set]\n")
        f.write(f"count = {len(manifest)}\n")
        for idx, (name, hz, rpm) in enumerate(manifest):
            f.write(f"loop{idx}_file = {name}\n")
            f.write(f"loop{idx}_rpm = {rpm:.0f}\n")
    print("wrote engine_set.cfg")


if __name__ == "__main__":
    main()
