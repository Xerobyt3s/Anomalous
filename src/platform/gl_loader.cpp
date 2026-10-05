#include "platform/gl_loader.h"
#include "core/log.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

bool gl_loader_init()
{
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        log_error("gl_loader: gladLoadGLLoader failed");
        return false;
    }
    return true;
}
