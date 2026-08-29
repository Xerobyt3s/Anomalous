#include "terminal/mapdata.h"
#include "physics/heightfield.h"

#include <cstring>

namespace anom {
namespace {

constexpr f32 kVisitRadius = 90.0f;

} // namespace

void MapData::init(const Heightfield& hf)
{
    origin_x_ = hf.origin().x;
    origin_z_ = hf.origin().z;
    cell_ = f_max(hf.span_x(), hf.span_z()) / static_cast<f32>(kMapDataGrid);
    reset();
    ready_ = true;
}

void MapData::reset()
{
    std::memset(revealed_, 0, sizeof(revealed_));
    count_ = 0;
}

u32 MapData::reveal(Vec3 center, f32 radius)
{
    if (!ready_) {
        return 0;
    }

    const i32 last = static_cast<i32>(kMapDataGrid) - 1;
    i32 c0 = static_cast<i32>((center.x - radius - origin_x_) / cell_);
    i32 c1 = static_cast<i32>((center.x + radius - origin_x_) / cell_);
    i32 r0 = static_cast<i32>((center.z - radius - origin_z_) / cell_);
    i32 r1 = static_cast<i32>((center.z + radius - origin_z_) / cell_);
    c0 = c0 < 0 ? 0 : c0;
    r0 = r0 < 0 ? 0 : r0;
    c1 = c1 > last ? last : c1;
    r1 = r1 > last ? last : r1;

    u32 added = 0;
    for (i32 row = r0; row <= r1; row++) {
        for (i32 col = c0; col <= c1; col++) {
            const i32 idx = row * static_cast<i32>(kMapDataGrid) + col;
            if (revealed_[idx]) {
                continue;
            }
            const f32 cx = origin_x_ + (static_cast<f32>(col) + 0.5f) * cell_;
            const f32 cz = origin_z_ + (static_cast<f32>(row) + 0.5f) * cell_;
            const f32 dx = cx - center.x;
            const f32 dz = cz - center.z;
            if (dx * dx + dz * dz > radius * radius) {
                continue;
            }
            revealed_[idx] = 1;
            order_[count_++] = static_cast<u16>(idx);
            added++;
        }
    }
    return added;
}

void MapData::visit(Vec3 pos)
{
    reveal(pos, kVisitRadius);
}

u32 MapData::reveal_radius(Vec3 center, f32 radius)
{
    return reveal(center, radius);
}

bool MapData::cell(u32 index, MapCell& out) const
{
    if (index >= count_) {
        return false;
    }
    const u16 idx = order_[index];
    const u32 col = idx % kMapDataGrid;
    const u32 row = idx / kMapDataGrid;
    out.x = origin_x_ + (static_cast<f32>(col) + 0.5f) * cell_;
    out.z = origin_z_ + (static_cast<f32>(row) + 0.5f) * cell_;
    out.size = cell_;
    return true;
}

} // namespace anom
