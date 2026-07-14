#include "platform/gl_loader.h"
#include "core/log.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define GLFN(ret, name, params) PFN_##name name;
GL_FUNCTION_LIST
#undef GLFN

b32 gl_loader_init(void)
{
    b32 all_loaded = 1;
#define GLFN(ret, name, params) \
    name = (PFN_##name)glfwGetProcAddress(#name); \
    if (!name) { \
        log_error("gl_loader: missing %s", #name); \
        all_loaded = 0; \
    }
    GL_FUNCTION_LIST
#undef GLFN
    return all_loaded;
}
