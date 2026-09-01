#ifndef __QUARK_GL_SHADER__
#define __QUARK_GL_SHADER__

#include "../QuarkIRenderer.hpp"

#include <glad/glad.h>

namespace qc {

class QuarkGLShader {
public:
    QuarkGLShader() = default;

    static GLuint CompileGLShader(GLenum type, const char* source);
    static GLuint CreateDefaultProgram();
    static void BindProgram(GLuint program);
    static void UnbindProgram();
    static void SetUniform2f(GLuint program, const char* name, float x, float y);
    static void SetUniform1i(GLuint program, const char* name, int value);
    static int GetUniformLocation(GLuint program, const char* name);

private:
    static const char* kVS2D;
    static const char* kFS2D;
};

} // namespace qc

#endif // __QUARK_GL_SHADER__
