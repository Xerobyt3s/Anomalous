#include "render/cable_render.h"
#include "carsys/cables.h"
#include "render/device.h"
#include "render/gpu_mesh.h"

#include <cmath>

namespace anom {
namespace {
Quat quat_y_to(Vec3 dir)
{
    const Vec3 up{0.0f, 1.0f, 0.0f};
    const f32 d = dot(up, dir);
    if (d > 0.9999f) {
        return quat_identity();
    }
    if (d < -0.9999f) {
        return quat_from_axis_angle(Vec3{1.0f, 0.0f, 0.0f}, kPi);
    }
    return quat_from_axis_angle(normalize(cross(up, dir)), std::acos(f_clamp(d, -1.0f, 1.0f)));
}

}

void draw_cable(RenderDevice& device, const Cable& cable)
{
    const Vec3* p = cable.p;
    if (cable.state == CableState::Stowed || !cable.sim_init) {
        return;
    }

    const GpuMesh* seg = device.assets().mesh("cable_seg");
    for (u32 i = 0; i + 1 < kCablePoints; i++) {
        const Vec3 delta = p[i + 1] - p[i];
        const f32 len = length(delta);
        if (len < 1e-5f) {
            continue;
        }
        device.draw_mesh(seg, mat4_trs(p[i], quat_y_to(delta * (1.0f / len)),
                                       Vec3{kCableRadius, len * 1.02f, kCableRadius}));
    }

    if (cable.state != CableState::Dragged) {
        return;
    }
    const Vec3 delta = p[kCablePoints - 1] - p[kCablePoints - 2];
    const f32 len = length(delta);
    const Quat rot = len > 1e-5f
                       ? quat_y_to(delta * (1.0f / len))
                             * quat_from_axis_angle(Vec3{1.0f, 0.0f, 0.0f}, -kPi * 0.5f)
                       : quat_identity();
    device.draw_mesh(device.assets().mesh("cable_plug"),
                     mat4_trs(p[kCablePoints - 1], rot, Vec3{1.0f, 1.0f, 1.0f}));
}

}
