#pragma once

// Minimal OpenGL 3.3 core loader. Only the entry points the renderer uses are declared;
// they are resolved through SDL_GL_GetProcAddress after the context is created.

#include <SDL_opengl.h>

#ifndef APIENTRYP
#define APIENTRYP APIENTRY*
#endif

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define RR_GL_FUNCTIONS(X)                                                                         \
    X(void, glGenBuffers, GLsizei n, GLuint* buffers)                                              \
    X(void, glDeleteBuffers, GLsizei n, const GLuint* buffers)                                     \
    X(void, glBindBuffer, GLenum target, GLuint buffer)                                            \
    X(void, glBufferData, GLenum target, GLsizeiptr size, const void* data, GLenum usage)          \
    X(void, glBufferSubData, GLenum target, GLintptr offset, GLsizeiptr size, const void* data)    \
    X(void, glGenVertexArrays, GLsizei n, GLuint* arrays)                                          \
    X(void, glDeleteVertexArrays, GLsizei n, const GLuint* arrays)                                 \
    X(void, glBindVertexArray, GLuint array)                                                       \
    X(void, glEnableVertexAttribArray, GLuint index)                                               \
    X(void, glVertexAttribPointer, GLuint index, GLint size, GLenum type, GLboolean normalized,    \
      GLsizei stride, const void* pointer)                                                         \
    X(GLuint, glCreateShader, GLenum type)                                                         \
    X(void, glDeleteShader, GLuint shader)                                                         \
    X(void, glShaderSource, GLuint shader, GLsizei count, const GLchar* const* string,             \
      const GLint* length)                                                                         \
    X(void, glCompileShader, GLuint shader)                                                        \
    X(void, glGetShaderiv, GLuint shader, GLenum pname, GLint* params)                             \
    X(void, glGetShaderInfoLog, GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog)  \
    X(GLuint, glCreateProgram, void)                                                               \
    X(void, glDeleteProgram, GLuint program)                                                       \
    X(void, glAttachShader, GLuint program, GLuint shader)                                         \
    X(void, glLinkProgram, GLuint program)                                                         \
    X(void, glGetProgramiv, GLuint program, GLenum pname, GLint* params)                           \
    X(void, glGetProgramInfoLog, GLuint program, GLsizei bufSize, GLsizei* length,                 \
      GLchar* infoLog)                                                                             \
    X(void, glUseProgram, GLuint program)                                                          \
    X(GLint, glGetUniformLocation, GLuint program, const GLchar* name)                             \
    X(void, glUniform1i, GLint location, GLint v0)                                                 \
    X(void, glUniform1f, GLint location, GLfloat v0)                                               \
    X(void, glUniform2f, GLint location, GLfloat v0, GLfloat v1)                                   \
    X(void, glUniform3f, GLint location, GLfloat v0, GLfloat v1, GLfloat v2)                       \
    X(void, glUniform4f, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)           \
    X(void, glUniformMatrix4fv, GLint location, GLsizei count, GLboolean transpose,                \
      const GLfloat* value)                                                                        \
    X(void, glActiveTexture, GLenum texture)                                                       \
    X(void, glGenerateMipmap, GLenum target)                                                       \
    X(void, glBlendFuncSeparate, GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha,        \
      GLenum dfactorAlpha)                                                                         \
    X(void, glGenFramebuffers, GLsizei n, GLuint* framebuffers)                                    \
    X(void, glDeleteFramebuffers, GLsizei n, const GLuint* framebuffers)                           \
    X(void, glBindFramebuffer, GLenum target, GLuint framebuffer)                                  \
    X(void, glFramebufferTexture2D, GLenum target, GLenum attachment, GLenum textarget,            \
      GLuint texture, GLint level)                                                                 \
    X(void, glGenRenderbuffers, GLsizei n, GLuint* renderbuffers)                                  \
    X(void, glDeleteRenderbuffers, GLsizei n, const GLuint* renderbuffers)                         \
    X(void, glBindRenderbuffer, GLenum target, GLuint renderbuffer)                                \
    X(void, glRenderbufferStorage, GLenum target, GLenum internalformat, GLsizei width,            \
      GLsizei height)                                                                              \
    X(void, glFramebufferRenderbuffer, GLenum target, GLenum attachment,                           \
      GLenum renderbuffertarget, GLuint renderbuffer)                                              \
    X(GLenum, glCheckFramebufferStatus, GLenum target)                                             \
    X(void, glBlitFramebuffer, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0,    \
      GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter)

#define RR_GL_DECLARE(ret, name, ...) \
    typedef ret(APIENTRYP PFN_rr_##name)(__VA_ARGS__); \
    extern PFN_rr_##name rr_##name;
RR_GL_FUNCTIONS(RR_GL_DECLARE)
#undef RR_GL_DECLARE

#ifndef RR_GL_NO_REDIRECT
#define glGenBuffers rr_glGenBuffers
#define glDeleteBuffers rr_glDeleteBuffers
#define glBindBuffer rr_glBindBuffer
#define glBufferData rr_glBufferData
#define glBufferSubData rr_glBufferSubData
#define glGenVertexArrays rr_glGenVertexArrays
#define glDeleteVertexArrays rr_glDeleteVertexArrays
#define glBindVertexArray rr_glBindVertexArray
#define glEnableVertexAttribArray rr_glEnableVertexAttribArray
#define glVertexAttribPointer rr_glVertexAttribPointer
#define glCreateShader rr_glCreateShader
#define glDeleteShader rr_glDeleteShader
#define glShaderSource rr_glShaderSource
#define glCompileShader rr_glCompileShader
#define glGetShaderiv rr_glGetShaderiv
#define glGetShaderInfoLog rr_glGetShaderInfoLog
#define glCreateProgram rr_glCreateProgram
#define glDeleteProgram rr_glDeleteProgram
#define glAttachShader rr_glAttachShader
#define glLinkProgram rr_glLinkProgram
#define glGetProgramiv rr_glGetProgramiv
#define glGetProgramInfoLog rr_glGetProgramInfoLog
#define glUseProgram rr_glUseProgram
#define glGetUniformLocation rr_glGetUniformLocation
#define glUniform1i rr_glUniform1i
#define glUniform1f rr_glUniform1f
#define glUniform2f rr_glUniform2f
#define glUniform3f rr_glUniform3f
#define glUniform4f rr_glUniform4f
#define glUniformMatrix4fv rr_glUniformMatrix4fv
#define glActiveTexture rr_glActiveTexture
#define glGenerateMipmap rr_glGenerateMipmap
#define glBlendFuncSeparate rr_glBlendFuncSeparate
#define glGenFramebuffers rr_glGenFramebuffers
#define glDeleteFramebuffers rr_glDeleteFramebuffers
#define glBindFramebuffer rr_glBindFramebuffer
#define glFramebufferTexture2D rr_glFramebufferTexture2D
#define glGenRenderbuffers rr_glGenRenderbuffers
#define glDeleteRenderbuffers rr_glDeleteRenderbuffers
#define glBindRenderbuffer rr_glBindRenderbuffer
#define glRenderbufferStorage rr_glRenderbufferStorage
#define glFramebufferRenderbuffer rr_glFramebufferRenderbuffer
#define glCheckFramebufferStatus rr_glCheckFramebufferStatus
#define glBlitFramebuffer rr_glBlitFramebuffer
#endif

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

namespace rr::gl {
// Resolves all entry points; returns false (and logs) if any is missing.
bool LoadFunctions();
}  // namespace rr::gl
