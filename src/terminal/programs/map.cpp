#include "terminal/programs/map.h"
#include "carsys/items.h"
#include "core/arena.h"
#include "physics/heightfield.h"
#include "terminal/mapdata.h"
#include "terminal/screen.h"
#include "terminal/shell.h"
#include "terminal/virus.h"
#include "world/terrain.h"

#include <cmath>

namespace anom {
namespace {

constexpr f32 kLidarRevealSpeed = 95.0f;
constexpr f32 kLidarWideRevealSpeed = 950.0f;
constexpr f32 kWideRefresh = 10.0f;
constexpr f32 kSettleTime = 1.4f;
constexpr f32 kSurveyPitch = 0.82f;
constexpr f32 kSurveyDist = 190.0f;
constexpr f32 kSurveyFov = 50.0f * kDegToRad;

constexpr f32 kDownloadTime = 26.0f;
constexpr u32 kDownloadBytes = 96256;

constexpr u32 kMapFileBase = 256;
constexpr u32 kMapFileCellBytes = 24;

constexpr f32 kMarkerStep = 3.4f;
constexpr i32 kMarkerHeight = 14;

f32 ease01(f32 t)
{
    t = f_clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

void MapProgram::init(Arena& arena)
{
    points_ = arena.push_array<TermPoint>(kTermMaxPoints);
    point_count_ = 0;
}

void MapProgram::push_point(f32 x, f32 y, f32 z, f32 shade)
{
    if (!points_ || point_count_ >= kTermMaxPoints) {
        return;
    }
    points_[point_count_++] = TermPoint{x, y, z, shade};
}

void MapProgram::sync_file(const ProgramContext& ctx)
{
    Fs& fs = ctx.shell->fs();
    if (!fs.mounted(kFsDriveA)) {
        return;
    }

    u32 count = map_->count();
    const FsRef root = Fs::root(kFsDriveA);
    FsRef ref = fs.resolve(root, "MAP.DAT");
    if (!fs.valid(ref)) {
        if (file_created_) {
            map_->reset();
            count = 0;
        }
        ref = fs.mkfile_rom(root, "MAP.DAT", {}, kMapFileBase, FsExe::None);
        if (!fs.valid(ref)) {
            saved_count_ = 0;
            disk_full_ = true;
            corrupt_ = false;
            return;
        }
        file_created_ = true;
    }

    FsNode* node = fs.node_mut(ref);
    const u32 desired = kMapFileBase + count * kMapFileCellBytes;
    if (desired > node->size) {
        const u32 room = node->size + fs.free_bytes(kFsDriveA);
        node->size = desired < room ? desired : room;
    }
    saved_count_ = node->size > kMapFileBase ? (node->size - kMapFileBase) / kMapFileCellBytes : 0;
    saved_count_ = saved_count_ > count ? count : saved_count_;
    disk_full_ = node->size < desired;
    corrupt_ = node->corrupted;
}

void MapProgram::enter(const ProgramContext& ctx, const TermView& view)
{
    exit_ = false;
    materialize_ = 0.0f;
    auto_orbit_ = true;
    zoom_ = 1.0f;
    wide_ = false;
    refresh_ = 0.0f;
    sync_file(ctx);
    point_count_ = 0;
    downloading_ = view.bus_state == PORT_LINKED && view.bus_tower && view.tower_breached
                && !download_done_;
    dl_t_ = 0.0f;
}

void MapProgram::add_marker_column(const TermView& view, Vec3 mark, f32 kind)
{
    const Heightfield& hf = view.terrain->heightfield();
    const f32 ground = hf.sample(mark.x, mark.z);
    for (i32 i = 0; i < kMarkerHeight; i++) {
        push_point(mark.x, ground + 2.0f + static_cast<f32>(i) * kMarkerStep, mark.z, kind);
    }
}

void MapProgram::rebuild_lidar(const TermView& view)
{
    const Heightfield& hf = view.terrain->heightfield();
    const f32 cx = std::floor(view.car_pos.x / kLidarSpacing + 0.5f) * kLidarSpacing;
    const f32 cz = std::floor(view.car_pos.z / kLidarSpacing + 0.5f) * kLidarSpacing;
    lidar_center_ = Vec3{cx, 0.0f, cz};

    point_count_ = 0;
    const f32 half = static_cast<f32>(kLidarN - 1) * 0.5f;
    for (i32 r = 0; r < kLidarN; r++) {
        for (i32 c = 0; c < kLidarN; c++) {
            const f32 wx = cx + (static_cast<f32>(c) - half) * kLidarSpacing;
            const f32 wz = cz + (static_cast<f32>(r) - half) * kLidarSpacing;
            const f32 road = lidar_tier_ >= 1 && view.terrain->road_amount(wx, wz) > 0.4f
                               ? 1.0f
                               : 0.0f;
            push_point(wx, hf.sample(wx, wz) + 0.4f, wz, road);
        }
    }

    if (lidar_tier_ >= 1) {
        add_marker_column(view, view.garage_pos, 2.0f);
        if (view.mission_stage == 1 || view.mission_stage == 2) {
            add_marker_column(view, view.mission_pos, 3.0f);
        }
    }
    points_dirty_ = true;
}

void MapProgram::rebuild_lidar_wide(const TermView& view)
{
    const Heightfield& hf = view.terrain->heightfield();
    const u32 cells = map_->count() < saved_count_ ? map_->count() : saved_count_;

    point_count_ = 0;
    u32 noise = 0x51ED2701u;
    for (u32 i = 0; i < cells && point_count_ + 9 < kTermMaxPoints - 64; i++) {
        MapCell cell;
        if (!map_->cell(i, cell)) {
            continue;
        }
        const f32 sub = cell.size / 3.0f;
        for (i32 sr = -1; sr <= 1; sr++) {
            for (i32 sc = -1; sc <= 1; sc++) {
                const f32 wx = cell.x + static_cast<f32>(sc) * sub;
                const f32 wz = cell.z + static_cast<f32>(sr) * sub;
                f32 wy = hf.sample(wx, wz) + 0.4f;
                f32 kind = lidar_tier_ >= 1 && view.terrain->road_amount(wx, wz) > 0.4f ? 1.0f
                                                                                        : 0.0f;
                if (corrupt_) {
                    noise = noise * 1664525u + 1013904223u;
                    wy += static_cast<f32>((noise >> 8) % 61) - 30.0f;
                    kind = static_cast<f32>((noise >> 20) % 4);
                }
                push_point(wx, wy, wz, kind);
            }
        }
    }

    if (lidar_tier_ >= 1) {
        add_marker_column(view, view.garage_pos, 2.0f);
        if (view.mission_stage == 1 || view.mission_stage == 2) {
            add_marker_column(view, view.mission_pos, 3.0f);
        }
    }
    points_dirty_ = true;
}

bool MapProgram::project(const Mat4& vp, Vec3 world, i32& out_row, i32& out_col) const
{
    const f32* m = vp.m;
    const f32 cx = m[0] * world.x + m[4] * world.y + m[8] * world.z + m[12];
    const f32 cy = m[1] * world.x + m[5] * world.y + m[9] * world.z + m[13];
    const f32 cw = m[3] * world.x + m[7] * world.y + m[11] * world.z + m[15];
    if (cw < 0.1f) {
        return false;
    }
    const f32 sx = (cx / cw * 0.5f + 0.5f) * static_cast<f32>(kTermTexW);
    const f32 sy = (1.0f - (cy / cw * 0.5f + 0.5f)) * static_cast<f32>(kTermTexH);
    const i32 col = static_cast<i32>((sx - static_cast<f32>(kTermOriginX))
                                     / static_cast<f32>(kTermCellW));
    const i32 row = static_cast<i32>((sy - static_cast<f32>(kTermOriginY))
                                     / static_cast<f32>(kTermCellH));
    if (col < 0 || col >= static_cast<i32>(kTermCols) || row < 1
        || row >= static_cast<i32>(kTermRows) - 1) {
        return false;
    }
    out_row = row;
    out_col = col;
    return true;
}

bool MapProgram::update_download(const ProgramContext& ctx, const TermView& view, f32 dt)
{
    if (!downloading_) {
        return false;
    }
    if (view.bus_state != PORT_LINKED || !view.bus_tower || !view.tower_breached) {
        downloading_ = false;
        exit_ = true;
        ctx.screen->print("RELAY LINK LOST. TRANSFER ABORTED.\n");
        return true;
    }

    dl_t_ += dt;
    const f32 frac = f_clamp01(dl_t_ / kDownloadTime);
    if (frac >= 1.0f) {
        downloading_ = false;
        sync_file(ctx);
        map_->reveal_radius(view.tower_pos, 620.0f);
        sync_file(ctx);
        download_done_ = true;
        materialize_ = 0.0f;
        point_count_ = 0;
        return false;
    }

    Screen& s = *ctx.screen;
    s.grid_clear();
    s.grid_title("RELAY LINK -- DATA TRANSFER");
    s.grid_text(3, 4, TC_DIM, "SOURCE  RELAY R-4 :: SURVEY CACHE");
    s.grid_text(4, 4, TC_DIM, "TARGET  A:\\MAP.DAT");
    s.grid_text(6, 4, TC_GREEN, "%7u / %7u BYTES",
                static_cast<u32>(frac * static_cast<f32>(kDownloadBytes)), kDownloadBytes);
    s.grid_text(6, 40, TC_DIM, "%d B/S",
                3200 + static_cast<i32>(std::fmod(ctx.blink * 7.3f, 1.0f) * 900.0f));

    constexpr i32 kWidth = 46;
    s.grid_text(9, 6, TC_DIM, "[");
    const i32 filled = static_cast<i32>(frac * static_cast<f32>(kWidth));
    for (i32 i = 0; i < kWidth; i++) {
        const u16 glyph = i < filled ? '#' : (i == filled ? '>' : '.');
        s.grid_put(9, 7 + i, glyph, i < filled ? TC_GREEN : TC_DIM);
    }
    s.grid_text(9, 7 + kWidth, TC_DIM, "]");
    s.grid_text(10, (static_cast<i32>(kTermCols) - 4) / 2, TC_BRIGHT, "%3.0f%%",
                static_cast<f64>(frac * 100.0f));

    if (std::fmod(ctx.blink, 0.9f) < 0.55f) {
        s.grid_text(13, 4, TC_AMBER, "TRANSFER IN PROGRESS -- DO NOT DISCONNECT");
    }
    s.grid_text(static_cast<i32>(kTermRows) - 1, 1, TC_DIM, "[Q] ABORT");
    return true;
}

void MapProgram::update(const ProgramContext& ctx, const TermView& view, f32 dt)
{
    if (update_download(ctx, view, dt)) {
        return;
    }

    f32 sweep_rate = 1.9f;
    if (virus_->active()) {
        sweep_rate = 1.9f + 14.0f * f_max(0.0f, std::sin(ctx.blink * 3.1f) - 0.6f);
    }
    sweep_ = f_wrap_angle(sweep_ + dt * sweep_rate);
    materialize_ += dt;
    if (!wide_) {
        if (view.orbit != 0.0f) {
            auto_orbit_ = false;
            yaw_ += view.orbit * 0.9f * dt;
        } else if (auto_orbit_) {
            yaw_ += 0.12f * dt;
        }
        yaw_ = f_wrap_angle(yaw_);
    }
    zoom_ = f_clamp(zoom_ - view.zoom * 0.8f * dt, 0.45f, 1.9f);

    const Heightfield& hf = view.terrain->heightfield();
    if (wide_) {
        refresh_ -= dt;
        if (refresh_ <= 0.0f) {
            refresh_ = kWideRefresh;
            lidar_tier_ = view.antenna_tier;
            sync_file(ctx);
            rebuild_lidar_wide(view);
            materialize_ = 0.0f;
        }
    } else {
        const f32 moved_x = view.car_pos.x - lidar_center_.x;
        const f32 moved_z = view.car_pos.z - lidar_center_.z;
        if (point_count_ == 0 || moved_x * moved_x + moved_z * moved_z > 12.0f * 12.0f
            || lidar_tier_ != view.antenna_tier) {
            lidar_tier_ = view.antenna_tier;
            rebuild_lidar(view);
        }
    }
    ctx.scene->points = points_;
    ctx.scene->point_count = point_count_;
    ctx.scene->points_dirty = points_dirty_;
    ctx.scene->point_reveal = materialize_ * (wide_ ? kLidarWideRevealSpeed : kLidarRevealSpeed);
    ctx.scene->point_center = view.car_pos;
    ctx.scene->sweep = sweep_;
    points_dirty_ = false;

    const f32 settle = ease01(materialize_ / kSettleTime);
    Vec3 target;
    f32 dist = 0.0f;
    f32 pitch = 0.0f;
    f32 fov = kSurveyFov;
    f32 far_plane = 3200.0f;
    f32 cam_yaw = yaw_;
    if (wide_) {
        const f32 ext_x = hf.span_x();
        const f32 ext_z = hf.span_z();
        target = Vec3{hf.origin().x + ext_x * 0.5f, 0.0f, hf.origin().z + ext_z * 0.5f};
        target.y = hf.sample(target.x, target.z);
        dist = f_max(ext_x, ext_z) * 0.80f * zoom_;
        pitch = 1.15f;
        far_plane = dist * 2.5f + 500.0f;
        cam_yaw = 0.0f;
    } else {
        target = view.car_pos + Vec3{0.0f, 4.0f, 0.0f};
        dist = (kSurveyDist + 200.0f * (1.0f - settle)) * zoom_;
        pitch = kSurveyPitch + 0.30f * (1.0f - settle);
        fov = kSurveyFov - (13.0f * kDegToRad) * (1.0f - settle);
    }

    const Vec3 eye = target
                   + Vec3{std::sin(cam_yaw) * std::cos(pitch) * dist, std::sin(pitch) * dist,
                          std::cos(cam_yaw) * std::cos(pitch) * dist};
    const Mat4 vp = mat4_perspective(fov,
                                     static_cast<f32>(kTermTexW) / static_cast<f32>(kTermTexH),
                                     0.5f, far_plane)
                  * mat4_look_at(eye, target, Vec3{0.0f, 1.0f, 0.0f});
    ctx.scene->vp3d = vp;

    Screen& s = *ctx.screen;
    s.grid_clear();
    if (wide_) {
        s.grid_title("TERRAIN SURVEY -- WIDE RANGE");
        const f32 coverage = 100.0f * static_cast<f32>(map_->count())
                           / static_cast<f32>(MapData::total());
        const u32 file_kb = (kMapFileBase + saved_count_ * kMapFileCellBytes + 1023) / 1024;
        s.grid_text(1, 1, TC_DIM, "COVERAGE %4.1f%%   MAP.DAT %3uK", static_cast<f64>(coverage),
                    file_kb);
        s.grid_text(1, static_cast<i32>(kTermCols) - 14, TC_DIM, "NEXT SWEEP %2.0fS",
                    static_cast<f64>(f_max(refresh_, 0.0f)));
        if (corrupt_) {
            if (std::fmod(ctx.blink, 0.8f) < 0.55f) {
                s.grid_text(static_cast<i32>(kTermRows) - 2, 1, TC_RED,
                            "MAP DATA CORRUPTED - SURVEY UNRELIABLE");
            }
        } else if (disk_full_) {
            s.grid_text(static_cast<i32>(kTermRows) - 2, 1, TC_AMBER,
                        "DISK FULL - NEW SURVEY DATA NOT SAVED");
        }
    } else {
        s.grid_title("TERRAIN SURVEY -- IMMEDIATE");
        s.grid_text(1, 1, TC_DIM, "GRID %+05.0f/%+05.0f", static_cast<f64>(view.car_pos.x),
                    static_cast<f64>(view.car_pos.z));
        const std::string_view ant = antenna_variant_name(view.antenna_tier);
        s.grid_text(1, 22, view.antenna_tier == 0 ? TC_AMBER : TC_DIM, "ANT: %.*s%s",
                    static_cast<int>(ant.size()), ant.data(),
                    view.antenna_tier == 0 ? " (TERRAIN ONLY)" : "");
        const f32 span = static_cast<f32>(kLidarN - 1) * kLidarSpacing * 0.5f;
        s.grid_text(1, static_cast<i32>(kTermCols) - 11, TC_DIM, "RANGE %3.0fM",
                    static_cast<f64>(span));
        if (view.antenna_tier >= 2) {
            if (virus_->active()) {
                const i32 fake = 1
                               + static_cast<i32>(std::fmod(ctx.blink * 0.37f, 1.0f) * 40.0f);
                s.grid_text(static_cast<i32>(kTermRows) - 2, 1, TC_RED,
                            "ANOMALY SCAN: %d CONTACTS CLOSING", fake);
            } else {
                s.grid_text(static_cast<i32>(kTermRows) - 2, 1,
                            std::fmod(ctx.blink, 1.4f) < 0.9f ? TC_GREEN : TC_DIM,
                            "ANOMALY SCAN: NO CONTACTS");
            }
        }
    }

    if (materialize_ < 1.6f && std::fmod(ctx.blink, 0.5f) < 0.32f) {
        s.grid_text(static_cast<i32>(kTermRows) / 2, static_cast<i32>(kTermCols) / 2 - 6,
                    TC_BRIGHT, "SCANNING ...");
    }

    i32 row = 0;
    i32 col = 0;
    if (project(vp, view.car_pos + Vec3{0.0f, 3.0f, 0.0f}, row, col)) {
        s.grid_text(row, col, TC_BRIGHT, "@");
    }
    if (virus_->active()) {
        for (i32 g = 0; g < 3; g++) {
            const f32 gf = static_cast<f32>(g);
            const f32 ang = ctx.blink * (0.31f + 0.17f * gf) + gf * 2.3f;
            const f32 radius = 40.0f + 25.0f * gf;
            const Vec3 ghost = view.car_pos
                             + Vec3{std::sin(ang) * radius, 3.0f, std::cos(ang * 1.3f) * radius};
            if (std::fmod(ctx.blink * (1.0f + 0.4f * gf), 1.9f) < 0.7f
                && project(vp, ghost, row, col)) {
                s.grid_text(row, col, TC_RED, "@");
            }
        }
    }

    if (view.antenna_tier >= 1) {
        const Vec3 marks[2] = {view.garage_pos, view.mission_pos};
        static const char* kLabels[2] = {"G", "X"};
        const bool show[2] = {true, view.mission_stage == 1 || view.mission_stage == 2};
        for (u32 m = 0; m < 2; m++) {
            if (!show[m] || (m == 1 && std::fmod(ctx.blink, 0.7f) > 0.45f)) {
                continue;
            }
            Vec3 top = marks[m];
            top.y = hf.sample(top.x, top.z) + 52.0f;
            if (project(vp, top, row, col)) {
                s.grid_text(row, col, TC_AMBER, "%s", kLabels[m]);
            }
        }
    }

    const i32 last_row = static_cast<i32>(kTermRows) - 1;
    if (wide_) {
        s.grid_text(last_row, 1, TC_DIM, "[M] IMMEDIATE   ^ v ZOOM");
    } else {
        s.grid_text(last_row, 1, TC_DIM, "[M] WIDE RANGE   < > ORBIT   ^ v ZOOM   %s",
                    auto_orbit_ ? "AUTO" : "    ");
    }
    s.grid_text(last_row, static_cast<i32>(kTermCols) - 20, TC_DIM, "@ CAR  G GAR  X OBJ");
}

void MapProgram::key_char(const ProgramContext& ctx, const TermView& view, char c)
{
    (void)view;
    if (c != 'M' || downloading_) {
        return;
    }
    wide_ = !wide_;
    materialize_ = 0.0f;
    refresh_ = 0.0f;
    zoom_ = 1.0f;
    auto_orbit_ = true;
    point_count_ = 0;
    ctx.screen->click();
}

bool MapProgram::key(const ProgramContext& ctx, const TermView& view, TermKey k)
{
    (void)view;
    if (k != TermKey::Quit && k != TermKey::Escape && k != TermKey::Enter) {
        return false;
    }
    if (downloading_) {
        if (k == TermKey::Enter) {
            return false;
        }
        downloading_ = false;
        ctx.screen->print("TRANSFER ABORTED.\n");
    }
    return true;
}

} // namespace anom
