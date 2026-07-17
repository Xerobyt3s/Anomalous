#include "terminal/mapdata.h"
#include "physics/heightfield.h"

#include <string.h>

#define MAPDATA_VISIT_RADIUS 90.0f

static f32 s_origin_x;
static f32 s_origin_z;
static f32 s_cell;
static b32 s_ready;
static u8 s_revealed[MAPDATA_MAX_CELLS];
static u16 s_order[MAPDATA_MAX_CELLS];
static u32 s_count;

void mapdata_init(const struct Heightfield* hf)
{
    s_origin_x = hf->origin.x;
    s_origin_z = hf->origin.z;
    f32 extent_x = (f32)(hf->size_x - 1) * hf->cell_size;
    f32 extent_z = (f32)(hf->size_z - 1) * hf->cell_size;
    s_cell = f_max(extent_x, extent_z) / (f32)MAPDATA_GRID;
    mapdata_reset();
    s_ready = 1;
}

void mapdata_reset(void)
{
    memset(s_revealed, 0, sizeof(s_revealed));
    s_count = 0;
}

void mapdata_visit(Vec3 pos)
{
    if (!s_ready) {
        return;
    }
    f32 r = MAPDATA_VISIT_RADIUS;
    i32 c0 = (i32)((pos.x - r - s_origin_x) / s_cell);
    i32 c1 = (i32)((pos.x + r - s_origin_x) / s_cell);
    i32 r0 = (i32)((pos.z - r - s_origin_z) / s_cell);
    i32 r1 = (i32)((pos.z + r - s_origin_z) / s_cell);
    c0 = c0 < 0 ? 0 : c0;
    r0 = r0 < 0 ? 0 : r0;
    c1 = c1 >= MAPDATA_GRID ? MAPDATA_GRID - 1 : c1;
    r1 = r1 >= MAPDATA_GRID ? MAPDATA_GRID - 1 : r1;
    for (i32 row = r0; row <= r1; row++) {
        for (i32 col = c0; col <= c1; col++) {
            i32 idx = row * MAPDATA_GRID + col;
            if (s_revealed[idx]) {
                continue;
            }
            f32 cx = s_origin_x + ((f32)col + 0.5f) * s_cell;
            f32 cz = s_origin_z + ((f32)row + 0.5f) * s_cell;
            f32 dx = cx - pos.x;
            f32 dz = cz - pos.z;
            if (dx * dx + dz * dz > r * r) {
                continue;
            }
            s_revealed[idx] = 1;
            s_order[s_count++] = (u16)idx;
        }
    }
}

u32 mapdata_reveal_radius(Vec3 center, f32 radius)
{
    if (!s_ready) {
        return 0;
    }
    u32 added = 0;
    i32 c0 = (i32)((center.x - radius - s_origin_x) / s_cell);
    i32 c1 = (i32)((center.x + radius - s_origin_x) / s_cell);
    i32 r0 = (i32)((center.z - radius - s_origin_z) / s_cell);
    i32 r1 = (i32)((center.z + radius - s_origin_z) / s_cell);
    c0 = c0 < 0 ? 0 : c0;
    r0 = r0 < 0 ? 0 : r0;
    c1 = c1 >= MAPDATA_GRID ? MAPDATA_GRID - 1 : c1;
    r1 = r1 >= MAPDATA_GRID ? MAPDATA_GRID - 1 : r1;
    for (i32 row = r0; row <= r1; row++) {
        for (i32 col = c0; col <= c1; col++) {
            i32 idx = row * MAPDATA_GRID + col;
            if (s_revealed[idx]) {
                continue;
            }
            f32 cx = s_origin_x + ((f32)col + 0.5f) * s_cell;
            f32 cz = s_origin_z + ((f32)row + 0.5f) * s_cell;
            f32 dx = cx - center.x;
            f32 dz = cz - center.z;
            if (dx * dx + dz * dz > radius * radius) {
                continue;
            }
            s_revealed[idx] = 1;
            s_order[s_count++] = (u16)idx;
            added++;
        }
    }
    return added;
}

u32 mapdata_count(void)
{
    return s_count;
}

u32 mapdata_total(void)
{
    return MAPDATA_MAX_CELLS;
}

b32 mapdata_cell(u32 index, f32* out_x, f32* out_z, f32* out_size)
{
    if (index >= s_count) {
        return 0;
    }
    u16 idx = s_order[index];
    i32 col = idx % MAPDATA_GRID;
    i32 row = idx / MAPDATA_GRID;
    *out_x = s_origin_x + ((f32)col + 0.5f) * s_cell;
    *out_z = s_origin_z + ((f32)row + 0.5f) * s_cell;
    *out_size = s_cell;
    return 1;
}
