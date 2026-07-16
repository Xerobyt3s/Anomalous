import math
import os
import random
import struct

MESH_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "meshes")

AMSH_MAGIC = 0x48534D41
AMSH_VERSION = 1


class MeshBuilder:
    def __init__(self):
        self.vertices = []
        self.indices = []
        self.submeshes = []
        self.current_material = None
        self.current_start = 0

    def begin_material(self, name):
        self.end_material()
        self.current_material = name
        self.current_start = len(self.indices)

    def end_material(self):
        if self.current_material is not None:
            count = len(self.indices) - self.current_start
            if count > 0:
                self.submeshes.append((self.current_start, count, self.current_material))
        self.current_material = None

    def vertex(self, pos, normal, uv):
        self.vertices.append((pos, normal, uv))
        return len(self.vertices) - 1

    def triangle(self, a, b, c):
        self.indices.extend((a, b, c))

    def quad(self, p0, p1, p2, p3, normal, uv_scale=1.0):
        du = math.dist(p0, p1) * uv_scale
        dv = math.dist(p0, p3) * uv_scale
        i0 = self.vertex(p0, normal, (0.0, 0.0))
        i1 = self.vertex(p1, normal, (du, 0.0))
        i2 = self.vertex(p2, normal, (du, dv))
        i3 = self.vertex(p3, normal, (0.0, dv))
        self.triangle(i0, i1, i2)
        self.triangle(i0, i2, i3)

    def box(self, center, half, uv_scale=0.25):
        cx, cy, cz = center
        hx, hy, hz = half
        x0, x1 = cx - hx, cx + hx
        y0, y1 = cy - hy, cy + hy
        z0, z1 = cz - hz, cz + hz
        self.quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), (0, 0, 1), uv_scale)
        self.quad((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (0, 0, -1), uv_scale)
        self.quad((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (1, 0, 0), uv_scale)
        self.quad((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0), (-1, 0, 0), uv_scale)
        self.quad((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0), (0, 1, 0), uv_scale)
        self.quad((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), (0, -1, 0), uv_scale)

    def write(self, path):
        self.end_material()
        with open(path, "wb") as f:
            f.write(struct.pack("<5I", AMSH_MAGIC, AMSH_VERSION,
                                len(self.vertices), len(self.indices), len(self.submeshes)))
            for pos, normal, uv in self.vertices:
                f.write(struct.pack("<8f", pos[0], pos[1], pos[2],
                                    normal[0], normal[1], normal[2], uv[0], uv[1]))
            for index in self.indices:
                f.write(struct.pack("<I", index))
            for first, count, material in self.submeshes:
                name = material.encode("ascii")[:31]
                f.write(struct.pack("<II", first, count) + name + b"\x00" * (32 - len(name)))
        print(f"wrote {path}: {len(self.vertices)} verts, {len(self.indices) // 3} tris, "
              f"{len(self.submeshes)} submeshes")


def normalize(v):
    length = math.sqrt(sum(c * c for c in v))
    return tuple(c / length for c in v)


def make_garage():
    b = MeshBuilder()
    wall = 0.25
    width_half = 5.0
    depth_half = 4.0
    height = 3.6

    b.begin_material("concrete")
    b.box((0.0, 0.075, 0.0), (width_half + 0.6, 0.075, depth_half + 0.6))
    b.box((0.0, 0.15 + height * 0.5, -depth_half + wall * 0.5), (width_half, height * 0.5, wall * 0.5))
    b.box((-width_half + wall * 0.5, 0.15 + height * 0.5, 0.0), (wall * 0.5, height * 0.5, depth_half))
    b.box((width_half - wall * 0.5, 0.15 + height * 0.5, 0.0), (wall * 0.5, height * 0.5, depth_half))
    b.box((0.0, 0.15 + height - 0.35, depth_half - wall * 0.5), (width_half, 0.35, wall * 0.5))

    b.begin_material("roof")
    b.box((0.0, 0.15 + height + 0.12, 0.0), (width_half + 0.35, 0.12, depth_half + 0.35))

    b.write(os.path.join(MESH_DIR, "garage.amsh"))


def add_cone(b, base_center, base_radius, height, sides):
    cx, cy, cz = base_center
    tip = b.vertex((cx, cy + height, cz), (0.0, 1.0, 0.0), (0.5, 1.0))
    slope = math.atan2(base_radius, height)
    ring = []
    for i in range(sides):
        angle = i / sides * 2.0 * math.pi
        nx, nz = math.cos(angle), math.sin(angle)
        normal = normalize((nx * math.cos(slope), math.sin(slope), nz * math.cos(slope)))
        ring.append(b.vertex((cx + nx * base_radius, cy, cz + nz * base_radius),
                             normal, (i / sides * 3.0, 0.0)))
    for i in range(sides):
        b.triangle(ring[i], tip, ring[(i + 1) % sides])
    center = b.vertex((cx, cy, cz), (0.0, -1.0, 0.0), (0.5, 0.5))
    for i in range(sides):
        b.triangle(ring[(i + 1) % sides], center, ring[i])


def add_cylinder(b, base_center, radius, height, sides):
    cx, cy, cz = base_center
    bottom = []
    top = []
    for i in range(sides + 1):
        angle = i / sides * 2.0 * math.pi
        nx, nz = math.cos(angle), math.sin(angle)
        u = i / sides * 2.0
        bottom.append(b.vertex((cx + nx * radius, cy, cz + nz * radius), (nx, 0.0, nz), (u, 0.0)))
        top.append(b.vertex((cx + nx * radius, cy + height, cz + nz * radius), (nx, 0.0, nz), (u, height)))
    for i in range(sides):
        b.triangle(bottom[i], top[i], top[i + 1])
        b.triangle(bottom[i], top[i + 1], bottom[i + 1])


def make_tree():
    b = MeshBuilder()
    b.begin_material("bark")
    add_cylinder(b, (0.0, 0.0, 0.0), 0.22, 2.4, 6)
    b.begin_material("leaves")
    add_cone(b, (0.0, 1.6, 0.0), 2.2, 3.0, 8)
    add_cone(b, (0.0, 3.2, 0.0), 1.6, 2.5, 8)
    add_cone(b, (0.0, 4.6, 0.0), 1.0, 2.0, 8)
    b.write(os.path.join(MESH_DIR, "tree_pine.amsh"))


def make_rock():
    b = MeshBuilder()
    b.begin_material("rock")
    rng = random.Random(31)
    base = [
        (1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1),
    ]
    faces = [
        (0, 2, 4), (4, 2, 1), (1, 2, 5), (5, 2, 0),
        (4, 3, 0), (1, 3, 4), (5, 3, 1), (0, 3, 5),
    ]
    points = []
    for vx, vy, vz in base:
        jitter = 0.75 + rng.random() * 0.5
        points.append((vx * jitter, vy * jitter, vz * jitter))
    mids = {}

    def midpoint(i, j):
        key = (min(i, j), max(i, j))
        if key not in mids:
            ax, ay, az = points[i]
            bx, by, bz = points[j]
            mx, my, mz = normalize(((ax + bx) * 0.5, (ay + by) * 0.5, (az + bz) * 0.5))
            jitter = 0.8 + rng.random() * 0.45
            points.append((mx * jitter, my * jitter, mz * jitter))
            mids[key] = len(points) - 1
        return mids[key]

    tris = []
    for a, b_i, c in faces:
        ab = midpoint(a, b_i)
        bc = midpoint(b_i, c)
        ca = midpoint(c, a)
        tris.extend([(a, ab, ca), (ab, b_i, bc), (ca, bc, c), (ab, bc, ca)])

    scale = (1.1, 0.75, 1.0)
    for a, b_i, c in tris:
        pa = tuple(points[a][k] * scale[k] for k in range(3))
        pb = tuple(points[b_i][k] * scale[k] for k in range(3))
        pc = tuple(points[c][k] * scale[k] for k in range(3))
        ux = tuple(pb[k] - pa[k] for k in range(3))
        vx = tuple(pc[k] - pa[k] for k in range(3))
        normal = normalize((ux[1] * vx[2] - ux[2] * vx[1],
                            ux[2] * vx[0] - ux[0] * vx[2],
                            ux[0] * vx[1] - ux[1] * vx[0]))
        ia = b.vertex(pa, normal, (pa[0] * 0.4, pa[2] * 0.4))
        ib = b.vertex(pb, normal, (pb[0] * 0.4, pb[2] * 0.4))
        ic = b.vertex(pc, normal, (pc[0] * 0.4, pc[2] * 0.4))
        b.triangle(ia, ib, ic)
    b.write(os.path.join(MESH_DIR, "rock.amsh"))


def main():
    os.makedirs(MESH_DIR, exist_ok=True)
    make_garage()
    make_tree()
    make_rock()


if __name__ == "__main__":
    main()
