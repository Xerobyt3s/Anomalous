import math
import os
import random
import struct
import zlib

ZONE_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "zones", "touge")

HEIGHTMAP_SIZE = 512
CELL_SIZE = 2.0
MASK_SIZE = 1024
ROAD_HALF_WIDTH = 4.2
ROAD_SHOULDER = 7.0
BANK_SLOPE = 0.85
BANK_REACH = 60.0
PAD_RADIUS = 15.0
PAD_BLEND = 30.0
SEED = 31

LEG_COUNT = 9
LEG_LONG = 420.0
LEG_SHORT = 220.0
LEG_TILT = math.radians(12.0)
HAIRPIN_LONG = 22.0
HAIRPIN_SHORT = 14.0
STEP = 2.0

# A jump drops you in at speed, and dead along the tangent runs you straight over the
# outside edge before the first bend. The entry is swung inboard to buy that back.
SPAWN_YAW_OFFSET = 35.0
VALLEY_HEIGHT = 30.0
SUMMIT_HEIGHT = 225.0
TREELINE = 172.0
MAX_GRADE = 0.11


def png_chunk(tag, data):
    chunk = tag + data
    return struct.pack(">I", len(data)) + chunk + struct.pack(">I", zlib.crc32(chunk))


def write_png(path, width, height, bit_depth, color_type, raw_rows):
    ihdr = struct.pack(">IIBBBBB", width, height, bit_depth, color_type, 0, 0, 0)
    blob = (b"\x89PNG\r\n\x1a\n"
            + png_chunk(b"IHDR", ihdr)
            + png_chunk(b"IDAT", zlib.compress(raw_rows, 9))
            + png_chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(blob)


def write_png_gray16(path, size, values):
    rows = bytearray()
    for y in range(size):
        rows.append(0)
        for x in range(size):
            rows += struct.pack(">H", values[y * size + x])
    write_png(path, size, size, 16, 0, bytes(rows))


def write_png_gray8(path, size, values):
    rows = bytearray()
    for y in range(size):
        rows.append(0)
        rows += bytes(values[y * size:(y + 1) * size])
    write_png(path, size, size, 8, 0, bytes(rows))


def smoothstep(edge0, edge1, x):
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


class ValueNoise:
    def __init__(self, seed, lattice_size):
        rng = random.Random(seed)
        self.size = lattice_size
        self.values = [rng.random() for _ in range(lattice_size * lattice_size)]

    def sample(self, x, y):
        size = self.size
        x0 = math.floor(x)
        y0 = math.floor(y)
        fx = x - x0
        fy = y - y0
        fx = fx * fx * (3.0 - 2.0 * fx)
        fy = fy * fy * (3.0 - 2.0 * fy)
        ix0 = int(x0) % size
        iy0 = int(y0) % size
        ix1 = (ix0 + 1) % size
        iy1 = (iy0 + 1) % size
        v00 = self.values[iy0 * size + ix0]
        v10 = self.values[iy0 * size + ix1]
        v01 = self.values[iy1 * size + ix0]
        v11 = self.values[iy1 * size + ix1]
        top = v00 + (v10 - v00) * fx
        bottom = v01 + (v11 - v01) * fx
        return top + (bottom - top) * fy


NOISE_A = ValueNoise(SEED, 16)
NOISE_B = ValueNoise(SEED + 1, 32)
NOISE_C = ValueNoise(SEED + 2, 64)


# The massif climbs toward +X. Ridges and gullies run down the face so that the traverses
# cross spurs instead of a smooth ramp.
def base_height(x, z):
    climb = smoothstep(-500.0, 380.0, x)
    h = VALLEY_HEIGHT + (SUMMIT_HEIGHT - VALLEY_HEIGHT) * climb
    h += 16.0 * math.sin(z * 0.0075 + 0.6) * climb
    h += 9.0 * math.sin(z * 0.014 - 1.2) * (0.35 + 0.65 * climb)
    h += 15.0 * (NOISE_A.sample(x * 0.0045 + 5.0, z * 0.0045 + 2.0) - 0.5) * 2.0
    h += 7.0 * (NOISE_B.sample(x * 0.017, z * 0.017) - 0.5) * 2.0
    h += 1.6 * (NOISE_C.sample(x * 0.07, z * 0.07) - 0.5) * 2.0
    h -= 26.0 * smoothstep(-300.0, -500.0, x)
    return h


# Walks the course by arc length: a diagonal traverse across the face, then a hairpin that
# reverses the run while the climb carries on uphill.
def build_road():
    pts = []
    x, z = -400.0, -215.0
    heading = math.atan2(math.cos(LEG_TILT), math.sin(LEG_TILT))
    turn_sign = 1.0

    for leg in range(LEG_COUNT):
        t = leg / (LEG_COUNT - 1.0)
        length = LEG_LONG + (LEG_SHORT - LEG_LONG) * t
        bend = 0.00055 * (1.0 if leg % 2 == 0 else -1.0)
        walked = 0.0
        while walked < length:
            pts.append((x, z))
            heading += bend * math.sin(walked * 0.0135 + leg * 1.7) * STEP
            x += math.cos(heading) * STEP
            z += math.sin(heading) * STEP
            walked += STEP

        if leg == LEG_COUNT - 1:
            break

        radius = HAIRPIN_LONG + (HAIRPIN_SHORT - HAIRPIN_LONG) * t
        sweep = (math.pi - 2.0 * LEG_TILT) * radius
        arc = 0.0
        while arc < sweep:
            pts.append((x, z))
            heading -= turn_sign * (STEP / radius)
            x += math.cos(heading) * STEP
            z += math.sin(heading) * STEP
            arc += STEP
        turn_sign = -turn_sign

    heights = [base_height(px, pz) for px, pz in pts]
    heights = smooth_profile(heights, 11, 2)
    return pts, limit_grade(heights)


def smooth_profile(heights, window, passes):
    for _ in range(passes):
        smoothed = []
        for i in range(len(heights)):
            lo = max(0, i - window)
            hi = min(len(heights), i + window + 1)
            smoothed.append(sum(heights[lo:hi]) / (hi - lo))
        heights = smoothed
    return heights


# The profile inherits whatever the hillside does, which throws up pitches no road would
# ever have been cut at. Each sweep shaves the rises that break the grade and then
# re-smooths, so a steep pitch is paid back over the run either side of it rather than
# left as a step.
def limit_grade(heights):
    limit = MAX_GRADE * STEP
    for sweep in range(6):
        for i in range(1, len(heights)):
            heights[i] = min(heights[i], heights[i - 1] + limit)
        for i in range(len(heights) - 2, -1, -1):
            heights[i] = min(heights[i], heights[i + 1] + limit)
        if sweep < 5:
            heights = smooth_profile(heights, 9, 1)
    return heights


ROAD_POINTS, ROAD_HEIGHTS = build_road()

BUCKET_SIZE = 16.0
BUCKETS = {}
for idx, (px, pz) in enumerate(ROAD_POINTS):
    BUCKETS.setdefault((int(px // BUCKET_SIZE), int(pz // BUCKET_SIZE)), []).append(idx)


def nearest_road(x, z, radius):
    kx = int(x // BUCKET_SIZE)
    kz = int(z // BUCKET_SIZE)
    reach = int(radius // BUCKET_SIZE) + 1
    best_d2 = radius * radius
    best_idx = -1
    for bz in range(kz - reach, kz + reach + 1):
        for bx in range(kx - reach, kx + reach + 1):
            for idx in BUCKETS.get((bx, bz), ()):
                px, pz = ROAD_POINTS[idx]
                d2 = (px - x) * (px - x) + (pz - z) * (pz - z)
                if d2 < best_d2:
                    best_d2 = d2
                    best_idx = idx
    if best_idx < 0:
        return None, None
    return math.sqrt(best_d2), best_idx


START = ROAD_POINTS[0]
START_H = ROAD_HEIGHTS[0]
_ahead = ROAD_POINTS[10]
_tan = (_ahead[0] - START[0], _ahead[1] - START[1])
_len = math.hypot(*_tan)
TANGENT = (_tan[0] / _len, _tan[1] / _len)
NORMAL = (-TANGENT[1], TANGENT[0])
PAD_CENTER = (START[0] + NORMAL[0] * 19.0 + TANGENT[0] * 24.0,
              START[1] + NORMAL[1] * 19.0 + TANGENT[1] * 24.0)
SUMMIT = ROAD_POINTS[-1]


# Blending terrain toward the road over a fixed radius digs a trench wherever the graded
# profile sits well below a spur. Instead the ground is held inside a wedge that opens out
# from the carriageway at the angle a bank will stand at, which cuts into the hillside on
# the high side and banks out on the low side for exactly as far as it needs to.
def build_grid():
    n = HEIGHTMAP_SIZE
    half = (n - 1) * CELL_SIZE * 0.5
    grid = [0.0] * (n * n)
    for iz in range(n):
        z = iz * CELL_SIZE - half
        row = iz * n
        for ix in range(n):
            grid[row + ix] = base_height(ix * CELL_SIZE - half, z)

    ceiling = [1.0e9] * (n * n)
    floor = [-1.0e9] * (n * n)
    reach = int(BANK_REACH / CELL_SIZE)
    for idx in range(len(ROAD_POINTS)):
        px, pz = ROAD_POINTS[idx]
        ph = ROAD_HEIGHTS[idx]
        cx = int((px + half) / CELL_SIZE)
        cz = int((pz + half) / CELL_SIZE)
        for jz in range(max(0, cz - reach), min(n, cz + reach + 1)):
            dz = (jz * CELL_SIZE - half) - pz
            row = jz * n
            for jx in range(max(0, cx - reach), min(n, cx + reach + 1)):
                dx = (jx * CELL_SIZE - half) - px
                d = math.sqrt(dx * dx + dz * dz)
                if d > BANK_REACH:
                    continue
                bank = BANK_SLOPE * max(0.0, d - ROAD_SHOULDER)
                c = row + jx
                if ph + bank < ceiling[c]:
                    ceiling[c] = ph + bank
                if ph - bank > floor[c]:
                    floor[c] = ph - bank

    for c in range(n * n):
        h = grid[c]
        if h < floor[c]:
            h = floor[c]
        if h > ceiling[c]:
            h = ceiling[c]
        grid[c] = h

    pad_reach = int(PAD_BLEND / CELL_SIZE) + 1
    pcx = int((PAD_CENTER[0] + half) / CELL_SIZE)
    pcz = int((PAD_CENTER[1] + half) / CELL_SIZE)
    for jz in range(max(0, pcz - pad_reach), min(n, pcz + pad_reach + 1)):
        dz = (jz * CELL_SIZE - half) - PAD_CENTER[1]
        row = jz * n
        for jx in range(max(0, pcx - pad_reach), min(n, pcx + pad_reach + 1)):
            dx = (jx * CELL_SIZE - half) - PAD_CENTER[0]
            w = 1.0 - smoothstep(PAD_RADIUS, PAD_BLEND, math.hypot(dx, dz))
            c = row + jx
            grid[c] = grid[c] * (1.0 - w) + START_H * w
    return grid


GRID = None


def ground_at(x, z):
    n = HEIGHTMAP_SIZE
    half = (n - 1) * CELL_SIZE * 0.5
    fx = min(max((x + half) / CELL_SIZE, 0.0), n - 1.001)
    fz = min(max((z + half) / CELL_SIZE, 0.0), n - 1.001)
    ix = int(fx)
    iz = int(fz)
    tx = fx - ix
    tz = fz - iz
    a = GRID[iz * n + ix]
    b = GRID[iz * n + ix + 1]
    c = GRID[(iz + 1) * n + ix]
    d = GRID[(iz + 1) * n + ix + 1]
    return (a + (b - a) * tx) * (1.0 - tz) + (c + (d - c) * tx) * tz


def make_heightmap():
    h_min = min(GRID)
    scale = max(max(GRID) - h_min, 1e-3)
    quantized = [int(round((h - h_min) / scale * 65535.0)) for h in GRID]
    write_png_gray16(os.path.join(ZONE_DIR, "heightmap.png"), HEIGHTMAP_SIZE, quantized)
    return h_min, scale


def make_roadmask():
    n = MASK_SIZE
    world = (HEIGHTMAP_SIZE - 1) * CELL_SIZE
    half = world * 0.5
    values = []
    for iz in range(n):
        z = iz / (n - 1) * world - half
        for ix in range(n):
            x = ix / (n - 1) * world - half
            dist, _ = nearest_road(x, z, 9.0)
            if dist is None:
                values.append(0)
                continue
            v = 1.0 - smoothstep(ROAD_HALF_WIDTH - 0.8, ROAD_HALF_WIDTH + 1.4, dist)
            values.append(int(v * 255.0))
    write_png_gray8(os.path.join(ZONE_DIR, "roadmask.png"), n, values)


def make_zone_cfg(h_min, h_scale):
    rng = random.Random(SEED + 100)
    limit = (HEIGHTMAP_SIZE - 1) * CELL_SIZE * 0.5 - 40.0
    lines = ["# generated by tools/make_touge.py", "",
             "[terrain]",
             "heightmap = heightmap.png",
             "roadmask = roadmask.png",
             "cell_size = " + str(CELL_SIZE),
             "height_offset = %.4f" % h_min,
             "height_scale = %.4f" % h_scale,
             "", "[spawn]"]

    # On the road at the foot of the climb, not on the apron: this is where a jump drops
    # you in, and it drops you in at speed.
    car_x = START[0] + TANGENT[0] * 30.0
    car_z = START[1] + TANGENT[1] * 30.0
    lines.append("car_pos = %.2f %.2f" % (car_x, car_z))
    lines.append("car_yaw_deg = %.1f"
                 % (math.degrees(math.atan2(TANGENT[0], -TANGENT[1])) + SPAWN_YAW_OFFSET))
    lines.append("")
    lines.append("[entities]")

    garage_yaw = math.degrees(math.atan2(-NORMAL[0], -NORMAL[1]))
    lines.append("spawn = building garage %.2f %.2f %.1f 1.0"
                 % (PAD_CENTER[0], PAD_CENTER[1], garage_yaw))
    lines.append("tower = %.1f %.1f %.1f" % (SUMMIT[0], SUMMIT[1], rng.uniform(0.0, 360.0)))

    for item, along, side in (("jerrycan", 34.0, -8.0), ("tire", 40.0, -9.5),
                              ("oilcan", 46.0, -8.6), ("cassette", 52.0, -9.2)):
        px = START[0] + TANGENT[0] * along + NORMAL[0] * side
        pz = START[1] + TANGENT[1] * along + NORMAL[1] * side
        lines.append("pickup = %s %.3f %.3f %.2f 1.000 0"
                     % (item, px, pz, rng.uniform(-180.0, 180.0)))

    placed = [PAD_CENTER]
    trees = 0
    attempts = 0
    while trees < 260 and attempts < 12000:
        attempts += 1
        x = rng.uniform(-limit, limit)
        z = rng.uniform(-limit, limit)
        ground = ground_at(x, z)
        # Pines give out above the treeline, and thin as they approach it.
        if ground > TREELINE:
            continue
        if rng.random() > 1.0 - smoothstep(TREELINE - 70.0, TREELINE, ground) * 0.85:
            continue
        if nearest_road(x, z, 11.0)[0] is not None:
            continue
        if math.hypot(x - PAD_CENTER[0], z - PAD_CENTER[1]) < 28.0:
            continue
        if any(math.hypot(x - ox, z - oz) < 12.0 for ox, oz in placed):
            continue
        placed.append((x, z))
        lines.append("spawn = tree tree_pine %.2f %.2f %.1f %.2f"
                     % (x, z, rng.uniform(0.0, 360.0), rng.uniform(0.8, 1.5)))
        trees += 1

    rocks = 0
    attempts = 0
    while rocks < 70 and attempts < 6000:
        attempts += 1
        x = rng.uniform(-limit, limit)
        z = rng.uniform(-limit, limit)
        ground = ground_at(x, z)
        # Scree belongs high on the face, not scattered across the valley floor.
        if rng.random() > 0.15 + 0.85 * smoothstep(TREELINE - 90.0, SUMMIT_HEIGHT, ground):
            continue
        if nearest_road(x, z, 8.5)[0] is not None:
            continue
        if math.hypot(x - PAD_CENTER[0], z - PAD_CENTER[1]) < 26.0:
            continue
        if any(math.hypot(x - ox, z - oz) < 9.0 for ox, oz in placed):
            continue
        placed.append((x, z))
        lines.append("spawn = rock rock %.2f %.2f %.1f %.2f"
                     % (x, z, rng.uniform(0.0, 360.0), rng.uniform(0.7, 2.4)))
        rocks += 1

    with open(os.path.join(ZONE_DIR, "zone.cfg"), "w") as f:
        f.write("\n".join(lines) + "\n")
    return trees, rocks


def main():
    global GRID
    os.makedirs(ZONE_DIR, exist_ok=True)
    xs = [p[0] for p in ROAD_POINTS]
    zs = [p[1] for p in ROAD_POINTS]
    length = len(ROAD_POINTS) * STEP
    gain = ROAD_HEIGHTS[-1] - ROAD_HEIGHTS[0]
    grades = [abs(ROAD_HEIGHTS[i + 1] - ROAD_HEIGHTS[i]) / STEP
              for i in range(len(ROAD_HEIGHTS) - 1)]
    print("course: %.0f m, %+.1f m climb, %.1f%% mean, %.1f%% steepest"
          % (length, gain, gain / length * 100.0, max(grades) * 100.0))
    print("  bounds x %.0f..%.0f  z %.0f..%.0f  (map half %.0f)"
          % (min(xs), max(xs), min(zs), max(zs),
             (HEIGHTMAP_SIZE - 1) * CELL_SIZE * 0.5))
    print("heightmap...")
    GRID = build_grid()
    h_min, h_scale = make_heightmap()
    print("  height range %.1f .. %.1f m" % (h_min, h_min + h_scale))
    print("road mask...")
    make_roadmask()
    print("zone.cfg...")
    trees, rocks = make_zone_cfg(h_min, h_scale)
    print("  %d trees, %d rocks" % (trees, rocks))


if __name__ == "__main__":
    main()
