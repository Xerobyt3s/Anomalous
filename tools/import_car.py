import argparse
import json
import os
import struct

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MESH_DIR = os.path.join(ROOT, "assets", "meshes")
TEX_DIR = os.path.join(ROOT, "assets", "textures")
CAR_DIR = os.path.join(ROOT, "assets", "cars")

SEAM = 0.012
SIDE = 0.05


def read_glb(path):
    data = open(path, "rb").read()
    magic, version, _ = struct.unpack_from("<III", data, 0)
    assert magic == 0x46546C67 and version == 2, "not a glTF 2 binary"
    json_len, _ = struct.unpack_from("<II", data, 12)
    doc = json.loads(data[20:20 + json_len])
    bin_start = 20 + json_len + 8
    return doc, data[bin_start:]


def accessor(doc, blob, index):
    acc = doc["accessors"][index]
    view = doc["bufferViews"][acc["bufferView"]]
    comps = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[acc["type"]]
    dtype = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[acc["componentType"]]
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    count = acc["count"]
    stride = view.get("byteStride", 0)
    item = np.dtype(dtype).itemsize * comps
    if stride and stride != item:
        rows = [np.frombuffer(blob, dtype, comps, start + i * stride) for i in range(count)]
        return np.array(rows)
    out = np.frombuffer(blob, dtype, count * comps, start)
    return out.reshape(count, comps) if comps > 1 else out.copy()


def trs(node):
    m = np.eye(4)
    if "matrix" in node:
        return np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T
    t = node.get("translation", [0, 0, 0])
    r = node.get("rotation", [0, 0, 0, 1])
    s = node.get("scale", [1, 1, 1])
    x, y, z, w = r
    rot = np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])
    m[:3, :3] = rot * np.array(s)
    m[:3, 3] = t
    return m


class Piece:
    def __init__(self, name, pos, nrm, uv, idx, material):
        self.name = name
        self.pos = pos
        self.nrm = nrm
        self.uv = uv
        self.idx = idx
        self.material = material

    def copy(self, name=None):
        return Piece(name or self.name, self.pos.copy(), self.nrm.copy(), self.uv.copy(), self.idx.copy(), self.material)

    def bounds(self):
        return self.pos.min(axis=0), self.pos.max(axis=0)


def load_pieces(path):
    doc, blob = read_glb(path)
    materials = [m.get("name", "material%d" % i) for i, m in enumerate(doc.get("materials", []))]
    colors = {}
    for i, m in enumerate(doc.get("materials", [])):
        colors[materials[i]] = m.get("pbrMetallicRoughness", {}).get("baseColorFactor", [0.8, 0.8, 0.8, 1.0])
    pieces = {}

    def visit(index, parent):
        node = doc["nodes"][index]
        world = parent @ trs(node)
        if "mesh" in node:
            parts = []
            for prim in doc["meshes"][node["mesh"]]["primitives"]:
                pos = accessor(doc, blob, prim["attributes"]["POSITION"]).astype(np.float64)
                nrm = accessor(doc, blob, prim["attributes"]["NORMAL"]).astype(np.float64)
                uv = (accessor(doc, blob, prim["attributes"]["TEXCOORD_0"]).astype(np.float64)
                      if "TEXCOORD_0" in prim["attributes"] else np.zeros((len(pos), 2)))
                idx = accessor(doc, blob, prim["indices"]).astype(np.int64).reshape(-1, 3)
                pos = (np.c_[pos, np.ones(len(pos))] @ world.T)[:, :3]
                nrm = nrm @ np.linalg.inv(world[:3, :3])
                nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
                material = materials[prim["material"]] if "material" in prim else "default"
                parts.append(Piece(node.get("name", "node%d" % index), pos, nrm, uv, idx, material))
            pieces[node.get("name", "node%d" % index)] = parts
        for child in node.get("children", []):
            visit(child, world)

    for root in doc["scenes"][doc.get("scene", 0)]["nodes"]:
        visit(root, np.eye(4))
    return pieces, colors


def mirrored(piece):
    out = piece.copy()
    out.pos[:, 0] *= -1.0
    out.nrm[:, 0] *= -1.0
    out.idx = out.idx[:, ::-1].copy()
    return out


def merge(pieces, name=None):
    pos, nrm, uv, idx = [], [], [], []
    base = 0
    for p in pieces:
        pos.append(p.pos)
        nrm.append(p.nrm)
        uv.append(p.uv)
        idx.append(p.idx + base)
        base += len(p.pos)
    return Piece(name or pieces[0].name, np.vstack(pos), np.vstack(nrm), np.vstack(uv), np.vstack(idx), pieces[0].material)


def is_half(piece):
    lo, hi = piece.bounds()
    return lo[0] > -SEAM and hi[0] > SEAM


def straddles(piece):
    lo, _ = piece.bounds()
    return lo[0] < SEAM


def whole(piece):
    if not is_half(piece):
        return piece
    if straddles(piece):
        return merge([piece, mirrored(piece)], piece.name)
    return piece


def side_of(piece, sign):
    tris = piece.idx
    centroid_x = piece.pos[tris].mean(axis=1)[:, 0]
    keep = tris[centroid_x * sign > 0.0]
    used = np.unique(keep)
    remap = -np.ones(len(piece.pos), dtype=np.int64)
    remap[used] = np.arange(len(used))
    out = Piece(piece.name, piece.pos[used], piece.nrm[used], piece.uv[used], remap[keep], piece.material)
    return out


def left_right(piece):
    lo, hi = piece.bounds()
    if lo[0] < -SIDE and hi[0] > SIDE:
        return side_of(piece, -1.0), side_of(piece, 1.0)
    left = piece if hi[0] < 0.0 else mirrored(piece)
    return left, mirrored(left)


def to_car(piece, scale, shift):
    out = piece.copy()
    out.pos = np.c_[-out.pos[:, 0], out.pos[:, 1], -out.pos[:, 2]] * scale + shift
    out.nrm = np.c_[-out.nrm[:, 0], out.nrm[:, 1], -out.nrm[:, 2]]
    return out


def doubled(piece):
    back = piece.copy()
    back.nrm = -back.nrm
    back.idx = back.idx[:, ::-1].copy()
    return merge([piece, back], piece.name)


def offset(piece, by):
    out = piece.copy()
    out.pos = out.pos - np.asarray(by)
    return out


def write_amsh(path, groups):
    vertices = []
    indices = []
    submeshes = []
    for material, piece in groups:
        base = len(vertices)
        first = len(indices)
        for p, n, t in zip(piece.pos, piece.nrm, piece.uv):
            vertices.append((p[0], p[1], p[2], n[0], n[1], n[2], t[0], t[1]))
        for tri in piece.idx:
            indices.extend(int(i) + base for i in tri)
        submeshes.append((first, len(indices) - first, material))
    assert len(submeshes) <= 16
    with open(path, "wb") as f:
        f.write(struct.pack("<5I", 0x48534D41, 1, len(vertices), len(indices), len(submeshes)))
        for v in vertices:
            f.write(struct.pack("<8f", *v))
        f.write(struct.pack("<%dI" % len(indices), *indices))
        for first, count, material in submeshes:
            name = material.encode("ascii")[:31]
            f.write(struct.pack("<II", first, count) + name + b"\0" * (32 - len(name)))


def srgb(c):
    c = max(0.0, min(1.0, c))
    return c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1.0 / 2.4) - 0.055


def write_texture(name, linear, noise=6, seed=1):
    rng = np.random.default_rng(seed)
    base = np.array([srgb(c) * 255.0 for c in linear[:3]])
    img = base + rng.normal(0.0, noise, (64, 64, 1))
    Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB").save(os.path.join(TEX_DIR, name + ".png"))


def box_piece(center, half, material):
    c = np.asarray(center, dtype=np.float64)
    h = np.asarray(half, dtype=np.float64)
    pos, nrm, uv, idx = [], [], [], []
    for axis in range(3):
        for sign in (-1.0, 1.0):
            n = np.zeros(3)
            n[axis] = sign
            u = np.zeros(3)
            u[(axis + 1) % 3] = 1.0
            v = np.cross(n, u)
            base = len(pos)
            for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                pos.append(c + n * h + u * h * su + v * h * sv)
                nrm.append(n)
                uv.append(((su + 1) * 0.5, (sv + 1) * 0.5))
            idx.append((base, base + 1, base + 2))
            idx.append((base, base + 2, base + 3))
    return Piece("box", np.array(pos), np.array(nrm), np.array(uv), np.array(idx), material)


def fmt(v):
    return " ".join("%.4f" % float(x) for x in v)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("glb", nargs="?", default=os.path.join(ROOT, "assets", "models", "Mustang.glb"))
    parser.add_argument("--name", default="mustang")
    parser.add_argument("--scale", type=float, default=1.35)
    parser.add_argument("--road", type=float, default=-0.765)
    args = parser.parse_args()
    name = args.name
    pieces, colors = load_pieces(args.glb)

    def get(node):
        parts = pieces.get(node)
        assert parts, "missing node " + node
        return merge(parts)

    front_wheel = get("Frontwheels")
    back_wheel = get("BackWheels")
    lo_f, hi_f = front_wheel.bounds()
    lo_b, hi_b = back_wheel.bounds()
    wheel_y = (lo_f[1] + hi_f[1]) * 0.5
    ground = min(lo_f[1], lo_b[1])
    body_raw = whole(get("Body"))
    lo_body, hi_body = body_raw.bounds()
    mid_z = (lo_body[2] + hi_body[2]) * 0.5
    shift = np.array([0.0, args.road - ground * args.scale, mid_z * args.scale])

    def car(node):
        return to_car(whole(get(node)), args.scale, shift)

    body = doubled(car("Body"))
    hood = doubled(car("Hood"))
    trunk = doubled(car("TrunkLib"))
    front_lights = car("FrontLights")
    back_lights = car("Backlights")
    glass = doubled(merge([car("WindShield"), car("RearWindow")], "glass"))
    left_door, right_door = left_right(to_car(get("DoorLeft"), args.scale, shift))
    left_door_glass, right_door_glass = left_right(to_car(get("DoorLeftWindow"), args.scale, shift))
    left_door, right_door = doubled(left_door), doubled(right_door)
    left_door_glass, right_door_glass = doubled(left_door_glass), doubled(right_door_glass)
    wheel, _ = left_right(to_car(front_wheel, args.scale, shift))

    lo, hi = hood.bounds()
    near_rear = hood.pos[hood.pos[:, 2] > hi[2] - 0.05]
    hood_hinge = np.array([0.0, near_rear[:, 1].max(), hi[2]])
    lo, hi = trunk.bounds()
    near_front = trunk.pos[trunk.pos[:, 2] < lo[2] + 0.05]
    trunk_hinge = np.array([0.0, near_front[:, 1].max(), lo[2]])
    lo, hi = left_door.bounds()
    door_hinge = np.array([lo[0], 0.0, lo[2]])

    wlo, whi = wheel.bounds()
    wheel_center = (wlo + whi) * 0.5
    wheel_radius = (whi[1] - wlo[1]) * 0.5
    wheel_half_width = (whi[0] - wlo[0]) * 0.5
    wheel = offset(wheel, wheel_center)
    rear_wheel, _ = left_right(to_car(back_wheel, args.scale, shift))
    rlo, rhi = rear_wheel.bounds()
    rear_center = (rlo + rhi) * 0.5
    front_center = wheel_center
    track = abs(front_center[0])

    paint = name + "_paint"
    rubber = name + "_rubber"
    metal = colors.get("Metal", [0.8, 0.0, 0.0, 1.0])[:3]
    grey = sum(metal) / 3.0
    write_texture(paint, [(c * 0.85 + grey * 0.15) * 0.12 for c in metal])
    write_texture(rubber, colors.get("Rubber", [0.05, 0.05, 0.05, 1.0]), noise=3, seed=2)

    def mat(piece):
        return {"Metal": paint, "Rubber": rubber, "Glass": "glass", "Light": "car_light"}.get(piece.material, paint)

    write_amsh(os.path.join(MESH_DIR, name + "_body.amsh"),
               [(paint, body), ("car_light", front_lights), ("car_tail", back_lights)])
    write_amsh(os.path.join(MESH_DIR, name + "_hood.amsh"), [(paint, offset(hood, hood_hinge))])
    write_amsh(os.path.join(MESH_DIR, name + "_trunk_lid.amsh"), [(paint, offset(trunk, trunk_hinge))])
    write_amsh(os.path.join(MESH_DIR, name + "_door_l.amsh"), [(paint, offset(left_door, door_hinge))])
    right_hinge = door_hinge * np.array([-1.0, 1.0, 1.0])
    write_amsh(os.path.join(MESH_DIR, name + "_door_r.amsh"), [(paint, offset(right_door, right_hinge))])
    write_amsh(os.path.join(MESH_DIR, name + "_door_glass_l.amsh"), [("glass", offset(left_door_glass, door_hinge))])
    write_amsh(os.path.join(MESH_DIR, name + "_door_glass_r.amsh"),
               [("glass", offset(right_door_glass, right_hinge))])
    write_amsh(os.path.join(MESH_DIR, name + "_glass.amsh"), [("glass", glass)])
    write_amsh(os.path.join(MESH_DIR, name + "_wheel.amsh"), [(mat(wheel), wheel)])

    lit = back_lights.copy()
    lit.pos = lit.pos + lit.nrm * 0.004
    write_amsh(os.path.join(MESH_DIR, name + "_brakelight.amsh"), [("car_tail_lit", lit)])
    blo, bhi = back_lights.bounds()
    tail_x = float(np.abs(back_lights.pos[:, 0]).mean())
    rev = []
    for side in (-1.0, 1.0):
        rev.append(box_piece((side * max(tail_x - 0.12, 0.1), blo[1] + 0.02, bhi[2] + 0.004), (0.05, 0.018, 0.006), "car_light"))
    write_amsh(os.path.join(MESH_DIR, name + "_revlight.amsh"), [("car_light", merge(rev))])

    blo, bhi = body.bounds()
    flo, fhi = front_lights.bounds()
    glo, ghi = car("WindShield").bounds()
    lamp_x = float(np.abs(front_lights.pos[:, 0]).mean())
    layout = {
        "body.extent_min": fmt(blo),
        "body.extent_max": fmt(bhi),
        "body.roof": "%.4f" % bhi[1],
        "wheels.front_center": fmt(front_center),
        "wheels.rear_center": fmt(rear_center),
        "wheels.radius": "%.4f" % wheel_radius,
        "wheels.half_width": "%.4f" % wheel_half_width,
        "wheels.track_x": "%.4f" % track,
        "wheels.road": "%.4f" % args.road,
        "hinges.hood": fmt(hood_hinge),
        "hinges.trunk": fmt(trunk_hinge),
        "hinges.door": fmt(door_hinge),
        "lamps.head": fmt((lamp_x, (flo[1] + fhi[1]) * 0.5, flo[2])),
        "lamps.tail": fmt((0.0, (blo[1] + bhi[1]) * 0.5 if False else float(back_lights.pos[:, 1].mean()), float(back_lights.bounds()[1][2]))),
        "glass.windshield_min": fmt(glo),
        "glass.windshield_max": fmt(ghi),
    }
    path = os.path.join(CAR_DIR, name + "_layout.cfg")
    with open(path, "w", newline="\n") as f:
        f.write("# generated by tools/import_car.py from %s; re-run it after changing the model\n" % os.path.basename(args.glb))
        section = None
        for key, value in layout.items():
            head, tail = key.split(".", 1)
            if head != section:
                f.write("\n[%s]\n" % head)
                section = head
            f.write("%s = %s\n" % (tail, value))
    for key, value in layout.items():
        print("%-24s %s" % (key, value))


if __name__ == "__main__":
    main()
