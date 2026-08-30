#include "world/destination.h"

namespace anom {
namespace {

constexpr Destination kDestinations[] = {
    {"testzone", "REDLINE FLATS", "assets/zones/testzone",
     "ORIGIN. GARAGE, RELAY R-4, SERVICE ROAD.", 0.0f},
    {"touge", "KAMIYAMA PASS", "assets/zones/touge",
     "SURVEYED. 3.3 KM CLIMB, NINE SWITCHBACKS, 240 M GAIN.", 412.0f},
    {"quarry", "COLD QUARRY", {}, "NO SURVEY DATA. LAST TRAFFIC CYCLE 04.", 880.0f},
    {"junction", "MILE 9 JUNCTION", {}, "NO SURVEY DATA. COORDINATES UNVERIFIED.", 1240.0f},
};

} // namespace

std::span<const Destination> destinations()
{
    return {kDestinations, sizeof(kDestinations) / sizeof(kDestinations[0])};
}

i32 destination_index(std::string_view id)
{
    const std::span<const Destination> all = destinations();
    for (u32 i = 0; i < all.size(); i++) {
        if (all[i].id == id) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

} // namespace anom
