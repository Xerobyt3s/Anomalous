import math
import os
import shutil
import sys

import numpy as np
from PIL import Image


def read_cfg(path):
    values = {}
    craters = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if "=" not in line:
                continue
            key, value = (part.strip() for part in line.split("=", 1))
            if key == "crater":
                craters.append([float(v) for v in value.split()])
            else:
                values[key] = value
    return values, craters


def noise(x, z, seed):
    return (math.sin(x * 0.37 + seed * 1.7) * math.cos(z * 0.29 - seed) +
            0.5 * math.sin(x * 0.81 - z * 0.67 + seed * 3.1))


def main(zone_dir):
    zone_cfg, _ = read_cfg(os.path.join(zone_dir, "zone.cfg"))
    cell = float(zone_cfg.get("cell_size", "1.0"))
    offset = float(zone_cfg.get("height_offset", "0.0"))
    scale = float(zone_cfg.get("height_scale", "1.0"))
    _, craters = read_cfg(os.path.join(zone_dir, "craters.cfg"))

    base_path = os.path.join(zone_dir, "heightmap_base.png")
    out_path = os.path.join(zone_dir, "heightmap.png")
    if not os.path.exists(base_path):
        shutil.copyfile(out_path, base_path)

    raw = np.asarray(Image.open(base_path)).astype(np.float64)
    heights = raw / 65535.0 * scale + offset
    size = heights.shape[0]
    half = (size - 1) * cell * 0.5
    coords = np.arange(size) * cell - half
    gx, gz = np.meshgrid(coords, coords)

    for cx, cz, radius, depth, seed in craters:
        dx, dz = gx - cx, gz - cz
        angle = np.arctan2(dz, dx)
        wobble = 1.0 + 0.12 * np.sin(angle * 3.0 + seed) + 0.07 * np.sin(angle * 7.0 - seed * 2.0)
        rho = np.sqrt(dx * dx + dz * dz) / (radius * wobble)
        bowl = -depth * np.clip(1.0 - rho * rho, 0.0, 1.0) ** 1.3
        rim = depth * 0.22 * np.exp(-((rho - 1.0) / 0.22) ** 2)
        rough = np.vectorize(noise)(gx, gz, seed) * 0.25 * np.exp(-((rho - 0.6) / 0.6) ** 2)
        heights = heights + bowl + rim + rough * (rho < 1.6)

    clipped = np.clip((heights - offset) / scale, 0.0, 1.0)
    Image.fromarray((clipped * 65535.0 + 0.5).astype(np.uint16)).save(out_path)
    print(f"wrote {out_path} with {len(craters)} craters")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join("assets", "zones", "testzone"))
