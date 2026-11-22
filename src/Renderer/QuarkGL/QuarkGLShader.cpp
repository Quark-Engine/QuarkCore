#include "QuarkGLShader.hpp"

#include "QuarkGLDevice.hpp"

#include <stdexcept>
#include <string>

namespace qci
{
    const char* QuarkGLShader::ms_pVS2D = R"(
        #version 330 core

        layout(location=0) in vec2 aPos;
        layout(location=1) in vec2 aUV;
        layout(location=2) in vec4 aColor;

        out vec2 vUV;
        out vec4 vColor;

        uniform vec2 uScreenSize;

        void main() {
            vec2 ndc = (aPos / uScreenSize) * 2.0 - 1.0;
            ndc.y = -ndc.y;
            gl_Position = vec4(ndc, 0.0, 1.0);
            vUV = aUV;
            vColor = aColor;
        }
    )";

    const char* QuarkGLShader::ms_pFS2D = R"(
        #version 330 core

        in vec2 vUV;
        in vec4 vColor;

        out vec4 FragColor;

        uniform sampler2D uTexture;

        void main() {
            FragColor = texture(uTexture, vUV) * vColor;
        }
    )";

    GLuint QuarkGLShader::CompileGLShader(GLenum type, const char* pSource)
    {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &pSource, nullptr);
        glCompileShader(shader);

        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char aLog[1024] = {};
            glGetShaderInfoLog(shader, sizeof(aLog), nullptr, aLog);
            glDeleteShader(shader);
            TraceLog(LogLevel::Error, "SHADER"
                                      "[OpenGL] Shader compile error: %s",
                     aLog);
            throw std::runtime_error(std::string("Shader compile: ") + aLog);
        }

        return shader;
    }

    GLuint QuarkGLShader::CreateDefaultProgram()
    {
        GLuint vs = CompileGLShader(GL_VERTEX_SHADER, ms_pVS2D);
        GLuint fs = CompileGLShader(GL_FRAGMENT_SHADER, ms_pFS2D);
        GLuint program = glCreateProgram();

        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);

        glDeleteShader(vs);
        glDeleteShader(fs);

        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char aLog[1024] = {};
            glGetProgramInfoLog(program, sizeof(aLog), nullptr, aLog);
            glDeleteProgram(program);
            throw std::runtime_error(std::string("Program link: ") + aLog);
        }

        return program;
    }

    void QuarkGLShader::BindProgram(GLuint program)
    {
        if (program != 0)
        {
            glUseProgram(program);
        }
    }

    void QuarkGLShader::UnbindProgram()
    {
        glUseProgram(0);
    }

    void QuarkGLShader::SetUniform2f(GLuint program, const char* pName, float x, float y)
    {
        if (!program || !pName)
        {
            return;
        }
        glUseProgram(program);
        const GLint loc = glGetUniformLocation(program, pName);
        if (loc >= 0)
        {
            glUniform2f(loc, x, y);
        }
    }

    void QuarkGLShader::SetUniform1i(GLuint program, const char* pName, int value)
    {
        if (!program || !pName)
        {
            return;
        }
        glUseProgram(program);
        const GLint loc = glGetUniformLocation(program, pName);
        if (loc >= 0)
        {
            glUniform1i(loc, value);
        }
    }

    int QuarkGLShader::GetUniformLocation(GLuint program, const char* pName)
    {
        if (!program || !pName)
        {
            return -1;
        }
        return glGetUniformLocation(program, pName);
    }
} // qci
