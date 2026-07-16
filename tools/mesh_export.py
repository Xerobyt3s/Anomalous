"""AMSH exporter for Blender.

Usage (Blender 3.x/4.x):
  1. Open your model, select the objects to export.
  2. Load this file in Blender's text editor and Run Script, or:
       blender model.blend --background --python tools/mesh_export.py -- out.amsh
  3. Exports selected mesh objects (or all meshes if none selected) into one
     .amsh, one submesh per material, in object space of the active object
     (multi-object selections are baked into world space).

Format: see src/assets/mesh_format.h. Y-up conversion is applied
(Blender +Z up, -Y forward -> engine +Y up, -Z forward).
"""

import struct
import sys

import bpy
import bmesh
from mathutils import Matrix

AMSH_MAGIC = 0x48534D41
AMSH_VERSION = 1

AXIS_FIX = Matrix(((1, 0, 0, 0),
                   (0, 0, 1, 0),
                   (0, -1, 0, 0),
                   (0, 0, 0, 1)))


def gather_objects():
    selected = [o for o in bpy.context.selected_objects if o.type == "MESH"]
    if selected:
        return selected
    return [o for o in bpy.context.scene.objects if o.type == "MESH"]


def export(filepath):
    objects = gather_objects()
    if not objects:
        raise RuntimeError("no mesh objects to export")

    single = len(objects) == 1
    vertices = []
    material_indices = {}
    material_tris = {}

    for obj in objects:
        depsgraph = bpy.context.evaluated_depsgraph_get()
        eval_obj = obj.evaluated_get(depsgraph)
        mesh = eval_obj.to_mesh()

        bm = bmesh.new()
        bm.from_mesh(mesh)
        bmesh.ops.triangulate(bm, faces=bm.faces)
        bm.to_mesh(mesh)
        bm.free()

        mesh.calc_loop_triangles()
        if hasattr(mesh, "calc_normals_split"):
            mesh.calc_normals_split()
        transform = AXIS_FIX if single else AXIS_FIX @ obj.matrix_world
        normal_matrix = transform.to_3x3().inverted_safe().transposed()
        uv_layer = mesh.uv_layers.active

        for tri in mesh.loop_triangles:
            slot_index = tri.material_index
            if slot_index < len(obj.material_slots) and obj.material_slots[slot_index].material:
                mat_name = obj.material_slots[slot_index].material.name
            else:
                mat_name = "default"
            tri_indices = []
            for loop_index in tri.loops:
                loop = mesh.loops[loop_index]
                pos = transform @ mesh.vertices[loop.vertex_index].co
                normal = (normal_matrix @ loop.normal).normalized()
                uv = uv_layer.data[loop_index].uv if uv_layer else (0.0, 0.0)
                key = (round(pos.x, 5), round(pos.y, 5), round(pos.z, 5),
                       round(normal.x, 4), round(normal.y, 4), round(normal.z, 4),
                       round(uv[0], 5), round(uv[1], 5))
                if key not in material_indices:
                    material_indices[key] = len(vertices)
                    vertices.append((pos.x, pos.y, pos.z, normal.x, normal.y, normal.z, uv[0], uv[1]))
                tri_indices.append(material_indices[key])
            material_tris.setdefault(mat_name, []).extend(reversed(tri_indices))

        eval_obj.to_mesh_clear()

    indices = []
    submeshes = []
    for mat_name in sorted(material_tris.keys()):
        tris = material_tris[mat_name]
        submeshes.append((len(indices), len(tris), mat_name))
        indices.extend(tris)

    with open(filepath, "wb") as f:
        f.write(struct.pack("<5I", AMSH_MAGIC, AMSH_VERSION,
                            len(vertices), len(indices), len(submeshes)))
        for v in vertices:
            f.write(struct.pack("<8f", *v))
        for i in indices:
            f.write(struct.pack("<I", i))
        for first, count, mat_name in submeshes:
            name = mat_name.encode("utf-8")[:31]
            f.write(struct.pack("<II", first, count) + name + b"\x00" * (32 - len(name)))

    print(f"exported {filepath}: {len(vertices)} verts, {len(indices) // 3} tris, "
          f"{len(submeshes)} submeshes")


if __name__ == "__main__":
    argv = sys.argv
    if "--" in argv:
        out_path = argv[argv.index("--") + 1]
    else:
        out_path = bpy.path.abspath("//export.amsh")
    export(out_path)
