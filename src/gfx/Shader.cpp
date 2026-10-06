#include "gfx/Shader.h"

#include "core/Log.h"
#include "gfx/GL.h"

namespace rr::gl {

namespace {
GLuint Compile(GLenum type, const char* source, const char* debugName) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        RR_ERROR("%s %s shader: %s", debugName, type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
}  // namespace

unsigned CompileProgram(const char* vertexSource, const char* fragmentSource, const char* debugName) {
    GLuint vs = Compile(GL_VERTEX_SHADER, vertexSource, debugName);
    GLuint fs = Compile(GL_FRAGMENT_SHADER, fragmentSource, debugName);
    if (!vs || !fs) return 0;
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        RR_ERROR("%s link: %s", debugName, log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

}  // namespace rr::gl
