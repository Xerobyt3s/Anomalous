#pragma once

#include "core/types.h"
#include "math/vmath.h"

#include <functional>
#include <vector>

namespace anom {

using SdfFn = f32 (*)(Vec3 p, const void* user);

struct SdfGrid {
    Vec3 origin{};
    f32 cell = 1.0f;
    i32 nx = 0;
    i32 ny = 0;
    i32 nz = 0;
};

struct SdfMesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<u32> indices;
};

void parallel_for(u32 count, const std::function<void(u32 begin, u32 end)>& body);
SdfGrid sdf_grid_for(Aabb bounds, f32 cell);
void surface_nets(const SdfGrid& grid, SdfFn sdf, SdfFn bound, f32 band, const void* user,
                  SdfMesh& out);
Vec3 sdf_gradient(SdfFn sdf, const void* user, Vec3 p, f32 eps);

} // namespace anom
