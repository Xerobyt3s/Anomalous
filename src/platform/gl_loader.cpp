#include "platform/gl_loader.h"
#include "core/log.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define GLFN(ret, name, params) PFN_##name name;
GL_FUNCTION_LIST
#undef GLFN

bool gl_loader_init()
{
    bool all_loaded = true;
#define GLFN(ret, name, params)                       \
    name = (PFN_##name)glfwGetProcAddress(#name);     \
    if (!name) {                                      \
        log_error("gl_loader: missing %s", #name);    \
        all_loaded = false;                           \
    }
    GL_FUNCTION_LIST
#undef GLFN
    return all_loaded;
}
