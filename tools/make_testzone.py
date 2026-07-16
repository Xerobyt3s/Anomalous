import math
import os
import random
import struct
import zlib

ZONE_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "zones", "testzone")
TEX_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "textures")

HEIGHTMAP_SIZE = 384
CELL_SIZE = 2.0
MASK_SIZE = 768
ROAD_HALF_WIDTH = 5.0
ROAD_BLEND = 26.0
PAD_RADIUS = 16.0
PAD_BLEND = 32.0
SEED = 7


def png_chunk(tag, data):
    chunk = tag + data
    return struct.pack(">I", len(data)) + chunk + struct.pack(">I", zlib.crc32(chunk))


def write_png(path, width, height, bit_depth, color_type, raw_rows):
    ihdr = struct.pack(">IIBBBBB", width, height, bit_depth, color_type, 0, 0, 0)
    payload = zlib.compress(raw_rows, 9)
    blob = (b"\x89PNG\r\n\x1a\n"
            + png_chunk(b"IHDR", ihdr)
            + png_chunk(b"IDAT", payload)
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


def write_png_rgb8(path, size, pixels):
    rows = bytearray()
    for y in range(size):
        rows.append(0)
        row = pixels[y * size:(y + 1) * size]
        for r, g, b in row:
            rows += bytes((r, g, b))
    write_png(path, size, size, 8, 2, bytes(rows))


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


def base_height(x, z):
    h = 11.0 * (NOISE_A.sample(x * 0.004 + 3.0, z * 0.004 + 7.0) - 0.5) * 2.0
    h += 4.0 * (NOISE_B.sample(x * 0.016, z * 0.016) - 0.5) * 2.0
    h += 1.1 * (NOISE_C.sample(x * 0.06, z * 0.06) - 0.5) * 2.0
    dist_center = math.hypot(x, z)
    h += smoothstep(290.0, 380.0, dist_center) * 24.0
    return h


def road_center(theta):
    r = 225.0 + 55.0 * math.sin(2.0 * theta + 1.3) + 28.0 * math.sin(3.0 * theta + 0.4)
    return (r * math.cos(theta), r * math.sin(theta))


def build_road():
    count = 1400
    points = [road_center(i / count * 2.0 * math.pi) for i in range(count)]
    heights = [base_height(px, pz) for px, pz in points]
    for _ in range(3):
        smoothed = []
        window = 45
        for i in range(count):
            total = 0.0
            for k in range(-window, window + 1):
                total += heights[(i + k) % count]
            smoothed.append(total / (2 * window + 1))
        heights = smoothed
    return points, heights


ROAD_POINTS, ROAD_HEIGHTS = build_road()

BUCKET_SIZE = 16.0
BUCKETS = {}
for idx, (px, pz) in enumerate(ROAD_POINTS):
    key = (int(px // BUCKET_SIZE), int(pz // BUCKET_SIZE))
    BUCKETS.setdefault(key, []).append(idx)


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


PAD_THETA = 0.35
PAD_ROAD_IDX = int(PAD_THETA / (2.0 * math.pi) * len(ROAD_POINTS))
PAD_ROAD_POINT = ROAD_POINTS[PAD_ROAD_IDX]
PAD_HEIGHT = ROAD_HEIGHTS[PAD_ROAD_IDX]
_next_point = ROAD_POINTS[(PAD_ROAD_IDX + 8) % len(ROAD_POINTS)]
_tangent = (_next_point[0] - PAD_ROAD_POINT[0], _next_point[1] - PAD_ROAD_POINT[1])
_tlen = math.hypot(*_tangent)
TANGENT = (_tangent[0] / _tlen, _tangent[1] / _tlen)
NORMAL = (-TANGENT[1], TANGENT[0])
PAD_CENTER = (PAD_ROAD_POINT[0] + NORMAL[0] * 20.0, PAD_ROAD_POINT[1] + NORMAL[1] * 20.0)


def zone_height(x, z):
    h = base_height(x, z)
    dist, idx = nearest_road(x, z, ROAD_BLEND)
    if dist is not None:
        w = 1.0 - smoothstep(ROAD_HALF_WIDTH, ROAD_BLEND, dist)
        h = h * (1.0 - w) + ROAD_HEIGHTS[idx] * w
    pad_dist = math.hypot(x - PAD_CENTER[0], z - PAD_CENTER[1])
    if pad_dist < PAD_BLEND:
        w = 1.0 - smoothstep(PAD_RADIUS, PAD_BLEND, pad_dist)
        h = h * (1.0 - w) + PAD_HEIGHT * w
    return h


def make_heightmap():
    n = HEIGHTMAP_SIZE
    half = (n - 1) * CELL_SIZE * 0.5
    heights = []
    for iz in range(n):
        z = iz * CELL_SIZE - half
        for ix in range(n):
            x = ix * CELL_SIZE - half
            heights.append(zone_height(x, z))
    h_min = min(heights)
    h_max = max(heights)
    scale = h_max - h_min
    quantized = [int(round((h - h_min) / scale * 65535.0)) for h in heights]
    write_png_gray16(os.path.join(ZONE_DIR, "heightmap.png"), n, quantized)
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
            dist, _ = nearest_road(x, z, 10.0)
            if dist is None:
                values.append(0)
                continue
            v = 1.0 - smoothstep(ROAD_HALF_WIDTH - 0.8, ROAD_HALF_WIDTH + 1.2, dist)
            values.append(int(v * 255.0))
    write_png_gray8(os.path.join(ZONE_DIR, "roadmask.png"), n, values)


def make_texture(path, size, base_color, variation, noise_scale, streak_axis=None, seed=0):
    noise_hi = ValueNoise(seed * 3 + 11, 32)
    noise_lo = ValueNoise(seed * 3 + 12, 8)
    pixels = []
    for y in range(size):
        for x in range(size):
            u = x / size
            v = y / size
            if streak_axis == "y":
                n = 0.65 * noise_hi.sample(u * noise_scale * 0.25 * 32, v * noise_scale * 32) \
                  + 0.35 * noise_lo.sample(u * 2 * 8, v * 8)
            else:
                n = 0.6 * noise_hi.sample(u * noise_scale * 32, v * noise_scale * 32) \
                  + 0.4 * noise_lo.sample(u * 8, v * 8)
            n = (n - 0.5) * 2.0
            px = []
            for c in range(3):
                value = base_color[c] + variation[c] * n
                px.append(max(0, min(255, int(value))))
            pixels.append(tuple(px))
    write_png_rgb8(path, size, pixels)


def make_textures():
    os.makedirs(TEX_DIR, exist_ok=True)
    make_texture(os.path.join(TEX_DIR, "grass.png"), 256, (68, 96, 48), (18, 22, 12), 1.0, None, 1)
    make_texture(os.path.join(TEX_DIR, "rock.png"), 256, (118, 112, 106), (34, 32, 30), 1.4, None, 2)
    make_texture(os.path.join(TEX_DIR, "road.png"), 256, (52, 52, 55), (10, 10, 11), 2.0, None, 3)
    make_texture(os.path.join(TEX_DIR, "concrete.png"), 256, (150, 148, 142), (16, 16, 15), 0.8, None, 4)
    make_texture(os.path.join(TEX_DIR, "roof.png"), 256, (72, 70, 74), (12, 12, 13), 1.2, "y", 5)
    make_texture(os.path.join(TEX_DIR, "bark.png"), 256, (86, 62, 44), (22, 16, 12), 1.0, "y", 6)
    make_texture(os.path.join(TEX_DIR, "leaves.png"), 256, (42, 66, 36), (14, 20, 12), 1.6, None, 7)
    write_png_rgb8(os.path.join(TEX_DIR, "white.png"), 4, [(255, 255, 255)] * 16)


def make_zone_cfg(h_min, h_scale):
    rng = random.Random(SEED + 100)
    lines = []
    lines.append("# generated by tools/make_testzone.py")
    lines.append("")
    lines.append("[terrain]")
    lines.append("heightmap = heightmap.png")
    lines.append("roadmask = roadmask.png")
    lines.append(f"cell_size = {CELL_SIZE}")
    lines.append(f"height_offset = {h_min:.4f}")
    lines.append(f"height_scale = {h_scale:.4f}")
    lines.append("")
    lines.append("[spawn]")
    car_x = PAD_CENTER[0] - NORMAL[0] * 8.0
    car_z = PAD_CENTER[1] - NORMAL[1] * 8.0
    car_yaw = math.degrees(math.atan2(TANGENT[0], -TANGENT[1]))
    lines.append(f"car_pos = {car_x:.2f} {car_z:.2f}")
    lines.append(f"car_yaw_deg = {car_yaw:.1f}")
    lines.append("")
    lines.append("[entities]")
    garage_yaw = math.degrees(math.atan2(-NORMAL[0], -NORMAL[1]))
    lines.append(f"spawn = building garage {PAD_CENTER[0]:.2f} {PAD_CENTER[1]:.2f} {garage_yaw:.1f} 1.0")

    placed = []
    tree_count = 0
    attempts = 0
    while tree_count < 90 and attempts < 3000:
        attempts += 1
        x = rng.uniform(-360.0, 360.0)
        z = rng.uniform(-360.0, 360.0)
        if math.hypot(x, z) > 355.0:
            continue
        dist, _ = nearest_road(x, z, 12.0)
        if dist is not None:
            continue
        if math.hypot(x - PAD_CENTER[0], z - PAD_CENTER[1]) < 30.0:
            continue
        if any(math.hypot(x - ox, z - oz) < 9.0 for ox, oz in placed):
            continue
        placed.append((x, z))
        yaw = rng.uniform(0.0, 360.0)
        scale = rng.uniform(0.8, 1.5)
        lines.append(f"spawn = tree tree_pine {x:.2f} {z:.2f} {yaw:.1f} {scale:.2f}")
        tree_count += 1

    rock_count = 0
    attempts = 0
    while rock_count < 16 and attempts < 1000:
        attempts += 1
        x = rng.uniform(-340.0, 340.0)
        z = rng.uniform(-340.0, 340.0)
        dist, _ = nearest_road(x, z, 9.0)
        if dist is not None:
            continue
        if math.hypot(x - PAD_CENTER[0], z - PAD_CENTER[1]) < 26.0:
            continue
        if any(math.hypot(x - ox, z - oz) < 7.0 for ox, oz in placed):
            continue
        placed.append((x, z))
        yaw = rng.uniform(0.0, 360.0)
        scale = rng.uniform(0.7, 2.2)
        lines.append(f"spawn = rock rock {x:.2f} {z:.2f} {yaw:.1f} {scale:.2f}")
        rock_count += 1

    with open(os.path.join(ZONE_DIR, "zone.cfg"), "w") as f:
        f.write("\n".join(lines) + "\n")
    return tree_count, rock_count


def main():
    os.makedirs(ZONE_DIR, exist_ok=True)
    print("generating heightmap...")
    h_min, h_scale = make_heightmap()
    print(f"  height range: {h_min:.2f} .. {h_min + h_scale:.2f} m")
    print("generating road mask...")
    make_roadmask()
    print("generating textures...")
    make_textures()
    print("writing zone.cfg...")
    trees, rocks = make_zone_cfg(h_min, h_scale)
    print(f"  placed {trees} trees, {rocks} rocks")
    print("done")


if __name__ == "__main__":
    main()
