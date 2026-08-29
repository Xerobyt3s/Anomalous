#pragma once

#include "core/types.h"
#include "math/vmath.h"

namespace anom {

class Heightfield;

inline constexpr u32 kMapDataGrid = 64;
inline constexpr u32 kMapDataCells = kMapDataGrid * kMapDataGrid;

struct MapCell {
    f32 x = 0.0f;
    f32 z = 0.0f;
    f32 size = 0.0f;
};

class MapData {
public:
    void init(const Heightfield& hf);
    void reset();

    void visit(Vec3 pos);
    u32 reveal_radius(Vec3 center, f32 radius);

    u32 count() const { return count_; }
    static constexpr u32 total() { return kMapDataCells; }
    bool cell(u32 index, MapCell& out) const;

private:
    u32 reveal(Vec3 center, f32 radius);

    f32 origin_x_ = 0.0f;
    f32 origin_z_ = 0.0f;
    f32 cell_ = 1.0f;
    bool ready_ = false;
    u8 revealed_[kMapDataCells] = {};
    u16 order_[kMapDataCells] = {};
    u32 count_ = 0;
};

} // namespace anom
