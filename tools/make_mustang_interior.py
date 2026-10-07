import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from make_meshes import MESH_DIR, MeshBuilder, basis_from_axis, beam, disc_z, make_double_sided, normalize

FLOOR_Y = -0.44
SEAT_X = 0.40
CUSHION_TOP = -0.19
HIPS = (-SEAT_X, -0.395, 0.06)
EYE = (-SEAT_X, 0.355, -0.02)
PEDALS = (-SEAT_X, -0.44, -0.30)
WHEEL_CENTER = (-SEAT_X, 0.08, -0.25)
WHEEL_AXIS = normalize((0.0, 0.42, 1.0))
WHEEL_RADIUS = 0.185
SHIFTER = (0.0, -0.065, -0.025)
DASH_FACE_Z = -0.46
BINNACLE = ((-0.40, 0.12, -0.43), (0.20, 0.06, 0.05))
BIG_DIALS = ((-0.47, 0.145), (-0.33, 0.145))
SMALL_DIALS = ((-0.555, 0.13), (-0.245, 0.13))
DIAL_FACE_Z = BINNACLE[0][2] + BINNACLE[1][2] + 0.002


def build():
    b = MeshBuilder()

    b.begin_material("car_carpet")
    b.box((0.0, FLOOR_Y, 0.40), (0.76, 0.015, 1.06))
    b.box((0.0, -0.335, 0.30), (0.13, 0.10, 0.95))
    b.box((0.0, -0.30, 1.86), (0.56, 0.02, 0.26))
    b.box((0.0, -0.10, 1.60), (0.56, 0.20, 0.02))
    b.box((0.0, -0.10, 2.12), (0.56, 0.20, 0.02))
    b.box((0.57, -0.10, 1.86), (0.02, 0.20, 0.26))
    b.box((-0.57, -0.10, 1.86), (0.02, 0.20, 0.26))

    b.begin_material("car_trim")
    b.box((0.0, -0.17, -0.70), (0.78, 0.27, 0.012))
    b.box((0.0, -0.17, 1.50), (0.74, 0.27, 0.012))
    b.begin_material("car_dash")
    b.box((0.0, 0.03, -0.58), (0.78, 0.09, 0.12))
    b.box(BINNACLE[0], BINNACLE[1])
    b.box((0.12, -0.16, -0.50), (0.14, 0.12, 0.05))

    b.begin_material("dial")
    for x, y in BIG_DIALS:
        disc_z(b, (x, y, DIAL_FACE_Z), 0.048, 14)
    for x, y in SMALL_DIALS:
        disc_z(b, (x, y, DIAL_FACE_Z), 0.03, 10)

    b.begin_material("car_dash")
    _, ws, wt = basis_from_axis(WHEEL_AXIS)

    def rim(a):
        return tuple(WHEEL_CENTER[k] + (ws[k] * math.cos(a) + wt[k] * math.sin(a)) * WHEEL_RADIUS for k in range(3))

    for i in range(14):
        beam(b, rim(i / 14 * 2.0 * math.pi), rim((i + 1) / 14 * 2.0 * math.pi), 0.014, 0.014)
    for a in (0.0, 2.0 * math.pi / 3.0, 4.0 * math.pi / 3.0):
        beam(b, WHEEL_CENTER, rim(a + math.pi * 0.5), 0.01, 0.008)
    beam(b, WHEEL_CENTER, tuple(WHEEL_CENTER[k] - WHEEL_AXIS[k] * 0.22 for k in range(3)), 0.022, 0.022)
    beam(b, (0.0, -0.235, 0.02), (SHIFTER[0], SHIFTER[1] - 0.02, SHIFTER[2] + 0.005), 0.012, 0.012)
    b.box(SHIFTER, (0.026, 0.022, 0.026))

    b.begin_material("car_tan")
    for sx in (1.0, -1.0):
        x = sx * SEAT_X
        b.box((x, CUSHION_TOP - 0.05, 0.20), (0.18, 0.05, 0.23))
        b.box((x, CUSHION_TOP - 0.125, 0.20), (0.15, 0.075, 0.18))
        for side in (-1.0, 1.0):
            b.box((x + side * 0.17, CUSHION_TOP - 0.01, 0.20), (0.025, 0.035, 0.22))
        beam(b, (x, CUSHION_TOP + 0.04, 0.42), (x, 0.27, 0.54), 0.18, 0.05)
        for side in (-1.0, 1.0):
            beam(b, (x + side * 0.165, CUSHION_TOP + 0.06, 0.40), (x + side * 0.165, 0.20, 0.50), 0.03, 0.06)
        b.box((x, 0.33, 0.57), (0.10, 0.055, 0.045))
    b.box((0.0, -0.30, 1.10), (0.62, 0.05, 0.20))
    b.box((0.0, -0.08, 1.33), (0.62, 0.17, 0.05))

    make_double_sided(b)
    path = os.path.join(MESH_DIR, "mustang_interior.amsh")
    b.write(path)
    print("wrote", path)
    print("seat_eye   ", EYE)
    print("seat_hips  ", HIPS)
    print("pedals     ", PEDALS)
    print("wheel      ", WHEEL_CENTER, WHEEL_AXIS, WHEEL_RADIUS)
    print("shifter    ", SHIFTER)
    print("dial_z     ", DIAL_FACE_Z + 0.004)


if __name__ == "__main__":
    build()
