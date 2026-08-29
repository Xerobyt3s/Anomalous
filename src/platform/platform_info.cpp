#include "platform/platform_info.h"

#include <GLFW/glfw3.h>

namespace anom {

GlfwVersion glfw_version()
{
    GlfwVersion v{};
    glfwGetVersion(&v.major, &v.minor, &v.revision);
    return v;
}

std::string_view glfw_version_string()
{
    // Points into GLFW's own static storage, which outlives everything.
    return glfwGetVersionString();
}

} // namespace anom
