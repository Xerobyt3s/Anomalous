import math
import os
import random
import struct

AUDIO_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "audio")
RATE = 44100


def write_wav(name, samples):
    peak = max(1e-6, max(abs(s) for s in samples))
    scale = 0.89 / peak
    data = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s * scale)) * 32767))
                    for s in samples)
    path = os.path.join(AUDIO_DIR, name)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(data)))
        f.write(data)
    print(f"wrote {path}: {len(samples) / RATE:.2f}s")


def seamless(samples, blend=0.05):
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


def highpass(samples, alpha):
    lp = lowpass(samples, alpha)
    return [s - l for s, l in zip(samples, lp)]


def engine_loop(name, f0, harmonics, growl):
    n = RATE
    samples = []
    rng = random.Random(11)
    noise = lowpass([rng.uniform(-1, 1) for _ in range(n)], 0.12)
    for i in range(n):
        t = i / RATE
        s = 0.0
        for h, amp in harmonics:
            s += amp * math.sin(2.0 * math.pi * f0 * h * t)
        am = 1.0 + growl * math.sin(2.0 * math.pi * (f0 * 0.5) * t)
        samples.append(s * am + noise[i] * 0.10)
    write_wav(name, samples)


def noise_loop(name, seed, lp_alpha, hp_alpha, crackle):
    n = RATE
    rng = random.Random(seed)
    samples = [rng.uniform(-1, 1) for _ in range(n)]
    samples = lowpass(samples, lp_alpha)
    if hp_alpha > 0.0:
        samples = highpass(samples, hp_alpha)
    if crackle > 0.0:
        for _ in range(90):
            at = rng.randrange(n - 220)
            amp = rng.uniform(0.4, 1.0) * crackle
            for k in range(220):
                samples[at + k] += amp * math.exp(-k / 30.0) * rng.uniform(-1, 1)
    write_wav(name, seamless(samples))


def thunk(name, freq, dur, noise_amp, body_amp, seed=3, attack=0.002):
    n = int(RATE * dur)
    rng = random.Random(seed)
    samples = []
    for i in range(n):
        t = i / RATE
        env = math.exp(-t * 22.0) * min(1.0, t / attack if attack > 0 else 1.0)
        body = body_amp * math.sin(2.0 * math.pi * freq * t) * env
        hit = noise_amp * rng.uniform(-1, 1) * math.exp(-t * 60.0)
        samples.append(body + hit)
    write_wav(name, samples)


def make_starter():
    n = RATE
    rng = random.Random(7)
    samples = []
    for i in range(n):
        t = i / RATE
        whirr = 0.6 * math.sin(2.0 * math.pi * 92.0 * t) + 0.3 * math.sin(2.0 * math.pi * 184.0 * t)
        am = 0.55 + 0.45 * math.sin(2.0 * math.pi * 12.0 * t)
        samples.append(whirr * am + rng.uniform(-1, 1) * 0.12)
    write_wav("starter.wav", seamless(samples))


def make_ratchet():
    n = int(RATE * 0.4)
    rng = random.Random(19)
    samples = [0.0] * n
    at = 0.0
    step = 0.085
    while at < 0.36:
        idx = int(at * RATE)
        for k in range(140):
            if idx + k < n:
                samples[idx + k] += math.exp(-k / 16.0) * rng.uniform(-1, 1)
        at += step
        step = max(0.045, step * 0.86)
    samples = highpass(samples, 0.25)
    write_wav("ratchet.wav", samples)


def make_engine_start():
    n = int(RATE * 0.6)
    samples = []
    rng = random.Random(23)
    for i in range(n):
        t = i / RATE
        f = 55.0 + 90.0 * min(1.0, t / 0.35)
        s = math.sin(2.0 * math.pi * f * t) + 0.4 * math.sin(2.0 * math.pi * 2.0 * f * t)
        env = min(1.0, t / 0.05) * (1.0 if t < 0.45 else max(0.0, 1.0 - (t - 0.45) / 0.15))
        samples.append(s * env + rng.uniform(-1, 1) * 0.08 * env)
    write_wav("engine_start.wav", samples)


def make_squeak(name, f_start, f_end, dur, seed=31):
    n = int(RATE * dur)
    rng = random.Random(seed)
    samples = []
    phase = 0.0
    for i in range(n):
        t = i / RATE
        f = f_start + (f_end - f_start) * (t / dur)
        phase += 2.0 * math.pi * f / RATE
        env = math.sin(math.pi * min(1.0, t / dur)) ** 2
        samples.append((math.sin(phase) * 0.5 + rng.uniform(-1, 1) * 0.1) * env)
    tail_n = int(RATE * 0.12)
    for i in range(tail_n):
        t = i / RATE
        env = math.exp(-t * 30.0)
        samples.append(0.9 * math.sin(2.0 * math.pi * 65.0 * t) * env
                       + rng.uniform(-1, 1) * 0.35 * math.exp(-t * 70.0))
    write_wav(name, samples)


def make_horn():
    n = RATE
    samples = []
    for i in range(n):
        t = i / RATE
        s = math.sin(2.0 * math.pi * 400.0 * t) + 0.9 * math.sin(2.0 * math.pi * 500.0 * t)
        s += 0.25 * math.sin(2.0 * math.pi * 800.0 * t) + 0.2 * math.sin(2.0 * math.pi * 1000.0 * t)
        samples.append(s)
    write_wav("horn.wav", samples)


def make_flap():
    n = int(RATE * 0.10)
    rng = random.Random(53)
    samples = []
    for i in range(n):
        t = i / RATE
        env = math.exp(-t * 45.0) * min(1.0, t / 0.004)
        s = math.sin(2.0 * math.pi * 48.0 * t) * env
        s += rng.uniform(-1, 1) * 0.12 * math.exp(-t * 90.0)
        samples.append(s)
    write_wav("flap.wav", lowpass(samples, 0.08))


def make_rain(name, seed, bed_alpha, bed_amp, crackle_per_s, crackle_amp, ping_per_s, ping_amp):
    dur = 5.0
    n = int(RATE * dur)
    rng = random.Random(seed)
    bed = lowpass([rng.uniform(-1, 1) for _ in range(n)], bed_alpha)
    samples = []
    for i in range(n):
        t = i / RATE
        wob = 1.0 + 0.16 * math.sin(2.0 * math.pi * 0.23 * t) \
                  + 0.09 * math.sin(2.0 * math.pi * 0.61 * t + 1.7)
        samples.append(bed[i] * bed_amp * wob)
    for _ in range(int(crackle_per_s * dur)):
        pos = rng.randrange(n)
        length = rng.randint(30, 130)
        amp = crackle_amp * rng.uniform(0.3, 1.0)
        for j in range(length):
            if pos + j >= n:
                break
            samples[pos + j] += rng.uniform(-1, 1) * amp * math.exp(-j / (length * 0.4))
    for _ in range(int(ping_per_s * dur)):
        pos = rng.randrange(n)
        freq = rng.uniform(160.0, 720.0)
        length = int(RATE * rng.uniform(0.008, 0.030))
        amp = ping_amp * rng.uniform(0.3, 1.0)
        for j in range(length):
            if pos + j >= n:
                break
            samples[pos + j] += amp * math.sin(2.0 * math.pi * freq * j / RATE) \
                              * math.exp(-j / (length * 0.35))
    write_wav(name, seamless(samples, 0.4))


def make_wiper():
    dur = 0.5
    n = int(RATE * dur)
    rng = random.Random(52)
    noise = lowpass([rng.uniform(-1, 1) for _ in range(n)], 0.28)
    samples = []
    for i in range(n):
        t = i / RATE
        arc = math.sin(math.pi * min(t / dur, 1.0))
        squeak = 0.0
        if t > dur * 0.62:
            st = (t - dur * 0.62) / (dur * 0.38)
            squeak = 0.24 * math.sin(2.0 * math.pi * (850.0 + 500.0 * st) * t) \
                   * math.sin(math.pi * st)
        samples.append(noise[i] * 0.8 * arc + squeak)
    write_wav("wiper.wav", samples)


def make_whir():
    dur = 0.42
    n = int(RATE * dur)
    rng = random.Random(77)
    samples = []
    phase = 0.0
    for i in range(n):
        t = i / RATE
        f = 92.0 + 58.0 * min(t / 0.36, 1.0)
        phase += 2.0 * math.pi * f / RATE
        motor = 0.55 * math.sin(phase) + 0.30 * math.sin(2.03 * phase) \
              + 0.18 * math.sin(3.09 * phase)
        gear = 0.22 * math.sin(phase * 9.0 + 0.6 * math.sin(phase * 0.5))
        env = min(t / 0.04, 1.0) * min(max((dur - t) / 0.06, 0.0), 1.0)
        samples.append((motor + gear + rng.uniform(-1, 1) * 0.06) * env)
    write_wav("whir.wav", samples)


def main():
    os.makedirs(AUDIO_DIR, exist_ok=True)
    make_horn()
    make_flap()
    engine_loop("engine_lo.wav", 55.0,
                [(1, 1.0), (2, 0.55), (3, 0.38), (4, 0.22), (5, 0.13), (6, 0.09), (8, 0.05)], 0.35)
    engine_loop("engine_hi.wav", 165.0,
                [(1, 1.0), (2, 0.62), (3, 0.45), (4, 0.33), (5, 0.22), (6, 0.16), (7, 0.11),
                 (8, 0.08), (10, 0.05)], 0.18)
    noise_loop("roll_road.wav", 43, 0.025, 0.0, 0.0)
    noise_loop("roll_grass.wav", 47, 0.05, 0.0, 0.25)
    thunk("thump.wav", 80.0, 0.12, 0.5, 0.7, seed=13)
    make_ratchet()
    make_engine_start()
    make_whir()
    make_rain("rain_light.wav", 61, 0.10, 0.45, 25.0, 0.20, 0.0, 0.0)
    make_rain("rain_heavy.wav", 62, 0.22, 0.75, 140.0, 0.35, 0.0, 0.0)
    make_rain("rain_roof.wav", 63, 0.30, 0.40, 40.0, 0.25, 90.0, 0.55)
    noise_loop("roll_road_wet.wav", 53, 0.09, 0.0, 0.35)
    make_wiper()


if __name__ == "__main__":
    main()
