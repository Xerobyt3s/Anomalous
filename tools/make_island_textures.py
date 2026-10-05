import os

import numpy as np
from PIL import Image

TEX_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "textures")

MEADOW_GAIN = 0.55
MEADOW_GRADE = np.array([0.92, 1.12, 1.60])
SIZE = 512


def load(name):
    image = Image.open(os.path.join(TEX_DIR, name)).convert("RGB")
    return np.asarray(image.resize((SIZE, SIZE), Image.LANCZOS)).astype(np.float64) / 255.0


def srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(np.maximum(c, 0.0031308), 1.0 / 2.4) - 0.055)


def is_data_map(name):
    stem = os.path.splitext(name)[0]
    return stem.endswith(("_n", "_s", "_g"))


def save(name, rgb):
    rgb = rgb if is_data_map(name) else linear_to_srgb(rgb)
    out = np.clip(rgb * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8)
    Image.fromarray(out, "RGB").save(os.path.join(TEX_DIR, name))
    print(f"wrote {name}: mean {out.reshape(-1, 3).mean(0).round(1)}")


def main():
    meadow = srgb_to_linear(load("meadow_ground.png")) * MEADOW_GAIN * MEADOW_GRADE
    save("island_grass.png", meadow)


if __name__ == "__main__":
    main()
