"""Generates assets/meshes/part_coil.amsh -- the roof coil ("Zippy go boom").

A base plate, a wound former, and a discharge toroid. Written rather than modelled
because the shape is entirely revolutions and a helix, which are cheaper to describe in
maths than in a mesh file.
"""
import math
import struct

MAGIC = 0x48534D41
VERSION = 1


class Mesh:
    def __init__(self):
        self.verts = []
        self.indices = []
        self.submeshes = []
        self._start = 0

    def begin(self):
        self._start = len(self.indices)

    def end(self, material):
        self.submeshes.append((self._start, len(self.indices) - self._start, material))

    def add(self, pos, normal, uv):
        self.verts.append((pos, normal, uv))
        return len(self.verts) - 1

    def quad(self, a, b, c, d):
        self.indices += [a, b, c, a, c, d]


def norm(v):
    length = math.sqrt(sum(c * c for c in v)) or 1.0
    return tuple(c / length for c in v)


def ring(mesh, centre, radius, y_normal, v, segments):
    out = []
    for i in range(segments + 1):
        a = i / segments * math.tau
        x = math.cos(a) * radius
        z = math.sin(a) * radius
        n = (0.0, y_normal, 0.0) if y_normal else norm((x, 0.0, z))
        out.append(mesh.add((centre[0] + x, centre[1], centre[2] + z), n, (i / segments, v)))
    return out


def tube(mesh, y0, y1, radius, segments):
    lo = ring(mesh, (0.0, y0, 0.0), radius, 0.0, 0.0, segments)
    hi = ring(mesh, (0.0, y1, 0.0), radius, 0.0, 1.0, segments)
    for i in range(segments):
        mesh.quad(lo[i], hi[i], hi[i + 1], lo[i + 1])


def disc(mesh, y, radius, up, segments):
    centre = mesh.add((0.0, y, 0.0), (0.0, 1.0 if up else -1.0, 0.0), (0.5, 0.5))
    rim = ring(mesh, (0.0, y, 0.0), radius, 1.0 if up else -1.0, 0.0, segments)
    for i in range(segments):
        if up:
            mesh.indices += [centre, rim[i], rim[i + 1]]
        else:
            mesh.indices += [centre, rim[i + 1], rim[i]]


def sweep(mesh, path, radius, segments):
    rings = []
    for i, point in enumerate(path):
        nxt = path[min(i + 1, len(path) - 1)]
        prv = path[max(i - 1, 0)]
        tangent = norm(tuple(nxt[k] - prv[k] for k in range(3)))
        helper = (0.0, 1.0, 0.0) if abs(tangent[1]) < 0.9 else (1.0, 0.0, 0.0)
        side = norm((tangent[1] * helper[2] - tangent[2] * helper[1],
                     tangent[2] * helper[0] - tangent[0] * helper[2],
                     tangent[0] * helper[1] - tangent[1] * helper[0]))
        up = (tangent[1] * side[2] - tangent[2] * side[1],
              tangent[2] * side[0] - tangent[0] * side[2],
              tangent[0] * side[1] - tangent[1] * side[0])
        row = []
        for s in range(segments + 1):
            a = s / segments * math.tau
            n = norm(tuple(side[k] * math.cos(a) + up[k] * math.sin(a) for k in range(3)))
            p = tuple(point[k] + n[k] * radius for k in range(3))
            row.append(mesh.add(p, n, (s / segments, i / max(len(path) - 1, 1))))
        rings.append(row)
    for i in range(len(rings) - 1):
        for s in range(segments):
            mesh.quad(rings[i][s], rings[i + 1][s], rings[i + 1][s + 1], rings[i][s + 1])


def torus(mesh, y, major, minor, major_segments, minor_segments):
    rows = []
    for i in range(major_segments + 1):
        a = i / major_segments * math.tau
        row = []
        for j in range(minor_segments + 1):
            b = j / minor_segments * math.tau
            r = major + math.cos(b) * minor
            p = (math.cos(a) * r, y + math.sin(b) * minor, math.sin(a) * r)
            n = norm((math.cos(a) * math.cos(b), math.sin(b), math.sin(a) * math.cos(b)))
            row.append(mesh.add(p, n, (i / major_segments, j / minor_segments)))
        rows.append(row)
    for i in range(major_segments):
        for j in range(minor_segments):
            mesh.quad(rows[i][j], rows[i + 1][j], rows[i + 1][j + 1], rows[i][j + 1])



def fix_winding(mesh):
    """Makes every triangle wind counter-clockwise about its own shading normal.

    The primitives here are built from different parameterisations and it is easy to get
    one of them backwards -- the swept helix and the torus both were. Rather than derive
    each by hand, compare the face normal against the vertices' normals and flip the ones
    that disagree.
    """
    flipped = 0
    for t in range(0, len(mesh.indices), 3):
        a, b, c = mesh.indices[t], mesh.indices[t + 1], mesh.indices[t + 2]
        pa, pb, pc = mesh.verts[a][0], mesh.verts[b][0], mesh.verts[c][0]
        e1 = [pb[k] - pa[k] for k in range(3)]
        e2 = [pc[k] - pa[k] for k in range(3)]
        face = (e1[1] * e2[2] - e1[2] * e2[1],
                e1[2] * e2[0] - e1[0] * e2[2],
                e1[0] * e2[1] - e1[1] * e2[0])
        shade = [sum(mesh.verts[v][1][k] for v in (a, b, c)) for k in range(3)]
        if sum(face[k] * shade[k] for k in range(3)) < 0.0:
            mesh.indices[t + 1], mesh.indices[t + 2] = c, b
            flipped += 1
    print("fix_winding: flipped %d of %d triangles" % (flipped, len(mesh.indices) // 3))


def build():
    mesh = Mesh()

    mesh.begin()
    tube(mesh, 0.0, 0.035, 0.145, 20)
    disc(mesh, 0.035, 0.145, True, 20)
    disc(mesh, 0.0, 0.145, False, 20)
    tube(mesh, 0.035, 0.335, 0.072, 16)
    tube(mesh, 0.335, 0.395, 0.030, 12)
    disc(mesh, 0.335, 0.072, True, 16)
    mesh.end("alloy")

    mesh.begin()
    turns = 15
    per_turn = 14
    path = []
    for i in range(turns * per_turn + 1):
        t = i / (turns * per_turn)
        a = t * turns * math.tau
        path.append((math.cos(a) * 0.088, 0.055 + t * 0.262, math.sin(a) * 0.088))
    sweep(mesh, path, 0.011, 5)
    mesh.end("car_trim")

    mesh.begin()
    torus(mesh, 0.425, 0.125, 0.042, 22, 8)
    mesh.end("alloy")
    fix_winding(mesh)
    return mesh


def write(mesh, path):
    with open(path, "wb") as f:
        f.write(struct.pack("<5I", MAGIC, VERSION, len(mesh.verts), len(mesh.indices),
                            len(mesh.submeshes)))
        for pos, n, uv in mesh.verts:
            f.write(struct.pack("<8f", pos[0], pos[1], pos[2], n[0], n[1], n[2], uv[0], uv[1]))
        for i in mesh.indices:
            f.write(struct.pack("<I", i))
        for first, count, material in mesh.submeshes:
            f.write(struct.pack("<2I", first, count))
            f.write(material.encode("ascii").ljust(32, b"\0"))
    print("part_coil.amsh: %d verts, %d tris, %d submeshes"
          % (len(mesh.verts), len(mesh.indices) // 3, len(mesh.submeshes)))


write(build(), "assets/meshes/part_coil.amsh")
