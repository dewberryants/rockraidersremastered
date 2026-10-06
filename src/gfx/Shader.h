#pragma once

namespace rr::gl {

// Compiles and links a vertex/fragment pair; returns 0 on failure (errors are logged).
unsigned CompileProgram(const char* vertexSource, const char* fragmentSource, const char* debugName);

}  // namespace rr::gl
