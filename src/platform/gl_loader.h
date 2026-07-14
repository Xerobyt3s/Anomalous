#pragma once

#include "core/types.h"

#define GL_APIENTRY __stdcall

typedef u32 GLenum;
typedef u32 GLbitfield;
typedef u32 GLuint;
typedef i32 GLint;
typedef i32 GLsizei;
typedef u8  GLboolean;
typedef u8  GLubyte;
typedef f32 GLfloat;
typedef f64 GLdouble;
typedef char GLchar;
typedef i64 GLsizeiptr;
typedef i64 GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_DONT_CARE 0x1100
#define GL_DEBUG_OUTPUT 0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS 0x8242
#define GL_DEBUG_SEVERITY_HIGH 0x9146
#define GL_DEBUG_SEVERITY_MEDIUM 0x9147
#define GL_DEBUG_SEVERITY_LOW 0x9148
#define GL_DEBUG_SEVERITY_NOTIFICATION 0x826B

typedef void (GL_APIENTRY *GlDebugCallback)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* user_param);

#define GL_FUNCTION_LIST \
    GLFN(void, glClear, (GLbitfield mask)) \
    GLFN(void, glClearColor, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)) \
    GLFN(void, glViewport, (GLint x, GLint y, GLsizei width, GLsizei height)) \
    GLFN(void, glEnable, (GLenum cap)) \
    GLFN(void, glDisable, (GLenum cap)) \
    GLFN(const GLubyte*, glGetString, (GLenum name)) \
    GLFN(void, glDebugMessageCallback, (GlDebugCallback callback, const void* user_param)) \
    GLFN(void, glDebugMessageControl, (GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint* ids, GLboolean enabled))

#define GLFN(ret, name, params) typedef ret (GL_APIENTRY *PFN_##name) params; extern PFN_##name name;
GL_FUNCTION_LIST
#undef GLFN

b32 gl_loader_init(void);
