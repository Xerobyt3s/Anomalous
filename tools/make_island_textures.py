import os

import numpy as np
from PIL import Image

TEX_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "textures")

MEADOW_GAIN = 0.55
MEADOW_GRADE = np.array([0.92, 1.12, 1.60])
ALBEDO_ROCK = np.array([0.088, 0.082, 0.074])
SIZE = 512


def load(name):
    image = Image.open(os.path.join(TEX_DIR, name)).convert("RGB")
    return np.asarray(image.resize((SIZE, SIZE), Image.LANCZOS)).astype(np.float64) / 255.0


def srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def save(name, rgb):
    out = np.clip(rgb * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8)
    Image.fromarray(out, "RGB").save(os.path.join(TEX_DIR, name))
    print(f"wrote {name}: mean {out.reshape(-1, 3).mean(0).round(1)}")


def main():
    meadow = srgb_to_linear(load("meadow_ground.png")) * MEADOW_GAIN * MEADOW_GRADE
    save("island_grass.png", meadow)
    save("island_turf.png", meadow)

    rock = load("rock.png")
    lum = rock @ np.array([0.2126, 0.7152, 0.0722])
    tint = rock / np.maximum(rock.mean(axis=(0, 1)), 1e-3)
    island_rock = ALBEDO_ROCK * (0.40 + 1.25 * lum)[..., None] * (0.80 + 0.20 * tint)
    save("island_rock.png", island_rock)
    save("island_grass_g.png", island_rock)
    matte = np.ones((16, 16, 3)) * np.array([0.92, 0.0, 1.0])
    save("island_turf_s.png", matte)
    save("island_rock_s.png", np.ones((16, 16, 3)) * np.array([0.85, 0.0, 1.0]))


if __name__ == "__main__":
    main()
