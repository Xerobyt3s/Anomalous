#pragma once

#include "core/fixed_string.h"
#include "core/types.h"
#include "math/vmath.h"
#include "physics/gravity_field.h"
#include "world/islands/island_shape.h"

#include <string_view>
#include <vector>

namespace anom {

class Arena;
class GravityField;
class Heightfield;
class PhysWorld;
class Terrain;
class World;

inline constexpr std::string_view kIslandMaterial = "island_proc";
inline constexpr f32 kIslandMaxRadius = 16.0f;
inline constexpr f32 kLinkDefaultWidth = 8.0f;
inline constexpr i32 kLinkGround = -1;

enum class DebrisKind : u8 {
    Rock,
    Turf,
};

struct DebrisInstance {
    DebrisKind kind = DebrisKind::Rock;
    u16 variant = 0;
    Vec3 pos{};
    Quat rot = quat_identity();
    f32 scale = 1.0f;
    Vec3 bob_dir{0.0f, 1.0f, 0.0f};
    f32 bob_amp = 0.0f;
    f32 bob_rate = 0.0f;
    Vec3 spin_axis{0.0f, 1.0f, 0.0f};
    f32 spin_rate = 0.0f;
    f32 phase = 0.0f;
    bool walkway = false;
    bool solid = false;
};

struct BuiltIsland {
    FixedString<32> name;
    Vec3 pos{};
    Quat rot = quat_identity();
    IslandParams params;
    bool crater = false;
    BakedMesh mesh;
    std::vector<Vec3> collision;
    std::vector<f32> grass;
};

struct BuiltLink {
    u32 from = 0;
    i32 to = kLinkGround;
    f32 width = kLinkDefaultWidth;
    GravityPath path;
    std::vector<Vec3> collision;
};

struct HazeSphere {
    Vec3 centre{};
    f32 radius = 0.0f;
};

struct CraterStamp {
    f32 x = 0.0f;
    f32 z = 0.0f;
    f32 radius = 0.0f;
    f32 depth = 0.0f;
};

class IslandField {
public:
    static constexpr u32 kRockVariants = 8;
    static constexpr u32 kTurfVariants = 6;
    static constexpr u32 kGrassRes = 192;
    static constexpr f32 kGrassExtent = 24.0f;
    static constexpr u32 kMaxHaze = 4;
    static constexpr f32 kHazeMargin = 40.0f;
    static constexpr u32 kMaxCraters = 32;
    static constexpr f32 kIslandGap = 3.0f;
    static constexpr f32 kLinkGap = 2.0f;
    static constexpr f32 kDebrisSolidBand = 3.5f;

    void build(const World& world, const Heightfield& hf, const Terrain* terrain = nullptr);
    void clear();
    void add_collision(PhysWorld& phys) const;
    void fill_gravity(GravityField& field) const;

    static u32 collect_craters(const World& world, CraterStamp* out, u32 max);
    static u64 signature(const World& world);
    static Mat4 debris_transform(const DebrisInstance& d, f32 time);

    const std::vector<BuiltIsland>& islands() const { return islands_; }
    const std::vector<BuiltLink>& links() const { return links_; }
    const std::vector<DebrisInstance>& debris() const { return debris_; }
    const BakedMesh& rock_variant(u32 i) const { return rocks_[i]; }
    const BakedMesh& turf_variant(u32 i) const { return turfs_[i]; }
    const HazeSphere* haze() const { return haze_; }
    u32 haze_count() const { return haze_count_; }
    u32 generation() const { return generation_; }
    u64 checksum() const;
    f64 build_ms() const { return build_ms_; }
    const std::vector<FixedString<64>>& rejected() const { return rejected_; }
    u32 solid_debris() const;

private:
    void build_variants();
    void build_link(BuiltLink& link, const Heightfield& hf, Vec3 ground_point, bool has_ground_point,
                    const std::vector<IslandShape>& shapes);
    void scatter_island_debris(const BuiltIsland& island, u32 index);
    void scatter_link_debris(const BuiltLink& link, u32 index);
    void build_haze();
    void settle_debris(const Heightfield& hf);
    i32 island_clear(const IslandShape& shape, Vec3 pos, Quat rot, const IslandParams& params,
                      const std::vector<IslandShape>& shapes) const;
    bool link_clear(const BuiltLink& link, const std::vector<IslandShape>& shapes, FixedString<64>& why) const;
    FixedString<64> link_name(const BuiltLink& link) const;
    void reject(std::string_view what, std::string_view why);

    std::vector<BuiltIsland> islands_;
    std::vector<BuiltLink> links_;
    std::vector<DebrisInstance> debris_;
    BakedMesh rocks_[kRockVariants];
    BakedMesh turfs_[kTurfVariants];
    std::vector<Vec3> rock_hulls_[kRockVariants];
    std::vector<Vec3> turf_hulls_[kTurfVariants];
    f32 rock_reach_[kRockVariants] = {};
    f32 turf_reach_[kTurfVariants] = {};
    std::vector<FixedString<64>> rejected_;
    bool variants_built_ = false;
    const Terrain* terrain_ = nullptr;
    HazeSphere haze_[kMaxHaze];
    u32 haze_count_ = 0;
    u32 generation_ = 0;
    f64 build_ms_ = 0.0;
};

void islands_rebuild(IslandField& field, World& world, Terrain& terrain, PhysWorld& phys,
                     Arena& statics_arena, Arena& scratch);

} // namespace anom
