#include "world/terrain.h"
#include "core/arena.h"
#include "core/config.h"
#include "core/log.h"
#include "assets/assets.h"

#include <stdio.h>

b32 terrain_load(Terrain* terrain, struct Arena* arena, const char* zone_dir, const struct Config* cfg)
{
    const char* heightmap_name = config_get_str(cfg, "terrain.heightmap", "heightmap.png");
    const char* roadmask_name = config_get_str(cfg, "terrain.roadmask", "roadmask.png");
    f32 cell_size = config_get_f32(cfg, "terrain.cell_size", 2.0f);
    f32 height_offset = config_get_f32(cfg, "terrain.height_offset", 0.0f);
    f32 height_scale = config_get_f32(cfg, "terrain.height_scale", 1.0f);

    char path[256];
    snprintf(path, sizeof(path), "%s/%s", zone_dir, heightmap_name);
    u32 width = 0, height = 0;
    u16* raw = assets_load_image_16(arena, path, &width, &height);
    if (!raw || width != height || width < 2) {
        log_error("terrain: bad heightmap %s (%ux%u)", path, width, height);
        return 0;
    }

    Heightfield* hf = &terrain->hf;
    hf->size_x = width;
    hf->size_z = height;
    hf->cell_size = cell_size;
    f32 half = (f32)(width - 1) * cell_size * 0.5f;
    hf->origin = v3(-half, 0.0f, -half);
    hf->heights = arena_push_array(arena, f32, (u64)width * height);
    hf->min_height = 1e30f;
    hf->max_height = -1e30f;
    for (u64 i = 0; i < (u64)width * height; i++) {
        f32 h = height_offset + (f32)raw[i] / 65535.0f * height_scale;
        hf->heights[i] = h;
        hf->min_height = f_min(hf->min_height, h);
        hf->max_height = f_max(hf->max_height, h);
    }

    snprintf(path, sizeof(path), "%s/%s", zone_dir, roadmask_name);
    u32 mask_w = 0, mask_h = 0;
    terrain->roadmask = assets_load_image_8(arena, path, &mask_w, &mask_h);
    terrain->mask_size = mask_w;
    if (!terrain->roadmask || mask_w != mask_h) {
        log_warn("terrain: missing or non-square road mask %s", path);
        terrain->roadmask = 0;
        terrain->mask_size = 0;
    }

    log_info("terrain: %ux%u cells, %.0fx%.0f m, height %.1f..%.1f m",
             width, height, (f64)((width - 1) * cell_size), (f64)((height - 1) * cell_size),
             (f64)hf->min_height, (f64)hf->max_height);
    return 1;
}

f32 terrain_road_amount(const Terrain* terrain, f32 x, f32 z)
{
    if (!terrain->roadmask || terrain->mask_size == 0) {
        return 0.0f;
    }
    const Heightfield* hf = &terrain->hf;
    f32 span_x = (f32)(hf->size_x - 1) * hf->cell_size;
    f32 span_z = (f32)(hf->size_z - 1) * hf->cell_size;
    f32 u = f_clamp01((x - hf->origin.x) / span_x);
    f32 v = f_clamp01((z - hf->origin.z) / span_z);
    u32 ix = (u32)(u * (f32)(terrain->mask_size - 1));
    u32 iz = (u32)(v * (f32)(terrain->mask_size - 1));
    return (f32)terrain->roadmask[iz * terrain->mask_size + ix] / 255.0f;
}
