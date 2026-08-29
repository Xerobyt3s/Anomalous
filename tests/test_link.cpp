#include "test.h"

#include "platform/platform_info.h"

TEST(link, glfw_resolves_against_our_crt)
{
    const anom::GlfwVersion v = anom::glfw_version();
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
