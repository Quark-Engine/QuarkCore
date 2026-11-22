#include "QuarkGL3D.hpp"
#include "../QuarkIRenderer.hpp"

#include <glad/glad.h>

namespace qci
{
    namespace
    {
        static const char* s_pVS3D = R"(
            #version 330 core

            layout(location=0) in vec3 aPosition;
            layout(location=1) in vec3 aNormal;
            layout(location=2) in vec2 aTexCoord;

            out vec3 vFragPos;
            out vec3 vNormal;
            out vec2 vTexCoord;

            uniform mat4 uModel;
            uniform mat4 uView;
            uniform mat4 uProjection;

            void main() {
                vFragPos = vec3(uModel * vec4(aPosition, 1.0));
                vNormal = mat3(uModel) * aNormal;
                vTexCoord = aTexCoord;
                gl_Position = uProjection * uView * vec4(vFragPos, 1.0);
            }
        )";

        static const char* s_pFS3D = R"(
            #version 330 core

            in vec3 vFragPos;
            in vec3 vNormal;
            in vec2 vTexCoord;

            out vec4 FragColor;

            uniform sampler2D uTexture;
            uniform vec4 uColor;

            void main() {
                vec4 tex = texture(uTexture, vTexCoord);
                vec3 result = tex.rgb * uColor.rgb;
                FragColor = vec4(result, tex.a * uColor.a);
            }
        )";

        static void SetUniformMatrix(GLint location, const Mat4& matrix)
        {
            if (location >= 0)
            {
                glUniformMatrix4fv(location, 1, GL_FALSE, matrix.m);
            }
        }

        static void SetUniformColor(GLint location, Color color)
        {
            if (location >= 0)
            {
                glUniform4f(location,
                            color.r / 255.0f,
                            color.g / 255.0f,
                            color.b / 255.0f,
                            color.a / 255.0f);
            }
        }

        static void BindWhiteTexture(const Model3DState& state)
        {
            if (state.m_WhiteTexture != 0)
            {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, state.m_WhiteTexture);
            }
        }
    } // anonymous

    void QuarkGL3D::Init3DState(Model3DState& state)
    {
        if (state.m_Initialized)
        {
            return;
        }

        state.m_Shader3D = Compile3DShader();
        state.m_ModelLoc = glGetUniformLocation(state.m_Shader3D, "uModel");
        state.viewLoc = glGetUniformLocation(state.m_Shader3D, "uView");
        state.projLoc = glGetUniformLocation(state.m_Shader3D, "uProjection");
        state.m_SamplerLoc = glGetUniformLocation(state.m_Shader3D, "uTexture");
        state.lightPosLoc = glGetUniformLocation(state.m_Shader3D, "uLightPos");
        state.colorLoc = glGetUniformLocation(state.m_Shader3D, "uColor");
        state.m_Initialized = true;

        const uint8_t aWhite[4] = {255, 255, 255, 255};
        glGenTextures(1, &state.m_WhiteTexture);
        glBindTexture(GL_TEXTURE_2D, state.m_WhiteTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, aWhite);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        const uint8_t aBlack[4] = {0, 0, 0, 255};
        glGenTextures(1, &state.m_BlackTexture);
        glBindTexture(GL_TEXTURE_2D, state.m_BlackTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, aBlack);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        const uint8_t aFlatNormal[4] = {128, 128, 255, 255};
        glGenTextures(1, &state.m_FlatNormalTexture);
        glBindTexture(GL_TEXTURE_2D, state.m_FlatNormalTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, aFlatNormal);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    GLuint QuarkGL3D::Compile3DShader()
    {
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &s_pVS3D, nullptr);
        glCompileShader(vs);

        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &s_pFS3D, nullptr);
        glCompileShader(fs);

        GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);

        glDeleteShader(vs);
        glDeleteShader(fs);
        return program;
    }

    void QuarkGL3D::Init3DGeometry(Model3DState& state)
    {
        if (state.m_PlaneVAO != 0)
        {
            return;
        }

        auto setup = [](GLuint& vao, GLuint& vbo, GLuint& ebo,
                        const float* pData, size_t dataSize,
                        const unsigned int* pIndices, size_t indexSize)
        {
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glGenBuffers(1, &ebo);

            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(dataSize), pData, GL_STATIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indexSize), pIndices, GL_STATIC_DRAW);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(0));

            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(12));

            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(24));

            glBindVertexArray(0);
        };

        float aPlaneVertices[] =
            {
                -0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
                0.5f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                -0.5f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        unsigned int aPlaneIndices[] = {0, 1, 2, 0, 2, 3};
        state.m_PlaneIndexCount = 6;
        setup(state.m_PlaneVAO, state.planeVBO, state.planeEBO,
              aPlaneVertices, sizeof(aPlaneVertices), aPlaneIndices, sizeof(aPlaneIndices));

        float aCubeVertices[] =
            {
                -0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,
                0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                -0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,
                -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,
                -0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f,
                0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,
                0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f,
                -0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                -0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
                0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                -0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,
                0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
                0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f,
                -0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f,
                0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                -0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                -0.5f, -0.5f, 0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                -0.5f, 0.5f, 0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                -0.5f, 0.5f, -0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        unsigned int aCubeIndices[] =
            {
                0, 1, 2, 0, 2, 3,
                4, 5, 6, 4, 6, 7,
                8, 9, 10, 8, 10, 11,
                12, 13, 14, 12, 14, 15,
                16, 17, 18, 16, 18, 19,
                20, 21, 22, 20, 22, 23};
        state.m_CubeIndexCount = 36;
        setup(state.m_CubeVAO, state.cubeVBO, state.cubeEBO,
              aCubeVertices, sizeof(aCubeVertices), aCubeIndices, sizeof(aCubeIndices));

        std::vector<float> vSphereVertices;
        std::vector<unsigned int> vSphereIndices;
        const int rings = 16;
        const int slices = 16;
        for (int r = 0; r <= rings; ++r)
        {
            float phi = PI * r / rings;
            for (int s = 0; s <= slices; ++s)
            {
                float theta = 2.0f * PI * s / slices;
                float x = sinf(phi) * cosf(theta);
                float y = cosf(phi);
                float z = sinf(phi) * sinf(theta);
                vSphereVertices.insert(vSphereVertices.end(), {x, y, z, x, y, z, static_cast<float>(s) / slices, static_cast<float>(r) / rings});
            }
        }
        for (int r = 0; r < rings; ++r)
        {
            for (int s = 0; s < slices; ++s)
            {
                vSphereIndices.push_back(static_cast<unsigned int>(r * (slices + 1) + s));
                vSphereIndices.push_back(static_cast<unsigned int>((r + 1) * (slices + 1) + s));
                vSphereIndices.push_back(static_cast<unsigned int>((r + 1) * (slices + 1) + (s + 1)));
                vSphereIndices.push_back(static_cast<unsigned int>(r * (slices + 1) + s));
                vSphereIndices.push_back(static_cast<unsigned int>((r + 1) * (slices + 1) + (s + 1)));
                vSphereIndices.push_back(static_cast<unsigned int>(r * (slices + 1) + (s + 1)));
            }
        }
        state.m_SphereIndexCount = static_cast<int>(vSphereIndices.size());
        setup(state.m_SphereVAO, state.sphereVBO, state.sphereEBO,
              vSphereVertices.data(), vSphereVertices.size() * sizeof(float),
              vSphereIndices.data(), vSphereIndices.size() * sizeof(unsigned int));

        auto dynVao = [](GLuint& vao, GLuint& vbo)
        {
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), reinterpret_cast<void*>(offsetof(Vertex3D, position)));

            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), reinterpret_cast<void*>(offsetof(Vertex3D, normal)));

            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), reinterpret_cast<void*>(offsetof(Vertex3D, texCoord)));

            glBindVertexArray(0);
        };

        dynVao(state.m_LineVAO, state.lineVBO);
        dynVao(state.m_TriVAO, state.triVBO);
    }

    void QuarkGL3D::Set3DView(Model3DState& state, const Mat4& view, const Mat4& projection)
    {
        state.m_ViewMatrix = view;
        state.m_ProjectionMatrix = projection;
        if (state.m_Initialized)
        {
            SetUniformMatrix(state.viewLoc, state.m_ViewMatrix);
            SetUniformMatrix(state.projLoc, state.m_ProjectionMatrix);
        }
    }

    Vec3 QuarkGL3D::TransformPoint(const Mat4& matrix, const Vec3& point)
    {
        float x = matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12];
        float y = matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13];
        float z = matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14];
        float w = matrix.m[3] * point.x + matrix.m[7] * point.y + matrix.m[11] * point.z + matrix.m[15];

        if (w != 0.0f)
        {
            x /= w;
            y /= w;
            z /= w;
        }

        return {x, y, z};
    }

    Mat4 QuarkGL3D::ApplyCurrentMatrix(const Mat4& current, const Mat4& transform)
    {
        return current * transform;
    }

    void QuarkGL3D::ApplyDrawState(Model3DState& state, const Mat4& model, Color color, GLuint textureId)
    {
        glUseProgram(state.m_Shader3D);
        SetUniformMatrix(state.viewLoc, state.m_ViewMatrix);
        SetUniformMatrix(state.projLoc, state.m_ProjectionMatrix);
        SetUniformMatrix(state.m_ModelLoc, model);
        SetUniformColor(state.colorLoc, color);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureId != 0 ? textureId : state.m_WhiteTexture);
    }

    void QuarkGL3D::FlushLines3D(Model3DState& state)
    {
        if (state.m_vLineVertices.empty())
        {
            return;
        }

        glUseProgram(state.m_Shader3D);
        SetUniformMatrix(state.viewLoc, state.m_ViewMatrix);
        SetUniformMatrix(state.projLoc, state.m_ProjectionMatrix);
        SetUniformMatrix(state.m_ModelLoc, Mat4::identity());
        SetUniformColor(state.colorLoc, state.m_CurrentLineColor);
        BindWhiteTexture(state);

        glBindVertexArray(state.m_LineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, state.lineVBO);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(state.m_vLineVertices.size() * sizeof(Vertex3D)), state.m_vLineVertices.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(state.m_vLineVertices.size()));
        RecordDebugRenderDraw(state.m_vLineVertices.size());
        glBindVertexArray(0);
        state.m_vLineVertices.clear();
    }

    void QuarkGL3D::FlushTriangles3D(Model3DState& state)
    {
        if (state.m_vTriVertices.empty())
        {
            return;
        }

        glUseProgram(state.m_Shader3D);
        SetUniformMatrix(state.viewLoc, state.m_ViewMatrix);
        SetUniformMatrix(state.projLoc, state.m_ProjectionMatrix);
        SetUniformMatrix(state.m_ModelLoc, Mat4::identity());
        SetUniformColor(state.colorLoc, state.m_CurrentTriColor);
        BindWhiteTexture(state);

        glBindVertexArray(state.m_TriVAO);
        glBindBuffer(GL_ARRAY_BUFFER, state.triVBO);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(state.m_vTriVertices.size() * sizeof(Vertex3D)), state.m_vTriVertices.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(state.m_vTriVertices.size()));
        RecordDebugRenderDraw(state.m_vTriVertices.size());
        glBindVertexArray(0);
        state.m_vTriVertices.clear();
    }

    void QuarkGL3D::DrawTriangle3DImpl(Model3DState& state, const Mat4& currentMatrix,
                                       Vertex3D v1, Vertex3D v2, Vertex3D v3, Color color)
    {
        if (!state.m_vTriVertices.empty() && (color.r != state.m_CurrentTriColor.r ||
                                           color.g != state.m_CurrentTriColor.g ||
                                           color.b != state.m_CurrentTriColor.b ||
                                           color.a != state.m_CurrentTriColor.a))
        {
            FlushTriangles3D(state);
        }

        state.m_CurrentTriColor = color;
        SetUniformColor(state.colorLoc, color);

        state.m_vTriVertices.push_back({TransformPoint(currentMatrix, v1.position), v1.normal, v1.texCoord});
        state.m_vTriVertices.push_back({TransformPoint(currentMatrix, v2.position), v2.normal, v2.texCoord});
        state.m_vTriVertices.push_back({TransformPoint(currentMatrix, v3.position), v3.normal, v3.texCoord});
    }

    void QuarkGL3D::DrawLine3D(Model3DState& state, const Mat4& currentMatrix,
                               Vec3 start, Vec3 end, Color color)
    {
        if (!state.m_vLineVertices.empty() && (color.r != state.m_CurrentLineColor.r ||
                                            color.g != state.m_CurrentLineColor.g ||
                                            color.b != state.m_CurrentLineColor.b ||
                                            color.a != state.m_CurrentLineColor.a))
        {
            FlushLines3D(state);
        }

        state.m_CurrentLineColor = color;
        SetUniformColor(state.colorLoc, color);

        state.m_vLineVertices.push_back({TransformPoint(currentMatrix, start), {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}});
        state.m_vLineVertices.push_back({TransformPoint(currentMatrix, end), {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}});
    }
} // qci
