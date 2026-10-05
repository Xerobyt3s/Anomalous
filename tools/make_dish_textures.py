import math
import os

import numpy as np
from PIL import Image

TEX_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "textures")

DISH_RADIUS = 12.0
DISH_FOCAL = 9.6
DISH_ELEVATION_DEG = 28.0
PANEL_RINGS = 12
INNER_PANELS = 48
OUTER_PANELS = 96

PAINT = np.array([0.44, 0.45, 0.46])
BARE_METAL = np.array([0.32, 0.33, 0.34])
BACK_SHEET = np.array([0.14, 0.14, 0.15])
STEEL = np.array([0.18, 0.185, 0.19])
CONCRETE = np.array([0.30, 0.29, 0.27])
SUB_PAINT = np.array([0.42, 0.43, 0.44])
RUST = np.array([0.26, 0.11, 0.05])
GRIME_DARKENING = 0.45


def hash2(ix, iy, seed):
    h = (ix.astype(np.int64) * 374761393 + iy.astype(np.int64) * 668265263 + seed * 2246822519) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    h = h ^ (h >> 16)
    return (h & 0xFFFFFF).astype(np.float64) / float(0xFFFFFF)


def value_noise(x, y, seed, period_x=None, period_y=None):
    ix = np.floor(x).astype(np.int64)
    iy = np.floor(y).astype(np.int64)
    fx = x - ix
    fy = y - iy
    ux = fx * fx * (3.0 - 2.0 * fx)
    uy = fy * fy * (3.0 - 2.0 * fy)

    def corner(dx, dy):
        cx = ix + dx
        cy = iy + dy
        if period_x:
            cx = np.mod(cx, period_x)
        if period_y:
            cy = np.mod(cy, period_y)
        return hash2(cx, cy, seed)

    a = corner(0, 0)
    b = corner(1, 0)
    c = corner(0, 1)
    d = corner(1, 1)
    return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy


def fbm(x, y, octaves, seed, period_x=None, period_y=None):
    total = np.zeros_like(x)
    amplitude = 0.5
    px, py = period_x, period_y
    for o in range(octaves):
        total += (value_noise(x, y, seed + o * 101, px, py) - 0.5) * amplitude
        x = x * 2.0
        y = y * 2.0
        px = px * 2 if px else None
        py = py * 2 if py else None
        amplitude *= 0.5
    return total


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(np.maximum(c, 0.0031308), 1.0 / 2.4) - 0.055)


def is_data_map(name):
    stem = os.path.splitext(name)[0]
    return stem.endswith(("_n", "_s", "_g"))


def to_u8(a):
    return (np.clip(a, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)


def save_rgb(name, rgb):
    path = os.path.join(TEX_DIR, name + ".png")
    Image.fromarray(to_u8(rgb if is_data_map(name) else linear_to_srgb(rgb)), "RGB").save(path, optimize=True)
    print(f"wrote {path}: {rgb.shape[1]}x{rgb.shape[0]}")


def normal_from_height(height, strength, wrap_x=True, wrap_y=True):
    hx = np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)
    hy = np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)
    if not wrap_x:
        hx[:, 0] = hx[:, 1]
        hx[:, -1] = hx[:, -2]
    if not wrap_y:
        hy[0, :] = hy[1, :]
        hy[-1, :] = hy[-2, :]
    nx = -hx * strength
    ny = -hy * strength
    nz = np.ones_like(height)
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    return np.stack((nx / length, ny / length, nz / length), axis=-1) * 0.5 + 0.5


def surface_map(roughness, metal, cavity):
    return np.stack((roughness, metal, cavity), axis=-1)


def tip_frame():
    e = math.radians(DISH_ELEVATION_DEG)
    x = np.array([1.0, 0.0, 0.0])
    z = np.array([0.0, math.sin(e), math.cos(e)])
    y = np.cross(z, x)
    return x, y, z


def bilinear_wrap_u(img, u, v):
    h, w = img.shape
    fx = np.mod(u, 1.0) * w - 0.5
    fy = np.clip(v, 0.0, 1.0) * h - 0.5
    x0 = np.floor(fx).astype(np.int64)
    y0 = np.clip(np.floor(fy).astype(np.int64), 0, h - 2)
    tx = fx - x0
    ty = np.clip(fy - y0, 0.0, 1.0)
    x0 = np.mod(x0, w)
    x1 = np.mod(x0 + 1, w)
    a = img[y0, x0]
    b = img[y0, x1]
    c = img[y0 + 1, x0]
    d = img[y0 + 1, x1]
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty


def make_reflector(width=4096, height=1024):
    u = (np.arange(width) + 0.5) / width
    v = (np.arange(height) + 0.5) / height
    U, V = np.meshgrid(u, v)
    r = np.maximum(V * DISH_RADIUS, 0.02)
    theta = U * 2.0 * math.pi

    panels_around = np.where(V < 0.5, INNER_PANELS, OUTER_PANELS).astype(np.float64)
    pu = np.mod(U * panels_around, 1.0)
    pv = np.mod(V * PANEL_RINGS, 1.0)
    texels_per_panel_u = width / panels_around
    texels_per_panel_v = height / PANEL_RINGS
    du_t = np.minimum(pu, 1.0 - pu) * texels_per_panel_u
    dv_t = np.minimum(pv, 1.0 - pv) * texels_per_panel_v
    seam = 1.0 - smoothstep(0.5, 1.5, np.minimum(du_t, dv_t))
    rivet_row = np.minimum(np.abs(du_t - 3.0), np.abs(dv_t - 3.0))
    rivets = (1.0 - smoothstep(0.3, 0.9, rivet_row)) * (
        np.abs(np.mod(np.where(du_t < dv_t, pv * texels_per_panel_v, pu * texels_per_panel_u), 6.0) - 3.0) < 0.9)

    panel_id_u = np.floor(U * panels_around)
    panel_id_v = np.floor(V * PANEL_RINGS)
    panel_tone = hash2(panel_id_u.astype(np.int64), panel_id_v.astype(np.int64) + 1000, 7) - 0.5

    gx = U * panels_around * 7.0
    gy = V * PANEL_RINGS * 7.0
    blotch = fbm(gx + 7.3, gy + 2.9, 4, 11) + 0.5
    fine = fbm(gx * 5.0, gy * 5.0, 2, 13)
    chip = smoothstep(0.70, 0.75, blotch + fine * 0.25)

    drift = fbm(U * 96.0 + 4.2, V * 36.0 + 1.7, 3, 17, int(96), None)
    grain = fbm(U * 1024.0, V * 384.0, 2, 19, 1024, None)
    paint = PAINT * (1.0 + drift[..., None] * 0.35 + grain[..., None] * 0.12 + panel_tone[..., None] * 0.08)
    roughness = 0.62 + grain * 0.15
    albedo = paint * (1.0 - chip[..., None]) + BARE_METAL * chip[..., None]
    roughness = roughness * (1.0 - chip) + 0.42 * chip
    albedo = albedo * (1.0 - rivets[..., None] * 0.3) + BARE_METAL * 0.9 * rivets[..., None] * 0.3
    albedo *= 1.0 - seam[..., None] * 0.55
    roughness = roughness * (1.0 - seam) + 0.85 * seam
    metal = np.maximum(chip, rivets * 0.7)

    tx, ty, tz = tip_frame()
    c = np.cos(theta)
    s = np.sin(theta)
    slope = r / (2.0 * DISH_FOCAL)
    n_local = np.stack((-slope * c, -slope * s, np.ones_like(r)), axis=-1)
    n_local /= np.linalg.norm(n_local, axis=-1, keepdims=True)
    n_world_y = n_local[..., 0] * tx[1] + n_local[..., 1] * ty[1] + n_local[..., 2] * tz[1]

    down = np.array([0.0, -1.0, 0.0])
    down_local = np.array([down @ tx, down @ ty, down @ tz])
    pr = np.stack((c, s, slope), axis=-1)
    pt = np.stack((-r * s, r * c, np.zeros_like(r)), axis=-1)
    d_dot_n = (n_local * down_local).sum(-1, keepdims=True)
    down_t = down_local - n_local * d_dot_n
    down_len = np.linalg.norm(down_t, axis=-1)
    down_t /= np.maximum(down_len, 1e-6)[..., None]
    a_theta = (down_t * pt).sum(-1) / np.maximum((pt * pt).sum(-1), 1e-8)
    a_r = (down_t * pr).sum(-1) / (pr * pr).sum(-1)
    world_per_unit = np.sqrt(a_theta ** 2 * (pt * pt).sum(-1) + a_r ** 2 * (pr * pr).sum(-1))
    step_metres = 0.16
    scale = step_metres / np.maximum(world_per_unit, 1e-4)
    d_theta = a_theta * scale
    d_r = a_r * scale

    weep = smoothstep(0.05, 0.35, fbm(U * 9.0, V * 4.0, 3, 23, 9, None))
    seam_metres = np.minimum(np.minimum(pu, 1.0 - pu) * 2.0 * math.pi * r / panels_around,
                             np.minimum(pv, 1.0 - pv) * DISH_RADIUS / PANEL_RINGS)
    seam_source = (1.0 - smoothstep(0.007, 0.04, seam_metres)) * weep
    pick = hash2(np.floor(U * 60.0).astype(np.int64), np.floor(V * 30.0).astype(np.int64), 29)
    cell_u = np.mod(U * 60.0, 1.0) - 0.5
    cell_v = np.mod(V * 30.0, 1.0) - 0.5
    wound = (pick > 0.93) * (1.0 - smoothstep(0.08, 0.22, np.sqrt(cell_u ** 2 + (cell_v * 0.6) ** 2)))
    source = seam_source * 0.5 + wound * 1.8

    rust = np.zeros_like(U)
    weight = 1.0
    wu = U.copy()
    wr = r.copy()
    wander = fbm(U * 60.0, V * 30.0, 2, 31, 60, None) * 0.6
    alive = down_len > 0.05
    for _ in range(28):
        wu = wu - d_theta / (2.0 * math.pi)
        wr = wr - d_r
        wu = wu + wander * step_metres / np.maximum(wr, 0.5) / (2.0 * math.pi)
        alive = alive & (wr > 0.0) & (wr < DISH_RADIUS)
        rust += np.where(alive, bilinear_wrap_u(source, wu, wr / DISH_RADIUS), 0.0) * weight
        weight *= 0.90
    rust *= 0.8 * np.minimum(down_len * 1.5, 1.0)
    rust += source * 0.6
    rust = 1.0 - np.exp(-rust)

    lowness = smoothstep(0.3, -0.6, n_world_y)
    pool = smoothstep(0.55, 0.95, n_world_y)
    grime_noise = fbm(U * 14.0, V * 7.0, 4, 37, 14, None) + 0.5
    grime = np.clip(grime_noise * 0.6 + pool * 0.35 + lowness * 0.1, 0.0, 1.0)

    def weather(base, base_rough, base_metal):
        a = base * (1.0 - rust[..., None]) + RUST * rust[..., None]
        ro = base_rough * (1.0 - rust) + 0.85 * rust
        me = base_metal * (1.0 - rust)
        a *= 1.0 - GRIME_DARKENING * grime[..., None]
        ro = np.maximum(ro, grime * 0.75)
        return a, ro, me

    front_albedo, front_rough, front_metal = weather(albedo, roughness, metal)
    cavity = 1.0 - seam * 0.5 - chip * 0.15
    height_field = -seam - chip * 0.25 + rivets * 0.4
    normal = normal_from_height(height_field, 0.35, True, False)
    save_rgb("dish_panel", front_albedo)
    save_rgb("dish_panel_n", normal)
    save_rgb("dish_panel_s", surface_map(front_rough, front_metal, cavity))

    tone = fbm(U * 96.0, V * 36.0, 3, 41, 96, None)
    back_base = BACK_SHEET * (0.8 + 0.4 * (0.5 + tone[..., None]))
    back_albedo, back_rough, back_metal = weather(back_base, np.full_like(U, 0.85), np.full_like(U, 0.2))
    save_rgb("dish_back", back_albedo)
    save_rgb("dish_back_n", normal_from_height(-seam * 0.6, 0.35, True, False))
    save_rgb("dish_back_s", surface_map(back_rough, back_metal, 1.0 - seam * 0.4))


def tiling_grid(size):
    t = (np.arange(size) + 0.5) / size
    return np.meshgrid(t, t)


def make_steel(size=512):
    U, V = tiling_grid(size)
    mottle = fbm(U * 6.0, V * 6.0, 4, 51, 6, 6) + 0.5
    grain = fbm(U * 64.0, V * 64.0, 2, 53, 64, 64)
    spots = smoothstep(0.70, 0.78, fbm(U * 24.0, V * 24.0, 4, 55, 24, 24) + 0.5)
    streak = smoothstep(0.55, 0.85, fbm(U * 24.0, V * 2.0, 3, 57, 24, 2) + 0.5)
    rust = np.clip(spots * 0.9 + streak * 0.35 * spots.max(), 0.0, 1.0) * 0.7
    grime = 0.3 + 0.3 * mottle
    base = STEEL * (0.75 + 0.6 * mottle[..., None] * 0.6 + grain[..., None] * 0.1)
    albedo = base * (1.0 - rust[..., None]) + RUST * rust[..., None]
    albedo *= 1.0 - GRIME_DARKENING * grime[..., None]
    roughness = np.maximum(0.72 + 0.18 * mottle, grime * 0.75)
    roughness = roughness * (1.0 - rust) + 0.85 * rust
    metal = 0.25 * (1.0 - rust)
    pits = fbm(U * 128.0, V * 128.0, 2, 59, 128, 128)
    height_field = pits * 0.6 - spots * 0.4
    save_rgb("dish_steel", albedo)
    save_rgb("dish_steel_n", normal_from_height(height_field, 1.5))
    save_rgb("dish_steel_s", surface_map(roughness, metal, 1.0 - spots * 0.2))


def make_concrete(size=1024):
    U, V = tiling_grid(size)
    broad = fbm(U * 4.0, V * 4.0, 4, 61, 4, 4)
    aggregate = fbm(U * 160.0, V * 160.0, 2, 63, 160, 160)
    pores = smoothstep(0.30, 0.18, value_noise(U * 220.0, V * 220.0, 65, 220, 220))
    form_v = np.mod(V * 4.0, 1.0)
    form_u = np.mod(U * 2.0, 1.0)
    joint = np.maximum(1.0 - smoothstep(0.0, 0.004, np.minimum(form_v, 1.0 - form_v)),
                       1.0 - smoothstep(0.0, 0.003, np.minimum(form_u, 1.0 - form_u)))
    stain = smoothstep(0.45, 0.80, fbm(U * 18.0, V * 1.5, 3, 67, 18, 2) + 0.5) * smoothstep(0.0, 0.7, 1.0 - V)
    albedo = CONCRETE * (1.0 + broad[..., None] * 0.30 + aggregate[..., None] * 0.18)
    albedo *= 1.0 - pores[..., None] * 0.35
    albedo *= 1.0 - joint[..., None] * 0.35
    albedo *= 1.0 - stain[..., None] * 0.30
    roughness = np.full_like(U, 0.92)
    height_field = aggregate * 0.5 - pores * 0.8 - joint * 1.0
    save_rgb("dish_concrete", albedo)
    save_rgb("dish_concrete_n", normal_from_height(height_field, 1.2))
    save_rgb("dish_concrete_s", surface_map(roughness, np.zeros_like(U), 1.0 - pores * 0.4 - joint * 0.4))


def make_subreflector(size=512):
    U, V = tiling_grid(size)
    drift = fbm(U * 3.0, V * 3.0, 3, 71, 3, 3)
    grain = fbm(U * 48.0, V * 48.0, 2, 73, 48, 48)
    chalk = smoothstep(0.55, 0.85, fbm(U * 8.0, V * 8.0, 4, 75, 8, 8) + 0.5)
    streak = smoothstep(0.5, 0.9, fbm(U * 20.0, V * 2.0, 3, 77, 20, 2) + 0.5)
    albedo = SUB_PAINT * (1.0 + drift[..., None] * 0.30 + grain[..., None] * 0.10)
    albedo *= 1.0 - (0.25 * streak + 0.15 * chalk)[..., None]
    roughness = 0.55 + 0.2 * chalk + grain * 0.1
    save_rgb("dish_sub", albedo)
    save_rgb("dish_sub_n", normal_from_height(grain * 0.4 - chalk * 0.2, 1.0))
    save_rgb("dish_sub_s", surface_map(roughness, np.zeros_like(U), np.ones_like(U)))


def make_beacon(size=64):
    U, V = tiling_grid(size)
    glow = 0.85 + 0.15 * fbm(U * 4.0, V * 4.0, 2, 81, 4, 4)
    albedo = np.stack((0.80 * glow, 0.06 * glow, 0.04 * glow), axis=-1)
    save_rgb("dish_beacon", albedo)
    save_rgb("dish_beacon_s", surface_map(np.full_like(U, 0.2), np.zeros_like(U), np.ones_like(U)))


def main():
    os.makedirs(TEX_DIR, exist_ok=True)
    make_reflector()
    make_steel()
    make_concrete()
    make_subreflector()
    make_beacon()


if __name__ == "__main__":
    main()
