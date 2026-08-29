#pragma once

#include "terminal/program.h"

namespace anom {

class Arena;
class MapData;
class Virus;

inline constexpr i32 kLidarN = 112;
inline constexpr f32 kLidarSpacing = 3.6f;

class MapProgram : public Program {
public:
    MapProgram(MapData& map, const Virus& virus) : map_(&map), virus_(&virus) {}

    void init(Arena& arena);

    void enter(const ProgramContext& ctx, const TermView& view) override;
    void update(const ProgramContext& ctx, const TermView& view, f32 dt) override;
    bool key(const ProgramContext& ctx, const TermView& view, TermKey key) override;
    void key_char(const ProgramContext& ctx, const TermView& view, char c) override;
    bool wants_exit() const override { return exit_; }

    void clear_download_done() { download_done_ = false; }

private:
    void sync_file(const ProgramContext& ctx);
    bool update_download(const ProgramContext& ctx, const TermView& view, f32 dt);
    void rebuild_lidar(const TermView& view);
    void rebuild_lidar_wide(const TermView& view);
    void add_marker_column(const TermView& view, Vec3 mark, f32 kind);
    void push_point(f32 x, f32 y, f32 z, f32 shade);
    bool project(const Mat4& vp, Vec3 world, i32& out_row, i32& out_col) const;

    MapData* map_ = nullptr;
    const Virus* virus_ = nullptr;

    TermPoint* points_ = nullptr;
    u32 point_count_ = 0;
    bool points_dirty_ = false;
    Vec3 lidar_center_;
    i32 lidar_tier_ = -1;

    f32 yaw_ = 0.0f;
    f32 zoom_ = 1.0f;
    f32 materialize_ = 0.0f;
    f32 refresh_ = 0.0f;
    f32 sweep_ = 0.0f;
    bool auto_orbit_ = true;
    bool wide_ = false;
    bool disk_full_ = false;
    bool corrupt_ = false;

    bool downloading_ = false;
    bool download_done_ = false;
    f32 dl_t_ = 0.0f;

    u32 saved_count_ = 0;
    bool file_created_ = false;
    bool exit_ = false;
};

} // namespace anom
