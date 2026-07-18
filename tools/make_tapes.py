import math
import os
import random
import struct

TAPE_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "tapes")
RATE = 22050


def write_wav(subdir, name, samples):
    peak = max(1e-6, max(abs(s) for s in samples))
    scale = 0.85 / peak
    data = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s * scale)) * 32767))
                    for s in samples)
    folder = os.path.join(TAPE_DIR, subdir)
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, name)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(data)))
        f.write(data)
    print(f"wrote {path}: {len(samples) / RATE:.2f}s")


def seamless(samples, blend=0.8):
    n = len(samples)
    k = int(RATE * blend)
    out = list(samples)
    for i in range(k):
        t = i / k
        out[n - k + i] = samples[n - k + i] * (1.0 - t) + samples[i] * t
    return out


def lowpass(samples, alpha):
    out = []
    acc = 0.0
    for s in samples:
        acc += alpha * (s - acc)
        out.append(acc)
    return out


def tone(samples, start, dur, freq, amp, attack=0.02, decay=0.3):
    i0 = int(start * RATE)
    n = int(dur * RATE)
    for i in range(n):
        idx = i0 + i
        if idx >= len(samples):
            break
        t = i / RATE
        env = min(1.0, t / attack) * math.exp(-t / decay)
        samples[idx] += amp * env * math.sin(2.0 * math.pi * freq * t)


def interval_signal():
    dur = 40.0
    n = int(dur * RATE)
    rng = random.Random(7)
    samples = lowpass([rng.uniform(-1, 1) * 0.05 for _ in range(n)], 0.06)
    for i in range(n):
        t = i / RATE
        samples[i] += 0.03 * math.sin(2.0 * math.pi * 55.0 * t)
    for rep in range(8):
        base = rep * 5.0
        tone(samples, base + 0.0, 1.0, 659.3, 0.55, decay=0.55)
        tone(samples, base + 1.1, 1.0, 523.3, 0.50, decay=0.55)
        tone(samples, base + 2.2, 1.6, 392.0, 0.48, decay=0.85)
        tone(samples, base + 0.35, 1.0, 659.3 * 0.5, 0.12, decay=0.55)
        tone(samples, base + 1.45, 1.0, 523.3 * 0.5, 0.11, decay=0.55)
        tone(samples, base + 2.55, 1.6, 392.0 * 0.5, 0.10, decay=0.85)
    write_wav("relay", "interval_signal.wav", seamless(samples))


def night_drive():
    dur = 64.0
    n = int(dur * RATE)
    rng = random.Random(23)
    chords = [(110.0, 130.8, 164.8, 196.0),
              (98.0, 123.5, 146.8, 196.0),
              (87.3, 110.0, 130.8, 174.6),
              (98.0, 116.5, 146.8, 175.0)]
    samples = [0.0] * n
    seg = dur / len(chords)
    for ci, chord in enumerate(chords):
        i0 = int(ci * seg * RATE)
        i1 = int((ci + 1) * seg * RATE)
        for i in range(i0, min(i1, n)):
            t = i / RATE
            frac = (i - i0) / (i1 - i0)
            fade = min(1.0, frac / 0.15, (1.0 - frac) / 0.15 + 0.35)
            s = 0.0
            for k, f in enumerate(chord):
                det = 1.0 + 0.0016 * math.sin(2.0 * math.pi * 0.11 * t + k * 1.7)
                s += math.sin(2.0 * math.pi * f * det * t) / (k + 1.5)
            pulse = 0.65 + 0.35 * math.sin(2.0 * math.pi * 1.6 * t)
            samples[i] += s * 0.22 * fade * pulse
    hiss = lowpass([rng.uniform(-1, 1) * 0.04 for _ in range(n)], 0.10)
    shimmer_f = 587.3
    for i in range(n):
        t = i / RATE
        sh = math.sin(2.0 * math.pi * shimmer_f * t) * 0.03
        sh *= 0.5 + 0.5 * math.sin(2.0 * math.pi * 0.05 * t)
        samples[i] += hiss[i] + sh
    write_wav("relay", "night_drive.wav", seamless(samples))


def recording_114():
    dur = 72.0
    n = int(dur * RATE)
    rng = random.Random(114)
    samples = lowpass([rng.uniform(-1, 1) * 0.09 for _ in range(n)], 0.05)
    for i in range(n):
        t = i / RATE
        samples[i] += 0.05 * math.sin(2.0 * math.pi * 50.0 * t) \
                    + 0.02 * math.sin(2.0 * math.pi * 100.0 * t + 0.6)
    tpos = 2.0
    while tpos < dur - 6.0:
        group = rng.randint(3, 5)
        for g in range(group):
            f = 1046.5 if g % 2 == 0 else 987.8
            tone(samples, tpos + g * 0.42, 0.22, f, 0.5, attack=0.005, decay=0.10)
        tpos += group * 0.42 + rng.uniform(1.6, 3.4)
        if rng.random() < 0.3:
            tone(samples, tpos, 3.5, 130.8, 0.28, attack=1.2, decay=2.2)
            tone(samples, tpos + 0.1, 3.5, 138.6, 0.20, attack=1.4, decay=2.2)
            tpos += 4.2
    for burst in range(5):
        b0 = int(rng.uniform(0.0, dur - 0.6) * RATE)
        blen = int(rng.uniform(0.15, 0.45) * RATE)
        for i in range(b0, min(b0 + blen, n)):
            samples[i] += rng.uniform(-1, 1) * 0.30
    write_wav("found", "recording_114.wav", seamless(samples))


if __name__ == "__main__":
    interval_signal()
    night_drive()
    recording_114()
