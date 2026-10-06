#pragma once

#include "core/types.h"
#include "physics/heightfield.h"

#include <string_view>

namespace anom {

class Arena;
class Config;
struct CraterStamp;

class Terrain {
public:
    bool load(Arena& arena, Arena& scratch, std::string_view zone_dir, const Config& cfg);

    f32 road_amount(f32 x, f32 z) const;
    f32 base_height(f32 x, f32 z) const;

    Heightfield& heightfield() { return hf_; }
    const Heightfield& heightfield() const { return hf_; }
    const u8* roadmask() const { return roadmask_; }
    u32 mask_size() const { return mask_size_; }
    void stamp_craters(const CraterStamp* craters, u32 count);
    u32 crater_signature() const { return crater_signature_; }

private:
    Heightfield hf_;
    const u8* roadmask_ = nullptr;
    u32 mask_size_ = 0;
    f32* base_heights_ = nullptr;
    u32 crater_signature_ = 0;
};

} // namespace anom
