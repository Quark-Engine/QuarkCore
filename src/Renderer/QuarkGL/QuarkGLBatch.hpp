#ifndef __QUARK_GL_BATCH__
#define __QUARK_GL_BATCH__

#include "../QuarkIRenderer.hpp"

#include <glad/glad.h>

#include <vector>

namespace qc {

class QuarkGLBatch {
public:
    struct BatchVertex {
        float x = 0.0f;
        float y = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 0.0f;
    };

    static constexpr std::size_t kMaxBatchVertices = 8192;

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

    GLuint GetCurrentTexture() const { return m_currentTexture; }
    GLuint GetCurrentShader() const { return m_currentShader; }
    bool HasPendingData() const { return !m_batchVertices.empty(); }

private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_whiteTexture = 0;
    GLuint m_currentTexture = 0;
    GLuint m_defaultShader = 0;
    GLuint m_currentShader = 0;
    int m_width = 0;
    int m_height = 0;
    bool m_camera2DActive = false;
    Camera2D m_camera2D{};
    std::vector<BatchVertex> m_batchVertices;
};

} // namespace qc

#endif // __QUARK_GL_BATCH__
