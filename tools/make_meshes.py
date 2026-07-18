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


def area2(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def ear_clip(poly):
    pts = list(poly)
    if sum(area2(pts[0], pts[i], pts[i + 1]) for i in range(1, len(pts) - 1)) < 0:
        pts.reverse()
    idx = list(range(len(pts)))
    tris = []
    guard = 0
    while len(idx) > 3 and guard < 10000:
        guard += 1
        clipped = False
        n = len(idx)
        for k in range(n):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % n]
            a, b, c = pts[i0], pts[i1], pts[i2]
            if area2(a, b, c) <= 1e-9:
                continue
            blocked = False
            for j in idx:
                if j in (i0, i1, i2):
                    continue
                p = pts[j]
                if area2(a, b, p) >= 0 and area2(b, c, p) >= 0 and area2(c, a, p) >= 0:
                    blocked = True
                    break
            if not blocked:
                tris.append((i0, i1, i2))
                del idx[k]
                clipped = True
                break
        if not clipped:
            break
    if len(idx) == 3:
        tris.append(tuple(idx))
    return pts, tris


def make_double_sided(b):
    b.end_material()
    base = len(b.vertices)
    b.vertices = b.vertices + [(p, tuple(-c for c in n), uv) for p, n, uv in b.vertices]
    old = b.indices
    new_indices = []
    new_subs = []
    for first, count, mat in b.submeshes:
        start = len(new_indices)
        new_indices.extend(old[first:first + count])
        for i in range(first, first + count, 3):
            new_indices.extend((old[i] + base, old[i + 2] + base, old[i + 1] + base))
        new_subs.append((start, count * 2, mat))
    b.indices = new_indices
    b.submeshes = new_subs


def side_face(b, poly_zy, x, normal_sign, uv_scale=0.25):
    pts, tris = ear_clip(poly_zy)
    ids = [b.vertex((x, y, z), (normal_sign, 0.0, 0.0), (z * uv_scale, y * uv_scale))
           for z, y in pts]
    for i0, i1, i2 in tris:
        if normal_sign > 0:
            b.triangle(ids[i2], ids[i1], ids[i0])
        else:
            b.triangle(ids[i0], ids[i1], ids[i2])


def quad_auto(b, p0, p1, p2, p3, uv_scale=0.25):
    u = tuple(p1[k] - p0[k] for k in range(3))
    v = tuple(p3[k] - p0[k] for k in range(3))
    n = normalize((u[1] * v[2] - u[2] * v[1],
                   u[2] * v[0] - u[0] * v[2],
                   u[0] * v[1] - u[1] * v[0]))
    b.quad(p0, p1, p2, p3, n, uv_scale)


def basis_from_axis(axis):
    axis = normalize(axis)
    up = (0.0, 1.0, 0.0) if abs(axis[1]) < 0.9 else (0.0, 0.0, 1.0)
    s = tuple(axis[(k + 1) % 3] * up[(k + 2) % 3] - axis[(k + 2) % 3] * up[(k + 1) % 3]
              for k in range(3))
    s = normalize(s)
    t = tuple(s[(k + 1) % 3] * axis[(k + 2) % 3] - s[(k + 2) % 3] * axis[(k + 1) % 3]
              for k in range(3))
    return axis, s, t


def beam(b, start, end, half_s, half_t, uv_scale=0.25):
    axis = tuple(end[k] - start[k] for k in range(3))
    a, s, t = basis_from_axis(axis)

    def corner(base, ds, dt):
        return tuple(base[k] + s[k] * ds + t[k] * dt for k in range(3))

    for ds, dt, flip in ((half_s, 0.0, False), (-half_s, 0.0, True)):
        n = tuple(s[k] * (1 if ds > 0 else -1) for k in range(3))
        p0 = corner(start, ds, -half_t)
        p1 = corner(end, ds, -half_t)
        p2 = corner(end, ds, half_t)
        p3 = corner(start, ds, half_t)
        if flip:
            b.quad(p3, p2, p1, p0, n, uv_scale)
        else:
            b.quad(p0, p1, p2, p3, n, uv_scale)
    for dt, flip in ((half_t, False), (-half_t, True)):
        n = tuple(t[k] * (1 if dt > 0 else -1) for k in range(3))
        p0 = corner(start, -half_s, dt)
        p1 = corner(end, -half_s, dt)
        p2 = corner(end, half_s, dt)
        p3 = corner(start, half_s, dt)
        if flip:
            b.quad(p0, p1, p2, p3, n, uv_scale)
        else:
            b.quad(p3, p2, p1, p0, n, uv_scale)


def tube_x(b, x0, x1, radius, sides, uv_scale=1.0):
    ring0 = []
    ring1 = []
    for i in range(sides + 1):
        angle = i / sides * 2.0 * math.pi
        ny, nz = math.cos(angle), math.sin(angle)
        u = i / sides * 2.0
        ring0.append(b.vertex((x0, ny * radius, nz * radius), (0.0, ny, nz), (u, 0.0)))
        ring1.append(b.vertex((x1, ny * radius, nz * radius), (0.0, ny, nz), (u, 1.0)))
    for i in range(sides):
        b.triangle(ring0[i], ring0[i + 1], ring1[i + 1])
        b.triangle(ring0[i], ring1[i + 1], ring1[i])


def ring_x(b, x, r_inner, r_outer, sides, normal_sign):
    n = (normal_sign, 0.0, 0.0)
    for i in range(sides):
        a0 = i / sides * 2.0 * math.pi
        a1 = (i + 1) / sides * 2.0 * math.pi
        p0 = (x, math.cos(a0) * r_inner, math.sin(a0) * r_inner)
        p1 = (x, math.cos(a1) * r_inner, math.sin(a1) * r_inner)
        p2 = (x, math.cos(a1) * r_outer, math.sin(a1) * r_outer)
        p3 = (x, math.cos(a0) * r_outer, math.sin(a0) * r_outer)
        i0 = b.vertex(p0, n, (0.0, 0.0))
        i1 = b.vertex(p1, n, (1.0, 0.0))
        i2 = b.vertex(p2, n, (1.0, 1.0))
        i3 = b.vertex(p3, n, (0.0, 1.0))
        if normal_sign > 0:
            b.triangle(i0, i1, i2)
            b.triangle(i0, i2, i3)
        else:
            b.triangle(i2, i1, i0)
            b.triangle(i3, i2, i0)


def disc_x(b, x, radius, sides, normal_sign):
    n = (normal_sign, 0.0, 0.0)
    center = b.vertex((x, 0.0, 0.0), n, (0.5, 0.5))
    ring = [b.vertex((x, math.cos(i / sides * 2.0 * math.pi) * radius,
                      math.sin(i / sides * 2.0 * math.pi) * radius), n,
                     (0.5 + math.cos(i / sides * 2.0 * math.pi) * 0.5,
                      0.5 + math.sin(i / sides * 2.0 * math.pi) * 0.5))
            for i in range(sides)]
    for i in range(sides):
        j = (i + 1) % sides
        if normal_sign > 0:
            b.triangle(center, ring[i], ring[j])
        else:
            b.triangle(center, ring[j], ring[i])


EXCEL_BELT = [
    (-2.19, -0.01), (-1.70, 0.06), (-1.24, 0.105), (-0.60, 0.155),
    (0.40, 0.175), (1.24, 0.20), (1.35, 0.206), (1.70, 0.225), (2.19, 0.21),
]
EXCEL_CHAMFER_RISE = 0.05
EXCEL_SIDE_X = 0.80
EXCEL_DECK_X = 0.66
EXCEL_FLOOR_Y = -0.42
EXCEL_ARCH_CENTER_Y = -0.255
EXCEL_ARCH_R = 0.34
EXCEL_ARCH_FLATTEN = 0.85
EXCEL_HOOD_Z0 = -1.90
EXCEL_HOOD_Z1 = -0.62
EXCEL_HOOD_X = 0.55
EXCEL_Y_SHIFT = -0.056
EXCEL_DOOR_Z0 = -0.55
EXCEL_DOOR_Z1 = 0.75
EXCEL_DOOR_SILL_Y = -0.30
EXCEL_TRUNK_Z0 = 1.42
EXCEL_TRUNK_Z1 = 2.08
EXCEL_TRUNK_X = 0.55


def excel_belt_y(z):
    for k in range(len(EXCEL_BELT) - 1):
        z0, y0 = EXCEL_BELT[k]
        z1, y1 = EXCEL_BELT[k + 1]
        if z0 <= z <= z1:
            t = (z - z0) / (z1 - z0)
            return y0 + (y1 - y0) * t
    return EXCEL_BELT[-1][1]


def excel_arch_points(center_z):
    dy = (EXCEL_FLOOR_Y - EXCEL_ARCH_CENTER_Y) / EXCEL_ARCH_FLATTEN
    dx = math.sqrt(max(EXCEL_ARCH_R * EXCEL_ARCH_R - dy * dy, 1e-6))
    steps = 7
    a_in = math.atan2(dy, dx)
    a_out = math.pi - a_in
    pts = []
    for i in range(steps + 1):
        a = a_in + (a_out - a_in) * i / steps
        pts.append((center_z + math.cos(a) * EXCEL_ARCH_R,
                    EXCEL_ARCH_CENTER_Y + math.sin(a) * EXCEL_ARCH_R * EXCEL_ARCH_FLATTEN))
    return pts


def excel_side_silhouette():
    pts = []
    notched = False
    for z, y in EXCEL_BELT:
        if z <= EXCEL_DOOR_Z0:
            pts.append((z, y))
            continue
        if not notched:
            notched = True
            pts.append((EXCEL_DOOR_Z0, excel_belt_y(EXCEL_DOOR_Z0)))
            pts.append((EXCEL_DOOR_Z0, EXCEL_DOOR_SILL_Y))
            pts.append((EXCEL_DOOR_Z1, EXCEL_DOOR_SILL_Y))
            pts.append((EXCEL_DOOR_Z1, excel_belt_y(EXCEL_DOOR_Z1)))
        if z > EXCEL_DOOR_Z1:
            pts.append((z, y))
    pts.append((2.19, -0.28))
    pts.append((1.85, EXCEL_FLOOR_Y))
    pts.extend(excel_arch_points(1.24))
    pts.extend(excel_arch_points(-1.24))
    pts.append((-1.95, EXCEL_FLOOR_Y))
    pts.append((-2.19, -0.28))
    return pts


def make_excel_body():
    b = MeshBuilder()
    side = EXCEL_SIDE_X
    deck = EXCEL_DECK_X
    rise = EXCEL_CHAMFER_RISE
    silhouette = excel_side_silhouette()

    b.begin_material("excel_paint")
    side_face(b, silhouette, side, 1.0)
    side_face(b, silhouette, -side, -1.0)

    chamfer_z = [-2.19, -1.90, -1.70, -1.24, -0.62, -0.60, EXCEL_DOOR_Z0, 0.40, EXCEL_DOOR_Z1,
                 1.24, 1.35, EXCEL_TRUNK_Z0, 1.60, 1.70, EXCEL_TRUNK_Z1, 2.19]
    for k in range(len(chamfer_z) - 1):
        z0, z1 = chamfer_z[k], chamfer_z[k + 1]
        zm = (z0 + z1) * 0.5
        if EXCEL_DOOR_Z0 <= zm <= EXCEL_DOOR_Z1:
            continue
        y0 = excel_belt_y(z0)
        y1 = excel_belt_y(z1)
        for sx in (1.0, -1.0):
            p0 = (sx * side, y0, z0)
            p1 = (sx * side, y1, z1)
            p2 = (sx * deck, y1 + rise, z1)
            p3 = (sx * deck, y0 + rise, z0)
            if sx > 0:
                quad_auto(b, p0, p1, p2, p3)
            else:
                quad_auto(b, p3, p2, p1, p0)

    surface_z = [-2.19, -1.90, -1.70, -1.24, -0.62, -0.60, 0.40, 1.24, 1.35, EXCEL_TRUNK_Z0,
                 1.60, 1.70, EXCEL_TRUNK_Z1, 2.19]
    for k in range(len(surface_z) - 1):
        z0, z1 = surface_z[k], surface_z[k + 1]
        zm = (z0 + z1) * 0.5
        y0 = excel_belt_y(z0) + rise
        y1 = excel_belt_y(z1) + rise
        if -0.60 <= zm <= 1.35:
            continue
        hood = EXCEL_HOOD_Z0 <= zm <= EXCEL_HOOD_Z1
        boot = EXCEL_TRUNK_Z0 <= zm <= EXCEL_TRUNK_Z1
        if hood or boot:
            inner_x = EXCEL_HOOD_X if hood else EXCEL_TRUNK_X
            quad_auto(b, (inner_x, y0, z0), (inner_x, y1, z1),
                      (deck, y1, z1), (deck, y0, z0))
            quad_auto(b, (-deck, y0, z0), (-deck, y1, z1),
                      (-inner_x, y1, z1), (-inner_x, y0, z0))
            continue
        quad_auto(b, (-deck, y0, z0), (-deck, y1, z1), (deck, y1, z1), (deck, y0, z0))

    nose_y = EXCEL_BELT[0][1]
    quad_auto(b, (side, nose_y, -2.19), (-side, nose_y, -2.19),
              (-deck, nose_y + rise, -2.19), (deck, nose_y + rise, -2.19))
    quad_auto(b, (-side, -0.28, -2.19), (side, -0.28, -2.19),
              (side, nose_y, -2.19), (-side, nose_y, -2.19))
    tail_y = EXCEL_BELT[-1][1]
    quad_auto(b, (-side, tail_y, 2.19), (side, tail_y, 2.19),
              (deck, tail_y + rise, 2.19), (-deck, tail_y + rise, 2.19))
    quad_auto(b, (side, -0.28, 2.19), (-side, -0.28, 2.19),
              (-side, tail_y, 2.19), (side, tail_y, 2.19))

    scuttle_y = excel_belt_y(-0.60) + rise
    roof_f = (0.56, 0.585, -0.05)
    roof_r = (0.54, 0.57, 0.55)
    deck_r_y = excel_belt_y(1.35) + rise
    for sx in (1.0, -1.0):
        beam(b, (sx * 0.64, scuttle_y, -0.60), (sx * roof_f[0], roof_f[1], roof_f[2]),
             0.035, 0.045)
        beam(b, (sx * 0.62, deck_r_y, 1.35), (sx * roof_r[0], roof_r[1], roof_r[2]),
             0.05, 0.045)
    b.box((0.0, 0.596, 0.25), (0.56, 0.022, 0.32))
    beam(b, (-0.56, 0.575, -0.055), (0.56, 0.575, -0.055), 0.03, 0.035)
    beam(b, (-0.55, 0.56, 0.56), (0.55, 0.56, 0.56), 0.03, 0.035)

    b.box((0.84, 0.28, -0.50), (0.06, 0.042, 0.045))
    b.box((-0.84, 0.28, -0.50), (0.06, 0.042, 0.045))

    b.begin_material("car_trim")
    b.box((0.0, -0.20, -2.17), (0.81, 0.055, 0.075))
    b.box((0.0, -0.18, 2.17), (0.81, 0.055, 0.075))
    b.box((0.0, -0.115, -2.195), (0.36, 0.038, 0.012))
    b.box((0.0, -0.46, 0.05), (0.62, 0.05, 1.55))
    b.box((0.30, -0.34, 2.12), (0.035, 0.035, 0.10))
    quad_auto(b, (-0.72, -0.40, -0.60), (0.72, -0.40, -0.60),
              (0.72, scuttle_y, -0.60), (-0.72, scuttle_y, -0.60))
    quad_auto(b, (0.72, -0.40, 1.35), (-0.72, -0.40, 1.35),
              (-0.72, deck_r_y, 1.35), (0.72, deck_r_y, 1.35))
    bay_front_y = excel_belt_y(EXCEL_HOOD_Z0) + rise
    b.box((0.0, -0.31, -1.26), (0.56, 0.025, 0.64))
    b.box((-0.44, -0.289, -0.82), (0.12, 0.012, 0.15))
    b.box((0.33, -0.184, -1.80), (0.025, 0.10, 0.035))
    b.box((-0.33, -0.184, -1.80), (0.025, 0.10, 0.035))
    b.box((0.22, -0.234, -1.28), (0.04, 0.02, 0.27))
    b.box((-0.22, -0.234, -1.28), (0.04, 0.02, 0.27))
    quad_auto(b, (-0.56, -0.30, EXCEL_HOOD_Z0), (0.56, -0.30, EXCEL_HOOD_Z0),
              (0.56, bay_front_y - 0.02, EXCEL_HOOD_Z0), (-0.56, bay_front_y - 0.02, EXCEL_HOOD_Z0))
    quad_auto(b, (0.56, -0.30, EXCEL_HOOD_Z0), (0.56, -0.30, EXCEL_HOOD_Z1),
              (0.56, excel_belt_y(EXCEL_HOOD_Z1) + rise - 0.01, EXCEL_HOOD_Z1),
              (0.56, excel_belt_y(EXCEL_HOOD_Z0) + rise - 0.01, EXCEL_HOOD_Z0))
    quad_auto(b, (-0.56, -0.30, EXCEL_HOOD_Z1), (-0.56, -0.30, EXCEL_HOOD_Z0),
              (-0.56, excel_belt_y(EXCEL_HOOD_Z0) + rise - 0.01, EXCEL_HOOD_Z0),
              (-0.56, excel_belt_y(EXCEL_HOOD_Z1) + rise - 0.01, EXCEL_HOOD_Z1))

    b.begin_material("car_signal")
    b.box((0.64, -0.135, -2.195), (0.10, 0.030, 0.012))
    b.box((-0.64, -0.135, -2.195), (0.10, 0.030, 0.012))

    b.begin_material("car_tail")
    b.box((0.47, 0.02, 2.195), (0.28, 0.055, 0.012))
    b.box((-0.47, 0.02, 2.195), (0.28, 0.055, 0.012))

    b.begin_material("car_carpet")
    b.box((0.0, -0.40, 0.35), (0.70, 0.018, 0.95))
    b.box((0.0, -0.265, 0.30), (0.12, 0.075, 0.90))
    b.box((0.0, -0.17, 1.73), (0.52, 0.02, 0.32))
    b.box((0.0, 0.02, 1.42), (0.52, 0.17, 0.02))
    b.box((0.0, 0.02, 2.06), (0.52, 0.17, 0.02))
    b.box((0.53, 0.02, 1.74), (0.02, 0.17, 0.34))
    b.box((-0.53, 0.02, 1.74), (0.02, 0.17, 0.34))

    b.begin_material("car_dash")
    b.box((0.0, 0.01, -0.44), (0.68, 0.09, 0.15))
    b.box((-0.37, 0.115, -0.38), (0.17, 0.035, 0.07))
    b.begin_material("dial")
    disc_z(b, (-0.44, 0.12, -0.309), 0.048, 14)
    disc_z(b, (-0.30, 0.12, -0.309), 0.048, 14)
    disc_z(b, (-0.405, 0.062, -0.309), 0.022, 10)
    disc_z(b, (-0.335, 0.062, -0.309), 0.022, 10)
    b.begin_material("car_dash")
    wheel_center = (-0.37, 0.13, -0.20)
    wheel_axis = normalize((0.0, 0.42, 1.0))
    _, ws, wt = basis_from_axis(wheel_axis)

    def rim_point(a):
        return tuple(wheel_center[k] + (ws[k] * math.cos(a) + wt[k] * math.sin(a)) * 0.185
                     for k in range(3))

    for i in range(12):
        beam(b, rim_point(i / 12 * 2.0 * math.pi),
             rim_point((i + 1) / 12 * 2.0 * math.pi), 0.014, 0.014)
    beam(b, wheel_center,
         tuple(wheel_center[k] - wheel_axis[k] * 0.16 for k in range(3)), 0.02, 0.02)
    beam(b, (0.0, -0.18, 0.10), (0.0, -0.02, 0.06), 0.012, 0.012)
    b.box((0.0, -0.005, 0.055), (0.026, 0.02, 0.026))

    b.begin_material("car_tan")
    for sx in (1.0, -1.0):
        b.box((sx * 0.37, -0.20, 0.25), (0.17, 0.05, 0.23))
        beam(b, (sx * 0.37, -0.155, 0.44), (sx * 0.37, 0.25, 0.56), 0.17, 0.05)
        b.box((sx * 0.37, 0.31, 0.60), (0.09, 0.05, 0.045))
        quad_auto(b, (sx * 0.655, -0.40, 0.75 if sx > 0 else 0.92),
                  (sx * 0.655, -0.40, 0.92 if sx > 0 else 0.75),
                  (sx * 0.655, 0.19, 0.92 if sx > 0 else 0.75),
                  (sx * 0.655, 0.19, 0.75 if sx > 0 else 0.92))
    b.box((0.0, -0.21, 1.02), (0.56, 0.045, 0.18))
    b.box((0.0, 0.0, 1.24), (0.56, 0.17, 0.05))
    b.box((0.0, 0.245, 1.36), (0.58, 0.015, 0.06))

    make_double_sided(b)
    b.vertices = [((p[0], p[1] + EXCEL_Y_SHIFT, p[2]), n, uv) for p, n, uv in b.vertices]
    b.write(os.path.join(MESH_DIR, "excel_body.amsh"))


def make_excel_hood():
    b = MeshBuilder()
    rise = EXCEL_CHAMFER_RISE
    hinge = (0.0, excel_belt_y(EXCEL_HOOD_Z1) + rise, EXCEL_HOOD_Z1)
    w = EXCEL_HOOD_X

    b.begin_material("excel_paint")
    zs = [EXCEL_HOOD_Z0, -1.70, -1.24, EXCEL_HOOD_Z1]
    for k in range(len(zs) - 1):
        z0, z1 = zs[k], zs[k + 1]
        y0 = excel_belt_y(z0) + rise
        y1 = excel_belt_y(z1) + rise
        quad_auto(b, (-w, y0, z0), (-w, y1, z1), (w, y1, z1), (w, y0, z0))
        quad_auto(b, (-w, y0 - 0.02, z0), (w, y0 - 0.02, z0),
                  (w, y1 - 0.02, z1), (-w, y1 - 0.02, z1))
    front_y = excel_belt_y(EXCEL_HOOD_Z0) + rise
    quad_auto(b, (-w, front_y - 0.06, EXCEL_HOOD_Z0), (w, front_y - 0.06, EXCEL_HOOD_Z0),
              (w, front_y, EXCEL_HOOD_Z0), (-w, front_y, EXCEL_HOOD_Z0))

    make_double_sided(b)
    b.vertices = [((p[0] - hinge[0], p[1] - hinge[1], p[2] - hinge[2]), n, uv)
                  for p, n, uv in b.vertices]
    b.write(os.path.join(MESH_DIR, "excel_hood.amsh"))


def make_excel_door(side):
    b = MeshBuilder()
    rise = EXCEL_CHAMFER_RISE
    z0, z1 = EXCEL_DOOR_Z0, EXCEL_DOOR_Z1
    panel = [(z0, excel_belt_y(z0)), (0.40, excel_belt_y(0.40)), (z1, excel_belt_y(z1)),
             (z1, EXCEL_DOOR_SILL_Y), (z0, EXCEL_DOOR_SILL_Y)]

    b.begin_material("excel_paint")
    side_face(b, panel, side * 0.80, side)
    for seg0, seg1 in ((z0, 0.40), (0.40, z1)):
        p0 = (side * 0.80, excel_belt_y(seg0), seg0)
        p1 = (side * 0.80, excel_belt_y(seg1), seg1)
        p2 = (side * 0.66, excel_belt_y(seg1) + rise, seg1)
        p3 = (side * 0.66, excel_belt_y(seg0) + rise, seg0)
        if side > 0:
            quad_auto(b, p0, p1, p2, p3)
        else:
            quad_auto(b, p3, p2, p1, p0)
    beam(b, (side * 0.62, excel_belt_y(0.42) + rise, 0.42),
         (side * 0.55, 0.575, 0.30), 0.035, 0.04)

    b.begin_material("car_tan")
    side_face(b, panel, side * 0.78, -side)

    make_double_sided(b)
    b.vertices = [((p[0] - side * 0.80, p[1] + EXCEL_Y_SHIFT, p[2] - z0), n, uv)
                  for p, n, uv in b.vertices]
    name = "excel_door_l" if side < 0 else "excel_door_r"
    b.write(os.path.join(MESH_DIR, name + ".amsh"))


def make_excel_trunk_lid():
    b = MeshBuilder()
    rise = EXCEL_CHAMFER_RISE
    w = EXCEL_TRUNK_X
    hinge_y = excel_belt_y(EXCEL_TRUNK_Z0) + rise

    b.begin_material("excel_paint")
    zs = [EXCEL_TRUNK_Z0, 1.60, 1.70, EXCEL_TRUNK_Z1]
    for k in range(len(zs) - 1):
        za, zb = zs[k], zs[k + 1]
        y0 = excel_belt_y(za) + rise
        y1 = excel_belt_y(zb) + rise
        quad_auto(b, (-w, y0, za), (-w, y1, zb), (w, y1, zb), (w, y0, za))
    rear_y = excel_belt_y(EXCEL_TRUNK_Z1) + rise
    quad_auto(b, (-w, rear_y - 0.05, EXCEL_TRUNK_Z1), (w, rear_y - 0.05, EXCEL_TRUNK_Z1),
              (w, rear_y, EXCEL_TRUNK_Z1), (-w, rear_y, EXCEL_TRUNK_Z1))

    make_double_sided(b)
    b.vertices = [((p[0], p[1] - hinge_y, p[2] - EXCEL_TRUNK_Z0), n, uv)
                  for p, n, uv in b.vertices]
    b.write(os.path.join(MESH_DIR, "excel_trunk_lid.amsh"))


def disc_z(b, center, radius, sides):
    cx, cy, cz = center
    n = (0.0, 0.0, 1.0)
    mid = b.vertex(center, n, (0.5, 0.5))
    ring = [b.vertex((cx + math.cos(i / sides * 2.0 * math.pi) * radius,
                      cy + math.sin(i / sides * 2.0 * math.pi) * radius, cz), n,
                     (0.5, 0.5)) for i in range(sides)]
    for i in range(sides):
        b.triangle(mid, ring[i], ring[(i + 1) % sides])


def make_wiper():
    b = MeshBuilder()
    b.begin_material("car_trim")
    b.box((0.20, 0.0, 0.0), (0.20, 0.011, 0.011))
    b.box((0.315, 0.042, 0.0), (0.155, 0.009, 0.014))
    b.box((0.255, 0.021, 0.0), (0.010, 0.024, 0.010))
    make_double_sided(b)
    b.write(os.path.join(MESH_DIR, "part_wiper.amsh"))


def make_excel_glass():
    b = MeshBuilder()
    b.begin_material("glass")
    quad_auto(b, (0.645, 0.190, -0.645), (-0.645, 0.190, -0.645),
              (-0.585, 0.600, -0.085), (0.585, 0.600, -0.085))
    quad_auto(b, (-0.635, 0.245, 1.39), (0.635, 0.245, 1.39),
              (0.565, 0.585, 0.585), (-0.565, 0.585, 0.585))
    for sx in (1.0, -1.0):
        quad_auto(b, (sx * 0.625, 0.228, 0.46), (sx * 0.607, 0.259, 1.27),
                  (sx * 0.542, 0.552, 0.56), (sx * 0.552, 0.560, 0.33))
    make_double_sided(b)
    b.vertices = [((p[0], p[1] + EXCEL_Y_SHIFT, p[2]), n, uv) for p, n, uv in b.vertices]
    b.write(os.path.join(MESH_DIR, "excel_glass.amsh"))


def make_excel_door_glass(side):
    b = MeshBuilder()
    b.begin_material("glass")
    quad_auto(b, (side * 0.635, 0.208, -0.50), (side * 0.625, 0.227, 0.40),
              (side * 0.553, 0.562, 0.28), (side * 0.563, 0.562, -0.095))
    make_double_sided(b)
    b.vertices = [((p[0] - side * 0.80, p[1] + EXCEL_Y_SHIFT, p[2] + 0.55), n, uv)
                  for p, n, uv in b.vertices]
    name = "excel_door_glass_l" if side < 0 else "excel_door_glass_r"
    b.write(os.path.join(MESH_DIR, name + ".amsh"))


def make_excel_extras():
    b = MeshBuilder()
    b.begin_material("excel_paint")
    b.box((0.0, 0.015, -0.20), (0.14, 0.018, 0.20))
    b.begin_material("car_light")
    b.box((0.0, 0.0, -0.33), (0.11, 0.012, 0.05))
    make_double_sided(b)
    b.vertices = [(tuple(c * 0.72 for c in p), n, uv) for p, n, uv in b.vertices]
    b.write(os.path.join(MESH_DIR, "excel_popup.amsh"))

    b = MeshBuilder()
    b.begin_material("excel_paint")
    b.box((0.0, -0.045, 0.0), (0.006, 0.048, 0.048))
    make_double_sided(b)
    b.write(os.path.join(MESH_DIR, "excel_fuelcap.amsh"))

    b = MeshBuilder()
    b.begin_material("car_tail")
    beam(b, (0.0, 0.0, 0.0), (0.0, 0.046, 0.0), 0.004, 0.003)
    b.begin_material("car_trim")
    b.box((0.0, 0.0, 0.001), (0.007, 0.007, 0.004))
    make_double_sided(b)
    b.write(os.path.join(MESH_DIR, "excel_needle.amsh"))

    b = MeshBuilder()
    b.begin_material("car_tail_lit")
    b.box((0.47, 0.02, 2.203), (0.26, 0.045, 0.008))
    b.box((-0.47, 0.02, 2.203), (0.26, 0.045, 0.008))
    b.vertices = [((p[0], p[1] + EXCEL_Y_SHIFT, p[2]), n, uv) for p, n, uv in b.vertices]
    b.write(os.path.join(MESH_DIR, "excel_brakelight.amsh"))

    b = MeshBuilder()
    b.begin_material("car_light")
    b.box((0.18, -0.06, 2.203), (0.09, 0.03, 0.008))
    b.box((-0.18, -0.06, 2.203), (0.09, 0.03, 0.008))
    b.vertices = [((p[0], p[1] + EXCEL_Y_SHIFT, p[2]), n, uv) for p, n, uv in b.vertices]
    b.write(os.path.join(MESH_DIR, "excel_revlight.amsh"))

    b = MeshBuilder()
    b.begin_material("car_tail")
    b.box((0.0, 0.0, 0.0), (0.5, 0.5, 0.5))
    b.write(os.path.join(MESH_DIR, "warn_red.amsh"))

    b = MeshBuilder()
    b.begin_material("car_signal")
    b.box((0.0, 0.0, 0.0), (0.5, 0.5, 0.5))
    b.write(os.path.join(MESH_DIR, "warn_amber.amsh"))

    b = MeshBuilder()
    b.begin_material("smoke")
    b.box((0.0, 0.0, 0.0), (0.5, 0.5, 0.5))
    b.write(os.path.join(MESH_DIR, "puff.amsh"))

    b = MeshBuilder()
    b.begin_material("alloy")
    beam(b, (0.0, 0.0, 0.02), (0.0, 0.0, -0.055), 0.005, 0.002)
    b.begin_material("car_trim")
    b.box((0.0, 0.0, 0.04), (0.016, 0.004, 0.022))
    make_double_sided(b)
    b.write(os.path.join(MESH_DIR, "part_key.amsh"))

    b = MeshBuilder()
    b.begin_material("beige")
    b.box((0.0, 0.01, -0.03), (0.17, 0.135, 0.145))
    b.box((0.0, -0.135, 0.0), (0.15, 0.012, 0.17))
    b.box((0.0, -0.095, 0.135), (0.14, 0.028, 0.062))
    b.begin_material("car_trim")
    b.box((0.0, 0.035, 0.118), (0.135, 0.095, 0.006))
    b.box((0.0, -0.10, 0.14), (0.12, 0.008, 0.045))
    b.box((0.0, 0.01, -0.178), (0.14, 0.10, 0.004))
    b.begin_material("car_signal")
    b.box((0.052, -0.015, -0.187), (0.016, 0.016, 0.011))
    b.begin_material("alloy")
    b.box((-0.052, -0.015, -0.187), (0.016, 0.016, 0.011))
    b.begin_material("car_dash")
    b.box((0.0, -0.088, 0.196), (0.052, 0.007, 0.004))
    b.begin_material("alloy")
    b.box((0.072, -0.088, 0.196), (0.008, 0.005, 0.004))
    b.vertices = [(tuple(c * 1.35 for c in p), n, uv) for p, n, uv in b.vertices]
    b.write(os.path.join(MESH_DIR, "part_computer.amsh"))

    b = MeshBuilder()
    b.begin_material("crt")
    b.box((0.0, 0.035, 0.121), (0.115, 0.078, 0.006))
    b.vertices = [(tuple(c * 1.35 for c in p), n, uv) for p, n, uv in b.vertices]
    b.write(os.path.join(MESH_DIR, "computer_glow.amsh"))


def make_part_meshes():
    b = MeshBuilder()
    b.begin_material("car_dash")
    b.box((0.0, 0.0, 0.02), (0.24, 0.10, 0.24))
    b.box((0.0, 0.115, -0.04), (0.16, 0.035, 0.16))
    b.begin_material("alloy")
    b.box((0.0, 0.10, 0.14), (0.10, 0.03, 0.07))
    b.box((-0.20, -0.02, -0.10), (0.05, 0.05, 0.05))
    b.write(os.path.join(MESH_DIR, "part_engine.amsh"))

    b = MeshBuilder()
    b.begin_material("car_trim")
    b.box((0.0, -0.01, 0.0), (0.095, 0.065, 0.125))
    b.begin_material("alloy")
    b.box((-0.045, 0.065, -0.07), (0.016, 0.016, 0.016))
    b.box((0.045, 0.065, -0.07), (0.016, 0.016, 0.016))
    b.begin_material("car_signal")
    b.box((0.0, 0.045, 0.04), (0.08, 0.012, 0.06))
    b.write(os.path.join(MESH_DIR, "part_battery.amsh"))

    b = MeshBuilder()
    b.begin_material("alloy")
    add_cylinder(b, (0.0, -0.065, 0.0), 0.065, 0.13, 10)
    b.begin_material("car_trim")
    add_cylinder(b, (0.0, -0.09, 0.0), 0.045, 0.025, 10)
    b.write(os.path.join(MESH_DIR, "part_alternator.amsh"))

    b = MeshBuilder()
    b.begin_material("car_trim")
    b.box((0.0, 0.0, 0.0), (0.29, 0.11, 0.022))
    b.begin_material("alloy")
    b.box((0.0, 0.115, 0.0), (0.29, 0.018, 0.03))
    b.box((0.20, 0.13, 0.0), (0.025, 0.02, 0.025))
    b.write(os.path.join(MESH_DIR, "part_radiator.amsh"))

    b = MeshBuilder()
    b.begin_material("car_tail")
    b.box((0.0, 0.0, 0.0), (0.16, 0.15, 0.065))
    b.begin_material("car_trim")
    beam(b, (-0.08, 0.16, 0.0), (0.08, 0.16, 0.0), 0.018, 0.018)
    b.box((0.11, 0.16, 0.0), (0.022, 0.03, 0.022))
    b.write(os.path.join(MESH_DIR, "part_jerrycan.amsh"))

    b = MeshBuilder()
    b.begin_material("car_trim")
    add_cylinder(b, (0.0, -0.11, 0.0), 0.055, 0.20, 10)
    b.begin_material("alloy")
    add_cylinder(b, (0.0, 0.09, 0.0), 0.02, 0.03, 8)
    b.write(os.path.join(MESH_DIR, "part_oilcan.amsh"))

    b = MeshBuilder()
    b.begin_material("car_dash")
    beam(b, (0.0, 0.0, 0.0), (0.0, 0.035, -0.24), 0.016, 0.016)
    b.box((0.0, 0.04, -0.245), (0.02, 0.022, 0.03))
    b.write(os.path.join(MESH_DIR, "excel_lever.amsh"))


def make_antenna_meshes():
    b = MeshBuilder()
    b.begin_material("car_trim")
    b.box((0.0, 0.015, 0.0), (0.045, 0.015, 0.045))
    b.begin_material("alloy")
    beam(b, (0.0, 0.03, 0.0), (0.0, 0.60, 0.10), 0.008, 0.008)
    b.box((0.0, 0.615, 0.103), (0.014, 0.014, 0.014))
    b.write(os.path.join(MESH_DIR, "antenna_whip.amsh"))

    b = MeshBuilder()
    b.begin_material("car_trim")
    b.box((0.0, 0.02, 0.0), (0.06, 0.02, 0.06))
    add_cylinder(b, (0.0, 0.04, 0.0), 0.028, 0.10, 8)
    b.begin_material("alloy")
    beam(b, (0.0, 0.14, 0.0), (0.0, 0.70, 0.06), 0.010, 0.010)
    beam(b, (-0.10, 0.42, 0.02), (0.10, 0.42, 0.02), 0.006, 0.006)
    beam(b, (-0.08, 0.54, 0.035), (0.08, 0.54, 0.035), 0.006, 0.006)
    b.begin_material("car_signal")
    b.box((0.0, 0.72, 0.062), (0.018, 0.018, 0.018))
    b.write(os.path.join(MESH_DIR, "antenna_std.amsh"))

    b = MeshBuilder()
    b.begin_material("car_trim")
    b.box((0.0, 0.03, 0.0), (0.16, 0.03, 0.16))
    b.box((0.0, 0.13, 0.0), (0.12, 0.07, 0.10))
    b.begin_material("alloy")
    beam(b, (-0.10, 0.20, -0.08), (-0.13, 0.82, -0.14), 0.008, 0.008)
    beam(b, (0.10, 0.20, -0.08), (0.13, 0.78, -0.02), 0.008, 0.008)
    beam(b, (0.0, 0.20, 0.08), (0.0, 0.95, 0.13), 0.011, 0.011)
    beam(b, (-0.06, 0.60, 0.10), (0.06, 0.60, 0.10), 0.006, 0.006)
    beam(b, (-0.09, 0.75, 0.115), (0.09, 0.75, 0.115), 0.006, 0.006)
    b.begin_material("car_signal")
    b.box((0.0, 0.13, 0.105), (0.045, 0.028, 0.008))
    b.write(os.path.join(MESH_DIR, "antenna_array.amsh"))

    b = MeshBuilder()
    b.begin_material("car_trim")
    add_cylinder(b, (0.0, 0.0, 0.0), 1.0, 1.0, 6)
    b.write(os.path.join(MESH_DIR, "cable_seg.amsh"))

    b = MeshBuilder()
    b.begin_material("car_signal")
    b.box((0.0, 0.0, 0.0), (0.022, 0.022, 0.045))
    b.begin_material("alloy")
    b.box((0.0, 0.0, 0.055), (0.012, 0.012, 0.012))
    b.write(os.path.join(MESH_DIR, "cable_plug.amsh"))

    b = MeshBuilder()
    b.begin_material("car_trim")
    b.box((0.0, 0.006, 0.0), (0.034, 0.006, 0.034))
    b.begin_material("alloy")
    add_cylinder(b, (0.0, 0.012, 0.0), 0.015, 0.026, 8)
    b.begin_material("car_signal")
    b.box((0.0, 0.040, 0.0), (0.017, 0.004, 0.017))
    b.begin_material("alloy")
    b.box((0.0, 0.047, 0.0), (0.007, 0.006, 0.007))
    b.write(os.path.join(MESH_DIR, "jack_coax.amsh"))

    b = MeshBuilder()
    b.begin_material("car_trim")
    b.box((0.0, 0.008, 0.0), (0.042, 0.008, 0.030))
    b.begin_material("alloy")
    b.box((0.0, 0.028, 0.0), (0.026, 0.014, 0.017))
    b.begin_material("car_tail")
    b.box((0.0, 0.046, 0.0), (0.028, 0.005, 0.019))
    b.write(os.path.join(MESH_DIR, "jack_bus.amsh"))

    b = MeshBuilder()
    b.begin_material("car_dash")
    b.box((0.0, 0.0, 0.0), (0.067, 0.0025, 0.067))
    b.begin_material("car_signal")
    b.box((0.0, 0.0032, 0.038), (0.048, 0.0008, 0.024))
    b.begin_material("alloy")
    b.box((0.0, -0.0032, -0.008), (0.019, 0.0008, 0.019))
    b.box((0.0, 0.0032, -0.045), (0.012, 0.0008, 0.016))
    b.write(os.path.join(MESH_DIR, "part_floppy.amsh"))

    b = MeshBuilder()
    b.begin_material("car_dash")
    b.box((0.0, 0.0, 0.0), (0.085, 0.052, 0.042))
    b.begin_material("alloy")
    b.box((0.0, 0.004, -0.056), (0.032, 0.032, 0.016))
    b.box((0.0, 0.004, -0.074), (0.024, 0.024, 0.004))
    b.begin_material("car_trim")
    b.box((-0.042, 0.058, 0.0), (0.026, 0.008, 0.028))
    b.box((0.086, 0.0, 0.005), (0.004, 0.03, 0.02))
    b.begin_material("car_signal")
    b.box((0.056, 0.056, -0.012), (0.009, 0.005, 0.009))
    b.begin_material("alloy")
    b.box((-0.09, 0.0, 0.012), (0.006, 0.007, 0.007))
    b.vertices = [(tuple(c * 1.35 for c in p), n, uv) for p, n, uv in b.vertices]
    b.write(os.path.join(MESH_DIR, "part_camera.amsh"))

    b = MeshBuilder()
    b.begin_material("car_trim")
    b.box((0.0, 0.0, 0.0), (0.055, 0.008, 0.036))
    b.begin_material("car_tan")
    b.box((0.0, 0.0085, 0.005), (0.042, 0.0008, 0.022))
    b.begin_material("alloy")
    b.box((-0.018, 0.0095, 0.003), (0.007, 0.0008, 0.007))
    b.box((0.018, 0.0095, 0.003), (0.007, 0.0008, 0.007))
    b.begin_material("car_dash")
    b.box((0.0, 0.0, -0.032), (0.048, 0.0082, 0.005))
    b.write(os.path.join(MESH_DIR, "part_cassette.amsh"))

    b = MeshBuilder()
    b.begin_material("car_trim")
    b.box((0.0, 0.0, 0.0), (0.088, 0.042, 0.026))
    b.begin_material("car_dash")
    b.box((0.0, 0.008, 0.025), (0.062, 0.013, 0.004))
    b.begin_material("alloy")
    b.box((-0.070, -0.026, 0.025), (0.007, 0.006, 0.004))
    b.box((-0.054, -0.026, 0.025), (0.007, 0.006, 0.004))
    b.box((-0.038, -0.026, 0.025), (0.007, 0.006, 0.004))
    b.box((0.052, -0.026, 0.025), (0.014, 0.006, 0.004))
    b.begin_material("car_signal")
    b.box((0.076, -0.026, 0.025), (0.005, 0.004, 0.004))
    b.write(os.path.join(MESH_DIR, "part_deck.amsh"))

    b = MeshBuilder()
    b.begin_material("car_trim")
    add_cylinder(b, (0.0, -0.11, 0.0), 0.115, 0.022, 10)
    add_cylinder(b, (0.0, 0.075, 0.0), 0.115, 0.022, 10)
    b.begin_material("car_signal")
    add_cylinder(b, (0.0, -0.088, 0.0), 0.088, 0.163, 10)
    b.begin_material("alloy")
    b.box((0.0, 0.11, 0.0), (0.022, 0.014, 0.022))
    b.box((0.105, -0.02, 0.0), (0.012, 0.02, 0.014))
    b.write(os.path.join(MESH_DIR, "part_reel.amsh"))

    make_relay_tower()


def make_relay_tower():
    b = MeshBuilder()
    height = 11.0
    base = 0.85
    top = 0.28
    legs = 4
    b.begin_material("alloy")
    corners = []
    for i in range(legs):
        ang = (i + 0.5) * (2.0 * math.pi / legs)
        corners.append((math.cos(ang), math.sin(ang)))
    rungs = 11
    for i in range(legs):
        cx, cz = corners[i]
        nx, nz = corners[(i + 1) % legs]
        for s in range(rungs):
            f0 = s / float(rungs)
            f1 = (s + 1) / float(rungs)
            r0 = base + (top - base) * f0
            r1 = base + (top - base) * f1
            beam(b, (cx * r0, f0 * height, cz * r0),
                 (cx * r1, f1 * height, cz * r1), 0.05, 0.05)
            beam(b, (cx * r0, f0 * height, cz * r0),
                 (nx * r0, f0 * height, nz * r0), 0.035, 0.035)
            if s % 2 == 0:
                beam(b, (cx * r0, f0 * height, cz * r0),
                     (nx * r1, f1 * height, nz * r1), 0.028, 0.028)
    b.box((0.0, height + 0.35, 0.0), (0.10, 0.35, 0.10))
    b.begin_material("car_signal")
    b.box((0.0, height + 0.78, 0.0), (0.06, 0.09, 0.06))
    b.begin_material("alloy")
    for dish in range(3):
        ay = height * 0.55 + dish * 1.1
        beam(b, (0.0, ay, top + 0.2), (0.0, ay, top + 0.9), 0.03, 0.03)
        b.box((0.0, ay, top + 0.95), (0.32, 0.32, 0.05))
    b.begin_material("car_trim")
    b.box((base + 0.55, 0.75, 0.0), (0.42, 0.75, 0.55))
    b.begin_material("car_dash")
    b.box((base + 0.55, 0.95, 0.56), (0.30, 0.42, 0.02))
    b.begin_material("car_signal")
    b.box((base + 0.86, 0.62, 0.30), (0.05, 0.05, 0.05))
    b.write(os.path.join(MESH_DIR, "relay_tower.amsh"))

    c = MeshBuilder()
    c.begin_material("alloy")
    c.box((0.0, height * 0.5, 0.0), (0.42, height * 0.5, 0.42))
    c.box((0.0, 0.5, 0.0), (0.95, 0.5, 0.95))
    c.begin_material("car_trim")
    c.box((base + 0.55, 0.75, 0.0), (0.42, 0.75, 0.55))
    c.write(os.path.join(MESH_DIR, "relay_tower_col.amsh"))


def make_excel_wheel():
    b = MeshBuilder()
    sides = 14
    tire_r = 0.28
    rim_r = 0.175
    half_w = 0.09

    b.begin_material("tire")
    tube_x(b, -half_w, half_w, tire_r, sides)
    ring_x(b, -half_w, rim_r, tire_r, sides, -1.0)
    ring_x(b, half_w, rim_r, tire_r, sides, 1.0)

    b.begin_material("alloy")
    disc_x(b, -half_w + 0.012, rim_r + 0.012, sides, -1.0)

    b.begin_material("car_trim")
    disc_x(b, half_w - 0.012, rim_r + 0.012, sides, 1.0)

    make_double_sided(b)
    b.write(os.path.join(MESH_DIR, "excel_wheel.amsh"))


def make_car_textures():
    from make_testzone import make_texture
    tex = os.path.join(os.path.dirname(__file__), "..", "assets", "textures")
    make_texture(os.path.join(tex, "excel_paint.png"), 64, (26, 68, 46), (5, 7, 5), 0.5, None, 21)
    make_texture(os.path.join(tex, "car_trim.png"), 64, (24, 24, 26), (4, 4, 4), 0.8, None, 22)
    make_texture(os.path.join(tex, "car_tail.png"), 64, (150, 24, 26), (10, 4, 4), 0.6, None, 23)
    make_texture(os.path.join(tex, "car_signal.png"), 64, (208, 130, 42), (10, 8, 6), 0.6, None, 24)
    make_texture(os.path.join(tex, "car_tan.png"), 64, (150, 122, 90), (10, 8, 8), 0.9, None, 25)
    make_texture(os.path.join(tex, "car_dash.png"), 64, (34, 32, 30), (4, 4, 4), 0.8, None, 26)
    make_texture(os.path.join(tex, "car_carpet.png"), 64, (40, 36, 32), (6, 5, 5), 1.2, None, 27)
    make_texture(os.path.join(tex, "tire.png"), 64, (26, 26, 28), (3, 3, 3), 0.7, None, 28)
    make_texture(os.path.join(tex, "alloy.png"), 64, (168, 170, 175), (8, 8, 8), 0.5, None, 29)
    make_texture(os.path.join(tex, "car_tail_lit.png"), 64, (242, 60, 54), (6, 4, 4), 0.4, None, 30)
    make_texture(os.path.join(tex, "car_light.png"), 64, (246, 241, 228), (4, 4, 4), 0.3, None, 31)
    make_texture(os.path.join(tex, "smoke.png"), 64, (118, 118, 122), (8, 8, 8), 1.0, None, 32)
    make_texture(os.path.join(tex, "dial.png"), 64, (214, 212, 202), (6, 6, 6), 0.5, None, 33)
    make_texture(os.path.join(tex, "beige.png"), 64, (200, 192, 172), (7, 7, 7), 0.6, None, 34)
    make_texture(os.path.join(tex, "crt.png"), 64, (96, 210, 130), (8, 20, 10), 1.4, None, 35)


def main():
    os.makedirs(MESH_DIR, exist_ok=True)
    make_garage()
    make_tree()
    make_rock()
    make_excel_body()
    make_excel_hood()
    make_excel_door(-1)
    make_excel_door(1)
    make_excel_glass()
    make_excel_door_glass(-1)
    make_excel_door_glass(1)
    make_wiper()
    make_excel_trunk_lid()
    make_excel_extras()
    make_excel_wheel()
    make_part_meshes()
    make_antenna_meshes()
    make_car_textures()


if __name__ == "__main__":
    main()
