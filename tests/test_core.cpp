#include "test.h"

#include "core/arena.h"
#include "core/config.h"
#include "core/fixed_string.h"
#include "core/pool.h"
#include "core/rng.h"

using namespace anom;

namespace {

struct Widget {
    u32 id;
    f32 value;
};

struct Gadget {
    u32 id;
};

} // namespace

TEST(arena, push_is_aligned_and_zeroed)
{
    Arena arena(megabytes(1));
    CHECK(arena.valid());

    auto* a = arena.push<u8>();
    auto* b = arena.push<u64>();
    CHECK(a != nullptr);
    CHECK(b != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(b) % alignof(u64) == 0);
    CHECK(*b == 0);

    auto* block = arena.push_array<u32>(64);
    for (u32 i = 0; i < 64; i++) {
        CHECK(block[i] == 0);
    }
}

TEST(arena, scope_rewinds_on_destruction)
{
    Arena arena(megabytes(1));
    arena.push_array<u8>(100);
    const u64 before = arena.used();
    {
        ArenaScope scope(arena);
        arena.push_array<u8>(4096);
        CHECK(arena.used() > before);
    }
    CHECK(arena.used() == before);
}

TEST(arena, nested_scopes_rewind_in_order)
{
    Arena arena(megabytes(1));
    const u64 base = arena.used();
    {
        ArenaScope outer(arena);
        arena.push_array<u8>(256);
        const u64 mid = arena.used();
        {
            ArenaScope inner(arena);
            arena.push_array<u8>(256);
            CHECK(arena.used() > mid);
        }
        CHECK(arena.used() == mid);
    }
    CHECK(arena.used() == base);
}

TEST(arena, reused_memory_is_re_zeroed)
{
    Arena arena(megabytes(1));
    {
        ArenaScope scope(arena);
        auto* first = arena.push_array<u32>(16);
        for (u32 i = 0; i < 16; i++) {
            first[i] = 0xDEADBEEFu;
        }
    }
    auto* second = arena.push_array<u32>(16);
    for (u32 i = 0; i < 16; i++) {
        CHECK(second[i] == 0);
    }
}

TEST(arena, move_transfers_ownership)
{
    Arena source(megabytes(1));
    source.push_array<u8>(128);
    const u64 used = source.used();

    Arena moved = std::move(source);
    CHECK(moved.valid());
    CHECK(moved.used() == used);
    CHECK(!source.valid());
}

TEST(pool, alloc_get_free_lifecycle)
{
    Arena arena(megabytes(1));
    Pool<Widget> pool;
    pool.init(arena, 8, "widgets");

    CHECK(pool.count() == 0);
    CHECK(pool.capacity() == 8);

    const Handle<Widget> h = pool.alloc();
    CHECK(h.valid());
    CHECK(pool.count() == 1);

    Widget* w = pool.get(h);
    CHECK(w != nullptr);
    CHECK(w->id == 0);
    w->id = 42;
    CHECK(pool.get(h)->id == 42);

    pool.free(h);
    CHECK(pool.count() == 0);
    CHECK(pool.get(h) == nullptr);
}

TEST(pool, stale_handle_is_rejected_after_reuse)
{
    Arena arena(megabytes(1));
    Pool<Widget> pool;
    pool.init(arena, 4, "widgets");

    const Handle<Widget> first = pool.alloc();
    pool.free(first);
    const Handle<Widget> second = pool.alloc();

    CHECK(first.idx == second.idx);
    CHECK(first.gen != second.gen);
    CHECK(pool.get(first) == nullptr);
    CHECK(pool.get(second) != nullptr);
}

TEST(pool, freshly_allocated_slot_is_zeroed)
{
    Arena arena(megabytes(1));
    Pool<Widget> pool;
    pool.init(arena, 4, "widgets");

    const Handle<Widget> first = pool.alloc();
    pool.get(first)->id = 0xABCDu;
    pool.get(first)->value = 1.5f;
    pool.free(first);

    const Handle<Widget> second = pool.alloc();
    CHECK(pool.get(second)->id == 0);
    CHECK(pool.get(second)->value == 0.0f);
}

TEST(pool, exhaustion_returns_invalid_handle)
{
    Arena arena(megabytes(1));
    Pool<Widget> pool;
    pool.init(arena, 2, "widgets");

    CHECK(pool.alloc().valid());
    CHECK(pool.alloc().valid());

    const Handle<Widget> overflow = pool.alloc();
    CHECK(!overflow.valid());
    CHECK(pool.get(overflow) == nullptr);
    CHECK(pool.count() == 2);
}

TEST(pool, live_indices_track_allocations)
{
    Arena arena(megabytes(1));
    Pool<Widget> pool;
    pool.init(arena, 8, "widgets");

    Handle<Widget> handles[5];
    for (i32 i = 0; i < 5; i++) {
        handles[i] = pool.alloc();
        pool.get(handles[i])->id = static_cast<u32>(i);
    }
    CHECK(pool.live_indices().size() == 5);

    pool.free(handles[1]);
    pool.free(handles[3]);
    CHECK(pool.live_indices().size() == 3);

    u32 seen = 0;
    for (const u32 idx : pool.live_indices()) {
        const Widget* w = pool.at(idx);
        CHECK(w != nullptr);
        CHECK(w->id != 1);
        CHECK(w->id != 3);
        seen++;
    }
    CHECK(seen == 3);
}

TEST(pool, double_free_is_ignored)
{
    Arena arena(megabytes(1));
    Pool<Widget> pool;
    pool.init(arena, 4, "widgets");

    const Handle<Widget> h = pool.alloc();
    pool.free(h);
    pool.free(h);
    CHECK(pool.count() == 0);

    const Handle<Widget> next = pool.alloc();
    CHECK(next.valid());
    CHECK(pool.count() == 1);
}

TEST(pool, clear_releases_everything)
{
    Arena arena(megabytes(1));
    Pool<Widget> pool;
    pool.init(arena, 8, "widgets");
    for (i32 i = 0; i < 6; i++) {
        pool.alloc();
    }
    pool.clear();
    CHECK(pool.count() == 0);
    CHECK(pool.live_indices().empty());
    CHECK(pool.alloc().valid());
}

TEST(pool, handles_are_distinct_types)
{
    static_assert(!std::is_convertible_v<Handle<Widget>, Handle<Gadget>>);
    static_assert(!std::is_same_v<Handle<Widget>, Handle<Gadget>>);
    CHECK(true);
}

TEST(rng, is_deterministic_for_a_seed)
{
    Rng a(1234);
    Rng b(1234);
    for (i32 i = 0; i < 64; i++) {
        CHECK(a.next_u32() == b.next_u32());
    }

    Rng c(4321);
    Rng d(1234);
    CHECK(c.next_u32() != d.next_u32());
}

TEST(rng, f32_stays_in_unit_range)
{
    Rng rng(99);
    for (i32 i = 0; i < 4096; i++) {
        const f32 v = rng.next_f32();
        CHECK(v >= 0.0f);
        CHECK(v < 1.0f);
    }
}

TEST(rng, range_respects_bounds)
{
    Rng rng(7);
    for (i32 i = 0; i < 4096; i++) {
        const f32 v = rng.range(-2.0f, 5.0f);
        CHECK(v >= -2.0f);
        CHECK(v <= 5.0f);
        const u32 u = rng.range_u32(3, 9);
        CHECK(u >= 3);
        CHECK(u <= 9);
    }
}

TEST(fixed_string, assign_truncates_and_reports)
{
    FixedString<8> s;
    CHECK(s.empty());
    CHECK(s.assign("short"));
    CHECK(s == "short");
    CHECK(s.size() == 5);

    CHECK(!s.assign("far too long for eight"));
    CHECK(s.size() == 7);
    CHECK(s == "far too");
}

TEST(fixed_string, append_and_format)
{
    FixedString<32> s;
    s.assign("a");
    CHECK(s.append("bc"));
    CHECK(s == "abc");

    FixedString<32> f;
    CHECK(f.format("%d-%s", 7, "ok"));
    CHECK(f == "7-ok");
}

TEST(fixed_string, format_overflow_is_reported)
{
    FixedString<8> f;
    CHECK(!f.format("%s", "0123456789"));
    CHECK(f.size() == 7);
}

TEST(config, parses_sections_and_types)
{
    Arena arena(megabytes(1));
    Config cfg;
    const char* text =
        "top = 1.5\n"
        "[body]\n"
        "mass = 1400 # comment\n"
        "com = 0.1 -0.2 0.3\n"
        "\n"
        "[gears]\n"
        "ratios = 3.6 2.1 1.4\n"
        "count = 3\n";

    CHECK(cfg.parse(arena, text));
    CHECK(cfg.count() == 5);
    CHECK_NEAR(cfg.get_f32("top", 0.0f), 1.5f, 1e-6);
    CHECK_NEAR(cfg.get_f32("body.mass", 0.0f), 1400.0f, 1e-6);
    CHECK(cfg.get_i32("gears.count", 0) == 3);
    CHECK_NEAR(cfg.get_f32("missing", 7.0f), 7.0f, 1e-6);

    const Vec3 com = cfg.get_vec3("body.com", Vec3{});
    CHECK_NEAR(com.x, 0.1f, 1e-5);
    CHECK_NEAR(com.y, -0.2f, 1e-5);
    CHECK_NEAR(com.z, 0.3f, 1e-5);

    f32 ratios[8] = {};
    CHECK(cfg.get_f32_list("gears.ratios", ratios) == 3);
    CHECK_NEAR(ratios[0], 3.6f, 1e-5);
    CHECK_NEAR(ratios[2], 1.4f, 1e-5);
}

TEST(config, missing_keys_return_fallbacks)
{
    Arena arena(megabytes(1));
    Config cfg;
    CHECK(cfg.parse(arena, "a = 1\n"));
    CHECK(!cfg.has("b"));
    CHECK(cfg.get_str("b", "fallback") == "fallback");
    CHECK(cfg.get_i32("b", -5) == -5);
    const Vec3 v = cfg.get_vec3("b", Vec3{1.0f, 2.0f, 3.0f});
    CHECK_NEAR(v.x, 1.0f, 1e-6);
    CHECK_NEAR(v.z, 3.0f, 1e-6);
}

TEST(config, malformed_lines_are_skipped)
{
    Arena arena(megabytes(1));
    Config cfg;
    const char* text =
        "good = 1\n"
        "this line has no equals\n"
        "[unterminated\n"
        "= 5\n"
        "also_good = 2\n";
    CHECK(cfg.parse(arena, text));
    CHECK(cfg.count() == 2);
    CHECK(cfg.get_i32("good", 0) == 1);
    CHECK(cfg.get_i32("also_good", 0) == 2);
}

TEST(config, vec3_keeps_fallback_components_when_short)
{
    Arena arena(megabytes(1));
    Config cfg;
    CHECK(cfg.parse(arena, "partial = 9.0\n"));
    const Vec3 v = cfg.get_vec3("partial", Vec3{1.0f, 2.0f, 3.0f});
    CHECK_NEAR(v.x, 9.0f, 1e-6);
    CHECK_NEAR(v.y, 2.0f, 1e-6);
    CHECK_NEAR(v.z, 3.0f, 1e-6);
}

TEST(config, handles_crlf_and_trailing_content)
{
    Arena arena(megabytes(1));
    Config cfg;
    CHECK(cfg.parse(arena, "[s]\r\nkey = 12\r\nlast = 3"));
    CHECK(cfg.get_i32("s.key", 0) == 12);
    CHECK(cfg.get_i32("s.last", 0) == 3);
}

TEST(config, empty_input_parses_to_nothing)
{
    Arena arena(megabytes(1));
    Config cfg;
    CHECK(cfg.parse(arena, ""));
    CHECK(cfg.count() == 0);
}
