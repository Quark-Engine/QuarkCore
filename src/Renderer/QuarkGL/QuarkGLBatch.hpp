#ifndef __QUARK_GL_BATCH__
#define __QUARK_GL_BATCH__

#include "../QuarkIRenderer.hpp"

#include <glad/glad.h>

#include <vector>

namespace qci
{
    class QuarkGLBatch
    {
    public:
        struct BatchVertex
        {
            float m_X = 0.0f;
            float m_Y = 0.0f;
            float m_U = 0.0f;
            float m_V = 0.0f;
            float m_R = 0.0f;
            float m_G = 0.0f;
            float m_B = 0.0f;
            float m_A = 0.0f;
        };

        static constexpr std::size_t MAX_BATCH_VERTICES = 8192;

        QuarkGLBatch() = default;
        ~QuarkGLBatch();

        void Init(GLuint defaultShader, GLuint whiteTexture, int width, int height);
        void Shutdown();
        void Begin(GLuint shader, GLuint whiteTexture, int width, int height);
        void End();

        void SetScreenSize(int width, int height);
        void SetCamera(const Camera2D& camera, bool active);
        void SetCurrentShader(GLuint shader);
        void Flush();
        void EnsureBatchTexture(GLuint textureId);
        void PushVertex(const BatchVertex& vtx);
        void PushQuad(GLuint textureId, float x, float y, float w, float h, Color color);
        void PushTexturedQuad(GLuint textureId, Rectangle uv, float x, float y, float w, float h, Color color);
        void PushCircleImpl(float cx, float cy, float r, Color color);

        GLuint GetCurrentTexture() const
        {
            return m_CurrentTexture;
        }

        GLuint GetCurrentShader() const
        {
            return m_CurrentShader;
        }

        bool HasPendingData() const
        {
            return !m_vBatchVertices.empty();
        }

    private:
        GLuint m_Vao = 0;
        GLuint m_Vbo = 0;
        GLuint m_WhiteTexture = 0;
        GLuint m_CurrentTexture = 0;
        GLuint m_DefaultShader = 0;
        GLuint m_CurrentShader = 0;
        int m_Width = 0;
        int m_Height = 0;
        bool m_Camera2DActive = false;
        Camera2D m_Camera2D{};
        std::vector<BatchVertex> m_vBatchVertices;
    };
} // qci

#endif // __QUARK_GL_BATCH__
