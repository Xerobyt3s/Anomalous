#include "world/terrain.h"
#include "assets/image.h"
#include "core/arena.h"
#include "core/config.h"
#include "core/fixed_string.h"
#include "core/log.h"

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
