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
#define GL_BLEND 0x0BE2
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

#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_BACK 0x0405
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_RED 0x1903
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_RGB8 0x8051
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_REPEAT 0x2901
#define GL_RGBA8 0x8058
#define GL_RGBA16F 0x881A
#define GL_ALWAYS 0x0207
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_R8 0x8229
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_DYNAMIC_STORAGE_BIT 0x0100
#define GL_TRIANGLE_STRIP 0x0005
#define GL_POINTS 0x0000
#define GL_COLOR 0x1800
#define GL_DEPTH 0x1801
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_NONE 0
#define GL_CLAMP_TO_BORDER 0x812D
#define GL_TEXTURE_BORDER_COLOR 0x1004
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#define GL_PROGRAM_POINT_SIZE 0x8642
#define GL_ONE 1
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_TEXTURE_BASE_LEVEL 0x813C
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_R32F 0x822E
#define GL_FRONT_AND_BACK 0x0408
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02

typedef void (GL_APIENTRY *GlDebugCallback)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* user_param);

#define GL_FUNCTION_LIST \
    GLFN(void, glClear, (GLbitfield mask)) \
    GLFN(void, glClearColor, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)) \
    GLFN(void, glViewport, (GLint x, GLint y, GLsizei width, GLsizei height)) \
    GLFN(void, glEnable, (GLenum cap)) \
    GLFN(void, glDisable, (GLenum cap)) \
    GLFN(const GLubyte*, glGetString, (GLenum name)) \
    GLFN(void, glDebugMessageCallback, (GlDebugCallback callback, const void* user_param)) \
    GLFN(void, glDebugMessageControl, (GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint* ids, GLboolean enabled)) \
    GLFN(GLuint, glCreateShader, (GLenum type)) \
    GLFN(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length)) \
    GLFN(void, glCompileShader, (GLuint shader)) \
    GLFN(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint* params)) \
    GLFN(void, glGetShaderInfoLog, (GLuint shader, GLsizei buf_size, GLsizei* length, GLchar* info_log)) \
    GLFN(void, glDeleteShader, (GLuint shader)) \
    GLFN(GLuint, glCreateProgram, (void)) \
    GLFN(void, glAttachShader, (GLuint program, GLuint shader)) \
    GLFN(void, glDetachShader, (GLuint program, GLuint shader)) \
    GLFN(void, glLinkProgram, (GLuint program)) \
    GLFN(void, glGetProgramiv, (GLuint program, GLenum pname, GLint* params)) \
    GLFN(void, glGetProgramInfoLog, (GLuint program, GLsizei buf_size, GLsizei* length, GLchar* info_log)) \
    GLFN(void, glDeleteProgram, (GLuint program)) \
    GLFN(void, glUseProgram, (GLuint program)) \
    GLFN(void, glCreateBuffers, (GLsizei n, GLuint* buffers)) \
    GLFN(void, glNamedBufferStorage, (GLuint buffer, GLsizeiptr size, const void* data, GLbitfield flags)) \
    GLFN(void, glNamedBufferSubData, (GLuint buffer, GLintptr offset, GLsizeiptr size, const void* data)) \
    GLFN(void, glDeleteBuffers, (GLsizei n, const GLuint* buffers)) \
    GLFN(void, glBindBufferBase, (GLenum target, GLuint index, GLuint buffer)) \
    GLFN(void, glCreateVertexArrays, (GLsizei n, GLuint* arrays)) \
    GLFN(void, glDeleteVertexArrays, (GLsizei n, const GLuint* arrays)) \
    GLFN(void, glBindVertexArray, (GLuint array)) \
    GLFN(void, glVertexArrayVertexBuffer, (GLuint vao, GLuint binding_index, GLuint buffer, GLintptr offset, GLsizei stride)) \
    GLFN(void, glEnableVertexArrayAttrib, (GLuint vao, GLuint index)) \
    GLFN(void, glVertexArrayAttribFormat, (GLuint vao, GLuint attrib_index, GLint size, GLenum type, GLboolean normalized, GLuint relative_offset)) \
    GLFN(void, glVertexArrayAttribBinding, (GLuint vao, GLuint attrib_index, GLuint binding_index)) \
    GLFN(void, glCreateTextures, (GLenum target, GLsizei n, GLuint* textures)) \
    GLFN(void, glTextureStorage2D, (GLuint texture, GLsizei levels, GLenum internal_format, GLsizei width, GLsizei height)) \
    GLFN(void, glTextureSubImage2D, (GLuint texture, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels)) \
    GLFN(void, glTextureParameteri, (GLuint texture, GLenum pname, GLint param)) \
    GLFN(void, glTextureParameterfv, (GLuint texture, GLenum pname, const GLfloat* params)) \
    GLFN(void, glNamedFramebufferDrawBuffer, (GLuint framebuffer, GLenum buf)) \
    GLFN(void, glNamedFramebufferReadBuffer, (GLuint framebuffer, GLenum src)) \
    GLFN(void, glCopyImageSubData, (GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ, GLuint dstName, GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei srcWidth, GLsizei srcHeight, GLsizei srcDepth)) \
    GLFN(void, glBindTextureUnit, (GLuint unit, GLuint texture)) \
    GLFN(void, glDeleteTextures, (GLsizei n, const GLuint* textures)) \
    GLFN(void, glPixelStorei, (GLenum pname, GLint param)) \
    GLFN(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count)) \
    GLFN(void, glDrawElements, (GLenum mode, GLsizei count, GLenum type, const void* indices)) \
    GLFN(void, glBlendFunc, (GLenum sfactor, GLenum dfactor)) \
    GLFN(void, glDepthMask, (GLboolean flag)) \
    GLFN(void, glDepthFunc, (GLenum func)) \
    GLFN(void, glLineWidth, (GLfloat width)) \
    GLFN(void, glCullFace, (GLenum mode)) \
    GLFN(void, glVertexArrayElementBuffer, (GLuint vao, GLuint buffer)) \
    GLFN(void, glProgramUniformMatrix4fv, (GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat* value)) \
    GLFN(void, glProgramUniform4f, (GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)) \
    GLFN(void, glProgramUniform1f, (GLuint program, GLint location, GLfloat v0)) \
    GLFN(void, glProgramUniform2f, (GLuint program, GLint location, GLfloat v0, GLfloat v1)) \
    GLFN(void, glProgramUniform4fv, (GLuint program, GLint location, GLsizei count, const GLfloat* value)) \
    GLFN(void, glProgramUniform1i, (GLuint program, GLint location, GLint v0)) \
    GLFN(void, glGenerateTextureMipmap, (GLuint texture)) \
    GLFN(void, glProgramUniform3f, (GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2)) \
    GLFN(void, glCreateFramebuffers, (GLsizei n, GLuint* framebuffers)) \
    GLFN(void, glDeleteFramebuffers, (GLsizei n, const GLuint* framebuffers)) \
    GLFN(void, glNamedFramebufferTexture, (GLuint framebuffer, GLenum attachment, GLuint texture, GLint level)) \
    GLFN(GLenum, glCheckNamedFramebufferStatus, (GLuint framebuffer, GLenum target)) \
    GLFN(void, glClearNamedFramebufferfv, (GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLfloat* value)) \
    GLFN(void, glBindFramebuffer, (GLenum target, GLuint framebuffer)) \
    GLFN(void, glVertexArrayBindingDivisor, (GLuint vao, GLuint binding_index, GLuint divisor)) \
    GLFN(void, glDrawArraysInstanced, (GLenum mode, GLint first, GLsizei count, GLsizei instance_count)) \
    GLFN(void, glDrawElementsInstanced, (GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instance_count)) \
    GLFN(GLint, glGetUniformLocation, (GLuint program, const GLchar* name)) \
    GLFN(void, glPolygonMode, (GLenum face, GLenum mode)) \
    GLFN(void, glReadPixels, (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void* pixels)) \
    GLFN(void, glFinish, (void))

#define GLFN(ret, name, params) typedef ret (GL_APIENTRY *PFN_##name) params; extern PFN_##name name;
GL_FUNCTION_LIST
#undef GLFN

bool gl_loader_init();
