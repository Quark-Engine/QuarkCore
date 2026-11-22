#ifndef __QUARK_GL_SHADER__
#define __QUARK_GL_SHADER__

#include "../QuarkIRenderer.hpp"

#include <glad/glad.h>

namespace qci
{
    class QuarkGLShader
    {
    public:
        QuarkGLShader() = default;

        static GLuint CompileGLShader(GLenum type, const char* pSource);
        static GLuint CreateDefaultProgram();
        static void BindProgram(GLuint program);
        static void UnbindProgram();
        static void SetUniform2f(GLuint program, const char* pName, float x, float y);
        static void SetUniform1i(GLuint program, const char* pName, int value);
        static int GetUniformLocation(GLuint program, const char* pName);

    private:
        static const char* ms_pVS2D;
        static const char* ms_pFS2D;
    };
} // qci

#endif // __QUARK_GL_SHADER__
