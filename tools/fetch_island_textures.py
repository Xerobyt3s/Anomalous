import io
import os
import sys
import urllib.request
import zipfile

import numpy as np
from PIL import Image

TEX_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "textures")
CREDITS = os.path.join(os.path.dirname(__file__), "..", "assets", "CREDITS.txt")
CACHE = os.path.join(os.environ.get("TEMP", "."), "anomalous_ambientcg")
SIZE = 1024

MATERIALS = {
    "island_rock": dict(asset="Rock051", luminance=0.085, also=[]),
    "island_soil": dict(asset="Ground048", luminance=0.045, also=[]),
}


def fetch(asset):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, f"{asset}_1K-JPG.zip")
    if not os.path.exists(path):
        url = f"https://ambientcg.com/get?file={asset}_1K-JPG.zip"
        print(f"downloading {url}")
        with urllib.request.urlopen(url) as response, open(path, "wb") as out:
            out.write(response.read())
    return zipfile.ZipFile(path)


def read(archive, asset, suffix):
    name = f"{asset}_1K-JPG_{suffix}.jpg"
    if name not in archive.namelist():
        return None
    image = Image.open(io.BytesIO(archive.read(name)))
    return np.asarray(image.convert("RGB").resize((SIZE, SIZE), Image.LANCZOS)).astype(np.float64) / 255.0


def srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def save(name, rgb):
    out = np.clip(rgb * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8)
    Image.fromarray(out, "RGB").save(os.path.join(TEX_DIR, f"{name}.png"))
    print(f"wrote {name}.png")


def convert(name, asset, luminance, also):
    archive = fetch(asset)
    color = srgb_to_linear(read(archive, asset, "Color"))
    lum = color @ np.array([0.2126, 0.7152, 0.0722])
    albedo = color * (luminance / max(lum.mean(), 1e-4))
    save(name, albedo)
    for alias in also:
        save(alias, albedo)

    normal = read(archive, asset, "NormalGL")
    if normal is not None:
        save(f"{name}_n", normal)

    rough = read(archive, asset, "Roughness")
    ao = read(archive, asset, "AmbientOcclusion")
    surface = np.zeros((SIZE, SIZE, 3))
    surface[..., 0] = rough[..., 0] if rough is not None else 0.85
    surface[..., 2] = ao[..., 0] if ao is not None else 1.0
    save(f"{name}_s", surface)


def credit():
    with open(CREDITS, encoding="utf-8") as f:
        text = f.read()
    lines = []
    for name, params in MATERIALS.items():
        asset = params["asset"]
        line = (f"textures/{name}.png (+_n, _s): ambientCG \"{asset}\" "
                f"(https://ambientcg.com/view?id={asset}), CC0 1.0, 1K, linearised and graded by "
                f"tools/fetch_island_textures.py.")
        if f"\"{asset}\"" not in text:
            lines.append(line)
    if lines:
        with open(CREDITS, "a", encoding="utf-8") as f:
            for line in lines:
                f.write(line + "\n")


def main():
    for name, params in MATERIALS.items():
        convert(name, **params)
    credit()


if __name__ == "__main__":
    sys.exit(main())
