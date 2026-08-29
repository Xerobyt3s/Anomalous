// Proves the third-party wiring actually links, not just that it configures.
//
// The vendored glfw3.lib is a prebuilt static library carrying DEFAULTLIB:"MSVCRT",
// i.e. built /MD. The old MSBuild project compiled Debug as /MDd and then force-ignored
// MSVCRT to paper over the mismatch, putting two CRT instances in one image. We select
// /MD everywhere instead, so this test failing to link is the signal that the runtime
// choice in CMakeLists.txt has drifted.
//
// glfwGetVersion needs neither glfwInit nor a context, so this stays headless.

#include "test.h"

#include "platform/platform_info.h"

TEST(link, glfw_resolves_against_our_crt)
{
    const anom::GlfwVersion v = anom::glfw_version();

    // Any successful link gives a sane major version; a stub would give zeroes.
    CHECK(v.major >= 3);
    CHECK(v.minor >= 0);
    CHECK(v.revision >= 0);
}

TEST(link, glfw_version_string_is_populated)
{
    const std::string_view s = anom::glfw_version_string();
    CHECK(!s.empty());
    CHECK(s.find('.') != std::string_view::npos);
}
