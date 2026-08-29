#include "test.h"

#include "core/arena.h"
#include "platform/clock.h"
#include "platform/filesystem.h"
#include "platform/input.h"

#include <cstring>

using namespace anom;

TEST(input, press_and_release_are_edge_triggered)
{
    Input in;

    in.begin_frame();
    in.set_key(static_cast<i32>(Key::W), true);
    in.finish_frame();
    CHECK(in.down(Key::W));
    CHECK(in.pressed(Key::W));
    CHECK(!in.released(Key::W));

    in.begin_frame();
    in.finish_frame();
    CHECK(in.down(Key::W));
    CHECK(!in.pressed(Key::W));

    in.begin_frame();
    in.set_key(static_cast<i32>(Key::W), false);
    in.finish_frame();
    CHECK(!in.down(Key::W));
    CHECK(in.released(Key::W));

    in.begin_frame();
    in.finish_frame();
    CHECK(!in.released(Key::W));
}

TEST(input, out_of_range_keys_are_ignored)
{
    Input in;
    in.begin_frame();
    in.set_key(-1, true);
    in.set_key(Input::kMaxKeys, true);
    in.set_key(99999, true);
    in.finish_frame();
    CHECK(!in.down(Key::None));
}

TEST(input, mouse_buttons_and_scroll)
{
    Input in;
    in.begin_frame();
    in.set_mouse_button(static_cast<i32>(MouseButton::Right), true);
    in.add_scroll(1.5f);
    in.add_scroll(-0.5f);
    in.finish_frame();
    CHECK(in.down(MouseButton::Right));
    CHECK(in.pressed(MouseButton::Right));
    CHECK_NEAR(in.scroll(), 1.0f, 1e-6);

    in.begin_frame();
    in.finish_frame();
    CHECK_NEAR(in.scroll(), 0.0f, 1e-6);
}

TEST(input, first_mouse_sample_has_no_delta)
{
    Input in;
    in.begin_frame();
    in.set_mouse_pos(400.0f, 300.0f);
    in.finish_frame();
    CHECK_NEAR(in.mouse_delta().x, 0.0f, 1e-6);
    CHECK_NEAR(in.mouse_delta().y, 0.0f, 1e-6);

    in.begin_frame();
    in.set_mouse_pos(410.0f, 290.0f);
    in.finish_frame();
    CHECK_NEAR(in.mouse_delta().x, 10.0f, 1e-6);
    CHECK_NEAR(in.mouse_delta().y, -10.0f, 1e-6);
}

TEST(input, resync_suppresses_the_next_delta)
{
    Input in;
    in.begin_frame();
    in.set_mouse_pos(100.0f, 100.0f);
    in.finish_frame();

    in.begin_frame();
    in.set_mouse_pos(110.0f, 110.0f);
    in.finish_frame();
    CHECK_NEAR(in.mouse_delta().x, 10.0f, 1e-6);

    in.resync_mouse();
    in.begin_frame();
    in.set_mouse_pos(900.0f, 900.0f);
    in.finish_frame();
    CHECK_NEAR(in.mouse_delta().x, 0.0f, 1e-6);
}

TEST(input, chars_carry_full_codepoints)
{
    Input in;
    in.begin_frame();
    in.push_char('a');
    in.push_char(0x00E5);
    in.push_char(0x4E2D);
    in.push_char(0x1F600);
    in.finish_frame();

    const std::span<const u32> chars = in.chars();
    CHECK(chars.size() == 4);
    CHECK(chars[0] == 'a');
    CHECK(chars[1] == 0x00E5);
    CHECK(chars[2] == 0x4E2D);
    CHECK(chars[3] == 0x1F600);

    in.begin_frame();
    in.finish_frame();
    CHECK(in.chars().empty());
}

TEST(input, char_queue_overflow_drops_without_corruption)
{
    Input in;
    in.begin_frame();
    for (u32 i = 0; i < Input::kMaxCharsPerFrame * 2; i++) {
        in.push_char('a' + (i % 26));
    }
    in.finish_frame();
    CHECK(in.chars().size() == Input::kMaxCharsPerFrame);
    CHECK(in.chars()[0] == 'a');
}

TEST(unicode, utf8_wide_round_trip)
{
    const char* samples[] = {
        "plain ascii",
        "latin-1 \xC3\xA5\xC3\xA4\xC3\xB6",
        "cjk \xE4\xB8\xAD\xE6\x96\x87",
        "emoji \xF0\x9F\x98\x80",
        "",
    };

    for (const char* sample : samples) {
        wchar_t wide[256];
        char back[256];
        fs::utf8_to_wide(sample, wide);
        fs::wide_to_utf8(wide, back);
        CHECK_MSG(std::strcmp(sample, back) == 0, sample);
    }
}

TEST(unicode, surrogate_pair_encoding)
{
    wchar_t wide[16];
    const u32 written = fs::utf8_to_wide("\xF0\x9F\x98\x80", wide);
    CHECK(written == 2);
    CHECK(wide[0] >= 0xD800 && wide[0] < 0xDC00);
    CHECK(wide[1] >= 0xDC00 && wide[1] < 0xE000);
}

TEST(unicode, truncated_sequence_becomes_replacement)
{
    wchar_t wide[16];
    const std::string_view truncated("\xE4\xB8", 2);
    const u32 written = fs::utf8_to_wide(truncated, wide);
    CHECK(written == 2);
    CHECK(wide[0] == 0xFFFD);
    CHECK(wide[1] == 0xFFFD);
}

TEST(filesystem, reads_a_known_repo_file)
{
    Arena arena(megabytes(4));
    const fs::FileData data = fs::read_entire_file(arena, "CMakeLists.txt");
    CHECK(data.valid());
    CHECK(data.size > 0);
    CHECK(data.text().find("anomalous_core") != std::string_view::npos);
    CHECK(data.data[data.size] == 0);
}

TEST(filesystem, missing_file_returns_invalid)
{
    Arena arena(megabytes(1));
    const fs::FileData data = fs::read_entire_file(arena, "this/does/not/exist.txt");
    CHECK(!data.valid());
    CHECK(data.size == 0);
}

TEST(filesystem, exists_and_mtime)
{
    CHECK(fs::exists("CMakeLists.txt"));
    CHECK(!fs::exists("no_such_file_here.txt"));
    CHECK(fs::file_mtime("CMakeLists.txt") > 0);
    CHECK(fs::file_mtime("no_such_file_here.txt") == 0);
}

TEST(filesystem, list_dir_finds_sources)
{
    fs::DirEntry entries[64];
    const u32 count = fs::list_dir("src/core", entries);
    CHECK(count > 0);

    bool found_types = false;
    for (u32 i = 0; i < count; i++) {
        CHECK(!entries[i].name.empty());
        CHECK(entries[i].name != ".");
        CHECK(entries[i].name != "..");
        if (entries[i].name == "types.h") {
            found_types = true;
        }
    }
    CHECK(found_types);
}

TEST(filesystem, list_dir_marks_directories)
{
    fs::DirEntry entries[64];
    const u32 count = fs::list_dir("src", entries);
    CHECK(count > 0);

    bool found_dir = false;
    for (u32 i = 0; i < count; i++) {
        if (entries[i].name == "core") {
            found_dir = entries[i].is_dir;
        }
    }
    CHECK(found_dir);
}

TEST(filesystem, list_dir_of_missing_path_is_empty)
{
    fs::DirEntry entries[8];
    CHECK(fs::list_dir("no/such/directory", entries) == 0);
}

TEST(clock, advances_monotonically)
{
    const f64 first = time_seconds();
    f64 last = first;
    for (i32 i = 0; i < 1000; i++) {
        const f64 now = time_seconds();
        CHECK(now >= last);
        last = now;
    }
    CHECK(last >= first);
}
