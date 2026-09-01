#include "QuarkGLBatch.hpp"

#include "QuarkGLDevice.hpp"

#include <cmath>

namespace qc {

QuarkGLBatch::~QuarkGLBatch() {
    Shutdown();
}

void QuarkGLBatch::Init(GLuint defaultShader, GLuint whiteTexture, int width, int height) {
    m_defaultShader = defaultShader;
    m_currentShader = defaultShader;
    m_whiteTexture = whiteTexture;
    m_currentTexture = whiteTexture;
    m_width = width;
    m_height = height;

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kMaxBatchVertices * sizeof(BatchVertex)), nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), reinterpret_cast<void*>(offsetof(BatchVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), reinterpret_cast<void*>(offsetof(BatchVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), reinterpret_cast<void*>(offsetof(BatchVertex, r)));
    glBindVertexArray(0);
}

void QuarkGLBatch::Shutdown() {
    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    m_batchVertices.clear();
    m_currentTexture = 0;
    m_currentShader = 0;
}

void QuarkGLBatch::Begin(GLuint shader, GLuint whiteTexture, int width, int height) {
    m_defaultShader = shader;
    m_currentShader = shader;
    m_whiteTexture = whiteTexture;
    m_currentTexture = whiteTexture;
    m_width = width;
    m_height = height;
    m_batchVertices.clear();
}

void QuarkGLBatch::End() {
    Flush();
}

void QuarkGLBatch::SetScreenSize(int width, int height) {
    m_width = width;
    m_height = height;
}

void QuarkGLBatch::SetCamera(const Camera2D& camera, bool active) {
    m_camera2D = camera;
    m_camera2DActive = active;
}

void QuarkGLBatch::SetCurrentShader(GLuint shader) {
    m_currentShader = shader ? shader : m_defaultShader;
}

void QuarkGLBatch::Flush() {
    if (m_batchVertices.empty()) {
        return;
    }

    glUseProgram(m_currentShader);

    if (m_currentShader == m_defaultShader) {
        GLint screenLoc = glGetUniformLocation(m_currentShader, "uScreenSize");
        GLint textureLoc = glGetUniformLocation(m_currentShader, "uTexture");
        if (screenLoc >= 0) glUniform2f(screenLoc, static_cast<float>(m_width), static_cast<float>(m_height));
        if (textureLoc >= 0) glUniform1i(textureLoc, 0);
    } else {
        GLint screenLoc = glGetUniformLocation(m_currentShader, "uScreenSize");
        if (screenLoc >= 0) glUniform2f(screenLoc, static_cast<float>(m_width), static_cast<float>(m_height));
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_currentTexture ? m_currentTexture : m_whiteTexture);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_batchVertices.size() * sizeof(BatchVertex)),
                 m_batchVertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_batchVertices.size()));

    m_batchVertices.clear();
}

void QuarkGLBatch::EnsureBatchTexture(GLuint textureId) {
    const GLuint target = textureId ? textureId : m_whiteTexture;
    if (!m_currentTexture) {
        m_currentTexture = target;
    }
    if (target != m_currentTexture || m_batchVertices.size() >= kMaxBatchVertices) {
        Flush();
        m_currentTexture = target;
    }
}

void QuarkGLBatch::PushVertex(const BatchVertex& vtx) {
    if (m_batchVertices.size() >= kMaxBatchVertices) {
        Flush();
    }

    BatchVertex v = vtx;
    if (m_camera2DActive) {
        Vec2 screen = qc::GetWorldToScreen2D({v.x, v.y}, m_camera2D);
        v.x = screen.x;
        v.y = screen.y;
    }

    m_batchVertices.push_back(v);
}

void QuarkGLBatch::PushQuad(GLuint textureId, float x, float y, float w, float h, Color color) {
    EnsureBatchTexture(textureId);
    const auto n = QuarkGLDevice::ToNormColor(color);

    PushVertex({x, y, 0.0f, 0.0f, n[0], n[1], n[2], n[3]});
    PushVertex({x + w, y, 1.0f, 0.0f, n[0], n[1], n[2], n[3]});
    PushVertex({x + w, y + h, 1.0f, 1.0f, n[0], n[1], n[2], n[3]});
    PushVertex({x, y, 0.0f, 0.0f, n[0], n[1], n[2], n[3]});
    PushVertex({x + w, y + h, 1.0f, 1.0f, n[0], n[1], n[2], n[3]});
    PushVertex({x, y + h, 0.0f, 1.0f, n[0], n[1], n[2], n[3]});
}

void QuarkGLBatch::PushTexturedQuad(GLuint textureId, Rectangle uv, float x, float y, float w, float h, Color color) {
    EnsureBatchTexture(textureId);
    const auto n = QuarkGLDevice::ToNormColor(color);

    const float u0 = uv.x;
    const float v0 = uv.y;
    const float u1 = uv.x + uv.width;
    const float v1 = uv.y + uv.height;

    PushVertex({x, y, u0, v0, n[0], n[1], n[2], n[3]});
    PushVertex({x + w, y, u1, v0, n[0], n[1], n[2], n[3]});
    PushVertex({x + w, y + h, u1, v1, n[0], n[1], n[2], n[3]});
    PushVertex({x, y, u0, v0, n[0], n[1], n[2], n[3]});
    PushVertex({x + w, y + h, u1, v1, n[0], n[1], n[2], n[3]});
    PushVertex({x, y + h, u0, v1, n[0], n[1], n[2], n[3]});
}

void QuarkGLBatch::PushCircleImpl(float cx, float cy, float r, Color color) {
    EnsureBatchTexture(0);
    const auto n = QuarkGLDevice::ToNormColor(color);

    constexpr int segments = 48;
    for (int i = 0; i < segments; ++i) {
        const float a0 = static_cast<float>(i) / static_cast<float>(segments) * 6.28318530718f;
        const float a1 = static_cast<float>(i + 1) / static_cast<float>(segments) * 6.28318530718f;

        PushVertex({cx, cy, 0.5f, 0.5f, n[0], n[1], n[2], n[3]});
        PushVertex({cx + std::cos(a0) * r, cy + std::sin(a0) * r, 1.0f, 0.0f, n[0], n[1], n[2], n[3]});
        PushVertex({cx + std::cos(a1) * r, cy + std::sin(a1) * r, 0.0f, 1.0f, n[0], n[1], n[2], n[3]});
    }
}

} // namespace qc
