#include "platform/window.h"
#include "platform/gl_loader.h"
#include "core/log.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace anom {
namespace {
void glfw_error_callback(int error, const char* description)
{
    log_error("glfw: (%d) %s", error, description);
}

void GL_APIENTRY gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity,
                                   GLsizei length, const GLchar* message, const void* user_param)
{
    (void)source;
    (void)type;
    (void)id;
    (void)length;
    (void)user_param;
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        return;
    }
    if (severity == GL_DEBUG_SEVERITY_HIGH) {
        log_error("gl: %s", message);
    } else {
        log_warn("gl: %s", message);
    }
}

}

Window::~Window()
{
    destroy();
}

Window* Window::from_handle(GLFWwindow* window)
{
    return static_cast<Window*>(glfwGetWindowUserPointer(window));
}

void Window::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;
    Window* self = from_handle(window);
    if (!self || action == GLFW_REPEAT) {
        return;
    }
    self->input_.set_key(key, action == GLFW_PRESS);
}

void Window::mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    (void)mods;
    Window* self = from_handle(window);
    if (!self) {
        return;
    }
    self->input_.set_mouse_button(button, action == GLFW_PRESS);
}

void Window::cursor_pos_callback(GLFWwindow* window, double x, double y)
{
    Window* self = from_handle(window);
    if (!self) {
        return;
    }
    self->input_.set_mouse_pos(static_cast<f32>(x), static_cast<f32>(y));
}

void Window::scroll_callback(GLFWwindow* window, double dx, double dy)
{
    (void)dx;
    Window* self = from_handle(window);
    if (!self) {
        return;
    }
    self->input_.add_scroll(static_cast<f32>(dy));
}

void Window::char_callback(GLFWwindow* window, unsigned int codepoint)
{
    Window* self = from_handle(window);
    if (!self) {
        return;
    }
    self->input_.push_char(codepoint);
}

bool Window::create(const char* title, i32 width, i32 height)
{
    destroy();

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        log_error("window: glfwInit failed");
        return false;
    }
    glfw_owned_ = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

    handle_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!handle_) {
        log_error("window: creation failed (OpenGL 4.6 core required)");
        destroy();
        return false;
    }

    glfwSetWindowUserPointer(handle_, this);
    glfwMakeContextCurrent(handle_);
    glfwSwapInterval(1);

    glfwSetKeyCallback(handle_, key_callback);
    glfwSetMouseButtonCallback(handle_, mouse_button_callback);
    glfwSetCursorPosCallback(handle_, cursor_pos_callback);
    glfwSetScrollCallback(handle_, scroll_callback);
    glfwSetCharCallback(handle_, char_callback);

    if (!gl_loader_init()) {
        log_error("window: failed to load required OpenGL functions");
        destroy();
        return false;
    }

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(gl_debug_callback, nullptr);

    log_info("gl vendor:   %s", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    log_info("gl renderer: %s", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    log_info("gl version:  %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    return true;
}

void Window::destroy()
{
    if (handle_) {
        glfwDestroyWindow(handle_);
        handle_ = nullptr;
    }
    if (glfw_owned_) {
        glfwTerminate();
        glfw_owned_ = false;
    }
    cursor_captured_ = false;
}

bool Window::should_close() const
{
    return handle_ && glfwWindowShouldClose(handle_);
}

void Window::request_close()
{
    if (handle_) {
        glfwSetWindowShouldClose(handle_, GLFW_TRUE);
    }
}

void Window::poll()
{
    input_.begin_frame();
    glfwPollEvents();
    input_.finish_frame();
}

void Window::swap_buffers()
{
    if (handle_) {
        glfwSwapBuffers(handle_);
    }
}

void Window::set_title(const char* title)
{
    if (handle_) {
        glfwSetWindowTitle(handle_, title);
    }
}

FramebufferSize Window::framebuffer_size() const
{
    FramebufferSize size{0, 0};
    if (handle_) {
        glfwGetFramebufferSize(handle_, &size.width, &size.height);
    }
    return size;
}

void Window::set_vsync(bool on)
{
    glfwSwapInterval(on ? 1 : 0);
}

void Window::set_cursor_captured(bool captured)
{
    if (!handle_ || cursor_captured_ == captured) {
        return;
    }
    cursor_captured_ = captured;
    glfwSetInputMode(handle_, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (captured && glfwRawMouseMotionSupported()) {
        glfwSetInputMode(handle_, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    input_.resync_mouse();
}

}
