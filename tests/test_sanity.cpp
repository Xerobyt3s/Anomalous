// P0 sanity: proves the harness runs, the macros behave, and anomalous_core links into
// the test binary. Replaced by real coverage in P1 (math, arena, pool, config).

#include "test.h"

#include "core/types.h"
#include "core/version.h"

TEST(sanity, harness_runs)
{
    CHECK(true);
}

TEST(sanity, check_near_absolute)
{
    CHECK_NEAR(1.0, 1.0 + 1e-9, 1e-6);
}

TEST(sanity, check_near_relative_at_scale)
{
    // Absolute epsilon would fail here; the relative fallback is what makes the
    // tolerance meaningful for world-space physics baselines.
    CHECK_NEAR(1.0e6, 1.0e6 + 0.5, 1e-6);
}

TEST(core, scalar_type_widths)
{
    CHECK(sizeof(u8) == 1);
    CHECK(sizeof(u16) == 2);
    CHECK(sizeof(u32) == 4);
    CHECK(sizeof(u64) == 8);
    CHECK(sizeof(i32) == 4);
    CHECK(sizeof(f32) == 4);
    CHECK(sizeof(f64) == 8);
}

TEST(core, byte_size_helpers)
{
    CHECK(kilobytes(1) == 1024ull);
    CHECK(megabytes(1) == 1048576ull);
    CHECK(gigabytes(1) == 1073741824ull);
    // The original's arena reserve, which overflows a u32 and is why these return u64.
    CHECK(gigabytes(4) == 4294967296ull);
}

TEST(core, version_links_from_core_library)
{
    const anom::Version v = anom::version();
    CHECK(v.major == 0);
    CHECK(v.minor == 1);
    CHECK(v.patch == 0);
    CHECK(anom::version_string() == "0.1.0");
    CHECK(!anom::build_config().empty());
}
