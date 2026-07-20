#include "platform/platform.h"
#include "platform/gl_loader.h"
#include "core/arena.h"
#include "core/log.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <string.h>
#include <io.h>
#include <wchar.h>
#include <sys/types.h>
#include <sys/stat.h>

#define PLATFORM_WPATH_MAX 640

u32 platform_utf8_to_wide(const char* utf8, u16* out, u32 out_count)
{
    u32 n = 0;
    const u8* p = (const u8*)utf8;
    while (*p && n + 2 < out_count) {
        u32 cp = 0;
        if (p[0] < 0x80) {
            cp = p[0];
            p += 1;
        } else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            cp = ((u32)(p[0] & 0x1F) << 6) | (u32)(p[1] & 0x3F);
            p += 2;
        } else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            cp = ((u32)(p[0] & 0x0F) << 12) | ((u32)(p[1] & 0x3F) << 6) | (u32)(p[2] & 0x3F);
            p += 3;
        } else if ((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80
                   && (p[3] & 0xC0) == 0x80) {
            cp = ((u32)(p[0] & 0x07) << 18) | ((u32)(p[1] & 0x3F) << 12)
               | ((u32)(p[2] & 0x3F) << 6) | (u32)(p[3] & 0x3F);
            p += 4;
        } else {
            cp = 0xFFFD;
            p += 1;
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out[n++] = (u16)(0xD800 + (cp >> 10));
            out[n++] = (u16)(0xDC00 + (cp & 0x3FF));
        } else {
            out[n++] = (u16)cp;
        }
    }
    out[n] = 0;
    return n;
}

static u32 wide_to_utf8(const u16* w, char* out, u32 out_count)
{
    u32 n = 0;
    for (u32 i = 0; w[i]; i++) {
        u32 cp = w[i];
        if (cp >= 0xD800 && cp < 0xDC00 && w[i + 1] >= 0xDC00 && w[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (w[i + 1] - 0xDC00);
            i++;
        }
        if (cp < 0x80) {
            if (n + 1 >= out_count) { break; }
            out[n++] = (char)cp;
        } else if (cp < 0x800) {
            if (n + 2 >= out_count) { break; }
            out[n++] = (char)(0xC0 | (cp >> 6));
            out[n++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            if (n + 3 >= out_count) { break; }
            out[n++] = (char)(0xE0 | (cp >> 12));
            out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[n++] = (char)(0x80 | (cp & 0x3F));
        } else {
            if (n + 4 >= out_count) { break; }
            out[n++] = (char)(0xF0 | (cp >> 18));
            out[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[n++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[n] = 0;
    return n;
}

void* platform_fopen(const char* path, const char* mode)
{
    wchar_t wpath[PLATFORM_WPATH_MAX];
    wchar_t wmode[16];
    if (!platform_utf8_to_wide(path, (u16*)wpath, PLATFORM_WPATH_MAX)) {
        return 0;
    }
    u32 m = 0;
    for (; mode[m] && m < 15; m++) {
        wmode[m] = (wchar_t)mode[m];
    }
    wmode[m] = 0;
    return _wfopen(wpath, wmode);
}

static GLFWwindow* s_window;
static GameInput s_input;
static f32 s_prev_mouse_x;
static f32 s_prev_mouse_y;
static b32 s_first_mouse_sample = 1;
static b32 s_cursor_captured;

static void glfw_error_callback(int error, const char* description)
{
    log_error("glfw: (%d) %s", error, description);
}

static void glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void)window; (void)scancode; (void)mods;
    if (key < 0 || key >= MAX_KEYS) {
        return;
    }
    if (action == GLFW_PRESS) {
        s_input.key_down[key] = 1;
        s_input.key_pressed[key] = 1;
    } else if (action == GLFW_RELEASE) {
        s_input.key_down[key] = 0;
        s_input.key_released[key] = 1;
    }
}

static void glfw_mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    (void)window; (void)mods;
    if (button < 0 || button >= MAX_MOUSE_BUTTONS) {
        return;
    }
    if (action == GLFW_PRESS) {
        s_input.mouse_down[button] = 1;
        s_input.mouse_pressed[button] = 1;
    } else if (action == GLFW_RELEASE) {
        s_input.mouse_down[button] = 0;
        s_input.mouse_released[button] = 1;
    }
}

static void glfw_cursor_pos_callback(GLFWwindow* window, double x, double y)
{
    (void)window;
    s_input.mouse_x = (f32)x;
    s_input.mouse_y = (f32)y;
}

static void glfw_scroll_callback(GLFWwindow* window, double dx, double dy)
{
    (void)window; (void)dx;
    s_input.scroll_dy += (f32)dy;
}

#define CHAR_QUEUE_MAX 64
static u32 s_char_queue[CHAR_QUEUE_MAX];
static u32 s_char_head;
static u32 s_char_tail;

static void glfw_char_callback(GLFWwindow* window, unsigned int codepoint)
{
    (void)window;
    u32 next = (s_char_head + 1) % CHAR_QUEUE_MAX;
    if (next != s_char_tail) {
        s_char_queue[s_char_head] = codepoint;
        s_char_head = next;
    }
}

u32 platform_next_char(void)
{
    if (s_char_tail == s_char_head) {
        return 0;
    }
    u32 c = s_char_queue[s_char_tail];
    s_char_tail = (s_char_tail + 1) % CHAR_QUEUE_MAX;
    return c;
}

static void GL_APIENTRY gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* user_param)
{
    (void)source; (void)type; (void)id; (void)length; (void)user_param;
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        return;
    }
    if (severity == GL_DEBUG_SEVERITY_HIGH) {
        log_error("gl: %s", message);
    } else {
        log_warn("gl: %s", message);
    }
}

b32 platform_init(const char* title, i32 width, i32 height)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        log_error("platform: glfwInit failed");
        return 0;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

    s_window = glfwCreateWindow(width, height, title, 0, 0);
    if (!s_window) {
        log_error("platform: window creation failed (OpenGL 4.6 core required)");
        glfwTerminate();
        return 0;
    }

    glfwMakeContextCurrent(s_window);
    glfwSwapInterval(1);

    glfwSetKeyCallback(s_window, glfw_key_callback);
    glfwSetMouseButtonCallback(s_window, glfw_mouse_button_callback);
    glfwSetCursorPosCallback(s_window, glfw_cursor_pos_callback);
    glfwSetScrollCallback(s_window, glfw_scroll_callback);
    glfwSetCharCallback(s_window, glfw_char_callback);

    if (!gl_loader_init()) {
        log_error("platform: failed to load required OpenGL functions");
        glfwDestroyWindow(s_window);
        glfwTerminate();
        return 0;
    }

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(gl_debug_callback, 0);

    log_info("gl vendor:   %s", (const char*)glGetString(GL_VENDOR));
    log_info("gl renderer: %s", (const char*)glGetString(GL_RENDERER));
    log_info("gl version:  %s", (const char*)glGetString(GL_VERSION));
    return 1;
}

void platform_shutdown(void)
{
    if (s_window) {
        glfwDestroyWindow(s_window);
        s_window = 0;
    }
    glfwTerminate();
}

b32 platform_should_close(void)
{
    return (b32)glfwWindowShouldClose(s_window);
}

void platform_request_close(void)
{
    glfwSetWindowShouldClose(s_window, GLFW_TRUE);
}

void platform_poll_input(void)
{
    for (i32 i = 0; i < MAX_KEYS; i++) {
        s_input.key_pressed[i] = 0;
        s_input.key_released[i] = 0;
    }
    for (i32 i = 0; i < MAX_MOUSE_BUTTONS; i++) {
        s_input.mouse_pressed[i] = 0;
        s_input.mouse_released[i] = 0;
    }
    s_input.scroll_dy = 0.0f;

    s_prev_mouse_x = s_input.mouse_x;
    s_prev_mouse_y = s_input.mouse_y;

    glfwPollEvents();

    if (s_first_mouse_sample) {
        s_prev_mouse_x = s_input.mouse_x;
        s_prev_mouse_y = s_input.mouse_y;
        s_first_mouse_sample = 0;
    }
    s_input.mouse_dx = s_input.mouse_x - s_prev_mouse_x;
    s_input.mouse_dy = s_input.mouse_y - s_prev_mouse_y;
}

void platform_swap_buffers(void)
{
    glfwSwapBuffers(s_window);
}

void platform_set_title(const char* title)
{
    glfwSetWindowTitle(s_window, title);
}

void platform_framebuffer_size(i32* out_width, i32* out_height)
{
    glfwGetFramebufferSize(s_window, out_width, out_height);
}

f64 platform_time_now(void)
{
    return glfwGetTime();
}

const GameInput* platform_input(void)
{
    return &s_input;
}

void platform_set_cursor_captured(b32 captured)
{
    if (s_cursor_captured == captured) {
        return;
    }
    s_cursor_captured = captured;
    glfwSetInputMode(s_window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (captured && glfwRawMouseMotionSupported()) {
        glfwSetInputMode(s_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    s_first_mouse_sample = 1;
}

b32 platform_cursor_captured(void)
{
    return s_cursor_captured;
}

FileData platform_read_entire_file(struct Arena* arena, const char* path)
{
    FileData result = {0};
    FILE* file = (FILE*)platform_fopen(path, "rb");
    if (!file) {
        log_warn("file: could not open %s", path);
        return result;
    }
    fseek(file, 0, SEEK_END);
    i64 size = _ftelli64(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        fclose(file);
        return result;
    }
    u8* data = arena_push_array(arena, u8, (u64)size + 1);
    u64 bytes_read = fread(data, 1, (u64)size, file);
    fclose(file);
    if (bytes_read != (u64)size) {
        log_warn("file: short read on %s", path);
        return result;
    }
    data[size] = 0;
    result.data = data;
    result.size = (u64)size;
    return result;
}

i64 platform_file_mtime(const char* path)
{
    wchar_t wpath[PLATFORM_WPATH_MAX];
    if (!platform_utf8_to_wide(path, (u16*)wpath, PLATFORM_WPATH_MAX)) {
        return 0;
    }
    struct _stat64 info;
    if (_wstat64(wpath, &info) != 0) {
        return 0;
    }
    return (i64)info.st_mtime;
}

u32 platform_list_dir(const char* path, PlatformDirEntry* out, u32 max_count)
{
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s/*", path);
    wchar_t wpattern[PLATFORM_WPATH_MAX];
    if (!platform_utf8_to_wide(pattern, (u16*)wpattern, PLATFORM_WPATH_MAX)) {
        return 0;
    }
    struct _wfinddata64_t fd;
    intptr_t handle = _wfindfirst64(wpattern, &fd);
    if (handle == -1) {
        return 0;
    }
    u32 n = 0;
    do {
        if (wcscmp(fd.name, L".") == 0 || wcscmp(fd.name, L"..") == 0) {
            continue;
        }
        if (n >= max_count) {
            break;
        }
        wide_to_utf8((const u16*)fd.name, out[n].name, sizeof(out[n].name));
        out[n].is_dir = (fd.attrib & _A_SUBDIR) != 0;
        n++;
    } while (_wfindnext64(handle, &fd) == 0);
    _findclose(handle);
    return n;
}
