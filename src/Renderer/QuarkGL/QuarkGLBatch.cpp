#include "QuarkGLBatch.hpp"

#include "QuarkGLDevice.hpp"

#include <cmath>

namespace qci
{
    QuarkGLBatch::~QuarkGLBatch()
    {
        Shutdown();
    }

    void QuarkGLBatch::Init(GLuint defaultShader, GLuint whiteTexture, int width, int height)
    {
        m_DefaultShader = defaultShader;
        m_CurrentShader = defaultShader;
        m_WhiteTexture = whiteTexture;
        m_CurrentTexture = whiteTexture;
        m_Width = width;
        m_Height = height;

        glGenVertexArrays(1, &m_Vao);
        glGenBuffers(1, &m_Vbo);
        glBindVertexArray(m_Vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_Vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(MAX_BATCH_VERTICES * sizeof(BatchVertex)), nullptr, GL_DYNAMIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), reinterpret_cast<void*>(offsetof(BatchVertex, m_X)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), reinterpret_cast<void*>(offsetof(BatchVertex, m_U)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), reinterpret_cast<void*>(offsetof(BatchVertex, m_R)));
        glBindVertexArray(0);
    }

    void QuarkGLBatch::Shutdown()
    {
        if (m_Vao)
        {
            glDeleteVertexArrays(1, &m_Vao);
            m_Vao = 0;
        }
        if (m_Vbo)
        {
            glDeleteBuffers(1, &m_Vbo);
            m_Vbo = 0;
        }
        m_vBatchVertices.clear();
        m_CurrentTexture = 0;
        m_CurrentShader = 0;
    }

    void QuarkGLBatch::Begin(GLuint shader, GLuint whiteTexture, int width, int height)
    {
        m_DefaultShader = shader;
        m_CurrentShader = shader;
        m_WhiteTexture = whiteTexture;
        m_CurrentTexture = whiteTexture;
        m_Width = width;
        m_Height = height;
        m_vBatchVertices.clear();
    }

    void QuarkGLBatch::End()
    {
        Flush();
    }

    void QuarkGLBatch::SetScreenSize(int width, int height)
    {
        m_Width = width;
        m_Height = height;
    }

    void QuarkGLBatch::SetCamera(const Camera2D& camera, bool active)
    {
        m_Camera2D = camera;
        m_Camera2DActive = active;
    }

    void QuarkGLBatch::SetCurrentShader(GLuint shader)
    {
        m_CurrentShader = shader ? shader : m_DefaultShader;
    }

    void QuarkGLBatch::Flush()
    {
        if (m_vBatchVertices.empty())
        {
            return;
        }

        glUseProgram(m_CurrentShader);

        if (m_CurrentShader == m_DefaultShader)
        {
            GLint screenLoc = glGetUniformLocation(m_CurrentShader, "uScreenSize");
            GLint textureLoc = glGetUniformLocation(m_CurrentShader, "uTexture");
            if (screenLoc >= 0)
            {
                glUniform2f(screenLoc, static_cast<float>(m_Width), static_cast<float>(m_Height));
            }
            if (textureLoc >= 0)
            {
                glUniform1i(textureLoc, 0);
            }
        }
        else
        {
            GLint screenLoc = glGetUniformLocation(m_CurrentShader, "uScreenSize");
            if (screenLoc >= 0)
            {
                glUniform2f(screenLoc, static_cast<float>(m_Width), static_cast<float>(m_Height));
            }
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_CurrentTexture ? m_CurrentTexture : m_WhiteTexture);
        glBindVertexArray(m_Vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_Vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vBatchVertices.size() * sizeof(BatchVertex)),
                     m_vBatchVertices.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vBatchVertices.size()));
        RecordDebugRenderDraw(m_vBatchVertices.size());

        m_vBatchVertices.clear();
    }

    void QuarkGLBatch::EnsureBatchTexture(GLuint textureId)
    {
        const GLuint target = textureId ? textureId : m_WhiteTexture;
        if (!m_CurrentTexture)
        {
            m_CurrentTexture = target;
        }
        if (target != m_CurrentTexture || m_vBatchVertices.size() >= MAX_BATCH_VERTICES)
        {
            Flush();
            m_CurrentTexture = target;
        }
    }

    void QuarkGLBatch::PushVertex(const BatchVertex& vtx)
    {
        if (m_vBatchVertices.size() >= MAX_BATCH_VERTICES)
        {
            Flush();
        }

        BatchVertex v = vtx;
        if (m_Camera2DActive)
        {
            Vec2 screen = GetWorldToScreen2D({v.m_X, v.m_Y}, m_Camera2D);
            v.m_X = screen.x;
            v.m_Y = screen.y;
        }

        m_vBatchVertices.push_back(v);
    }

    void QuarkGLBatch::PushQuad(GLuint textureId, float x, float y, float w, float h, Color color)
    {
        EnsureBatchTexture(textureId);
        const auto aNorm = QuarkGLDevice::ToNormColor(color);

        PushVertex({x, y, 0.0f, 0.0f, aNorm[0], aNorm[1], aNorm[2], aNorm[3]});
        PushVertex({x + w, y, 1.0f, 0.0f, aNorm[0], aNorm[1], aNorm[2], aNorm[3]});
        PushVertex({x + w, y + h, 1.0f, 1.0f, aNorm[0], aNorm[1], aNorm[2], aNorm[3]});
        PushVertex({x, y, 0.0f, 0.0f, aNorm[0], aNorm[1], aNorm[2], aNorm[3]});
        PushVertex({x + w, y + h, 1.0f, 1.0f, aNorm[0], aNorm[1], aNorm[2], aNorm[3]});
        PushVertex({x, y + h, 0.0f, 1.0f, aNorm[0], aNorm[1], aNorm[2], aNorm[3]});
    }

    void QuarkGLBatch::PushTexturedQuad(GLuint textureId, Rectangle uv, float x, float y, float w, float h, Color color)
    {
        EnsureBatchTexture(textureId);
        const auto aNorm = QuarkGLDevice::ToNormColor(color);

        const float u0 = uv.x;
        const float v0 = uv.y;
        const float u1 = uv.x + uv.width;
        const float v1 = uv.y + uv.height;

        PushVertex({x, y, u0, v0, aNorm[0], aNorm[1], aNorm[2], aNorm[3]});
        PushVertex({x + w, y, u1, v0, aNorm[0], aNorm[1], aNorm[2], aNorm[3]});
        PushVertex({x + w, y + h, u1, v1, aNorm[0], aNorm[1], aNorm[2], aNorm[3]});
        PushVertex({x, y, u0, v0, aNorm[0], aNorm[1], aNorm[2], aNorm[3]});
        PushVertex({x + w, y + h, u1, v1, aNorm[0], aNorm[1], aNorm[2], aNorm[3]});
        PushVertex({x, y + h, u0, v1, aNorm[0], aNorm[1], aNorm[2], aNorm[3]});
    }

    void QuarkGLBatch::PushCircleImpl(float cx, float cy, float r, Color color)
    {
        EnsureBatchTexture(0);
        const auto aNorm = QuarkGLDevice::ToNormColor(color);

        constexpr int segments = 48;
        for (int i = 0; i < segments; ++i)
        {
            const float a0 = static_cast<float>(i) / static_cast<float>(segments) * 6.28318530718f;
            const float a1 = static_cast<float>(i + 1) / static_cast<float>(segments) * 6.28318530718f;

            PushVertex({cx, cy, 0.5f, 0.5f, aNorm[0], aNorm[1], aNorm[2], aNorm[3]});
            PushVertex({cx + std::cos(a0) * r, cy + std::sin(a0) * r, 1.0f, 0.0f, aNorm[0], aNorm[1], aNorm[2], aNorm[3]});
            PushVertex({cx + std::cos(a1) * r, cy + std::sin(a1) * r, 0.0f, 1.0f, aNorm[0], aNorm[1], aNorm[2], aNorm[3]});
        }
    }
} // qci
