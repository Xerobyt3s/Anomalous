#include "world/scenes.h"

namespace anom {
namespace {

constexpr SceneEntry kScenes[] = {
    {"testzone", "Redline Flats", "assets/zones/testzone"},
    {"touge", "Kamiyama Pass", "assets/zones/touge"},
    {"yard", "Arena: the Yard", "assets/zones/yard"},
    {"alleys", "Arena: Alleys", "assets/zones/alleys"},
    {"range", "Test range", "assets/zones/range"},
};

}

std::span<const SceneEntry> scenes()
{
    return {kScenes, sizeof(kScenes) / sizeof(kScenes[0])};
}

i32 scene_index(std::string_view id)
{
    for (u32 i = 0; i < scenes().size(); i++) {
        if (kScenes[i].id == id) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

i32 scene_index_of_dir(std::string_view zone_dir)
{
    for (u32 i = 0; i < scenes().size(); i++) {
        if (kScenes[i].zone_dir == zone_dir) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

}
