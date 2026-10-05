#include "world/terrain.h"
#include "assets/image.h"
#include "core/arena.h"
#include "core/config.h"
#include "core/fixed_string.h"
#include "core/log.h"
#include "world/islands/island_field.h"

#include <cmath>

namespace anom {

bool Terrain::load(Arena& arena, Arena& scratch, std::string_view zone_dir, const Config& cfg)
{
    const std::string_view heightmap_name = cfg.get_str("terrain.heightmap", "heightmap.png");
    const std::string_view roadmask_name = cfg.get_str("terrain.roadmask", "roadmask.png");
    const f32 cell_size = cfg.get_f32("terrain.cell_size", 2.0f);
    const f32 height_offset = cfg.get_f32("terrain.height_offset", 0.0f);
    const f32 height_scale = cfg.get_f32("terrain.height_scale", 1.0f);

    FixedString<256> path;
    path.format("%.*s/%.*s", static_cast<int>(zone_dir.size()), zone_dir.data(),
                static_cast<int>(heightmap_name.size()), heightmap_name.data());

    const Image16 raw = load_image_gray16(scratch, scratch, path.view());
    if (!raw.valid() || raw.width != raw.height || raw.width < 2) {
        log_error("terrain: bad heightmap %s (%ux%u)", path.c_str(), raw.width, raw.height);
        return false;
    }

    hf_.alloc(arena, raw.width, cell_size);
    for (u32 iz = 0; iz < raw.height; iz++) {
        for (u32 ix = 0; ix < raw.width; ix++) {
            const u16 sample = raw.pixels[static_cast<u64>(iz) * raw.width + ix];
            hf_.set_height(ix, iz,
                           height_offset + static_cast<f32>(sample) / 65535.0f * height_scale);
        }
    }
    hf_.recompute_extents();
    const u64 count = static_cast<u64>(raw.width) * raw.height;
    base_heights_ = arena.push_array<f32>(count);
    for (u64 i = 0; i < count; i++) {
        base_heights_[i] = hf_.heights()[i];
    }
    crater_signature_ = 0;

    path.format("%.*s/%.*s", static_cast<int>(zone_dir.size()), zone_dir.data(),
                static_cast<int>(roadmask_name.size()), roadmask_name.data());
    const Image8 mask = load_image_gray(arena, scratch, path.view());
    if (!mask.valid() || mask.width != mask.height) {
        log_warn("terrain: missing or non-square road mask %s", path.c_str());
        roadmask_ = nullptr;
        mask_size_ = 0;
    } else {
        roadmask_ = mask.pixels;
        mask_size_ = mask.width;
    }

    log_info("terrain: %ux%u cells, %.0fx%.0f m, height %.1f..%.1f m",
             hf_.size_x(), hf_.size_z(),
             static_cast<f64>(hf_.span_x()), static_cast<f64>(hf_.span_z()),
             static_cast<f64>(hf_.min_height()), static_cast<f64>(hf_.max_height()));
    return true;
}

void Terrain::stamp_craters(const CraterStamp* craters, u32 count)
{
    u32 signature = 2166136261u;
    for (u32 i = 0; i < count; i++) {
        const u8* bytes = reinterpret_cast<const u8*>(&craters[i]);
        for (u32 b = 0; b < sizeof(CraterStamp); b++) {
            signature = (signature ^ bytes[b]) * 16777619u;
        }
    }
    if (!base_heights_ || !hf_.valid() || signature == crater_signature_) {
        return;
    }
    crater_signature_ = signature;
    const f32 cell = hf_.cell_size();
    const Vec3 origin = hf_.origin();
    for (u32 iz = 0; iz < hf_.size_z(); iz++) {
        for (u32 ix = 0; ix < hf_.size_x(); ix++) {
            const f32 x = origin.x + static_cast<f32>(ix) * cell;
            const f32 z = origin.z + static_cast<f32>(iz) * cell;
            f32 h = base_heights_[static_cast<u64>(iz) * hf_.size_x() + ix];
            for (u32 c = 0; c < count; c++) {
                const CraterStamp& s = craters[c];
                const f32 dx = x - s.x;
                const f32 dz = z - s.z;
                const f32 angle = std::atan2(dz, dx);
                const f32 wobble = 1.0f + 0.12f * std::sin(angle * 3.0f + s.radius)
                                 + 0.07f * std::sin(angle * 7.0f - s.depth * 2.0f);
                const f32 rho = std::sqrt(dx * dx + dz * dz) / (s.radius * wobble);
                if (rho > 1.8f) {
                    continue;
                }
                const f32 bowl = std::pow(f_max(1.0f - rho * rho, 0.0f), 1.3f);
                const f32 rim = std::exp(-((rho - 1.0f) / 0.22f) * ((rho - 1.0f) / 0.22f));
                h += -s.depth * bowl + s.depth * 0.22f * rim;
            }
            hf_.set_height(ix, iz, h);
        }
    }
    hf_.recompute_extents();
}

f32 Terrain::road_amount(f32 x, f32 z) const
{
    if (!roadmask_ || mask_size_ == 0) {
        return 0.0f;
    }
    const f32 u = f_clamp01((x - hf_.origin().x) / hf_.span_x());
    const f32 v = f_clamp01((z - hf_.origin().z) / hf_.span_z());
    const u32 ix = static_cast<u32>(u * static_cast<f32>(mask_size_ - 1));
    const u32 iz = static_cast<u32>(v * static_cast<f32>(mask_size_ - 1));
    return static_cast<f32>(roadmask_[static_cast<u64>(iz) * mask_size_ + ix]) / 255.0f;
}

} // namespace anom
