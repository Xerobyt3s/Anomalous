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

    def merge_materials(self):
        order = []
        groups = {}
        for first, count, material in self.submeshes:
            if material not in groups:
                groups[material] = []
                order.append(material)
            groups[material].extend(self.indices[first:first + count])
        self.indices = []
        self.submeshes = []
        for material in order:
            self.submeshes.append((len(self.indices), len(groups[material]), material))
            self.indices.extend(groups[material])

    def write(self, path):
        self.end_material()
        self.merge_materials()
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
# The floor pan used to hang below the sills, which put it a couple of centimetres off
# the road at rest and scraping through it under any suspension travel. Tucked up so
# its underside is flush with the bodywork.
EXCEL_FLOOR_PAN_H = 0.05
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
    b.box((0.0, EXCEL_FLOOR_Y + EXCEL_FLOOR_PAN_H, 0.05),
          (0.62, EXCEL_FLOOR_PAN_H, 1.55))
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


def hash2(ix, iz, seed):
    n = (ix * 374761393 + iz * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def value_noise(x, z, seed):
    ix, iz = math.floor(x), math.floor(z)
    fx, fz = x - ix, z - iz
    sx, sz = fx * fx * (3.0 - 2.0 * fx), fz * fz * (3.0 - 2.0 * fz)
    a = hash2(ix, iz, seed)
    b = hash2(ix + 1, iz, seed)
    c = hash2(ix, iz + 1, seed)
    d = hash2(ix + 1, iz + 1, seed)
    near = a + (b - a) * sx
    far = c + (d - c) * sx
    return near + (far - near) * sz


def fbm(x, z, seed, octaves=3):
    total, amp, freq, norm = 0.0, 1.0, 1.0, 0.0
    for o in range(octaves):
        total += amp * value_noise(x * freq, z * freq, seed + o * 17)
        norm += amp
        amp *= 0.5
        freq *= 2.0
    return total / norm


def flat_tri(b, pa, pb, pc, outward, uv_scale=0.3):
    n = normalize(v_cross(v_sub(pb, pa), v_sub(pc, pa)))
    if v_dot(n, outward) < 0.0:
        pb, pc = pc, pb
        n = v_scale(n, -1.0)
    ax = max(range(3), key=lambda k: abs(n[k]))
    u_axis, v_axis = {0: (2, 1), 1: (0, 2), 2: (0, 1)}[ax]
    ids = [b.vertex(p, n, (p[u_axis] * uv_scale, p[v_axis] * uv_scale)) for p in (pa, pb, pc)]
    b.triangle(*ids)


GROUND = "island_grass"
TURF = "island_turf"
STONE = "island_rock"
GRASS_EXTENT = 24.0
GRASS_RES = 192
GRASS_HEIGHT_MIN = -8.0
GRASS_HEIGHT_MAX = 8.0


def write_grass_height(name, height_fn, inside_fn):
    import numpy as np
    from PIL import Image
    data = np.zeros((GRASS_RES, GRASS_RES), dtype=np.uint16)
    step = 2.0 * GRASS_EXTENT / GRASS_RES
    span = GRASS_HEIGHT_MAX - GRASS_HEIGHT_MIN
    for j in range(GRASS_RES):
        z = -GRASS_EXTENT + (j + 0.5) * step
        for i in range(GRASS_RES):
            x = -GRASS_EXTENT + (i + 0.5) * step
            if not inside_fn(x, z):
                continue
            t = (height_fn(x, z) - GRASS_HEIGHT_MIN) / span
            data[j, i] = max(1, min(65535, int(t * 65535.0 + 0.5)))
    path = os.path.join(MESH_DIR, "..", "textures", f"{name}_grass_h.png")
    Image.fromarray(data).save(path)
    print(f"wrote {path}")


def rock_blob(b, centre, size, seed, material=STONE):
    rng = random.Random(seed)
    base = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
    faces = [(0, 2, 4), (4, 2, 1), (1, 2, 5), (5, 2, 0),
             (4, 3, 0), (1, 3, 4), (5, 3, 1), (0, 3, 5)]
    points = [tuple(c * (0.75 + rng.random() * 0.5) for c in v) for v in base]
    mids = {}

    def midpoint(i, j):
        key = (min(i, j), max(i, j))
        if key not in mids:
            m = normalize(tuple((points[i][k] + points[j][k]) * 0.5 for k in range(3)))
            jitter = 0.78 + rng.random() * 0.45
            points.append(tuple(c * jitter for c in m))
            mids[key] = len(points) - 1
        return mids[key]

    tris = []
    for a, bb, c in faces:
        ab, bc, ca = midpoint(a, bb), midpoint(bb, c), midpoint(c, a)
        tris.extend([(a, ab, ca), (ab, bb, bc), (ca, bc, c), (ab, bc, ca)])
    b.begin_material(material)
    for a, bb, c in tris:
        pa, pb, pc = (tuple(centre[k] + points[i][k] * size[k] for k in range(3)) for i in (a, bb, c))
        mid = v_scale(v_add(v_add(pa, pb), pc), 1.0 / 3.0)
        flat_tri(b, pa, pb, pc, v_sub(mid, centre))


class Profile:
    def __init__(self, points):
        self.points = points
        self.lengths = [0.0]
        for i in range(1, len(points)):
            self.lengths.append(self.lengths[-1] + math.dist(points[i - 1], points[i]))
        self.total = self.lengths[-1]

    def at(self, s):
        s = max(0.0, min(self.total, s))
        for i in range(1, len(self.points)):
            if s <= self.lengths[i] or i == len(self.points) - 1:
                seg = self.lengths[i] - self.lengths[i - 1]
                t = 0.0 if seg < 1e-9 else (s - self.lengths[i - 1]) / seg
                x0, y0 = self.points[i - 1]
                x1, y1 = self.points[i]
                n = normalize((-(y1 - y0), x1 - x0, 0.0))
                return (x0 + (x1 - x0) * t, y0 + (y1 - y0) * t), (n[0], n[1])
        return self.points[-1], (0.0, 1.0)

    def point3(self, s, z, lift):
        (x, y), (nx, ny) = self.at(s)
        return (x + nx * lift, y + ny * lift, z)

    def normal3(self, s):
        _, (nx, ny) = self.at(s)
        return (nx, ny, 0.0)

    def offset(self, d):
        out = []
        for i, p in enumerate(self.points):
            _, (nx, ny) = self.at(self.lengths[i])
            out.append((p[0] + nx * d, p[1] + ny * d))
        return out


def solid_body(b, profile, width, back, floor, surface_material, with_uv, surface=True):
    hz = width * 0.5
    pts = profile
    if surface:
        b.begin_material(surface_material)
        prof = Profile(pts)
        for idx in range(len(pts) - 1):
            x0, y0 = pts[idx]
            x1, y1 = pts[idx + 1]
            s0, s1 = prof.lengths[idx], prof.lengths[idx + 1]
            n = normalize((-(y1 - y0), x1 - x0, 0.0))
            p = [(x0, y0, -hz), (x1, y1, -hz), (x1, y1, hz), (x0, y0, hz)]
            uv = [(s0 * 0.25, -hz * 0.25), (s1 * 0.25, -hz * 0.25),
                  (s1 * 0.25, hz * 0.25), (s0 * 0.25, hz * 0.25)]
            ids = [b.vertex(p[k], n, uv[k] if with_uv else (0.0, 0.0)) for k in range(4)]
            b.triangle(ids[0], ids[2], ids[1])
            b.triangle(ids[0], ids[3], ids[2])

    b.begin_material(STONE)
    xe, ye = pts[-1]
    outline = list(pts) + [(xe + back, ye), (xe + back, floor), (pts[0][0], floor)]
    if back <= 0.0:
        outline = list(pts) + [(xe, floor), (pts[0][0], floor)]
    flat, tris = ear_clip(outline)
    for side in (-1.0, 1.0):
        z = side * hz
        ids = [b.vertex((x, y, z), (0.0, 0.0, side), (x * 0.3, y * 0.3)) for x, y in flat]
        for i0, i1, i2 in tris:
            if side > 0:
                b.triangle(ids[i0], ids[i1], ids[i2])
            else:
                b.triangle(ids[i0], ids[i2], ids[i1])
    end_x = xe + max(back, 0.0)
    b.quad((end_x, floor, hz), (end_x, floor, -hz), (end_x, ye, -hz), (end_x, ye, hz), (1.0, 0.0, 0.0), 0.3)
    if back > 0.0:
        b.quad((xe, ye, hz), (end_x, ye, hz), (end_x, ye, -hz), (xe, ye, -hz), (0.0, 1.0, 0.0), 0.3)


def debris_cover(b, profile, width, seed, cell=1.5, grass=0.62):
    rng = random.Random(seed)
    prof = Profile(profile)
    ns = max(2, int(round(prof.total / cell)))
    nz = max(2, int(round(width / cell)))
    hz = width * 0.5
    corners = {}
    for i in range(ns + 1):
        for j in range(nz + 1):
            s = prof.total * i / ns
            z = -hz + width * j / nz
            if 0 < i < ns:
                s += (rng.random() - 0.5) * prof.total / ns * 0.7
            if 0 < j < nz:
                z += (rng.random() - 0.5) * width / nz * 0.7
            corners[(i, j)] = (s, z)

    def crack(c0, c1, key):
        h = hash2(key[0] * 7 + key[2], key[1] * 13 + key[3], seed)
        h2 = hash2(key[1] * 5 + key[3], key[0] * 11 + key[2], seed + 1)
        ds, dz = c1[0] - c0[0], c1[1] - c0[1]
        length = math.hypot(ds, dz)
        ns_, nz_ = -dz / max(length, 1e-6), ds / max(length, 1e-6)
        t = 0.35 + h2 * 0.3
        off = (h - 0.5) * length * 0.35
        return (c0[0] + ds * t + ns_ * off, c0[1] + dz * t + nz_ * off)

    def edge_key(p0, p1):
        return (p0 + p1) if p0 <= p1 else (p1 + p0)

    plates = {GROUND: [], STONE: []}
    for i in range(ns):
        for j in range(nz):
            ids = [(i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)]
            poly = []
            for k in range(4):
                c0 = corners[ids[k]]
                c1 = corners[ids[(k + 1) % 4]]
                poly.append(c0)
                key = edge_key(ids[k], ids[(k + 1) % 4])
                poly.append(crack(c0, c1, key))
            cs = sum(q[0] for q in poly) / len(poly)
            cz = sum(q[1] for q in poly) / len(poly)
            gap = 0.06 + rng.random() * 0.12
            shrunk = []
            for s_, z_ in poly:
                ds, dz = s_ - cs, z_ - cz
                d = math.hypot(ds, dz)
                k = max(0.0, d - gap) / d if d > 1e-6 else 0.0
                shrunk.append((cs + ds * k, cz + dz * k))
            turf = rng.random() < grass
            base = (0.02 if turf else -0.04) + rng.random() * 0.10
            ts = (rng.random() - 0.5) * 0.12
            tz = (rng.random() - 0.5) * 0.12
            tops = [prof.point3(s_, z_, base + ts * (s_ - cs) + tz * (z_ - cz)) for s_, z_ in shrunk]
            depth = (0.45 if turf else 0.3) + rng.random() * 0.3
            flare = 1.0 + rng.random() * 0.06
            bottoms = []
            for t, (s_, z_) in zip(tops, shrunk):
                n = prof.normal3(s_)
                bs, bz = cs + (s_ - cs) * flare, cz + (z_ - cz) * flare
                bp = prof.point3(bs, bz, base - depth)
                bottoms.append(bp)
            plates[GROUND if turf else STONE].append((tops, bottoms, prof.normal3(cs)))
    for material, items in plates.items():
        b.begin_material(material)
        for tops, _, n in items:
            centre = v_scale(tops[0], 0.0)
            for t in tops:
                centre = v_add(centre, v_scale(t, 1.0 / len(tops)))
            for k in range(len(tops)):
                flat_tri(b, centre, tops[k], tops[(k + 1) % len(tops)], n, 0.25)
    b.begin_material(STONE)
    for material, items in plates.items():
        for tops, bottoms, n in items:
            centre = v_scale(tops[0], 0.0)
            for t in tops:
                centre = v_add(centre, v_scale(t, 1.0 / len(tops)))
            for k in range(len(tops)):
                m = (k + 1) % len(tops)
                out = v_sub(v_scale(v_add(tops[k], tops[m]), 0.5), centre)
                flat_tri(b, tops[k], tops[m], bottoms[m], out, 0.3)
                flat_tri(b, tops[k], bottoms[m], bottoms[k], out, 0.3)
    pebbles = int(prof.total * width * 0.35)
    for k in range(pebbles):
        s_ = rng.random() * prof.total
        z_ = (rng.random() - 0.5) * width
        size = 0.08 + rng.random() ** 2 * 0.22
        c = prof.point3(s_, z_, size * 0.15)
        rock_blob(b, c, (size * (0.8 + rng.random() * 0.6), size * 0.6, size * (0.8 + rng.random() * 0.6)),
                  seed * 1000 + k)
    return prof


def debris_edges(b, prof, width, seed, count, start=0.0):
    rng = random.Random(seed + 101)
    hz = width * 0.5
    for k in range(count):
        s = start + rng.random() * (prof.total - start)
        side = -1.0 if k % 2 == 0 else 1.0
        size = 0.35 + rng.random() ** 1.8 * 1.3
        z = side * (hz + size * (rng.random() * 0.5 - 0.3))
        c = prof.point3(s, z, size * 0.05)
        rock_blob(b, c, (size * (0.9 + rng.random() * 0.5), size * (0.55 + rng.random() * 0.4),
                         size * (0.9 + rng.random() * 0.5)), seed * 31 + k)


CURL_RADIUS = 10.0
CURL_WIDTH = 24.0
CURL_BACK = 3.0
CURL_APRON = 4.0
CURL_SEGMENTS = 28
CURL_FLOOR = -1.5


def curl_profile():
    pts = [(-CURL_APRON, -0.25), (-CURL_APRON * 0.5, -0.05)]
    for s in range(CURL_SEGMENTS + 1):
        t = s / CURL_SEGMENTS * math.pi * 0.5
        pts.append((CURL_RADIUS * math.sin(t), CURL_RADIUS - CURL_RADIUS * math.cos(t)))
    return pts


def append_transformed(dst, src, origin, tx, ny, tz):
    src.end_material()
    for first, count, material in src.submeshes:
        dst.begin_material(material)
        remap = {}
        for idx in src.indices[first:first + count]:
            if idx not in remap:
                pos, normal, uv = src.vertices[idx]
                world = v_add(origin, v_add(v_add(v_scale(tx, pos[0]), v_scale(ny, pos[1])),
                                             v_scale(tz, pos[2])))
                n = normalize(v_add(v_add(v_scale(tx, normal[0]), v_scale(ny, normal[1])),
                                    v_scale(tz, normal[2])))
                remap[idx] = dst.vertex(world, n, uv)
        tri = src.indices[first:first + count]
        for k in range(0, len(tri), 3):
            dst.triangle(remap[tri[k]], remap[tri[k + 1]], remap[tri[k + 2]])


def chunk_mesh(seed, rx, rz):
    rng = random.Random(seed)
    shape = IslandShape(seed, rx, rz, 0.22)
    b = MeshBuilder()
    b.begin_material(TURF)
    rim = island_top(b, shape, 4, 18, True)
    b.begin_material(STONE)
    thick = 0.35 + rng.random() * 0.35
    bottom = []
    for idx, p in enumerate(rim):
        jag = (hash2(idx, 7, seed) - 0.5) * 0.5
        inset = 0.86 + hash2(idx, 11, seed) * 0.1
        bottom.append((p[0] * inset, -thick + jag, p[2] * inset))
    count = len(rim)
    for i in range(count):
        j = (i + 1) % count
        mid = v_scale(v_add(rim[i], rim[j]), 0.5)
        outward = (mid[0], 0.0, mid[2])
        flat_tri(b, rim[i], rim[j], bottom[j], outward)
        flat_tri(b, rim[i], bottom[j], bottom[i], outward)
    depth = (rx + rz) * (0.35 + rng.random() * 0.35)
    tip = ((rng.random() - 0.5) * rx * 0.4, -thick - depth, (rng.random() - 0.5) * rz * 0.4)
    hanging_root(b, bottom, depth, seed, tip)
    return b


def chunk_trail(b, profile, width, seed, spacing=2.6, keep=0.78, start=0.0):
    rng = random.Random(seed)
    prof = Profile(profile)
    hz = width * 0.5
    s = start + spacing * 0.5
    placed = 0
    while s < prof.total:
        z = -hz + spacing * 0.5 * rng.random()
        while z < hz:
            if rng.random() < keep:
                rx = 0.9 + rng.random() * 1.3
                rz = 0.9 + rng.random() * 1.3
                cs = min(max(s + (rng.random() - 0.5) * spacing * 0.5, 0.0), prof.total)
                cz = max(-hz + 0.5, min(hz - 0.5, z + (rng.random() - 0.5) * spacing * 0.5))
                lift = -0.05 + rng.random() * 0.12
                origin = prof.point3(cs, cz, lift)
                ny = prof.normal3(cs)
                tx = normalize((ny[1], -ny[0], 0.0))
                tz = (0.0, 0.0, 1.0)
                turn = rng.random() * math.tau
                c, sn = math.cos(turn), math.sin(turn)
                rtx = v_add(v_scale(tx, c), v_scale(tz, sn))
                rtz = v_sub(v_scale(tz, c), v_scale(tx, sn))
                append_transformed(b, chunk_mesh(seed * 977 + placed, rx, rz), origin, rtx, ny, rtz)
                placed += 1
            z += spacing * (0.8 + rng.random() * 0.4)
        s += spacing * (0.8 + rng.random() * 0.4)
    return placed


def make_curl():
    profile = curl_profile()
    b = MeshBuilder()
    placed = chunk_trail(b, profile, CURL_WIDTH, 11, start=CURL_APRON * 0.5)
    b.write(os.path.join(MESH_DIR, "curl.amsh"))
    print(f"curl: {placed} chunks")

    c = MeshBuilder()
    coarse = profile[::2]
    if coarse[-1] != profile[-1]:
        coarse.append(profile[-1])
    solid_body(c, coarse, CURL_WIDTH, CURL_BACK, CURL_FLOOR, STONE, False)
    c.write(os.path.join(MESH_DIR, "curl_col.amsh"))


BENDS = {
    "bend_a_b": dict(radius=10.0, angle=42.0, width=12.0, seed=37),
}


def bend_profile(radius, angle):
    pts = [(-3.0, -0.25), (-1.5, -0.05)]
    steps = max(4, int(angle / 4.0))
    a = math.radians(angle)
    for s in range(steps + 1):
        t = a * s / steps
        pts.append((radius * math.sin(t), radius - radius * math.cos(t)))
    return pts


def make_bend(name, radius, angle, width, seed):
    profile = bend_profile(radius, angle)
    b = MeshBuilder()
    placed = chunk_trail(b, profile, width, seed, spacing=2.4, start=1.0)
    b.write(os.path.join(MESH_DIR, f"{name}.amsh"))
    print(f"{name}: {placed} chunks")

    c = MeshBuilder()
    solid_body(c, profile, width, 0.0, CURL_FLOOR, STONE, False)
    c.write(os.path.join(MESH_DIR, f"{name}_col.amsh"))


def make_bends():
    for name, params in BENDS.items():
        make_bend(name, **params)


RAMP_WIDTH = 10.0
RAMP_ANGLE = 22.0
RAMP_RADIUS = 9.0
RAMP_LENGTH = 14.0


def ramp_profile():
    pts = [(-3.0, -0.25), (-1.5, -0.05)]
    a = math.radians(RAMP_ANGLE)
    steps = 10
    for s in range(steps + 1):
        t = a * s / steps
        pts.append((RAMP_RADIUS * math.sin(t), RAMP_RADIUS - RAMP_RADIUS * math.cos(t)))
    x0, y0 = pts[-1]
    pts.append((x0 + math.cos(a) * RAMP_LENGTH, y0 + math.sin(a) * RAMP_LENGTH))
    return pts


def make_ramp():
    profile = ramp_profile()
    b = MeshBuilder()
    placed = chunk_trail(b, profile, RAMP_WIDTH, 23, spacing=2.4, start=1.0)
    b.write(os.path.join(MESH_DIR, "ramp.amsh"))
    print(f"ramp: {placed} chunks")

    c = MeshBuilder()
    solid_body(c, profile, RAMP_WIDTH, 0.0, CURL_FLOOR, STONE, False)
    c.write(os.path.join(MESH_DIR, "ramp_col.amsh"))


SLAB_LENGTH = 30.0
SLAB_WIDTH = 24.0
SLAB_THICK = 3.0
SLAB_ROOT = 7.0


def slab_height(x, z):
    h = (fbm(x * 0.12 + 40.0, z * 0.12 + 40.0, 7) - 0.5) * 0.5
    edge = min(SLAB_LENGTH * 0.5 - abs(x), SLAB_WIDTH * 0.5 - abs(z))
    if edge < 1.2:
        h -= (1.2 - edge) * 0.22
    return h


def slab_rim(cell):
    nx = int(round(SLAB_LENGTH / cell))
    nz = int(round(SLAB_WIDTH / cell))
    hx, hz = SLAB_LENGTH * 0.5, SLAB_WIDTH * 0.5
    rim = []
    for i in range(nx):
        rim.append((-hx + i * cell, -hz))
    for k in range(nz):
        rim.append((hx, -hz + k * cell))
    for i in range(nx):
        rim.append((hx - i * cell, hz))
    for k in range(nz):
        rim.append((-hx, hz - k * cell))
    return rim


def slab_top(b, cell, with_uv):
    nx = int(round(SLAB_LENGTH / cell))
    nz = int(round(SLAB_WIDTH / cell))
    hx, hz = SLAB_LENGTH * 0.5, SLAB_WIDTH * 0.5
    ids = {}
    for k in range(nz + 1):
        for i in range(nx + 1):
            x = -hx + i * cell
            z = -hz + k * cell
            y = slab_height(x, z)
            e = 0.25
            n = normalize((slab_height(x - e, z) - slab_height(x + e, z), 2.0 * e,
                           slab_height(x, z - e) - slab_height(x, z + e)))
            ids[(i, k)] = b.vertex((x, y, z), n, (x * 0.25, z * 0.25) if with_uv else (0.0, 0.0))
    for k in range(nz):
        for i in range(nx):
            a, c = ids[(i, k + 1)], ids[(i + 1, k + 1)]
            d, e = ids[(i + 1, k)], ids[(i, k)]
            b.triangle(a, c, d)
            b.triangle(a, d, e)


def hanging_root(b, rim_bottom, depth, seed, tip):
    count = len(rim_bottom)
    rings = [rim_bottom]
    steps = 5
    for k in range(1, steps):
        f = (1.0 - k / steps) ** 0.8
        ring = []
        for idx, p in enumerate(rim_bottom):
            jag = (hash2(idx, k, seed) - 0.5) * 1.2
            d = depth * (k / steps) ** 1.3
            ring.append((p[0] * f + jag * 0.4, p[1] - d + jag, p[2] * f + jag * 0.4))
        rings.append(ring)
    b.begin_material(STONE)
    for k in range(len(rings)):
        ring = rings[k]
        for i in range(count):
            j = (i + 1) % count
            if k + 1 < len(rings):
                nxt = rings[k + 1]
                mid = v_scale(v_add(ring[i], nxt[j]), 0.5)
                outward = v_sub(mid, (0.0, mid[1] + 3.0, 0.0))
                flat_tri(b, ring[i], ring[j], nxt[j], outward)
                flat_tri(b, ring[i], nxt[j], nxt[i], outward)
            else:
                mid = v_scale(v_add(ring[i], tip), 0.5)
                outward = v_sub(mid, (0.0, mid[1] + 3.0, 0.0))
                flat_tri(b, ring[i], ring[j], tip, outward)


def make_slab():
    b = MeshBuilder()
    b.begin_material(GROUND)
    slab_top(b, 1.0, True)

    b.begin_material(STONE)
    rim = slab_rim(1.0)
    centre = (0.0, -SLAB_THICK * 0.5, 0.0)
    bottom = []
    for idx, (x, z) in enumerate(rim):
        jag = (fbm(x * 0.4, z * 0.4, 21) - 0.5) * 1.4
        inset = 0.94 + hash2(idx, 3, 5) * 0.06
        bottom.append((x * inset, -SLAB_THICK + jag, z * inset))
    count = len(rim)
    for i in range(count):
        j = (i + 1) % count
        t0 = (rim[i][0], slab_height(*rim[i]), rim[i][1])
        t1 = (rim[j][0], slab_height(*rim[j]), rim[j][1])
        outward = v_sub(v_scale(v_add(t0, t1), 0.5), centre)
        outward = (outward[0], 0.0, outward[2])
        flat_tri(b, t0, t1, bottom[j], outward)
        flat_tri(b, t0, bottom[j], bottom[i], outward)
    hanging_root(b, bottom, SLAB_ROOT, 9, (0.6, -SLAB_THICK - SLAB_ROOT, -0.4))
    b.write(os.path.join(MESH_DIR, "slab.amsh"))

    c = MeshBuilder()
    c.begin_material(STONE)
    slab_top(c, 2.0, False)
    coarse = slab_rim(2.0)
    count = len(coarse)
    base = [(x, -SLAB_THICK, z) for x, z in coarse]
    for i in range(count):
        j = (i + 1) % count
        t0 = (coarse[i][0], slab_height(*coarse[i]), coarse[i][1])
        t1 = (coarse[j][0], slab_height(*coarse[j]), coarse[j][1])
        outward = (t0[0] + t1[0], 0.0, t0[2] + t1[2])
        flat_tri(c, t0, t1, base[j], outward)
        flat_tri(c, t0, base[j], base[i], outward)
        flat_tri(c, (0.0, -SLAB_THICK, 0.0), base[i], base[j], (0.0, -1.0, 0.0))
    c.write(os.path.join(MESH_DIR, "slab_col.amsh"))

    hx, hz = SLAB_LENGTH * 0.5 - 0.4, SLAB_WIDTH * 0.5 - 0.4
    write_grass_height("slab", slab_height, lambda x, z: abs(x) < hx and abs(z) < hz)


ISLANDS = {
    "island_a": dict(seed=3, rx=13.0, rz=10.0, thick=2.5, root=9.0),
    "island_b": dict(seed=8, rx=9.0, rz=8.0, thick=2.0, root=7.0),
    "island_c": dict(seed=14, rx=16.0, rz=12.0, thick=3.0, root=12.0),
}


class IslandShape:
    def __init__(self, seed, rx, rz, amp=1.0):
        self.seed = seed
        self.rx = rx
        self.rz = rz
        self.amp = amp

    def radius(self, phi):
        n = fbm(math.cos(phi) * 1.4 + self.seed * 3.1, math.sin(phi) * 1.4 + self.seed, self.seed, 3)
        return 0.72 + 0.56 * n

    def rim_point(self, phi, f):
        r = self.radius(phi) * f
        return (math.cos(phi) * self.rx * r, math.sin(phi) * self.rz * r)

    def rho(self, x, z):
        phi = math.atan2(z / self.rz, x / self.rx)
        r = self.radius(phi)
        return math.hypot(x / self.rx, z / self.rz) / max(r, 1e-6)

    def height(self, x, z):
        rho = min(self.rho(x, z), 1.0)
        h = (fbm(x * 0.09 + self.seed * 7.0, z * 0.09, self.seed + 3) - 0.5) * 1.3
        h += 0.5 * (1.0 - rho * rho)
        if rho > 0.82:
            h -= (rho - 0.82) / 0.18 * 0.55
        return h * self.amp


def island_top(b, shape, rings, segments, with_uv):
    centre = b.vertex((0.0, shape.height(0.0, 0.0), 0.0), (0.0, 1.0, 0.0), (0.0, 0.0))
    ring_ids = []
    for k in range(1, rings + 1):
        f = k / rings
        ids = []
        for m in range(segments):
            phi = m / segments * math.tau
            x, z = shape.rim_point(phi, f)
            y = shape.height(x, z)
            e = 0.3
            n = normalize((shape.height(x - e, z) - shape.height(x + e, z), 2.0 * e,
                           shape.height(x, z - e) - shape.height(x, z + e)))
            ids.append(b.vertex((x, y, z), n, (x * 0.25, z * 0.25) if with_uv else (0.0, 0.0)))
        ring_ids.append(ids)

    def up_tri(i0, i1, i2):
        pa, pb, pc = b.vertices[i0][0], b.vertices[i1][0], b.vertices[i2][0]
        if v_cross(v_sub(pb, pa), v_sub(pc, pa))[1] >= 0.0:
            b.triangle(i0, i1, i2)
        else:
            b.triangle(i0, i2, i1)

    for m in range(segments):
        n = (m + 1) % segments
        up_tri(centre, ring_ids[0][m], ring_ids[0][n])
    for k in range(rings - 1):
        inner, outer = ring_ids[k], ring_ids[k + 1]
        for m in range(segments):
            n = (m + 1) % segments
            up_tri(inner[m], outer[m], outer[n])
            up_tri(inner[m], outer[n], inner[n])
    return [b.vertices[i][0] for i in ring_ids[-1]]


def make_island(name, seed, rx, rz, thick, root):
    shape = IslandShape(seed, rx, rz)
    b = MeshBuilder()
    b.begin_material(GROUND)
    rim = island_top(b, shape, 14, 64, True)
    b.begin_material(STONE)
    bottom = []
    for idx, p in enumerate(rim):
        jag = (hash2(idx, 7, seed) - 0.5) * 1.6
        inset = 0.9 + hash2(idx, 11, seed) * 0.08
        bottom.append((p[0] * inset, -thick + jag, p[2] * inset))
    count = len(rim)
    for i in range(count):
        j = (i + 1) % count
        mid = v_scale(v_add(rim[i], rim[j]), 0.5)
        outward = (mid[0], 0.0, mid[2])
        flat_tri(b, rim[i], rim[j], bottom[j], outward)
        flat_tri(b, rim[i], bottom[j], bottom[i], outward)
    rng = random.Random(seed)
    tip = ((rng.random() - 0.5) * rx * 0.3, -thick - root, (rng.random() - 0.5) * rz * 0.3)
    hanging_root(b, bottom, root, seed, tip)
    for k in range(4 + seed % 3):
        phi = rng.random() * math.tau
        x, z = shape.rim_point(phi, 0.25 + rng.random() * 0.45)
        size = 0.7 + rng.random() * 1.3
        rock_blob(b, (x, -thick - root * (0.35 + rng.random() * 0.4), z),
                  (size, size * 1.5, size), seed * 50 + k)
    for k in range(3 + seed % 4):
        phi = rng.random() * math.tau
        x, z = shape.rim_point(phi, 0.97)
        size = 0.5 + rng.random() * 0.9
        rock_blob(b, (x, shape.height(x, z) - size * 0.4, z), (size, size * 0.7, size), seed * 70 + k)
    b.write(os.path.join(MESH_DIR, f"{name}.amsh"))

    c = MeshBuilder()
    c.begin_material(STONE)
    crim = island_top(c, shape, 5, 24, False)
    cbase = [(p[0] * 0.92, -thick, p[2] * 0.92) for p in crim]
    count = len(crim)
    for i in range(count):
        j = (i + 1) % count
        mid = v_scale(v_add(crim[i], crim[j]), 0.5)
        outward = (mid[0], 0.0, mid[2])
        flat_tri(c, crim[i], crim[j], cbase[j], outward)
        flat_tri(c, crim[i], cbase[j], cbase[i], outward)
        flat_tri(c, cbase[i], cbase[j], tip, v_sub(v_scale(v_add(cbase[i], cbase[j]), 0.5), (0.0, -thick + 3.0, 0.0)))
    c.write(os.path.join(MESH_DIR, f"{name}_col.amsh"))

    write_grass_height(name, shape.height, lambda x, z: shape.rho(x, z) < 0.93)


def make_islands():
    for name, params in ISLANDS.items():
        make_island(name, **params)


TELESCOPE_SCALE = 24.0 / 70.0
TELESCOPE_ELEVATION_DEG = 28.0


def v_add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def v_sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def v_scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def v_dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def v_cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def v_lerp(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


class Frame:
    def __init__(self, origin, x, y, z):
        self.origin = origin
        self.x = x
        self.y = y
        self.z = z

    def point(self, local):
        return v_add(self.origin, self.direction(local))

    def direction(self, local):
        return v_add(v_add(v_scale(self.x, local[0]), v_scale(self.y, local[1])),
                     v_scale(self.z, local[2]))


def oriented_triangle(b, ia, ib, ic, hint):
    pa, pb, pc = b.vertices[ia][0], b.vertices[ib][0], b.vertices[ic][0]
    if v_dot(v_cross(v_sub(pb, pa), v_sub(pc, pa)), hint) >= 0.0:
        b.triangle(ia, ib, ic)
    else:
        b.triangle(ia, ic, ib)


def strut(b, a, c, radius):
    if math.dist(a, c) > 1e-4:
        beam(b, a, c, radius, radius, 0.5)


def lathe(b, origin, axis, profile, sides, uv_per_metre=0.5):
    _, right, forward = basis_from_axis(axis)
    axis = normalize(axis)
    rows = []
    count = len(profile)
    for i, (r, h) in enumerate(profile):
        prev = profile[max(i - 1, 0)]
        nxt = profile[min(i + 1, count - 1)]
        tr, th = nxt[0] - prev[0], nxt[1] - prev[1]
        tl = math.hypot(tr, th)
        tr, th = (1.0, 0.0) if tl < 1e-6 else (tr / tl, th / tl)
        nr, nh = th, -tr
        row = []
        for s in range(sides + 1):
            ang = s / sides * 2.0 * math.pi
            radial = v_add(v_scale(right, math.cos(ang)), v_scale(forward, math.sin(ang)))
            pos = v_add(origin, v_add(v_scale(radial, r), v_scale(axis, h)))
            normal = normalize(v_add(v_scale(radial, nr), v_scale(axis, nh)))
            if abs(nh) > 0.7:
                uv = (math.cos(ang) * r * uv_per_metre, math.sin(ang) * r * uv_per_metre)
            else:
                uv = (s / sides * 2.0 * math.pi * max(r, 0.5) * uv_per_metre, h * uv_per_metre)
            row.append(b.vertex(pos, normal, uv))
        rows.append(row)
    for i in range(count - 1):
        for s in range(sides):
            a, c = rows[i][s], rows[i + 1][s]
            hint = v_add(b.vertices[a][1], b.vertices[rows[i + 1][s + 1]][1])
            oriented_triangle(b, a, c, rows[i][s + 1], hint)
            oriented_triangle(b, rows[i][s + 1], c, rows[i + 1][s + 1], hint)


def frame_box(b, centre, ax, ay, az, half, uv_per_metre=0.5):
    axes = (ax, ay, az)
    for n in range(3):
        for sign in (1.0, -1.0):
            normal = v_scale(axes[n], sign)
            u = axes[(n + 1) % 3]
            v = axes[(n + 2) % 3]
            hu = half[(n + 1) % 3]
            hv = half[(n + 2) % 3]
            c = v_add(centre, v_scale(normal, half[n]))
            ids = []
            for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                p = v_add(c, v_add(v_scale(u, su * hu), v_scale(v, sv * hv)))
                ids.append(b.vertex(p, normal, (su * hu * uv_per_metre, sv * hv * uv_per_metre)))
            oriented_triangle(b, ids[0], ids[1], ids[2], normal)
            oriented_triangle(b, ids[0], ids[2], ids[3], normal)


def telescope_frames():
    s = TELESCOPE_SCALE
    e = math.radians(TELESCOPE_ELEVATION_DEG)
    tip = Frame((0.0, 36.0 * s, 0.0), (1.0, 0.0, 0.0), None,
                (0.0, math.sin(e), math.cos(e)))
    tip.y = v_cross(tip.z, tip.x)
    ali = Frame((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
    return tip, ali


def make_telescope():
    s = TELESCOPE_SCALE
    radius = 35.0 * s
    focal = 28.0 * s
    vertex_offset = 4.0 * s
    pedestal_radius = 17.0 * s
    pedestal_height = 5.0 * s
    bearing_span = 14.0 * s
    wheel_radius = 22.0 * s
    wheel_half_span = 12.0 * s
    leg_root_radius = 26.0 * s
    apex_radius = 4.5 * s
    sub_radius = 4.0 * s
    rings = 12
    sectors = 96

    def sag(r):
        return r * r / (4.0 * focal)

    def truss_depth(r):
        return (6.0 - 0.10 * r / s) * s

    tip, ali = telescope_frames()
    rim_z = vertex_offset + sag(radius)
    b = MeshBuilder()

    b.begin_material("dish_panel")
    grid = []
    for ri in range(rings + 1):
        r = radius * ri / rings
        row = []
        for si in range(sectors + 1):
            th = si / sectors * 2.0 * math.pi
            c, sn = math.cos(th), math.sin(th)
            slope = r / (2.0 * focal)
            local = (r * c, r * sn, vertex_offset + sag(r))
            normal = tip.direction(normalize((-slope * c, -slope * sn, 1.0)))
            row.append(b.vertex(tip.point(local), normal, (si / sectors, r / radius)))
        grid.append(row)
    for ri in range(rings):
        for si in range(sectors):
            a, c = grid[ri][si], grid[ri + 1][si]
            d, e = grid[ri + 1][si + 1], grid[ri][si + 1]
            hint = b.vertices[d][1]
            oriented_triangle(b, a, c, d, hint)
            oriented_triangle(b, a, d, e, hint)
    b.begin_material("dish_back")
    back = {}
    for ri in range(rings + 1):
        for si in range(sectors + 1):
            p, n, uv = b.vertices[grid[ri][si]]
            back[(ri, si)] = b.vertex(v_sub(p, v_scale(n, 0.05)), v_scale(n, -1.0), uv)
    for ri in range(rings):
        for si in range(sectors):
            a, c = back[(ri, si)], back[(ri + 1, si)]
            d, e = back[(ri + 1, si + 1)], back[(ri, si + 1)]
            hint = b.vertices[d][1]
            oriented_triangle(b, a, c, d, hint)
            oriented_triangle(b, a, d, e, hint)

    b.begin_material("dish_sub")
    sub_profile = [(sub_radius * i / 6.0, 0.075 / s * (sub_radius * i / 6.0) ** 2) for i in range(7)]
    sub_profile += [(sub_radius, 0.075 / s * sub_radius ** 2 + 0.4 * s), (3.0 * s, 1.6 * s),
                    (3.0 * s, 4.5 * s), (0.0, 4.5 * s)]
    lathe(b, tip.point((0.0, 0.0, vertex_offset + focal - 1.4 * s)), tip.z, sub_profile, 24, 1.0)

    b.begin_material("dish_steel")
    for si in range(0, sectors, 2):
        t0 = si / sectors * 2.0 * math.pi
        t1 = (si + 2) / sectors * 2.0 * math.pi
        strut(b, tip.point((radius * math.cos(t0), radius * math.sin(t0), rim_z)),
              tip.point((radius * math.cos(t1), radius * math.sin(t1), rim_z)), 0.40 * s)

    setback = 0.6 * s

    def front_node(r, th):
        return tip.point((r * math.cos(th), r * math.sin(th), vertex_offset + sag(r) - setback))

    def rear_node(r, th):
        return tip.point((r * math.cos(th), r * math.sin(th), vertex_offset + sag(r) - truss_depth(r)))

    truss_rings = [v * s for v in (3.5, 9.0, 15.0, 21.0, 27.0, 33.0)]
    ribs = 24
    for j in range(ribs):
        th = j / ribs * 2.0 * math.pi
        thn = (j + 1) / ribs * 2.0 * math.pi
        for i, r in enumerate(truss_rings):
            strut(b, rear_node(r, th), front_node(r, th), 0.24 * s)
            strut(b, rear_node(r, th), rear_node(r, thn), 0.26 * s)
            if i + 1 < len(truss_rings):
                rn = truss_rings[i + 1]
                strut(b, rear_node(r, th), rear_node(rn, th), 0.34 * s)
                strut(b, front_node(r, th), front_node(rn, th), 0.22 * s)
                strut(b, rear_node(r, th), front_node(rn, th), 0.16 * s)
                if i & 1:
                    strut(b, rear_node(r, th), rear_node(rn, thn), 0.16 * s)
                else:
                    strut(b, rear_node(r, thn), rear_node(rn, th), 0.16 * s)
            else:
                strut(b, rear_node(r, th), front_node(radius, th), 0.30 * s)
        strut(b, rear_node(truss_rings[0], th),
              tip.point((0.0, 0.0, vertex_offset - truss_depth(0.0))), 0.30 * s)

    rear = vertex_offset - truss_depth(0.0)
    lathe(b, tip.origin, tip.z,
          [(0.0, rear), (3.5 * s, rear), (3.5 * s, vertex_offset - 0.5 * s), (0.0, vertex_offset - 0.5 * s)], 16)
    axle = bearing_span + 2.0 * s
    lathe(b, tip.point((-axle, 0.0, 0.0)), tip.x,
          [(0.0, 0.0), (1.3 * s, 0.0), (1.3 * s, 2.0 * axle), (0.0, 2.0 * axle)], 12)

    apex_z = vertex_offset + focal + 1.0 * s
    for leg in range(4):
        th = (leg + 0.5) * math.pi * 0.5
        around = tip.direction((-math.sin(th), math.cos(th), 0.0))
        root = tip.point((leg_root_radius * math.cos(th), leg_root_radius * math.sin(th),
                          vertex_offset + sag(leg_root_radius)))
        apex = tip.point((apex_radius * math.cos(th), apex_radius * math.sin(th), apex_z))
        side = v_scale(around, 0.75 * s)
        strut(b, root, apex, 0.32 * s)
        strut(b, v_add(root, side), v_add(apex, side), 0.20 * s)
        strut(b, v_sub(root, side), v_sub(apex, side), 0.20 * s)
        bays = 12
        for k in range(bays):
            p0 = v_lerp(root, apex, k / bays)
            p1 = v_lerp(root, apex, (k + 1) / bays)
            if k & 1:
                strut(b, v_add(p0, side), v_sub(p1, side), 0.11 * s)
            else:
                strut(b, v_sub(p0, side), v_add(p1, side), 0.11 * s)
            strut(b, v_sub(p1, side), v_add(p1, side), 0.11 * s)
    for k in range(16):
        t0 = k / 16 * 2.0 * math.pi
        t1 = (k + 1) / 16 * 2.0 * math.pi
        strut(b, tip.point((apex_radius * math.cos(t0), apex_radius * math.sin(t0), apex_z)),
              tip.point((apex_radius * math.cos(t1), apex_radius * math.sin(t1), apex_z)), 0.22 * s)

    for side in (-1, 1):
        x = side * wheel_half_span
        segments = 14
        phi0, phi1 = math.radians(-40.0), math.radians(55.0)
        previous = None
        for k in range(segments + 1):
            phi = phi0 + (phi1 - phi0) * k / segments
            p = tip.point((x, wheel_radius * math.sin(phi), -wheel_radius * math.cos(phi)))
            if previous is not None:
                strut(b, previous, p, 0.55 * s)
            if k % 2 == 0:
                strut(b, tip.point((x, 0.0, 0.0)), p, 0.28 * s)
            if k > 0 and k % 4 == 0 and side < 0:
                q = tip.point((-x, wheel_radius * math.sin(phi), -wheel_radius * math.cos(phi)))
                strut(b, p, q, 0.24 * s)
            previous = p
    for side in (-1, 1):
        strut(b, tip.point((side * 6.0 * s, -9.0 * s, -17.0 * s)),
              tip.point((side * 3.5 * s, 0.0, rear)), 0.34 * s)

    leg_top, leg_front, leg_back = [], [], []
    for sx in (-1.0, 1.0):
        top = tip.point((sx * bearing_span, 0.0, 0.0))
        front = ali.point((sx * 14.5 * s, pedestal_height + 0.6 * s, 9.0 * s))
        back_leg = ali.point((sx * 14.5 * s, pedestal_height + 0.6 * s, -9.0 * s))
        leg_top.append(top)
        leg_front.append(front)
        leg_back.append(back_leg)
        frame_box(b, top, ali.x, ali.y, ali.z, (1.6 * s, 1.8 * s, 1.8 * s))
        strut(b, front, top, 0.95 * s)
        strut(b, back_leg, top, 0.95 * s)
        for t in (0.35, 0.65):
            strut(b, v_lerp(front, top, t), v_lerp(back_leg, top, t), 0.30 * s)
        strut(b, v_lerp(front, top, 0.35), v_lerp(back_leg, top, 0.65), 0.18 * s)
        strut(b, front, back_leg, 0.45 * s)
    for t in (0.35, 0.65):
        strut(b, v_lerp(leg_back[0], leg_top[0], t), v_lerp(leg_back[1], leg_top[1], t), 0.45 * s)
    strut(b, leg_back[0], leg_back[1], 0.45 * s)
    strut(b, v_lerp(leg_back[0], leg_top[0], 0.35), v_lerp(leg_back[1], leg_top[1], 0.65), 0.18 * s)

    walk_z = rim_z - 0.9 * s
    rail_radius = radius + 1.0 * s
    catwalk = 1.4 * s
    lathe(b, tip.point((0.0, 0.0, walk_z)), tip.z,
          [(rail_radius - catwalk, 0.0), (rail_radius, 0.0), (rail_radius, 0.10 * s),
           (rail_radius - catwalk, 0.10 * s)], sectors)
    posts = 48
    rail_top = []
    for i in range(posts):
        ang = i / posts * 2.0 * math.pi
        radial = (math.cos(ang) * rail_radius, math.sin(ang) * rail_radius, walk_z)
        foot = tip.point(radial)
        top = tip.point((radial[0], radial[1], walk_z + 1.2))
        strut(b, foot, top, 0.03)
        rail_top.append(top)
    for i in range(posts):
        strut(b, rail_top[i], rail_top[(i + 1) % posts], 0.025)

    b.begin_material("dish_concrete")
    frame_box(b, tip.point((0.0, -9.0 * s, -17.0 * s)), tip.x, tip.y, tip.z, (8.0 * s, 2.6 * s, 2.6 * s),
              1.0 / 3.0)
    pedestal = [(0.0, -1.5 * s), (pedestal_radius, -1.5 * s), (pedestal_radius, pedestal_height),
                (pedestal_radius - 1.5 * s, pedestal_height),
                (pedestal_radius - 1.5 * s, pedestal_height + 0.6 * s), (0.0, pedestal_height + 0.6 * s)]
    lathe(b, ali.origin, ali.y, pedestal, 32, 1.0 / 3.0)

    cabin_top = vertex_offset + focal - 1.4 * s + 4.5 * s
    b.begin_material("dish_steel")
    strut(b, tip.point((0.0, 0.0, cabin_top)), tip.point((0.0, 0.0, cabin_top + 0.5)), 0.05)
    b.begin_material("dish_beacon")
    strut(b, tip.point((0.0, 0.0, cabin_top + 0.5)), tip.point((0.0, 0.0, cabin_top + 0.8)), 0.12)

    b.write(os.path.join(MESH_DIR, "telescope.amsh"))

    c = MeshBuilder()
    c.begin_material("dish_concrete")
    lathe(c, ali.origin, ali.y, pedestal, 16, 1.0 / 3.0)
    for i in range(2):
        strut(c, leg_front[i], leg_top[i], 0.95 * s)
        strut(c, leg_back[i], leg_top[i], 0.95 * s)
        strut(c, leg_front[i], leg_back[i], 0.45 * s)
    strut(c, leg_back[0], leg_back[1], 0.45 * s)
    c.write(os.path.join(MESH_DIR, "telescope_col.amsh"))


def make_excel_wheel():
    b = MeshBuilder()
    sides = 18
    tire_r = 0.28
    lip_r = 0.19
    dish_r = 0.185
    hub_r = 0.058
    half_w = 0.09
    dish_x = 0.052

    b.begin_material("tire")
    tube_x(b, -half_w, half_w, tire_r, sides)
    ring_x(b, -half_w, lip_r, tire_r, sides, -1.0)
    ring_x(b, half_w, lip_r, tire_r, sides, 1.0)

    b.begin_material("car_trim")
    for sign in (-1.0, 1.0):
        face_x = sign * half_w
        rx = sign * dish_x
        ring_x(b, face_x, lip_r - 0.018, lip_r, sides, sign)
        tube_x(b, min(rx, face_x), max(rx, face_x), dish_r, sides)
        ring_x(b, rx, hub_r, dish_r, sides, sign)

    b.begin_material("alloy")
    for sign in (-1.0, 1.0):
        cap_x = sign * (dish_x + 0.010)
        tube_x(b, min(sign * dish_x, cap_x), max(sign * dish_x, cap_x), hub_r, sides)
        disc_x(b, cap_x, hub_r, sides, sign)
        for i in range(5):
            ang = i / 5.0 * 2.0 * math.pi + 0.3
            b.box((sign * (dish_x + 0.006), math.cos(ang) * 0.105, math.sin(ang) * 0.105),
                  (0.009, 0.011, 0.011))

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


GENERATORS = {
    "telescope": make_telescope,
    "telescope_textures": lambda: __import__("make_dish_textures").main(),
    "relay_tower": make_relay_tower,
    "slab": make_slab,
    "curl": make_curl,
    "ramp": make_ramp,
    "islands": make_islands,
    "bends": make_bends,
}


if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1:
        os.makedirs(MESH_DIR, exist_ok=True)
        for name in sys.argv[1:]:
            GENERATORS[name]()
    else:
        main()
