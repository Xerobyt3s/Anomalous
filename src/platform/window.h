#pragma once

#include "core/types.h"
#include "platform/input.h"

struct GLFWwindow;

namespace anom {
struct FramebufferSize {
    i32 width;
    i32 height;
};

class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(const char* title, i32 width, i32 height);
    void destroy();

    bool valid() const { return handle_ != nullptr; }
    bool should_close() const;
    void request_close();

    void poll();
    void swap_buffers();

    void set_title(const char* title);
    FramebufferSize framebuffer_size() const;

    void set_cursor_captured(bool captured);
    void set_vsync(bool on);
    bool cursor_captured() const { return cursor_captured_; }

    Input& input() { return input_; }
    GLFWwindow* handle() const { return handle_; }
    const Input& input() const { return input_; }

private:
    static Window* from_handle(GLFWwindow* window);
    static void key_callback(GLFWwindow*, int key, int scancode, int action, int mods);
    static void mouse_button_callback(GLFWwindow*, int button, int action, int mods);
    static void cursor_pos_callback(GLFWwindow*, double x, double y);
    static void scroll_callback(GLFWwindow*, double dx, double dy);
    static void char_callback(GLFWwindow*, unsigned int codepoint);

    GLFWwindow* handle_ = nullptr;
    Input input_;
    bool cursor_captured_ = false;
    bool glfw_owned_ = false;
};

}
