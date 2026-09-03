#include "QuarkGLRenderer.hpp"
#include "../DebugFont.h"
#include "../DefaultFont.h"
#include "../../QuarkInternal.hpp"
#include "../../QuarkModelAnim.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/DefaultLogger.hpp>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

static const char* kVS2D = R"(
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
    vUV   = aUV;
    vColor = aColor;
}
)";

static const char* kFS2D = R"(
#version 330 core

in vec2 vUV;
in vec4 vColor;

out vec4 FragColor;

uniform sampler2D uTexture;

void main() {
    FragColor = texture(uTexture, vUV) * vColor;
}
)";

static const char* kVS3D = R"(
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
    vFragPos    = vec3(uModel * vec4(aPosition, 1.0));
    vNormal     = mat3(uModel) * aNormal;
    vTexCoord   = aTexCoord;
    gl_Position = uProjection * uView * vec4(vFragPos, 1.0);
}
)";

static const char* kFS3D = R"(
#version 330 core

in vec3 vFragPos;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

uniform sampler2D uTexture;
uniform vec4 uColor;

void main() {
    vec4  tex     = texture(uTexture, vTexCoord);
    vec3  result  = tex.rgb * uColor.rgb;
    FragColor     = vec4(result, tex.a * uColor.a);
}
)";

namespace qc {

static Mat4 TransposeMat4(const Mat4& matrix) {
    Mat4 result{};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            result.m[row * 4 + col] = matrix.m[col * 4 + row];
        }
    }
    return result;
}

static void ApplyMaterialShaderUniforms(const Shader& shader, const Mat4& model,
                                        const Mat4& view, const Mat4& projection,
                                        Color tint, bool useTexture) {
    const Mat4 mvp = projection * view * model;
    const Mat4 normalMatrix = TransposeMat4(model.inverted());

    if (shader.locs[SHADER_LOC_MATRIX_MVP] >= 0)
        glUniformMatrix4fv(shader.locs[SHADER_LOC_MATRIX_MVP], 1, GL_FALSE, mvp.m);
    if (shader.locs[SHADER_LOC_MATRIX_VIEW] >= 0)
        glUniformMatrix4fv(shader.locs[SHADER_LOC_MATRIX_VIEW], 1, GL_FALSE, view.m);
    if (shader.locs[SHADER_LOC_MATRIX_PROJECTION] >= 0)
        glUniformMatrix4fv(shader.locs[SHADER_LOC_MATRIX_PROJECTION], 1, GL_FALSE, projection.m);
    if (shader.locs[SHADER_LOC_MATRIX_MODEL] >= 0)
        glUniformMatrix4fv(shader.locs[SHADER_LOC_MATRIX_MODEL], 1, GL_FALSE, model.m);
    if (shader.locs[SHADER_LOC_MATRIX_NORMAL] >= 0)
        glUniformMatrix4fv(shader.locs[SHADER_LOC_MATRIX_NORMAL], 1, GL_FALSE, normalMatrix.m);
    if (shader.locs[SHADER_LOC_COLOR_DIFFUSE] >= 0)
        glUniform4f(shader.locs[SHADER_LOC_COLOR_DIFFUSE],
            tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f, tint.a / 255.0f);

    GLint texture0Loc = glGetUniformLocation(shader.id, "texture0");
    if (texture0Loc >= 0) glUniform1i(texture0Loc, 0);

    GLint useTextureLoc = glGetUniformLocation(shader.id, "useTexture");
    if (useTextureLoc >= 0) glUniform1i(useTextureLoc, useTexture ? 1 : 0);
}

static const Shader* ResolveMaterialShader(const Material* material) {
    if (material && material->shader && material->shader->id != 0) return material->shader;
    return nullptr;
}

template <typename Fn>
static void WithShaderProgram(const Shader& shader, Fn&& fn) {
    if (shader.id == 0) return;

    GLint previousProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);

    if (static_cast<GLuint>(previousProgram) != shader.id) {
        glUseProgram(shader.id);
    }

    fn();

    if (static_cast<GLuint>(previousProgram) != shader.id) {
        glUseProgram(static_cast<GLuint>(previousProgram));
    }
}

static const char* shaderLocationNames[SHADER_LOC_COUNT] = {
    "aPosition",        // SHADER_LOC_VERTEX_POSITION
    "aTexCoord0",       // SHADER_LOC_VERTEX_TEXCOORD01
    "aTexCoord1",       // SHADER_LOC_VERTEX_TEXCOORD02
    "aNormal",          // SHADER_LOC_VERTEX_NORMAL
    "aTangent",         // SHADER_LOC_VERTEX_TANGENT
    "aColor",           // SHADER_LOC_VERTEX_COLOR
    "mvp",              // SHADER_LOC_MATRIX_MVP
    "view",             // SHADER_LOC_MATRIX_VIEW
    "projection",       // SHADER_LOC_MATRIX_PROJECTION
    "model",            // SHADER_LOC_MATRIX_MODEL
    "normalMatrix",     // SHADER_LOC_MATRIX_NORMAL
    "viewPos",          // SHADER_LOC_VECTOR_VIEW
    "colDiffuse",       // SHADER_LOC_COLOR_DIFFUSE
    "colSpecular",      // SHADER_LOC_COLOR_SPECULAR
    "colAmbient",       // SHADER_LOC_COLOR_AMBIENT
    "albedo",           // SHADER_LOC_MAP_ALBEDO
    "metalness",        // SHADER_LOC_MAP_METALNESS
    "normal",           // SHADER_LOC_MAP_NORMAL
    "roughness",        // SHADER_LOC_MAP_ROUGHNESS
    "occlusion",        // SHADER_LOC_MAP_OCCLUSION
    "emission",         // SHADER_LOC_MAP_EMISSION
    "height",           // SHADER_LOC_MAP_HEIGHT
    "cubemap",          // SHADER_LOC_MAP_CUBEMAP
    "irradiance",       // SHADER_LOC_MAP_IRRADIANCE
    "prefilter",        // SHADER_LOC_MAP_PREFILTER
    "brdf",             // SHADER_LOC_MAP_BRDF
    "boneIds",          // SHADER_LOC_VERTEX_BONEIDS
    "boneWeights",      // SHADER_LOC_VERTEX_BONEWEIGHTS
    "boneTransforms",   // SHADER_LOC_MATRIX_BONETRANSFORMS
    "instanceTransform" // SHADER_LOC_VERTEX_INSTANCETRANSFORM
};

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable:4611)
#endif

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

QuarkGLRenderer::~QuarkGLRenderer() {
    this->Shutdown();
}

void QuarkGLRenderer::Init(SDL_Window* window, int width, int height) {
    m_window = window;
    m_width = width;
    m_height = height;
    m_device.Init(window, width, height);
    m_context = m_device.GetContext();

    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* glsl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));

    GLint maxTexSize = 0, max3DTexSize = 0, maxCubeMapSize = 0, maxArrayLayers = 0;
    GLint maxSamples = 0, maxAttribs = 0, maxUBO = 0, maxColorAttach = 0, maxRenderbufferSize = 0;
    GLint maxVertUniforms = 0, maxFragUniforms = 0, maxCombinedTexUnits = 0;
    GLint maxViewportDims[2] = {0, 0};

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &max3DTexSize);
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maxCubeMapSize);
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxArrayLayers);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRenderbufferSize);
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxColorAttach);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maxViewportDims);
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxAttribs);
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &maxVertUniforms);
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, &maxFragUniforms);
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &maxUBO);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxCombinedTexUnits);

    GLint numExtensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);

    TraceLog(LogLevel::Info, "OPENGL", TextFormat("GPU / Renderer: %s", renderer ? renderer : "Unknown"));
    TraceLog(LogLevel::Info, "OPENGL", TextFormat("Vendor: %s", vendor ? vendor : "Unknown"));
    TraceLog(LogLevel::Info, "OPENGL", TextFormat("OpenGL Version: %s", version ? version : "Unknown"));
    TraceLog(LogLevel::Info, "OPENGL", TextFormat("GLSL Version: %s", glsl ? glsl : "Unknown"));
    TraceLog(LogLevel::Info, "OPENGL", TextFormat("Context Config: Initial Viewport %dx%d, Extensions Count: %d", width, height, numExtensions));
    TraceLog(LogLevel::Trace, "OPENGL", TextFormat("Limits: Max 2D Texture: %d, Max 3D Texture: %d, Max CubeMap: %d, Max Array Layers: %d", maxTexSize, max3DTexSize, maxCubeMapSize, maxArrayLayers));
    TraceLog(LogLevel::Trace, "OPENGL", TextFormat("Limits: Max MSAA Samples: %dx, Max Vertex Attribs: %d, Max Combined Texture Units: %d", maxSamples, maxAttribs, maxCombinedTexUnits));
    TraceLog(LogLevel::Trace, "OPENGL", TextFormat("Limits: Max UBO Bindings: %d, Max Vertex Uniforms: %d, Max Fragment Uniforms: %d", maxUBO, maxVertUniforms, maxFragUniforms));
    TraceLog(LogLevel::Trace, "OPENGL", TextFormat("Limits: Max Color Attachments: %d, Max Renderbuffer: %d, Max Viewport: %dx%d", maxColorAttach, maxRenderbufferSize, maxViewportDims[0], maxViewportDims[1]));

    InitGL();
    if (m_vsyncExplicitlySet) {
        SDL_GL_SetSwapInterval(m_vsync ? 1 : 0);
        TraceLog(LogLevel::Info, "OPENGL", TextFormat("VSync configured: %s (Swap Interval: %d)", m_vsync ? "ON" : "OFF", m_vsync ? 1 : 0));
    }

    m_lastFrameCounter = SDL_GetPerformanceCounter();
    TraceLog(LogLevel::Info, "OPENGL", "OpenGL renderer initialized successfully.");
}

void QuarkGLRenderer::Shutdown() {
    if (m_window == nullptr) {
        return;
    }

    TraceLog(LogLevel::Info, "OPENGL", "Shutting down OpenGL renderer...");
    m_font.Clear();
    m_batch.Shutdown();
    m_device.Shutdown();

    for (auto& [id, fd] : m_fonts)
        if (fd.atlasTexture) glDeleteTextures(1, &fd.atlasTexture);
    m_fonts.clear();
    m_defaultFontId = 0;

    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }

    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }

    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }

    if (m_whiteTexture) {
        glDeleteTextures(1, &m_whiteTexture);
        m_whiteTexture = 0;
    }

    if (m_3d.shader3D) {
        glDeleteProgram(m_3d.shader3D);
        m_3d.shader3D = 0;
    }

    if (m_3d.whiteTexture) {
        glDeleteTextures(1, &m_3d.whiteTexture);
        m_3d.whiteTexture = 0;
    }

    if (m_3d.blackTexture) {
        glDeleteTextures(1, &m_3d.blackTexture);
        m_3d.blackTexture = 0;
    }

    if (m_3d.flatNormalTexture) {
        glDeleteTextures(1, &m_3d.flatNormalTexture);
        m_3d.flatNormalTexture = 0;
    }

    for (const auto& [_, cachedTexture] : m_textureCache) {
        if (cachedTexture.texture.id) {
            glDeleteTextures(1, &cachedTexture.texture.id);
        }
    }
    m_textureCache.clear();
    m_textureCacheKeys.clear();

    auto del=[](GLuint& va,GLuint& vb,GLuint& eb) {
        if(va) {
            glDeleteVertexArrays(1, &va);
            va = 0;
        }

        if(vb) {
            glDeleteBuffers(1, &vb);
            vb = 0;
        }

        if(eb) {
            glDeleteBuffers(1, &eb);
            eb = 0;
        }
    };
    del(m_3d.planeVAO,  m_3d.planeVBO,  m_3d.planeEBO);
    del(m_3d.cubeVAO,   m_3d.cubeVBO,   m_3d.cubeEBO);
    del(m_3d.sphereVAO, m_3d.sphereVBO, m_3d.sphereEBO);
    if(m_3d.lineVAO) {
        glDeleteVertexArrays(1, &m_3d.lineVAO);
        m_3d.lineVAO = 0;
    }

    if(m_3d.lineVBO) {
        glDeleteBuffers(1, &m_3d.lineVBO);
        m_3d.lineVBO = 0;
    }

    if(m_3d.triVAO) {
        glDeleteVertexArrays(1, &m_3d.triVAO); 
        m_3d.triVAO = 0;
    }

    if(m_3d.triVBO) {
        glDeleteBuffers(1, &m_3d.triVBO);
        m_3d.triVBO = 0;
    }

    if (m_context) {
        SDL_GL_DestroyContext(m_context);
        m_context = nullptr;
    }

    m_window = nullptr;
    TraceLog(LogLevel::Info, "OPENGL", "OpenGL renderer shut down successfully.");
}

void QuarkGLRenderer::InitGL() {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_program       = CreateDefaultProgram();
    m_defaultShader = m_program;
    m_currentShader = m_program;
    
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, kMaxBatchVertices*sizeof(BatchVertex), nullptr, GL_DYNAMIC_DRAW);
    // layout: vec2 pos, vec2 uv, vec4 color
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), reinterpret_cast<void*>(offsetof(BatchVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), reinterpret_cast<void*>(offsetof(BatchVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), reinterpret_cast<void*>(offsetof(BatchVertex, r)));
    glBindVertexArray(0);

    const uint8_t white[4] = {255,255,255,255};
    glGenTextures(1, &m_whiteTexture);
    glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA,GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_currentTexture = m_whiteTexture;
    m_batch.Init(m_program, m_whiteTexture, m_width, m_height);
    m_batch.SetCamera(m_camera2D, m_camera2DActive);
    RefreshViewport();
}

void QuarkGLRenderer::RefreshViewport() {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(m_window, &w, &h);

    m_width = w; m_height = h;
    glViewport(0, 0, w, h);
}

void QuarkGLRenderer::BeginDrawing() {
    m_drawing = true;
    m_currentTexture = m_whiteTexture;
    m_batchVertices.clear();
    m_batch.Begin(m_defaultShader, m_whiteTexture, m_width, m_height);
    m_batch.SetCamera(m_camera2D, m_camera2DActive);
    m_device.BeginDrawing();
}

void QuarkGLRenderer::EndDrawing() {
    FlushBatch();
    m_device.EndDrawing();
    m_frameTime = m_device.GetFrameTime();
    m_shouldClose = m_device.ShouldClose();
}

void QuarkGLRenderer::SetTargetFPS(int fps) {
    m_targetFps = fps;
    m_device.SetTargetFPS(fps);
    if (!m_vsyncExplicitlySet && m_context) {
        if (fps == 0) SDL_GL_SetSwapInterval(0);
        else SDL_GL_SetSwapInterval(1);
    }
}

bool QuarkGLRenderer::SetVSync(bool enabled) {
    m_vsync = enabled;
    m_vsyncExplicitlySet = true;
    if (!m_device.SetVSync(enabled)) {
        TraceLog(LogLevel::Warn, "RENDERER", (std::string("SDL_GL_SetSwapInterval failed: ") + SDL_GetError()).c_str());
        return false;
    }
    return true;
}

void QuarkGLRenderer::ClearBackground(Color c) {
    m_device.ClearBackground(c);
}

std::array<float,4> QuarkGLRenderer::ToNormColor(Color c) {
    return QuarkGLDevice::ToNormColor(c);
}

GLuint QuarkGLRenderer::CreateTextureFromRgba(const uint8_t* px, int w, int h) {
    return QuarkGLTexture::CreateTextureFromRgba(px, w, h);
}

GLuint QuarkGLRenderer::CompileGLShader(GLenum type, const char* src) {
    return QuarkGLShader::CompileGLShader(type, src);
}

GLuint QuarkGLRenderer::CreateDefaultProgram() {
    TraceLog(LogLevel::Trace, "SHADER", "[OpenGL] Creating default 2D shader program...");
    const GLuint program = m_shader.CreateDefaultProgram();
    TraceLog(LogLevel::Info, "SHADER", TextFormat("[OpenGL] Default 2D shader program created (ID: %u)", program));
    return program;
}

void QuarkGLRenderer::FlushBatch() {
    m_batch.Flush();
}

void QuarkGLRenderer::EnsureBatchTexture(GLuint id) {
    m_batch.EnsureBatchTexture(id);
}

void QuarkGLRenderer::PushVertex(const BatchVertex& vtx) {
    m_batch.PushVertex({vtx.x, vtx.y, vtx.u, vtx.v, vtx.r, vtx.g, vtx.b, vtx.a});
}

void QuarkGLRenderer::PushQuad(GLuint tex, float x, float y, float w, float h, Color col) {
    m_batch.PushQuad(tex, x, y, w, h, col);
}

void QuarkGLRenderer::PushTexturedQuad(GLuint tex, Rectangle uv,
                                        float x, float y, float w, float h, Color col) {
    m_batch.PushTexturedQuad(tex, uv, x, y, w, h, col);
}

void QuarkGLRenderer::PushCircleImpl(float cx, float cy, float r, Color col) {
    m_batch.PushCircleImpl(cx, cy, r, col);
}

void QuarkGLRenderer::DrawRectangle(float x, float y, float w, float h, Color c) {
    PushQuad(0, x, y, w, h, c);
}

void QuarkGLRenderer::DrawRectangle(const Rectangle& r, Color c) {
    PushQuad(0, r.x, r.y, r.width, r.height, c);
}

void QuarkGLRenderer::DrawRectangleV(Vec2 p, Vec2 s, Color c) {
    PushQuad(0, p.x, p.y, s.x, s.y, c);
}

void QuarkGLRenderer::DrawCircle(float cx, float cy, float r, Color c) {
    PushCircleImpl(cx, cy, r, c);
}

void QuarkGLRenderer::DrawLine(float x1, float y1, float x2, float y2, Color c) {
    DrawLineV({x1, y1},{x2, y2}, c);
}

void QuarkGLRenderer::DrawLineV(Vec2 s, Vec2 e, Color c) {
    float dx = e.x - s.x;
    float dy = e.y - s.y;
    float length = sqrtf(dx * dx + dy * dy);
    if (length <= 0) return;

    float angle = atan2f(dy, dx) * 180.0f / PI;
    ITexture white = { m_whiteTexture, 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, true };
    DrawTexturePro(white, { 0, 0, 1, 1 }, { s.x, s.y, length, 1.0f }, { 0, 0.5f }, angle, c);
}

void QuarkGLRenderer::DrawRectangleLines(Rectangle r, float, Color c) {
    DrawLine(r.x, r.y, r.x + r.width, r.y, c);
    DrawLine(r.x + r.width, r.y, r.x + r.width, r.y + r.height, c);
    DrawLine(r.x + r.width, r.y + r.height, r.x, r.y + r.height, c);
    DrawLine(r.x, r.y + r.height, r.x, r.y, c);
}

void QuarkGLRenderer::DrawRectangleRounded(Rectangle r, float rnd, int, Color c) {
    float rad = rnd * std::min(r.width, r.height) / 2.f;

    DrawCircle(r.x + rad, r.y + rad, rad, c);
    DrawCircle(r.x + r.width - rad, r.y + rad, rad, c);
    DrawCircle(r.x + r.width - rad, r.y + r.height - rad, rad, c);
    DrawCircle(r.x + rad, r.y + r.height - rad, rad, c);

    PushQuad(0, r.x+ rad, r.y, r.width - 2 * rad, r.height, c);
    PushQuad(0, r.x, r.y + rad, r.width, r.height - 2 * rad, c);
}

void QuarkGLRenderer::DrawTriangle(Vec2 a, Vec2 b, Vec2 c, Color col) {
    EnsureBatchTexture(0);
    auto n = ToNormColor(col);
    PushVertex({ a.x, a.y, 0, 0, n[0], n[1], n[2], n[3] });
    PushVertex({ b.x, b.y, 0, 0, n[0], n[1], n[2], n[3] });
    PushVertex({ c.x, c.y, 0, 0, n[0], n[1], n[2], n[3] });
}

void QuarkGLRenderer::DrawCircleLines(float cx, float cy, float r, Color c) {
    constexpr int segments = 36;
    for (int i = 0; i < segments; i++) {
        float a1 = (float)i / segments * 2.0f * PI;
        float a2 = (float)(i + 1) / segments * 2.0f * PI;
        DrawLineV({ cx + r * cosf(a1), cy + r * sinf(a1) }, { cx + r * cosf(a2), cy + r * sinf(a2) }, c);
    }
}

void QuarkGLRenderer::DrawEllipse(float cx, float cy, float rh, float rv, Color c) {
    EnsureBatchTexture(0);
    auto n = ToNormColor(c);
    constexpr int segments = 36;
    for (int i = 0; i < segments; i++) {
        float a1 = (float)i / segments * 2.0f * PI;
        float a2 = (float)(i + 1) / segments * 2.0f * PI;
        PushVertex({ cx, cy, 0.5f, 0.5f, n[0], n[1], n[2], n[3] });
        PushVertex({ cx + rh * cosf(a1), cy + rv * sinf(a1), 1, 0, n[0], n[1], n[2], n[3] });
        PushVertex({ cx + rh * cosf(a2), cy + rv * sinf(a2), 0, 1, n[0], n[1], n[2], n[3] });
    }
}

void QuarkGLRenderer::DrawPoly(Vec2 cen, int sides, float r, float rot, Color c) {
    if(sides < 3) return;
    EnsureBatchTexture(0);
    auto n = ToNormColor(c);
    for (int i = 0; i < sides; i++) {
        float a1 = (float)i / sides * 2.0f * PI + rot * PI / 180.0f;
        float a2 = (float)(i + 1) / sides * 2.0f * PI + rot * PI / 180.0f;
        PushVertex({ cen.x, cen.y, 0.5f, 0.5f, n[0], n[1], n[2], n[3] });
        PushVertex({ cen.x + r * cosf(a1), cen.y + r * sinf(a1), 1, 0, n[0], n[1], n[2], n[3] });
        PushVertex({ cen.x + r * cosf(a2), cen.y + r * sinf(a2), 0, 1, n[0], n[1], n[2], n[3] });
    }
}

void QuarkGLRenderer::DrawTexture(const ITexture& t, float x, float y, Color tint) {
    if(!t.id) return;
    PushQuad(t.id, x, y, (float)t.width, (float)t.height, tint);
}

void QuarkGLRenderer::DrawTextureV(const ITexture& t, Vec2 p, Color tint) {
    DrawTexture(t, p.x, p.y, tint);
}

void QuarkGLRenderer::DrawTextureRec(const ITexture& t, Rectangle src, Vec2 pos, Color tint) {
    ITexture copy = t;
    DrawTexturePro(copy, src, {pos.x, pos.y, src.width, src.height}, {0, 0}, 0, tint);
}

void QuarkGLRenderer::DrawTextureEx(const ITexture& t, Vec2 pos, float rot, float scale, Color tint) {
    ITexture copy = t;
    Rectangle src{0, 0, (float)t.width, (float)t.height};
    Rectangle dst{pos.x, pos.y, (float)t.width * scale, (float)t.height * scale};
    DrawTexturePro(copy, src, dst, {(float)t.width * scale / 2, (float)t.height * scale / 2}, rot, tint);
}

void QuarkGLRenderer::DrawTexturePro(ITexture t, Rectangle src, Rectangle dst,
                                      Vec2 origin, float rotation, Color tint) {
    if(!t.id) return;
    EnsureBatchTexture(t.id);

    auto n = ToNormColor(tint);

    float tw = (float)t.width, th = (float)t.height;
    float u0 = src.x / tw, v0 = src.y / th, u1 = (src.x + src.width) / tw, v1 = (src.y + src.height) / th;
    Vec2 v[4] = {{-origin.x, -origin.y}, {dst.width - origin.x, -origin.y},
               {dst.width - origin.x, dst.height - origin.y}, {-origin.x, dst.height - origin.y}};

    if(rotation != 0) {
        float rad = rotation * PI / 180.f, cA = cosf(rad), sA = sinf(rad);
        for(auto& p : v) {
            float rx = p.x * cA - p.y * sA;
            float ry = p.x * sA + p.y * cA;
            p.x = rx;
            p.y = ry;
        }
    }

    for(auto& p : v) {
        p.x += dst.x;
        p.y += dst.y;
    }

    PushVertex({v[0].x, v[0].y, u0, v0, n[0], n[1], n[2], n[3]});
    PushVertex({v[1].x, v[1].y, u1, v0, n[0], n[1], n[2], n[3]});
    PushVertex({v[2].x, v[2].y, u1, v1, n[0], n[1], n[2], n[3]});
    PushVertex({v[0].x, v[0].y, u0, v0, n[0], n[1], n[2], n[3]});
    PushVertex({v[2].x, v[2].y, u1, v1, n[0], n[1], n[2], n[3]});
    PushVertex({v[3].x, v[3].y, u0, v1, n[0], n[1], n[2], n[3]});
}

void QuarkGLRenderer::DrawTextureTiled(ITexture t, float scale, Vec2 off, Color tint) {
    if(!t.id) return;

    int tx = (int)ceilf(m_width / (t.width * scale)) + 1;
    int ty = (int)ceilf(m_height / (t.height * scale)) + 1;

    for(int y = -1; y < ty; ++y) for(int x = -1; x < tx; ++x)
        DrawTexture(t, off.x + x * t.width * scale, off.y + y * t.height * scale, tint);
}

void QuarkGLRenderer::DrawTextureNPatch(ITexture t, NPatchInfo np, Rectangle dst,
                                         Vec2 origin, float rot, Color tint) {
    if (!t.id) return;

    const float srcW = np.source.width  > 0.f ? np.source.width  : 1.f;
    const float srcH = np.source.height > 0.f ? np.source.height : 1.f;
    (void)srcW; (void)srcH;

    const float dLeft   = static_cast<float>(np.left);
    const float dTop    = static_cast<float>(np.top);
    const float dRight  = static_cast<float>(np.right);
    const float dBottom = static_cast<float>(np.bottom);
    const float dMiddleW = dst.width  - dLeft - dRight;
    const float dMiddleH = dst.height - dTop  - dBottom;

    const float sLeft   = np.source.x + dLeft;
    const float sTop    = np.source.y + dTop;
    const float sRight  = np.source.x + np.source.width  - dRight;
    const float sBottom = np.source.y + np.source.height - dBottom;

    auto patch = [&](float sx, float sy, float sw, float sh,
                     float dx, float dy, float dw, float dh) {
        if (sw <= 0.f || sh <= 0.f || dw <= 0.f || dh <= 0.f) return;
        Rectangle src{ sx, sy, sw, sh };
        Rectangle dpt{ dst.x + dx, dst.y + dy, dw, dh };
        DrawTexturePro(t, src, dpt, origin, rot, tint);
    };

    if (np.layout == NPATCH_THREE_PATCH_HORIZONTAL) {
        // 1x3: left, center (stretch), right
        patch(np.source.x,      np.source.y, dLeft,   np.source.height, 0.f,          0.f, dLeft,   dst.height);
        patch(sLeft,            np.source.y, np.source.width - dLeft - dRight, np.source.height,
              dLeft, 0.f, dMiddleW, dst.height);
        patch(sRight,           np.source.y, dRight,  np.source.height, dLeft + dMiddleW, 0.f, dRight, dst.height);
        return;
    }

    if (np.layout == NPATCH_THREE_PATCH_VERTICAL) {
        // 3x1: top, center (stretch), bottom
        patch(np.source.x, np.source.y,      np.source.width, dTop,    0.f, 0.f,           dst.width, dTop);
        patch(np.source.x, sTop,             np.source.width, np.source.height - dTop - dBottom,
              0.f, dTop, dst.width, dMiddleH);
        patch(np.source.x, sBottom,          np.source.width, dBottom, 0.f, dTop + dMiddleH, dst.width, dBottom);
        return;
    }

    // NPATCH_NINE_PATCH (3x3)
    patch(np.source.x, np.source.y, dLeft, dTop, 0.f, 0.f, dLeft, dTop);
    patch(sRight, np.source.y, dRight, dTop, dLeft + dMiddleW, 0.f, dRight, dTop);
    patch(np.source.x, sBottom, dLeft, dBottom, 0.f, dTop + dMiddleH, dLeft, dBottom);
    patch(sRight, sBottom, dRight, dBottom, dLeft + dMiddleW, dTop + dMiddleH, dRight, dBottom);

    patch(sLeft, np.source.y, np.source.width - dLeft - dRight, dTop, dLeft, 0.f, dMiddleW, dTop);
    patch(np.source.x, sTop, dLeft, np.source.height - dTop - dBottom, 0.f, dTop, dLeft, dMiddleH);
    patch(sRight, sTop, dRight, np.source.height - dTop - dBottom, dLeft + dMiddleW, dTop, dRight, dMiddleH);
    patch(sLeft, sBottom, np.source.width - dLeft - dRight, dBottom, dLeft, dTop + dMiddleH, dMiddleW, dBottom);
    patch(sLeft, sTop, np.source.width - dLeft - dRight, np.source.height - dTop - dBottom,
          dLeft, dTop, dMiddleW, dMiddleH);
}

ITexture QuarkGLRenderer::LoadTexture(const char* path) {
    return m_texture.LoadTexture(path);
}

ITexture QuarkGLRenderer::LoadTextureFromImage(const Image& image) {
    return m_texture.LoadTextureFromImage(image);
}

void QuarkGLRenderer::UnloadTexture(ITexture& t) {
    m_texture.UnloadTexture(t);
}

bool QuarkGLRenderer::isTextureValid(ITexture& t) {
    return m_texture.IsTextureValid(t);
}

ITexture QuarkGLRenderer::GetRenderTextureTexture(IRenderTexture rt) {
    return m_texture.GetRenderTextureTexture(rt);
}

IRenderTexture QuarkGLRenderer::LoadRenderTexture(int w, int h) {
    return m_texture.LoadRenderTexture(w, h);
}

void QuarkGLRenderer::UnloadRenderTexture(IRenderTexture rt) {
    m_texture.UnloadRenderTexture(rt);
}

bool QuarkGLRenderer::isRenderTextureValid(IRenderTexture& rt) {
    return m_texture.IsRenderTextureValid(rt);
}

namespace {
void FlipRowsRgba(void* data, int width, int height) {
    const int rowBytes = width * 4;
    std::vector<uint8_t> tmp(rowBytes);
    uint8_t* pixels = static_cast<uint8_t*>(data);
    for (int y = 0; y < height / 2; ++y) {
        uint8_t* top = pixels + static_cast<size_t>(y) * rowBytes;
        uint8_t* bottom = pixels + static_cast<size_t>(height - 1 - y) * rowBytes;
        std::memcpy(tmp.data(), top, rowBytes);
        std::memcpy(top, bottom, rowBytes);
        std::memcpy(bottom, tmp.data(), rowBytes);
    }
}
} // namespace

Image QuarkGLRenderer::ReadTextureImage(const ITexture& t) {
    if (!t.valid || t.id == 0 || t.width <= 0 || t.height <= 0) return Image{};

    FlushBatch();

    const size_t bytes = static_cast<size_t>(t.width) * t.height * 4;
    void* buf = MemAlloc(bytes);
    if (!buf) return Image{};

    glBindTexture(GL_TEXTURE_2D, t.id);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glBindTexture(GL_TEXTURE_2D, 0);

    FlipRowsRgba(buf, t.width, t.height);

    Image img{};
    img.data = buf;
    img.width = t.width;
    img.height = t.height;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    TraceLog(LogLevel::Info, "IMAGE", TextFormat("[OpenGL] Read texture pixels back to CPU: %ux%u (ID: %u)", t.width, t.height, t.id));
    return img;
}

Image QuarkGLRenderer::ReadScreenImage() {
    if (m_width <= 0 || m_height <= 0) return Image{};

    FlushBatch();

    const int w = m_width;
    const int h = m_height;
    const size_t bytes = static_cast<size_t>(w) * h * 4;
    void* buf = MemAlloc(bytes);
    if (!buf) return Image{};

    glBindFramebuffer(GL_FRAMEBUFFER, m_currentFbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    if (m_currentFbo != 0) {
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    } else {
        glReadBuffer(GL_BACK);
    }
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);

    FlipRowsRgba(buf, w, h);

    Image img{};
    img.data = buf;
    img.width = w;
    img.height = h;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    TraceLog(LogLevel::Info, "IMAGE", TextFormat("[OpenGL] Read backbuffer pixels to CPU: %dx%d", w, h));
    return img;
}

ITexture QuarkGLRenderer::GenCheckerTexture(int w, int h, int cell, Color ca, Color cb) {
    std::vector<uint8_t> px((size_t) w * h * 4);

    for(int y = 0; y < h; ++y) for(int x = 0; x < w; ++x) {
        Color c = ((x / cell +  y / cell) %2 == 0) ? ca : cb;
        size_t i = ((size_t)y * w + x) * 4;

        px[i] = c.r;
        px[i+1] = c.g;
        px[i+2] = c.b;
        px[i+3] = c.a;
    }

    ITexture t{};
    t.id = CreateTextureFromRgba(px.data(), w, h);
    t.width = w;
    t.height = h;
    t.mipmaps = 1;
    t.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    t.valid = true;
    TraceLog(LogLevel::Info, "TEXTURE", TextFormat("[OpenGL] Generated checker texture: %dx%d (Cell: %dpx, ID: %u)", w, h, cell, t.id));
    return t;
}

void QuarkGLRenderer::BeginTextureMode(IRenderTexture rt) {
    FlushBatch();

    glBindFramebuffer(GL_FRAMEBUFFER, rt.id);
    m_currentFbo = rt.id;
    m_width = rt.texture.width;
    m_height = rt.texture.height;
    m_batch.SetScreenSize(m_width, m_height);
    m_batch.SetCamera(m_camera2D, m_camera2DActive);

    glViewport(0, 0, m_width, m_height);
}

void QuarkGLRenderer::EndTextureMode() {
    FlushBatch();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_currentFbo = 0;

    RefreshViewport();
    m_batch.SetScreenSize(m_width, m_height);
    m_batch.SetCamera(m_camera2D, m_camera2DActive);
}

static std::vector<int> DefaultCodepoints() {
    std::vector<int> cps;
    cps.reserve(96);
    for (int c = 32; c <= 126; ++c) cps.push_back(c);
    return cps;
}

static int DecodeUTF8(const char*& p) {
    const unsigned char lead = static_cast<unsigned char>(*p);
    int cp = 0, seq = 0;
    if ((lead & 0x80) == 0)      { cp = lead; seq = 1; }
    else if ((lead & 0xE0) == 0xC0) { cp = lead & 0x1F; seq = 2; }
    else if ((lead & 0xF0) == 0xE0) { cp = lead & 0x0F; seq = 3; }
    else if ((lead & 0xF8) == 0xF0) { cp = lead & 0x07; seq = 4; }
    else                        { cp = lead; seq = 1; }
    for (int k = 1; k < seq && p[k] != '\0'; ++k)
        cp = (cp << 6) | (static_cast<unsigned char>(p[k]) & 0x3F);
    p += seq;
    return cp;
}

bool QuarkGLRenderer::LoadFontInternal(const char* filePath, const unsigned char* fileData, int dataSize,
                                       int pointSize, const int* codepoints, int codepointCount, FontData& out) {
    TraceLog(LogLevel::Trace, "FONT", TextFormat("[OpenGL] FreeType initializing font: %s (size: %d pt)", filePath ? filePath : "<memory>", pointSize));
    FT_Library ft = nullptr;
    if (FT_Init_FreeType(&ft) != 0) {
        TraceLog(LogLevel::Error, "FONT", "[OpenGL] Failed to initialize FreeType library");
        return false;
    }

    FT_Face face = nullptr;
    if (fileData != nullptr) {
        if (FT_New_Memory_Face(ft, fileData, (FT_Long)dataSize, 0, &face) != 0) {
            TraceLog(LogLevel::Error, "FONT", "[OpenGL] FreeType failed to open font from memory");
            FT_Done_FreeType(ft);
            return false;
        }
    } else if (FT_New_Face(ft, filePath, 0, &face) != 0) {
        TraceLog(LogLevel::Error, "FONT", TextFormat("[OpenGL] FreeType failed to open font file: %s", filePath ? filePath : "<null>"));
        FT_Done_FreeType(ft);
        return false;
    }

    FT_Select_Charmap(face, FT_ENCODING_UNICODE);
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)pointSize);

    std::vector<int> cps;
    const int* cpsPtr = codepoints;
    int cpsCount = codepointCount;
    if (cpsPtr == nullptr || cpsCount <= 0) {
        cps = DefaultCodepoints();
        cpsPtr = cps.data();
        cpsCount = (int)cps.size();
    }

    constexpr int AW = 1024, AH = 1024;
    std::vector<uint8_t> atlas((size_t)AW * AH * 4, 0);
    int penX = 1, penY = 1, rowH = 0;
    int renderedGlyphs = 0;
    out.glyphs.clear();
    out.glyphs.reserve((size_t)cpsCount);

    for (int i = 0; i < cpsCount; ++i) {
        const int cp = cpsPtr[i];
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER|FT_LOAD_TARGET_NORMAL) != 0) continue;

        FT_GlyphSlot slot = face->glyph;
        int gw = (int)slot->bitmap.width;
        int gh = (int)slot->bitmap.rows;

        if (penX + gw + 1 > AW) {
            penX = 1;
            penY += rowH + 1;
            rowH = 0;
        }

        if (penY + gh + 1 > AH) {
            TraceLog(LogLevel::Warn, "FONT", TextFormat("[OpenGL] Font atlas overflow (%dx%d) for font: %s", AW, AH, filePath ? filePath : "<memory>"));
            FT_Done_Face(face);
            FT_Done_FreeType(ft);
            return false;
        }

        for (int row = 0; row < gh; ++row) for(int col = 0; col < gw; ++col) {
            size_t dst = ((penY + row) * AW + (penX + col)) * 4;
            uint8_t alpha = slot->bitmap.buffer[row * slot->bitmap.pitch + col];

            atlas[dst] = 255;
            atlas[dst + 1] = 255;
            atlas[dst + 2] = 255;
            atlas[dst + 3] = alpha;
        }

        GlyphData g;
        g.value    = cp;
        g.uv = Rectangle{(float)penX / AW, (float)penY / AH,
                               gw > 0 ? (float)gw / AW : 0.f, gh > 0 ? (float)gh / AH : 0.f};
        g.rec = Rectangle{(float)penX, (float)penY, gw > 0 ? (float)gw : 0.f, gh > 0 ? (float)gh : 0.f};
        g.advanceX = (float)slot->advance.x / 64.f;
        g.offsetX = (float)slot->bitmap_left;
        g.offsetY = (float)slot->bitmap_top;
        g.width = gw;
        g.height = gh;

        if (gw > 0 && gh > 0) {
            unsigned char* gdata = static_cast<unsigned char*>(std::malloc((size_t)gw * gh * 4));
            if (gdata) {
                for (int row = 0; row < gh; ++row) {
                    for (int col = 0; col < gw; ++col) {
                        unsigned char alpha = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
                        gdata[((size_t)row * gw + col) * 4 + 0] = 255;
                        gdata[((size_t)row * gw + col) * 4 + 1] = 255;
                        gdata[((size_t)row * gw + col) * 4 + 2] = 255;
                        gdata[((size_t)row * gw + col) * 4 + 3] = alpha;
                    }
                }
                g.image = Image{ gdata, gw, gh, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
            }
        }
        penX += gw + 1;
        rowH  = std::max(rowH, gh);
        renderedGlyphs++;
        out.glyphs.push_back(g);
    }

    out.atlasTexture = CreateTextureFromRgba(atlas.data(), AW, AH);
    out.atlasWidth   = AW;
    out.atlasHeight  = AH;
    out.baseSize     = pointSize;
    out.glyphCount   = renderedGlyphs;
    out.ascent       = (int)(face->size->metrics.ascender  / 64);
    out.descent      = (int)(face->size->metrics.descender / 64);
    out.lineHeight   = (int)(face->size->metrics.height    / 64);
    out.lineGap      = out.lineHeight - (out.ascent - out.descent);

    const char* family = face->family_name ? face->family_name : "Unknown";
    const char* style  = face->style_name ? face->style_name : "Regular";
    TraceLog(LogLevel::Info, "FONT", TextFormat("[OpenGL] Font rasterized: %s (%s %s, %d glyphs, Atlas: %dx%d, Ascent: %d, Descent: %d, LineHeight: %d)",
        filePath ? filePath : "<in-memory>", family, style, renderedGlyphs, AW, AH, out.ascent, out.descent, out.lineHeight));

    FT_Done_Face(face);
    FT_Done_FreeType(ft);
    return true;
}

int QuarkGLRenderer::FindGlyph(const FontData& fd, int codepoint) {
    for (int i = 0; i < (int)fd.glyphs.size(); ++i) {
        if (fd.glyphs[i].value == codepoint) return i;
    }
    for (int i = 0; i < (int)fd.glyphs.size(); ++i) {
        if (fd.glyphs[i].value == 63) return i;
    }
    return -1;
}

uint32_t QuarkGLRenderer::EnsureDefaultFont() {
    if (m_defaultFontId != 0) return m_defaultFontId;

    if (pixel_ttf == nullptr || pixel_ttf_len == 0) return 0;

    FontData fd{};
    if (!LoadFontInternal(nullptr, pixel_ttf, static_cast<int>(pixel_ttf_len), 32, nullptr, 0, fd)) return 0;

    uint32_t id = m_nextFontId++;
    m_fonts[id]  = std::move(fd);
    m_defaultFontId = id;
    return id;
}

const QuarkGLRenderer::FontData* QuarkGLRenderer::GetFontData(IFont font) const {
    auto it = m_fonts.find(font.id);
    return it != m_fonts.end() ? &it->second : nullptr;
}

void QuarkGLRenderer::DrawTextWithFontData(const FontData& fd, const char* text,
                                            Vec2 pos, float fontSize, float spacing, Color tint) {
    if (!text) return;

    const float scale      = fontSize / (float)fd.baseSize;
    const float lineHeight = (float)fd.lineHeight * scale;
    const float baseline   = (float)fd.ascent     * scale;
    float x = pos.x, y = pos.y;

    bool first = true;

    for (const char* c = text; *c; ) {
        if (*c == '\n') {
            x = pos.x;
            y += lineHeight;
            first = true;
            ++c;
            continue;
        }

        const int cp = DecodeUTF8(c);
        const int idx = FindGlyph(fd, cp);
        if (idx < 0) {
            continue;
        }

        const GlyphData& g = fd.glyphs[(size_t)idx];

        if (!first) x += spacing;
        first = false;

        float gx = x + g.offsetX * scale;
        float gy = y + baseline - g.offsetY * scale;
        float gw = (float)g.width * scale, gh = (float)g.height * scale;
        if (gw > 0 && gh > 0)
            PushTexturedQuad(fd.atlasTexture, g.uv, gx, gy, gw, gh, tint);

        x += g.advanceX * scale;
    }
}

Vec2 QuarkGLRenderer::MeasureTextWithFontData(const FontData& fd, const char* text, float fontSize, float spacing) const {
    if (!text) return {};

    float scale = (float)fontSize/(float)fd.baseSize;
    float lh = (float)fd.lineHeight * scale;

    float x = 0, maxW = 0;
    bool first = true;
    int lines = 1;

    for (const char* c = text; *c; ) {
        if(*c == '\n') {
            maxW = std::max(maxW, x);
            x = 0;
            first = true;
            ++lines;
            ++c;
            continue;
        }

        const int cp = DecodeUTF8(c);
        const int idx = FindGlyph(fd, cp);
        if (idx < 0) continue;

        if(!first) x += spacing;
        first = false;
        x += fd.glyphs[(size_t)idx].advanceX * scale;
    }

    return {std::max(maxW, x), lh * (float)lines};
}

IFont QuarkGLRenderer::LoadFont(const char* filePath, int fontSize,
                                const int* codepoints, int codepointCount) {
    if (!filePath) {
        IFont handle{};
        handle.id = m_font.EnsureDefaultFont();
        return handle;
    }

    qc::FontData fd{};
    if (!m_font.LoadFontInternal(filePath, nullptr, 0, fontSize, codepoints, codepointCount, fd)) {
        return IFont{};
    }

    const uint32_t id = m_font.AddFont(std::move(fd));
    IFont handle{};
    handle.id = id;
    return handle;
}

IFont QuarkGLRenderer::LoadFontFromMemory(const char* fileType, const unsigned char* fileData, int dataSize,
                                          int fontSize, const int* codepoints, int codepointCount) {
    if (!fileData || dataSize <= 0) {
        return IFont{};
    }

    qc::FontData fd{};
    if (!m_font.LoadFontInternal(fileType, fileData, dataSize, fontSize, codepoints, codepointCount, fd)) {
        return IFont{};
    }

    const uint32_t id = m_font.AddFont(std::move(fd));
    IFont handle{};
    handle.id = id;
    return handle;
}

void QuarkGLRenderer::UnloadFont(IFont& font) {
    auto it = m_fonts.find(font.id);
    if (it != m_fonts.end()) {
        GLuint atlasId = it->second.atlasTexture;
        if (it->second.atlasTexture)
            glDeleteTextures(1, &it->second.atlasTexture);

        for (GlyphData& g : it->second.glyphs) {
            std::free(g.image.data);
            g.image = Image{};
        }

        if (font.id == m_defaultFontId) m_defaultFontId = 0;

        m_fonts.erase(it);
        TraceLog(LogLevel::Info, "FONT", TextFormat("[OpenGL] Font unloaded (Font ID: %u, Atlas ID: %u)", font.id, atlasId));
    }

    font.id = 0;
}

void QuarkGLRenderer::FillFont(IFont font, Font& out) {
    const FontData* fd = GetFontData(font);
    if (!fd) {
        uint32_t id = EnsureDefaultFont();
        fd = (id != 0) ? GetFontData(IFont{ id }) : nullptr;
    }
    if (!fd) {
        out.valid = false;
        return;
    }

    out.baseSize     = fd->baseSize;
    out.glyphCount   = fd->glyphCount;
    out.glyphPadding = 2;
    out.valid        = true;
    out._rendererFontId = font.id;
    out.texture = Texture2D{ fd->atlasTexture, fd->atlasWidth, fd->atlasHeight, 1,
                             PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, true };

    delete[] out.recs;
    delete[] out.glyphs;
    out.recs   = new Rectangle[fd->glyphCount];
    out.glyphs = new GlyphInfo[fd->glyphCount];

    for (int i = 0; i < fd->glyphCount; ++i) {
        const GlyphData& g = fd->glyphs[(size_t)i];
        out.recs[i] = g.rec;
        out.glyphs[i].value     = g.value;
        out.glyphs[i].offsetX   = (int)g.offsetX;
        out.glyphs[i].offsetY   = (int)g.offsetY;
        out.glyphs[i].advanceX  = (int)g.advanceX;
        out.glyphs[i].image     = g.image;
    }
}

void QuarkGLRenderer::DrawDebugText(const char* text, int x, int y, int fontSize, Color color) {
    if (!text || !*text || fontSize <= 0) return;

    static std::unordered_map<int, IFont> s_debugFonts;
    auto it = s_debugFonts.find(fontSize);
    if (it == s_debugFonts.end()) {
        FontData fontData{};
        if (!LoadFontInternal(nullptr, tahoma_ttf, static_cast<int>(tahoma_ttf_len), fontSize,
                              nullptr, 0, fontData)) return;

        IFont font{};
        font.id = m_nextFontId++;
        m_fonts[font.id] = std::move(fontData);
        it = s_debugFonts.emplace(fontSize, font).first;
    }

    const Color shadow = { 0, 0, 0, 255 };
    const std::array<Vec2, 9> offsets = {
        Vec2{ -1.f, -1.f }, Vec2{ 0.f, -1.f }, Vec2{ 1.f, -1.f },
        Vec2{ -1.f, 0.f }, Vec2{ 0.f, 0.f }, Vec2{ 1.f, 0.f },
        Vec2{ -1.f, 1.f }, Vec2{ 0.f, 1.f }, Vec2{ 1.f, 1.f }
    };

    for (const Vec2& delta : offsets) {
        if (delta.x == 0.f && delta.y == 0.f) continue;
        DrawTextEx(it->second, text, Vec2{ static_cast<float>(x) + delta.x, static_cast<float>(y) + delta.y }, static_cast<float>(fontSize), 0.f, shadow);
    }

    DrawTextEx(it->second, text, Vec2{ static_cast<float>(x) + 1.f, static_cast<float>(y) + 1.f }, static_cast<float>(fontSize), 0.f, shadow);
    DrawTextEx(it->second, text, Vec2{ static_cast<float>(x), static_cast<float>(y) }, static_cast<float>(fontSize), 0.f, color);
}

void QuarkGLRenderer::DrawText(const char* text, int x, int y, int fontSize, Color color) {
    uint32_t id = EnsureDefaultFont();
    if (!id) return;

    DrawTextWithFontData(m_fonts[id], text, {(float)x,(float)y}, (float)fontSize, 0.f, color);
}

void QuarkGLRenderer::DrawTextEx(IFont font, const char* text, Vec2 pos,
                                  float fontSize, float spacing, Color tint) {
    const FontData* fd = GetFontData(font);
    if (!fd) return;

    DrawTextWithFontData(*fd, text, pos, fontSize, spacing, tint);
}

Vec2 QuarkGLRenderer::MeasureTextEx(IFont font, const char* text,
                                     float fontSize, float spacing) {
    const FontData* fd = GetFontData(font);
    if (!fd) return {};

    return MeasureTextWithFontData(*fd, text, fontSize, spacing);
}

int QuarkGLRenderer::MeasureText(const char* text, int fontSize) {
    uint32_t id = EnsureDefaultFont();
    if (!id) return 0;

    return (int)std::round(MeasureTextWithFontData(m_fonts[id], text, (float)fontSize, 0.f).x);
}

void QuarkGLRenderer::BeginShaderMode(const Shader& sh) {
    if (sh.id) {
        m_currentShader = sh.id;
        m_batch.SetCurrentShader(sh.id);
        glUseProgram(sh.id);
    }
}

void QuarkGLRenderer::EndShaderMode() {
    FlushBatch();
    m_currentShader = m_defaultShader;
    m_batch.SetCurrentShader(m_defaultShader);
    glUseProgram(m_defaultShader);
}

Shader QuarkGLRenderer::LoadShader(const char* vsFileName, const char* fsFileName) {
    TraceLog(LogLevel::Trace, "SHADER", TextFormat("[OpenGL] Loading shader files: VS='%s', FS='%s'",
        vsFileName ? vsFileName : "<none>", fsFileName ? fsFileName : "<none>"));
    std::string vsSource, fsSource;
    if (vsFileName) {
        std::ifstream vsFile(vsFileName);
        if (vsFile.is_open()) {
            vsSource.assign((std::istreambuf_iterator<char>(vsFile)),
                            (std::istreambuf_iterator<char>()));
            TraceLog(LogLevel::Trace, "SHADER", TextFormat("[OpenGL] Read vertex shader file '%s' (%zu bytes)", vsFileName, vsSource.size()));
        } else {
            TraceLog(LogLevel::Error, "SHADER", TextFormat("[OpenGL] Failed to open vertex shader file: %s", vsFileName));
            return Shader{};
        }
    }

    if (fsFileName) {
        std::ifstream fsFile(fsFileName);
        if (fsFile.is_open()) {
            fsSource.assign((std::istreambuf_iterator<char>(fsFile)),
                            (std::istreambuf_iterator<char>()));
            TraceLog(LogLevel::Trace, "SHADER", TextFormat("[OpenGL] Read fragment shader file '%s' (%zu bytes)", fsFileName, fsSource.size()));
        } else {
            TraceLog(LogLevel::Error, "SHADER", TextFormat("[OpenGL] Failed to open fragment shader file: %s", fsFileName));
            return Shader{};
        }
    }

    return LoadShaderFromMemory(vsSource.empty() ? nullptr : vsSource.c_str(),
                                fsSource.empty() ? nullptr : fsSource.c_str());
}

Shader QuarkGLRenderer::LoadShaderFromMemory(const char* vsSource, const char* fsSource) {
    GLuint vs = 0, fs = 0;
    if (vsSource) {
        vs = CompileGLShader(GL_VERTEX_SHADER, vsSource);
        if (vs == 0) return Shader{};
    }
    if (fsSource) {
        fs = CompileGLShader(GL_FRAGMENT_SHADER, fsSource);
        if (fs == 0) {
            if (vs) glDeleteShader(vs);
            return Shader{};
        }
    }

    GLuint p = glCreateProgram();
    if (vs) glAttachShader(p, vs);
    if (fs) glAttachShader(p, fs);
    glLinkProgram(p);

    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        TraceLog(LogLevel::Error, "SHADER", TextFormat("[OpenGL] Program link error: %s", log));
        glDeleteProgram(p);
        return Shader{};
    }

    GLint numUniforms = 0, numAttribs = 0;
    glGetProgramiv(p, GL_ACTIVE_UNIFORMS, &numUniforms);
    glGetProgramiv(p, GL_ACTIVE_ATTRIBUTES, &numAttribs);

    Shader result{p};
    int foundLocCount = 0;
    for (int i = 0; i < SHADER_LOC_COUNT; ++i) {
        result.locs[i] = GetShaderLocation(result, static_cast<ShaderLocationIndex>(i));
        if (result.locs[i] != -1) foundLocCount++;
    }
    TraceLog(LogLevel::Info, "SHADER", TextFormat("[OpenGL] Shader program linked successfully (ID: %u, Active Uniforms: %d, Active Attributes: %d, Standard Locs: %d/%d)",
        p, numUniforms, numAttribs, foundLocCount, SHADER_LOC_COUNT));
    return result;
}

void QuarkGLRenderer::UnloadShader(Shader& sh) {
    if (sh.id) {
        TraceLog(LogLevel::Info, "SHADER", TextFormat("[OpenGL] Shader program unloaded (ID: %u)", sh.id));
        glDeleteProgram(sh.id);
        sh.id = 0;
    }
}

bool QuarkGLRenderer::isShaderValid(Shader& sh) {
    return sh.id != 0;
}

int QuarkGLRenderer::GetShaderLocation(const Shader& sh, const char* name) {
    return sh.id ? glGetUniformLocation(sh.id, name) : -1;
}

int QuarkGLRenderer::GetShaderLocation(const Shader& sh, ShaderLocationIndex locIndex) {
    if (!sh.id || locIndex >= SHADER_LOC_COUNT) return -1;

    if (locIndex <= SHADER_LOC_VERTEX_COLOR || locIndex >= SHADER_LOC_VERTEX_BONEIDS) {
        return glGetAttribLocation(sh.id, shaderLocationNames[locIndex]);
    } else {
        return glGetUniformLocation(sh.id, shaderLocationNames[locIndex]);
    }
}

int QuarkGLRenderer::GetShaderAttributeLocation(const Shader& sh, const char* name) {
    return sh.id ? glGetAttribLocation(sh.id,name) : -1;
}

void QuarkGLRenderer::SetShaderValue(const Shader& shader, int loc, float v) {
    if (loc < 0) return;
    WithShaderProgram(shader, [&]() {
        glUniform1f(loc, v);
    });
}

void QuarkGLRenderer::SetShaderValue(const Shader& shader, int loc, int v) {
    if (loc < 0) return;
    WithShaderProgram(shader, [&]() {
        glUniform1i(loc, v);
    });
}

void QuarkGLRenderer::SetShaderValue(const Shader& shader, int loc, const Color& c) {
    if (loc < 0) return;
    WithShaderProgram(shader, [&]() {
        glUniform4f(loc, c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f);
    });
}

void QuarkGLRenderer::SetShaderValue(const Shader& shader, int loc, const Vec2& v) {
    if (loc < 0) return;
    WithShaderProgram(shader, [&]() {
        glUniform2f(loc, v.x, v.y);
    });
}

void QuarkGLRenderer::SetShaderValue(const Shader& shader, int locIndex, const Vec3& value) {
    if (locIndex < 0) return;
    WithShaderProgram(shader, [&]() {
        glUniform3f(locIndex, value.x, value.y, value.z);
    });
}

void QuarkGLRenderer::SetShaderValue(const Shader& shader, int locIndex, const Vec4& value) {
    if (locIndex < 0) return;
    WithShaderProgram(shader, [&]() {
        glUniform4f(locIndex, value.x, value.y, value.z, value.w);
    });
}

void QuarkGLRenderer::SetShaderValueMatrix(const Shader& shader, int loc, const float* m) {
    if (loc < 0 || !m) return;
    WithShaderProgram(shader, [&]() {
        glUniformMatrix4fv(loc, 1, GL_FALSE, m);
    });
}

void QuarkGLRenderer::SetShaderValueSampler(const Shader& shader, int loc, int unit) {
    if (loc < 0) return;
    WithShaderProgram(shader, [&]() {
        glUniform1i(loc, unit);
    });
}

void QuarkGLRenderer::SetShaderValue(const Shader& s, int loc, const void* value, int uniformType) {
    if (loc < 0 || !value) return;
    WithShaderProgram(s, [&]() {
        switch (uniformType) {
            case SHADER_UNIFORM_FLOAT:
                glUniform1f(loc, *reinterpret_cast<const float*>(value));
                break;
            case SHADER_UNIFORM_VEC2:
                glUniform2fv(loc, 1, reinterpret_cast<const float*>(value));
                break;
            case SHADER_UNIFORM_VEC3:
                glUniform3fv(loc, 1, reinterpret_cast<const float*>(value));
                break;
            case SHADER_UNIFORM_VEC4:
                glUniform4fv(loc, 1, reinterpret_cast<const float*>(value));
                break;
            case SHADER_UNIFORM_INT:
                glUniform1i(loc, *reinterpret_cast<const int*>(value));
                break;
            case SHADER_UNIFORM_IVEC2:
                glUniform2iv(loc, 1, reinterpret_cast<const int*>(value));
                break;
            case SHADER_UNIFORM_IVEC3:
                glUniform3iv(loc, 1, reinterpret_cast<const int*>(value));
                break;
            case SHADER_UNIFORM_IVEC4:
                glUniform4iv(loc, 1, reinterpret_cast<const int*>(value));
                break;
            case SHADER_UNIFORM_SAMPLER2D:
                glUniform1i(loc, *reinterpret_cast<const int*>(value));
                break;
            default:
                break;
        }
    });
}

void QuarkGLRenderer::SetShaderValueV(const Shader& s, int loc, const void* value, int uniformType, int count) {
    if (loc < 0 || !value || count <= 0) return;
    WithShaderProgram(s, [&]() {
        switch (uniformType) {
            case SHADER_UNIFORM_FLOAT:
                glUniform1fv(loc, count, reinterpret_cast<const float*>(value));
                break;
            case SHADER_UNIFORM_VEC2:
                glUniform2fv(loc, count, reinterpret_cast<const float*>(value));
                break;
            case SHADER_UNIFORM_VEC3:
                glUniform3fv(loc, count, reinterpret_cast<const float*>(value));
                break;
            case SHADER_UNIFORM_VEC4:
                glUniform4fv(loc, count, reinterpret_cast<const float*>(value));
                break;
            case SHADER_UNIFORM_INT:
                glUniform1iv(loc, count, reinterpret_cast<const int*>(value));
                break;
            case SHADER_UNIFORM_IVEC2:
                glUniform2iv(loc, count, reinterpret_cast<const int*>(value));
                break;
            case SHADER_UNIFORM_IVEC3:
                glUniform3iv(loc, count, reinterpret_cast<const int*>(value));
                break;
            case SHADER_UNIFORM_IVEC4:
                glUniform4iv(loc, count, reinterpret_cast<const int*>(value));
                break;
            case SHADER_UNIFORM_SAMPLER2D:
                glUniform1iv(loc, count, reinterpret_cast<const int*>(value));
                break;
            default:
                break;
        }
    });
}

void QuarkGLRenderer::SetShaderValueMatrix(const Shader& s, int loc, const Matrix& mat) {
    if (loc < 0) return;
    WithShaderProgram(s, [&]() {
        glUniformMatrix4fv(loc, 1, GL_FALSE, mat.m);
    });
}

void QuarkGLRenderer::SetShaderValueTexture(const Shader& s, int loc, const ITexture& texture) {
    if (loc < 0) return;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    SetShaderValueSampler(s, loc, 0);
}

void QuarkGLRenderer::SetShaderValueTextureUnit(const Shader& s, int loc, const ITexture& texture, int textureUnit) {
    if (loc < 0 || textureUnit < 0) return;
    glActiveTexture(GL_TEXTURE0 + textureUnit);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    SetShaderValueSampler(s, loc, textureUnit);
    glActiveTexture(GL_TEXTURE0);
}

void QuarkGLRenderer::SetTextureFilterMode(TextureFilterMode mode) {
    gTextureFilterMode = mode;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
        (mode == TextureFilterMode::Nearest) ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
        (mode == TextureFilterMode::Nearest) ? GL_NEAREST : GL_LINEAR);
}

void QuarkGLRenderer::SetTextureFilter(int filter) {
    const bool point = (filter == TEXTURE_FILTER_POINT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, point ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, point ? GL_NEAREST : GL_LINEAR);
}

void QuarkGLRenderer::SetTextureWrap(int wrap) {
    GLenum glWrap = GL_REPEAT;
    switch (wrap) {
        case TEXTURE_WRAP_CLAMP:
            glWrap = GL_CLAMP_TO_EDGE;
            break;
        case TEXTURE_WRAP_MIRROR_REPEAT:
            glWrap = GL_MIRRORED_REPEAT;
            break;
        case TEXTURE_WRAP_MIRROR_CLAMP:
            glWrap = GL_MIRRORED_REPEAT;
            break;
        case TEXTURE_WRAP_REPEAT:
        default:
            glWrap = GL_REPEAT;
            break;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, glWrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, glWrap);
}

void QuarkGLRenderer::BeginScissorMode(int x, int y, int width, int height) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, width, height);
}

void QuarkGLRenderer::EndScissorMode() {
    glDisable(GL_SCISSOR_TEST);
}

void QuarkGLRenderer::SetBlendMode(int mode) {
    glEnable(GL_BLEND);

    switch (mode) {
        case BLEND_ADDITIVE:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case BLEND_MULTIPLIED:
            glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BLEND_ADD_COLORS:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case BLEND_SUBTRACT_COLORS:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_COLOR);
            break;
        case BLEND_MOD_COLOR:
            glBlendFunc(GL_DST_COLOR, GL_ZERO);
            break;
        case BLEND_ALPHA:
        default:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
    }
}

void QuarkGLRenderer::BeginMode2D(const Camera2D& cam) {
    m_camera2D = cam;
    m_camera2DActive = true;
    m_batch.SetCamera(cam, true);
}

void QuarkGLRenderer::EndMode2D() {
    m_camera2DActive = false;
    m_batch.SetCamera(m_camera2D, false);
}

void QuarkGLRenderer::BeginMode3D(const Camera3D& camera) {
    Init3DState();
    Init3DGeometry();

    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glUseProgram(m_3d.shader3D);

    Mat4 view = Mat4::lookAt(camera.position, camera.target, camera.up);
    float asp = (float)m_width / (float)m_height;
    Mat4 proj = Mat4::perspective(camera.fovy * PI / 180.f, asp, 0.1f, 1000.f);

    Set3DView(view,proj);

    if(m_3d.samplerLoc >= 0)
        glUniform1i(m_3d.samplerLoc, 0);
    if(m_3d.colorLoc >= 0)
        glUniform4f(m_3d.colorLoc, 1, 1, 1, 1);
    if(m_3d.lightPosLoc >= 0)
        glUniform3f(m_3d.lightPosLoc,
            m_3d.lightPosition.x, m_3d.lightPosition.y, m_3d.lightPosition.z);
}

void QuarkGLRenderer::EndMode3D() {
    FlushLines3D();
    FlushTriangles3D();

    glDisable(GL_DEPTH_TEST);
    glUseProgram(0);
}

void QuarkGLRenderer::PushMatrix() {
    m_matrixStack.push_back(m_currentMatrix);
}

void QuarkGLRenderer::PopMatrix()  {
    if(!m_matrixStack.empty()) {
        m_currentMatrix = m_matrixStack.back();
        m_matrixStack.pop_back();
    }
    else
        m_currentMatrix = Mat4::identity();
}

void QuarkGLRenderer::Translate(const Vec3& t) {
    m_currentMatrix = m_currentMatrix * Mat4::translation(t.x, t.y, t.z);
}

void QuarkGLRenderer::Rotate(float angle, const Vec3& axis) {
    Vec3 a = axis;
    float len = a.length();
    if(len <= 0) return;
    a = a * (1 / len);

    float c = cosf(angle);
    float s = sinf(angle);
    float t = 1 - c;

    Mat4 r = Mat4::identity();
    r.m[0] = c + a.x * a.x * t;
    r.m[1] = a.x * a.y * t + a.z * s;
    r.m[2] = a.x * a.z * t - a.y * s;

    r.m[4] = a.y * a.x * t - a.z * s;
    r.m[5] = c + a.y * a.y * t; 
    r.m[6] = a.y * a.z * t + a.x * s;

    r.m[8] = a.z * a.x * t + a.y * s;
    r.m[9] = a.z * a.y * t - a.x * s;
    r.m[10] = c + a.z * a.z * t;

    m_currentMatrix = m_currentMatrix * r;
}

void QuarkGLRenderer::Scale(const Vec3& s) {
    m_currentMatrix = m_currentMatrix * Mat4::scale(s.x, s.y, s.z);
}

void QuarkGLRenderer::MultMatrix(const Mat4& m) {
    m_currentMatrix = m_currentMatrix * m;
}

const float* QuarkGLRenderer::GetMatrixModelview() {
    return m_3d.viewMatrix.m;
}

const float* QuarkGLRenderer::GetMatrixProjection() {
    return m_3d.projectionMatrix.m;
}

void QuarkGLRenderer::EnableBackfaceCulling() {
    glEnable(GL_CULL_FACE);

    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
}

void QuarkGLRenderer::DisableBackfaceCulling() {
    glDisable(GL_CULL_FACE);
}

void QuarkGLRenderer::Init3DState() {
    if(m_3d.initialized) return;

    m_3d.shader3D    = Compile3DShader();

    m_3d.modelLoc    = glGetUniformLocation(m_3d.shader3D, "uModel");
    m_3d.viewLoc     = glGetUniformLocation(m_3d.shader3D, "uView");
    m_3d.projLoc     = glGetUniformLocation(m_3d.shader3D, "uProjection");
    m_3d.samplerLoc  = glGetUniformLocation(m_3d.shader3D, "uTexture");
    m_3d.lightPosLoc = glGetUniformLocation(m_3d.shader3D, "uLightPos");
    m_3d.colorLoc    = glGetUniformLocation(m_3d.shader3D, "uColor");

    m_3d.initialized = true;

    const uint8_t white[4] = {255, 255, 255, 255};
    glGenTextures(1, &m_3d.whiteTexture);

    glBindTexture(GL_TEXTURE_2D, m_3d.whiteTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    const uint8_t black[4] = {0, 0, 0, 255};
    glGenTextures(1, &m_3d.blackTexture);

    glBindTexture(GL_TEXTURE_2D, m_3d.blackTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    const uint8_t flatNormal[4] = {128, 128, 255, 255};
    glGenTextures(1, &m_3d.flatNormalTexture);

    glBindTexture(GL_TEXTURE_2D, m_3d.flatNormalTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, flatNormal);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

GLuint QuarkGLRenderer::Compile3DShader() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &kVS3D, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &kFS3D, nullptr);
    glCompileShader(fs);

    GLuint p = glCreateProgram();

    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);

    glDeleteShader(vs);
    glDeleteShader(fs);

    return p;
}

void QuarkGLRenderer::Init3DGeometry() {
    if(m_3d.planeVAO != 0) return;

    auto setup=[](GLuint& vao, GLuint& vbo, GLuint& ebo,
                  const float* vd, size_t vs,const unsigned int* id, size_t is) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vs, vd, GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)is, id, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24);

        glBindVertexArray(0);
    };

    // Plane
    float pv[] = {-0.5f, 0, -0.5f, 0, 1, 0, 0, 0, 0.5f, 0, -0.5f, 0,1, 0, 1, 0,
                 0.5f, 0, 0.5f, 0, 1, 0, 1, 1, -0.5f, 0, 0.5f, 0, 1, 0, 0, 1};
    unsigned int pi[] = {0, 1, 2, 0, 2, 3};
    m_3d.planeIndexCount = 6;
    setup(m_3d.planeVAO, m_3d.planeVBO, m_3d.planeEBO, pv, sizeof(pv), pi, sizeof(pi));

    // Cube
    float cv[] = {
        -0.5f, -0.5f,  0.5f,  0,  0, 1, 0, 0, 0.5f, -0.5f, 0.5f, 0, 0, 1, 1, 0, 0.5f, 0.5f, 0.5f, 0, 0, 1, 1, 1, -0.5f, 0.5f, 0.5f, 0, 0, 1, 0, 1,
        -0.5f, -0.5f, -0.5f,  0,  0, -1, 0, 0, -0.5f, 0.5f, -0.5f, 0, 0, -1, 1, 0, 0.5f, 0.5f, -0.5f, 0, 0, -1, 1, 1, 0.5f, -0.5f, -0.5f, 0, 0, -1, 0, 1,
        -0.5f,  0.5f, -0.5f,  0,  1, 0, 0, 0, -0.5f, 0.5f, 0.5f, 0, 1, 0, 1, 0, 0.5f, 0.5f, 0.5f, 0, 1, 0, 1, 1, 0.5f, 0.5f, -0.5f, 0, 1, 0, 0, 1,
        -0.5f, -0.5f, -0.5f,  0, -1, 0, 0, 0, 0.5f, -0.5f, -0.5f, 0, -1, 0, 1, 0, 0.5f, -0.5f, 0.5f, 0, -1, 0, 1, 1, -0.5f, -0.5f, 0.5f, 0, -1, 0, 0, 1,
         0.5f, -0.5f, -0.5f,  1,  0, 0, 0, 0, 0.5f, 0.5f, -0.5f, 1, 0, 0, 1, 0, 0.5f, 0.5f, 0.5f, 1, 0, 0, 1, 1, 0.5f,-0.5f, 0.5f, 1, 0, 0, 0, 1,
        -0.5f, -0.5f, -0.5f,  -1, 0, 0, 0, 0, -0.5f, -0.5f, 0.5f, -1, 0, 0, 1, 0, -0.5f, 0.5f, 0.5f, -1, 0, 0, 1, 1, -0.5f, 0.5f, -0.5f, -1, 0, 0, 0, 1
    };
    unsigned int ci[] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11, 12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};
    m_3d.cubeIndexCount = 36;
    setup(m_3d.cubeVAO, m_3d.cubeVBO, m_3d.cubeEBO, cv, sizeof(cv), ci, sizeof(ci));

    // Sphere
    std::vector<float> sv;
    std::vector<unsigned int> si;

    const int R = 16, S = 16;
    for(int r = 0; r <= R; ++r) {
        float phi = PI * r / R;
        for(int s = 0; s <= S; ++s) {
            float th = 2 * PI * s /S;
            float x = sinf(phi) * cosf(th), y = cosf(phi), z = sinf(phi) * sinf(th);
            sv.insert(sv.end(), {x, y, z, x, y, z, (float)s / S, (float)r / R});
        }
    }
    for (int r = 0; r < R; ++r) {
        for (int s = 0; s < S; ++s) {

            si.push_back(r * (S + 1) + s);
            si.push_back((r + 1) * (S + 1) + s);
            si.push_back((r + 1) * (S + 1) + (s + 1));

            si.push_back(r * (S + 1) + s);
            si.push_back((r + 1) * (S + 1) + (s + 1));
            si.push_back(r * (S + 1) + (s + 1));
        }
    }

    m_3d.sphereIndexCount = static_cast<int>(si.size());

    setup(
        m_3d.sphereVAO,
        m_3d.sphereVBO,
        m_3d.sphereEBO,
        sv.data(),
        sv.size() * 4,
        si.data(),
        si.size() * 4
    );

    // Line / Triangle dynamic VAOs
    auto dynVao = [](GLuint& vao, GLuint& vbo) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof(Vertex3D),
            (void*)offsetof(Vertex3D, position)
        );

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof(Vertex3D),
            (void*)offsetof(Vertex3D, normal)
        );

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(
            2,
            2,
            GL_FLOAT,
            GL_FALSE,
            sizeof(Vertex3D),
            (void*)offsetof(Vertex3D, texCoord)
        );

        glBindVertexArray(0);
    };

    dynVao(m_3d.lineVAO, m_3d.lineVBO);
    dynVao(m_3d.triVAO, m_3d.triVBO);
}

void QuarkGLRenderer::Set3DView(const Mat4& view,const Mat4& proj) {
    m_3d.viewMatrix = view;
    m_3d.projectionMatrix = proj;

    if(m_3d.initialized) {
        glUniformMatrix4fv(m_3d.viewLoc, 1, GL_FALSE, m_3d.viewMatrix.m);
        glUniformMatrix4fv(m_3d.projLoc, 1, GL_FALSE, m_3d.projectionMatrix.m);
    }
}

Vec3 QuarkGLRenderer::TransformPoint(const Mat4& m,const Vec3& p) const {
    float x =
        m.m[0]  * p.x +
        m.m[4]  * p.y +
        m.m[8]  * p.z +
        m.m[12];

    float y =
        m.m[1]  * p.x +
        m.m[5]  * p.y +
        m.m[9]  * p.z +
        m.m[13];

    float z =
        m.m[2]  * p.x +
        m.m[6]  * p.y +
        m.m[10] * p.z +
        m.m[14];

    float w =
        m.m[3]  * p.x +
        m.m[7]  * p.y +
        m.m[11] * p.z +
        m.m[15];

    if (w != 0.0f) {
        x /= w;
        y /= w;
        z /= w;
    }

    return {
        x,
        y,
        z
    };
}

Mat4 QuarkGLRenderer::ApplyCurrentMatrix(const Mat4& t) const {
    return m_currentMatrix * t;
}

void QuarkGLRenderer::FlushLines3D() {
    if (m_3d.lineVertices.empty()) return;

    glUseProgram(m_3d.shader3D);
    if (m_3d.viewLoc >= 0) glUniformMatrix4fv(m_3d.viewLoc, 1, GL_FALSE, m_3d.viewMatrix.m);
    if (m_3d.projLoc >= 0) glUniformMatrix4fv(m_3d.projLoc, 1, GL_FALSE, m_3d.projectionMatrix.m);

    Mat4 id = Mat4::identity();
    if (m_3d.modelLoc >= 0) glUniformMatrix4fv(m_3d.modelLoc, 1, GL_FALSE, id.m);
    if (m_3d.colorLoc >= 0) {
        glUniform4f(m_3d.colorLoc,
                    m_3d.currentLineColor.r / 255.f,
                    m_3d.currentLineColor.g / 255.f,
                    m_3d.currentLineColor.b / 255.f,
                    m_3d.currentLineColor.a / 255.f);
    }

    glBindTexture(GL_TEXTURE_2D, m_3d.whiteTexture);
    glBindVertexArray(m_3d.lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_3d.lineVBO);

    glBufferData(GL_ARRAY_BUFFER,
        (GLsizeiptr)(m_3d.lineVertices.size() * sizeof(Vertex3D)),
        m_3d.lineVertices.data(),
        GL_DYNAMIC_DRAW
    );

    glDrawArrays(GL_LINES, 0, (GLsizei)m_3d.lineVertices.size());

    glBindVertexArray(0);
    m_3d.lineVertices.clear();
}

void QuarkGLRenderer::FlushTriangles3D() {
    if (m_3d.triVertices.empty()) return;

    glUseProgram(m_3d.shader3D);
    if (m_3d.viewLoc >= 0) glUniformMatrix4fv(m_3d.viewLoc, 1, GL_FALSE, m_3d.viewMatrix.m);
    if (m_3d.projLoc >= 0) glUniformMatrix4fv(m_3d.projLoc, 1, GL_FALSE, m_3d.projectionMatrix.m);

    Mat4 id = Mat4::identity();
    if (m_3d.modelLoc >= 0) glUniformMatrix4fv(m_3d.modelLoc, 1, GL_FALSE, id.m);
    if (m_3d.colorLoc >= 0) {
        glUniform4f(m_3d.colorLoc,
                    m_3d.currentTriColor.r / 255.f,
                    m_3d.currentTriColor.g / 255.f,
                    m_3d.currentTriColor.b / 255.f,
                    m_3d.currentTriColor.a / 255.f);
    }

    glBindTexture(GL_TEXTURE_2D, m_3d.whiteTexture);
    glBindVertexArray(m_3d.triVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_3d.triVBO);

    glBufferData(GL_ARRAY_BUFFER,
        (GLsizeiptr)(m_3d.triVertices.size() * sizeof(Vertex3D)),
        m_3d.triVertices.data(),
        GL_DYNAMIC_DRAW
    );

    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_3d.triVertices.size());

    glBindVertexArray(0);
    m_3d.triVertices.clear();
}

void QuarkGLRenderer::DrawTriangle3DImpl(Vertex3D v1,Vertex3D v2,Vertex3D v3,Color color){
    if (!m_3d.triVertices.empty() &&
        (color.r != m_3d.currentTriColor.r ||
         color.g != m_3d.currentTriColor.g ||
         color.b != m_3d.currentTriColor.b ||
         color.a != m_3d.currentTriColor.a)) {
        FlushTriangles3D();
    }

    m_3d.currentTriColor = color;

    if (m_3d.colorLoc >= 0)
        glUniform4f(
            m_3d.colorLoc,
            color.r / 255.f,
            color.g / 255.f,
            color.b / 255.f,
            color.a / 255.f
        );

    m_3d.triVertices.push_back({
        TransformPoint(m_currentMatrix, v1.position),
        v1.normal,
        v1.texCoord
    });

    m_3d.triVertices.push_back({
        TransformPoint(m_currentMatrix, v2.position),
        v2.normal,
        v2.texCoord
    });

    m_3d.triVertices.push_back({
        TransformPoint(m_currentMatrix, v3.position),
        v3.normal,
        v3.texCoord
    });
}

void QuarkGLRenderer::DrawLine3D(Vec3 s,Vec3 e,Color color){
    if (!m_3d.lineVertices.empty() &&
        (color.r != m_3d.currentLineColor.r ||
         color.g != m_3d.currentLineColor.g ||
         color.b != m_3d.currentLineColor.b ||
         color.a != m_3d.currentLineColor.a)) {
        FlushLines3D();
    }

    m_3d.currentLineColor = color;

    if (m_3d.colorLoc >= 0)
        glUniform4f(
            m_3d.colorLoc,
            color.r / 255.f,
            color.g / 255.f,
            color.b / 255.f,
            color.a / 255.f
        );

    m_3d.lineVertices.push_back({
        TransformPoint(m_currentMatrix, s),
        { 0, 1, 0 },
        { 0, 0 }
    });

    m_3d.lineVertices.push_back({
        TransformPoint(m_currentMatrix, e),
        { 0, 1, 0 },
        { 0, 0 }
    });
}

void QuarkGLRenderer::DrawPlane(Vec3 c,Vec2 size,Color color){
    FlushLines3D();
    FlushTriangles3D();

    const Mat4 transform = ApplyCurrentMatrix(
        Mat4::translation(c.x, c.y, c.z) * Mat4::scale(size.x, 1.0f, size.y));
    QuarkGL3D::ApplyDrawState(m_3d, transform, color, m_3d.whiteTexture);

    glBindVertexArray(m_3d.planeVAO);
    glDrawElements(GL_TRIANGLES, m_3d.planeIndexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void QuarkGLRenderer::DrawCube(Vec3 pos,float w,float h,float l,Color color){
    FlushLines3D();
    FlushTriangles3D();

    const Mat4 transform = ApplyCurrentMatrix(
        Mat4::translation(pos.x, pos.y, pos.z) * Mat4::scale(w, h, l));
    QuarkGL3D::ApplyDrawState(m_3d, transform, color, m_3d.whiteTexture);

    glBindVertexArray(m_3d.cubeVAO);
    glDrawElements(GL_TRIANGLES, m_3d.cubeIndexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void QuarkGLRenderer::DrawCubeV(Vec3 p, Vec3 s, Color c) {
    DrawCube(p, s.x, s.y, s.z, c);
}

void QuarkGLRenderer::DrawCubeWires(Vec3 pos, float w, float h, float l, Color color) {
    float hw = w * 0.5f;
    float hh = h * 0.5f;
    float hl = l * 0.5f;

    Vec3 v[8] = {
        pos + Vec3{ -hw, -hh, -hl },
        pos + Vec3{  hw, -hh, -hl },
        pos + Vec3{  hw,  hh, -hl },
        pos + Vec3{ -hw,  hh, -hl },

        pos + Vec3{ -hw, -hh,  hl },
        pos + Vec3{  hw, -hh,  hl },
        pos + Vec3{  hw,  hh,  hl },
        pos + Vec3{ -hw,  hh,  hl }
    };

    DrawLine3D(v[0], v[1], color);
    DrawLine3D(v[1], v[2], color);
    DrawLine3D(v[2], v[3], color);
    DrawLine3D(v[3], v[0], color);

    DrawLine3D(v[4], v[5], color);
    DrawLine3D(v[5], v[6], color);
    DrawLine3D(v[6], v[7], color);
    DrawLine3D(v[7], v[4], color);

    DrawLine3D(v[0], v[4], color);
    DrawLine3D(v[1], v[5], color);
    DrawLine3D(v[2], v[6], color);
    DrawLine3D(v[3], v[7], color);
}

void QuarkGLRenderer::DrawCubeWiresV(Vec3 p, Vec3 s, Color c) {
    DrawCubeWires(p, s.x, s.y, s.z, c);
}

void QuarkGLRenderer::DrawSphere(Vec3 pos, float r, Color color) {
    FlushLines3D();
    FlushTriangles3D();

    const Mat4 transform = ApplyCurrentMatrix(
        Mat4::translation(pos.x, pos.y, pos.z) * Mat4::scale(r, r, r));
    QuarkGL3D::ApplyDrawState(m_3d, transform, color, m_3d.whiteTexture);

    glBindVertexArray(m_3d.sphereVAO);
    glDrawElements(GL_TRIANGLES, m_3d.sphereIndexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void QuarkGLRenderer::DrawSphereEx(Vec3 c, float r, int rings, int slices, Color color) {
    for (int ri = 0; ri < rings; ++ri) {
        for (int si = 0; si < slices; ++si) {

            float phi1 = PI * ri / rings;
            float phi2 = PI * (ri + 1) / rings;

            float theta1 = 2.0f * PI * si / slices;
            float theta2 = 2.0f * PI * (si + 1) / slices;

            Vec3 a = {
                r * sinf(phi1) * cosf(theta1),
                r * cosf(phi1),
                r * sinf(phi1) * sinf(theta1)
            };

            Vec3 b = {
                r * sinf(phi1) * cosf(theta2),
                r * cosf(phi1),
                r * sinf(phi1) * sinf(theta2)
            };

            Vec3 d = {
                r * sinf(phi2) * cosf(theta1),
                r * cosf(phi2),
                r * sinf(phi2) * sinf(theta1)
            };

            Vec3 e = {
                r * sinf(phi2) * cosf(theta2),
                r * cosf(phi2),
                r * sinf(phi2) * sinf(theta2)
            };

            DrawTriangle3DImpl(
                { c + a, a.normalized(), {0, 0} },
                { c + b, b.normalized(), {0, 0} },
                { c + e, e.normalized(), {0, 0} },
                color
            );

            DrawTriangle3DImpl(
                { c + a, a.normalized(), {0, 0} },
                { c + e, e.normalized(), {0, 0} },
                { c + d, d.normalized(), {0, 0} },
                color
            );
        }
    }
}

void QuarkGLRenderer::DrawSphereWires(Vec3 c, float r, int rings, int slices, Color color) {
    for(int ri = 0; ri <= rings; ++ri) {
        float phi = PI * ri / rings;

        for(int si = 0; si < slices; ++si) {
            float t1= 2 * PI * si / slices, t2 = 2 * PI * (si + 1) / slices;
            DrawLine3D(c+ Vec3{r * sinf(phi) * cosf(t1), r * cosf(phi), r * sinf(phi) * sinf(t1)},
                       c+ Vec3{r * sinf(phi) * cosf(t2), r * cosf(phi), r * sinf(phi) * sinf(t2)}, color);
        }
    }
    for(int si = 0; si < slices; ++si) {
        float th = 2 * PI * si / slices;

        for(int ri = 0; ri < rings; ++ri){
            float p1 = PI * ri / rings, p2 = PI * (ri + 1) / rings;
            DrawLine3D(c + Vec3{r * sinf(p1) * cosf(th), r * cosf(p1), r * sinf(p1) * sinf(th)},
                       c + Vec3{r * sinf(p2) * cosf(th), r * cosf(p2), r * sinf(p2) * sinf(th)}, color);
        }
    }
}

void QuarkGLRenderer::DrawCylinder(Vec3 pos, float rTop, float rBot, float h, int sl, Color color) {
    DrawCylinderEx(pos + Vec3{0, -h / 2, 0}, pos + Vec3{0, h / 2, 0}, rBot, rTop, sl, color);
}

void QuarkGLRenderer::DrawCylinderEx(Vec3 s, Vec3 e, float rs, float re, int sides, Color color) {
    if(sides < 3) return;

    Vec3 dir = e - s;
    float len = dir.length();
    if(len < 1e-6f) return;
    dir = dir * (1 / len);

    Vec3 up{0, 1, 0};
    if(fabsf(dir.dot(up)) > 0.99f)
        up = {1, 0, 0};
    Vec3 xd = dir.cross(up).normalized(), yd = dir.cross(xd).normalized();

    for(int i = 0; i < sides; ++i) {
        float a1 = 2 * PI * i / sides, a2 = 2 * PI * (i + 1) / sides;
        Vec3 p1 = s + xd * cosf(a1) * rs + yd * sinf(a1) * rs, p2 = s + xd * cosf(a2) * rs + yd * sinf(a2) * rs;
        Vec3 p3 = e + xd * cosf(a2) * re + yd * sinf(a2) * re, p4 = e + xd * cosf(a1) * re + yd * sinf(a1) * re;

        DrawTriangle3DImpl({p1, (p1 - s).normalized(), {0, 0}}, {p2, (p2 - s).normalized(), {0, 0}}, {p3, (p3 - e).normalized(), {0, 0}}, color);
        DrawTriangle3DImpl({p1, (p1 - s).normalized(), {0, 0}}, {p3, (p3 - e).normalized(), {0, 0}}, {p4, (p4 - e).normalized(), {0, 0}}, color);
        DrawTriangle3DImpl({s, dir * -1, {0, 0}}, {p2, dir * -1, {0, 0}}, {p1, dir * -1, {0, 0}}, color);
        DrawTriangle3DImpl({e, dir, {0, 0}}, {p3, dir, {0, 0}}, {p4, dir, {0, 0}}, color);
    }
}

void QuarkGLRenderer::DrawCylinderWires(Vec3 pos, float rTop, float rBot, float h, int sl, Color color) {
    DrawCylinderWiresEx(pos + Vec3{0, -h / 2, 0}, pos + Vec3{0, h / 2, 0}, rBot, rTop, sl, color);
}

void QuarkGLRenderer::DrawCylinderWiresEx(Vec3 s, Vec3 e, float rs, float re, int sl, Color color) {
    if(sl < 3) return;

    Vec3 dir = e - s;
    float len = dir.length();
    if(len < 1e-6f) return;
    dir = dir * (1 / len);

    Vec3 up{0, 1, 0};
    if(fabsf(dir.dot(up)) > 0.99f) up = {1, 0, 0};

    Vec3 xd = dir.cross(up).normalized(), yd = dir.cross(xd).normalized();

    for(int i = 0; i < sl; ++i) {
        float a1 = 2 * PI * i / sl, a2 = 2 * PI * (i + 1) / sl;
        Vec3 p1 = s + xd * cosf(a1) * rs + yd * sinf(a1) * rs, p2 = s + xd * cosf(a2) * rs + yd * sinf(a2) * rs;
        Vec3 p3 = e + xd * cosf(a1) * re + yd * sinf(a1) * re, p4 = e + xd * cosf(a2) * re + yd * sinf(a2) * re;

        DrawLine3D(p1, p2, color);
        DrawLine3D(p3, p4, color);
        DrawLine3D(p1, p3, color);
    }
}

void QuarkGLRenderer::DrawGrid(int slices,float spacing,Color color) {
    float half = (float)slices * spacing / 2;
    for(int i = 0; i <= slices; ++i){
        float f =- half + (float)i * spacing;

        DrawLine3D({f, 0, -half}, {f, 0, half}, color);
        DrawLine3D({-half, 0, f}, {half, 0, f}, color);
    }
}

Model QuarkGLRenderer::LoadModel(const char* filePath) {
    TraceLog(LogLevel::Info, "MODEL", TextFormat("[OpenGL] Loading 3D model: %s", filePath ? filePath : "<null>"));
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filePath, aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        TraceLog(LogLevel::Error, "MODEL", TextFormat("[OpenGL] Failed to load model %s: %s", filePath ? filePath : "<null>", importer.GetErrorString()));
        return Model{};
    }

    TraceLog(LogLevel::Trace, "MODEL", TextFormat("[OpenGL] Assimp scene parsed: %u meshes, %u materials, %u textures, %u animations",
        scene->mNumMeshes, scene->mNumMaterials, scene->mNumTextures, scene->mNumAnimations));

    Model model{};
    model.meshCount = scene->mNumMeshes;
    model.meshes = new Mesh[model.meshCount];
    model.materialCount = scene->mNumMaterials;
    model.materials = new Material[model.materialCount];
    model.meshMaterial = new int[model.meshCount];

    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        aiMaterial* material = scene->mMaterials[i];
        Material& mat = model.materials[i];
        mat = {};
        mat.maps = new MaterialMap[12];

        aiColor4D diffuse;
        if (AI_SUCCESS == aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &diffuse)) {
            mat.maps[MATERIAL_MAP_ALBEDO].color = Color{
                static_cast<unsigned char>(diffuse.r * 255),
                static_cast<unsigned char>(diffuse.g * 255),
                static_cast<unsigned char>(diffuse.b * 255),
                static_cast<unsigned char>(diffuse.a * 255)
            };
        }

        aiString path;
        std::string materialDirectory = filePath ? filePath : "";
        const size_t lastSlash = materialDirectory.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            materialDirectory = materialDirectory.substr(0, lastSlash + 1);
        } else {
            materialDirectory = "";
        }

        const std::array<std::pair<int, aiTextureType>, 7> textureTypes = {{
            { MATERIAL_MAP_ALBEDO, aiTextureType_BASE_COLOR },
            { MATERIAL_MAP_ALBEDO, aiTextureType_DIFFUSE },
            { MATERIAL_MAP_METALNESS, aiTextureType_METALNESS },
            { MATERIAL_MAP_NORMAL, aiTextureType_NORMALS },
            { MATERIAL_MAP_ROUGHNESS, aiTextureType_DIFFUSE_ROUGHNESS },
            { MATERIAL_MAP_OCCLUSION, aiTextureType_AMBIENT_OCCLUSION },
            { MATERIAL_MAP_EMISSION, aiTextureType_EMISSION_COLOR }
        }};
        for (const auto& [mapIndex, textureType] : textureTypes) {
            if (mat.maps[mapIndex].texture.valid) continue;
            if (AI_SUCCESS != material->GetTexture(textureType, 0, &path)) continue;

            std::string texturePath = materialDirectory + path.C_Str();
            TraceLog(LogLevel::Trace, "MODEL",
                     TextFormat("[OpenGL] Model material #%u loading map %d texture: %s",
                                i, mapIndex, texturePath.c_str()));
            ITexture loadedTex = this->LoadTexture(texturePath.c_str());
            mat.maps[mapIndex].texture.id = loadedTex.id;
            mat.maps[mapIndex].texture.width = loadedTex.width;
            mat.maps[mapIndex].texture.height = loadedTex.height;
            mat.maps[mapIndex].texture.mipmaps = loadedTex.mipmaps;
            mat.maps[mapIndex].texture.format = loadedTex.format;
            mat.maps[mapIndex].texture.valid = loadedTex.valid;
        }
    }

    int totalVertices = 0;
    int totalTriangles = 0;

    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        aiMesh* mesh = scene->mMeshes[i];
        Mesh& qMesh = model.meshes[i];
        qMesh = {};

        std::vector<float> vertices;
        std::vector<unsigned short> indices;

        for (unsigned int j = 0; j < mesh->mNumVertices; ++j) {
            vertices.push_back(mesh->mVertices[j].x);
            vertices.push_back(mesh->mVertices[j].y);
            vertices.push_back(mesh->mVertices[j].z);

            if (mesh->HasNormals()) {
                vertices.push_back(mesh->mNormals[j].x);
                vertices.push_back(mesh->mNormals[j].y);
                vertices.push_back(mesh->mNormals[j].z);
            } else {
                vertices.push_back(0.0f); vertices.push_back(0.0f); vertices.push_back(0.0f);
            }

            if (mesh->HasTextureCoords(0)) {
                vertices.push_back(mesh->mTextureCoords[0][j].x);
                vertices.push_back(mesh->mTextureCoords[0][j].y);
            } else {
                vertices.push_back(0.0f); vertices.push_back(0.0f);
            }
        }

        for (unsigned int j = 0; j < mesh->mNumFaces; ++j) {
            aiFace face = mesh->mFaces[j];
            for (unsigned int k = 0; k < face.mNumIndices; ++k) {
                indices.push_back(static_cast<unsigned short>(face.mIndices[k]));
            }
        }

        qMesh.vertexCount = mesh->mNumVertices;
        qMesh.triangleCount = mesh->mNumFaces;
        totalVertices += qMesh.vertexCount;
        totalTriangles += qMesh.triangleCount;

        qMesh.vertices = new float[qMesh.vertexCount * 3];
        qMesh.normals = new float[qMesh.vertexCount * 3];
        qMesh.texcoords = new float[qMesh.vertexCount * 2];
        qMesh.indices = new unsigned short[qMesh.triangleCount * 3];

        qMesh.boneIndices = new unsigned char[static_cast<size_t>(qMesh.vertexCount) * 4u]{};
        qMesh.boneWeights = new float[static_cast<size_t>(qMesh.vertexCount) * 4u]{};
        for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
            const aiBone* bone = mesh->mBones[boneIndex];
            for (unsigned int weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
                const aiVertexWeight& weight = bone->mWeights[weightIndex];
                const unsigned int vertexIndex = weight.mVertexId;
                float* dstWeights = qMesh.boneWeights + static_cast<size_t>(vertexIndex) * 4u;
                unsigned char* dstBones = qMesh.boneIndices + static_cast<size_t>(vertexIndex) * 4u;
                for (int slot = 0; slot < 4; ++slot) {
                    if (dstWeights[slot] <= 0.0f) {
                        dstBones[slot] = static_cast<unsigned char>(boneIndex);
                        dstWeights[slot] = weight.mWeight;
                        break;
                    }
                }
            }
        }

        for (int vertexIndex = 0; vertexIndex < qMesh.vertexCount; ++vertexIndex) {
            const int packedBase = vertexIndex * 8;
            qMesh.vertices[vertexIndex * 3 + 0] = vertices[packedBase + 0];
            qMesh.vertices[vertexIndex * 3 + 1] = vertices[packedBase + 1];
            qMesh.vertices[vertexIndex * 3 + 2] = vertices[packedBase + 2];
            qMesh.normals[vertexIndex * 3 + 0] = vertices[packedBase + 3];
            qMesh.normals[vertexIndex * 3 + 1] = vertices[packedBase + 4];
            qMesh.normals[vertexIndex * 3 + 2] = vertices[packedBase + 5];
            qMesh.texcoords[vertexIndex * 2 + 0] = vertices[packedBase + 6];
            qMesh.texcoords[vertexIndex * 2 + 1] = vertices[packedBase + 7];
        }

        std::copy(indices.begin(), indices.end(), qMesh.indices);

        glGenVertexArrays(1, &qMesh.vaoId);
        glGenBuffers(1, &qMesh.vboId);
        glGenBuffers(1, &qMesh.eboId);

        glBindVertexArray(qMesh.vaoId);

        glBindBuffer(GL_ARRAY_BUFFER, qMesh.vboId);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, qMesh.eboId);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned short), indices.data(), GL_STATIC_DRAW);

        // Position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        // Normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        // TexCoords
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));

        glBindVertexArray(0);

        model.meshMaterial[i] = mesh->mMaterialIndex;

        const char* meshName = mesh->mName.length > 0 ? mesh->mName.C_Str() : "unnamed";
        TraceLog(LogLevel::Trace, "MODEL", TextFormat("[OpenGL] Mesh #%u ('%s'): %d vertices, %d triangles, VAO: %u, Material: #%d",
            i, meshName, qMesh.vertexCount, qMesh.triangleCount, qMesh.vaoId, model.meshMaterial[i]));
    }

    TraceLog(LogLevel::Info, "MODEL", TextFormat("[OpenGL] Model loaded successfully: %s (%d meshes, %d materials, %d total vertices, %d total triangles)",
        filePath ? filePath : "<null>", model.meshCount, model.materialCount, totalVertices, totalTriangles));
    qcPopulateModelSkeleton(scene, model);
    return model;
}

void  QuarkGLRenderer::UnloadModel(Model& model) {
    for (int i = 0; i < model.meshCount; ++i) {
        Mesh& mesh = model.meshes[i];
        if (mesh.vaoId)
            glDeleteVertexArrays(1, &mesh.vaoId);
        if (mesh.vboId)
            glDeleteBuffers(1, &mesh.vboId);
        if (mesh.eboId)
            glDeleteBuffers(1, &mesh.eboId);

        delete[] mesh.vertices;
        delete[] mesh.texcoords;
        delete[] mesh.texcoords2;
        delete[] mesh.normals;
        delete[] mesh.tangents;
        delete[] mesh.colors;
        delete[] mesh.indices;
        delete[] mesh.boneIndices;
        delete[] mesh.boneWeights;
        delete[] mesh.animVertices;
        delete[] mesh.animNormals;
        delete[] mesh.bindVertices;
        delete[] mesh.bindNormals;
        mesh = {};
    }

    delete[] model.meshes;
    model.meshes = nullptr;

    for (int i = 0; i < model.materialCount; ++i) {
        Material& mat = model.materials[i];

        if (mat.maps && mat.maps[MATERIAL_MAP_ALBEDO].texture.valid) {
            ITexture tempTex;
            tempTex.id = mat.maps[MATERIAL_MAP_ALBEDO].texture.id;
            tempTex.width = mat.maps[MATERIAL_MAP_ALBEDO].texture.width;
            tempTex.height = mat.maps[MATERIAL_MAP_ALBEDO].texture.height;
            tempTex.mipmaps = mat.maps[MATERIAL_MAP_ALBEDO].texture.mipmaps;
            tempTex.format = mat.maps[MATERIAL_MAP_ALBEDO].texture.format;
            tempTex.valid = mat.maps[MATERIAL_MAP_ALBEDO].texture.valid;
            this->UnloadTexture(tempTex);
        }

        delete[] mat.maps;
        mat = {};
    }
    delete[] model.materials;
    model.materials = nullptr;

    delete[] model.meshMaterial;
    model.meshMaterial = nullptr;

    TraceLog(LogLevel::Info, "MODEL", TextFormat("[OpenGL] Model unloaded (%d meshes, %d materials)", model.meshCount, model.materialCount));
    qcFreeModelSkeleton(model);
    model = {};
}

void QuarkGLRenderer::UploadMesh(Mesh& mesh, bool dynamic) {
    if (!mesh.vertices || mesh.vertexCount <= 0) return;

    if (mesh.vaoId)
        glDeleteVertexArrays(1, &mesh.vaoId);
    if (mesh.vboId)
        glDeleteBuffers(1, &mesh.vboId);
    if (mesh.eboId)
        glDeleteBuffers(1, &mesh.eboId);

    glGenVertexArrays(1, &mesh.vaoId);
    glGenBuffers(1, &mesh.vboId);

    if (mesh.indices && mesh.triangleCount > 0) glGenBuffers(1, &mesh.eboId);

    std::vector<float> vertexData;
    vertexData.reserve(mesh.vertexCount * 8);
    for (int i = 0; i < mesh.vertexCount; ++i) {
        vertexData.push_back(mesh.vertices[i * 3 + 0]);
        vertexData.push_back(mesh.vertices[i * 3 + 1]);
        vertexData.push_back(mesh.vertices[i * 3 + 2]);

        if (mesh.normals) {
            vertexData.push_back(mesh.normals[i * 3 + 0]);
            vertexData.push_back(mesh.normals[i * 3 + 1]);
            vertexData.push_back(mesh.normals[i * 3 + 2]);
        } else {
            vertexData.push_back(0.0f);
            vertexData.push_back(0.0f);
            vertexData.push_back(0.0f);
        }

        if (mesh.texcoords) {
            vertexData.push_back(mesh.texcoords[i * 2 + 0]);
            vertexData.push_back(mesh.texcoords[i * 2 + 1]);
        } else {
            vertexData.push_back(0.0f);
            vertexData.push_back(0.0f);
        }
    }

    glBindVertexArray(mesh.vaoId);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizei>(vertexData.size() * sizeof(float)),
                 vertexData.data(),
                 dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);

    if (mesh.eboId) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.eboId);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizei>(mesh.triangleCount * 3 * sizeof(unsigned short)),
                     mesh.indices, dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    }

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(0));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));

    glBindVertexArray(0);
    TraceLog(LogLevel::Trace, "MESH", TextFormat("[OpenGL] Uploaded mesh to GPU (VAO: %u, VBO: %u, EBO: %u, %d vertices, %d triangles, dynamic: %s)",
        mesh.vaoId, mesh.vboId, mesh.eboId, mesh.vertexCount, mesh.triangleCount, dynamic ? "yes" : "no"));
}

void QuarkGLRenderer::UpdateMeshBuffer(Mesh& mesh, int index, const void* data, int dataSize, int offset) {
    if (!data || dataSize <= 0) return;

    if ((index == 0 || index == 1 || index == 2) && mesh.vboId) {
        std::vector<float> vertexData;
        vertexData.reserve(mesh.vertexCount * 8);
        for (int i = 0; i < mesh.vertexCount; ++i) {
            vertexData.push_back(mesh.vertices ? mesh.vertices[i * 3 + 0] : 0.0f);
            vertexData.push_back(mesh.vertices ? mesh.vertices[i * 3 + 1] : 0.0f);
            vertexData.push_back(mesh.vertices ? mesh.vertices[i * 3 + 2] : 0.0f);

            vertexData.push_back(mesh.normals ? mesh.normals[i * 3 + 0] : 0.0f);
            vertexData.push_back(mesh.normals ? mesh.normals[i * 3 + 1] : 0.0f);
            vertexData.push_back(mesh.normals ? mesh.normals[i * 3 + 2] : 0.0f);

            vertexData.push_back(mesh.texcoords ? mesh.texcoords[i * 2 + 0] : 0.0f);
            vertexData.push_back(mesh.texcoords ? mesh.texcoords[i * 2 + 1] : 0.0f);
        }

        glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId);
        glBufferSubData(
            GL_ARRAY_BUFFER,
            0,
            static_cast<GLsizei>(vertexData.size() * sizeof(float)),
            vertexData.data()
        );
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    } else if (index == 6 && mesh.eboId) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.eboId);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, offset, dataSize, data);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
}

void QuarkGLRenderer::UnloadMesh(Mesh& mesh) {
    if (mesh.vaoId) {
        TraceLog(LogLevel::Trace, "MESH", TextFormat("[OpenGL] Unloaded mesh from GPU (VAO: %u, VBO: %u, EBO: %u)", mesh.vaoId, mesh.vboId, mesh.eboId));
        glDeleteVertexArrays(1, &mesh.vaoId);
    }
    if (mesh.vboId)
        glDeleteBuffers(1, &mesh.vboId);
    if (mesh.eboId)
        glDeleteBuffers(1, &mesh.eboId);

    mesh.vaoId = 0;
    mesh.vboId = 0;
    mesh.eboId = 0;

    delete[] mesh.vertices;
    mesh.vertices = nullptr;
    delete[] mesh.texcoords;
    mesh.texcoords = nullptr;
    delete[] mesh.texcoords2;
    mesh.texcoords2 = nullptr;
    delete[] mesh.normals;
    mesh.normals = nullptr;
    delete[] mesh.tangents;
    mesh.tangents = nullptr;
    delete[] mesh.colors;
    mesh.colors = nullptr;
    delete[] mesh.indices;
    mesh.indices = nullptr;
    delete[] mesh.boneIndices;
    mesh.boneIndices = nullptr;
    delete[] mesh.boneWeights;
    mesh.boneWeights = nullptr;
    delete[] mesh.animVertices;
    mesh.animVertices = nullptr;
    delete[] mesh.animNormals;
    mesh.animNormals = nullptr;

    mesh.vertexCount = 0;
    mesh.triangleCount = 0;
}

void QuarkGLRenderer::BindMaterialMaps(const Material& material, GLuint shaderProgram) {
    if (!material.maps) return;

    for (int shadowIndex = 0; shadowIndex < 4; ++shadowIndex) {
        const int mapIndex = MATERIAL_MAP_HEIGHT + shadowIndex;
        const GLint shadowMapLoc = glGetUniformLocation(
            shaderProgram, TextFormat("shadowMaps[%i]", shadowIndex));
        if (shadowMapLoc < 0) continue;

        glActiveTexture(GL_TEXTURE1 + shadowIndex);
        glBindTexture(GL_TEXTURE_2D, material.maps[mapIndex].texture.valid
                                         ? material.maps[mapIndex].texture.id
                                         : m_3d.whiteTexture);
        glUniform1i(shadowMapLoc, 1 + shadowIndex);
    }

    const struct {
        int slot;
        int mapIndex;
        const char* name;
        GLuint fallback;
    } pbrMaps[] = {
        { 5, MATERIAL_MAP_METALNESS, "metalnessMap", m_3d.whiteTexture },
        { 6, MATERIAL_MAP_NORMAL,    "normalMap",    m_3d.flatNormalTexture },
        { 7, MATERIAL_MAP_ROUGHNESS, "roughnessMap", m_3d.whiteTexture },
        { 8, MATERIAL_MAP_OCCLUSION, "occlusionMap", m_3d.whiteTexture },
        { 9, MATERIAL_MAP_EMISSION,  "emissionMap",  m_3d.blackTexture },
    };
    for (const auto& mapBind : pbrMaps) {
        const MaterialMap& map = material.maps[mapBind.mapIndex];
        const GLint location = glGetUniformLocation(shaderProgram, mapBind.name);
        if (location < 0) continue;

        glActiveTexture(GL_TEXTURE0 + mapBind.slot);
        glBindTexture(GL_TEXTURE_2D, map.texture.valid ? map.texture.id : mapBind.fallback);
        glUniform1i(location, mapBind.slot);
    }

    glActiveTexture(GL_TEXTURE0);
}

void QuarkGLRenderer::DrawMesh(const Mesh& mesh, const Material& material, const Mat4& transform) {
    if (!mesh.vaoId) return;
    if (!m_3d.initialized) Init3DState();

    GLuint texId = m_3d.whiteTexture;
    const bool hasTexture = material.maps && material.maps[MATERIAL_MAP_ALBEDO].texture.valid;
    if (hasTexture) {
        texId = material.maps[MATERIAL_MAP_ALBEDO].texture.id;
    }

    const Shader* customShader = ResolveMaterialShader(&material);
    if (customShader) {
        glUseProgram(customShader->id);
        ApplyMaterialShaderUniforms(*customShader, transform, m_3d.viewMatrix, m_3d.projectionMatrix, WHITE, hasTexture);
    } else {
        QuarkGL3D::ApplyDrawState(m_3d, transform, WHITE, texId);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texId);
    if (customShader && material.maps) {
        BindMaterialMaps(material, customShader->id);
    }
    glBindVertexArray(mesh.vaoId);

    if (mesh.eboId) {
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.triangleCount * 3), GL_UNSIGNED_SHORT, nullptr);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
    }

    glBindVertexArray(0);
}

void QuarkGLRenderer::DrawMeshInstanced(const Mesh& mesh, const Material& material, const Mat4* transforms, int instances) {
    if (!transforms || instances <= 0) return;

    for (int i = 0; i < instances; ++i) {
        DrawMesh(mesh, material, transforms[i]);
    }
}

void  QuarkGLRenderer::DrawModel(const Model& model, const Vec3& pos, float scale,
                                   float rx, float ry, float rz) {
    Mat4 t = Mat4::translation(pos.x, pos.y, pos.z)
          * Mat4::rotationY(ry) * Mat4::rotationX(rx) * Mat4::rotationZ(rz)
          * Mat4::scale(scale, scale, scale);

    DrawModelEx(model, t);
}

void QuarkGLRenderer::DrawModelEx(const Model& model, const Mat4& transform) {
    if (!m_3d.initialized) Init3DState();
    Mat4 final = ApplyCurrentMatrix(transform * model.transform);

    for(int i = 0; i < model.meshCount; ++i) {
        const Mesh& mesh = model.meshes[i];

        glActiveTexture(GL_TEXTURE0);
        GLuint texId = m_3d.whiteTexture;
        const Material* material = nullptr;
        const Shader* customShader = nullptr;
        bool hasTexture = false;

        if(model.meshMaterial && model.meshMaterial[i] >= 0 && model.meshMaterial[i] < model.materialCount) {
            material = &model.materials[model.meshMaterial[i]];
            customShader = ResolveMaterialShader(material);
            hasTexture = material->maps && material->maps[MATERIAL_MAP_ALBEDO].texture.valid;
            if(hasTexture)
                texId = material->maps[MATERIAL_MAP_ALBEDO].texture.id;
        }

        if (customShader) {
            glUseProgram(customShader->id);
            const Color materialColor = material && material->maps
                ? material->maps[MATERIAL_MAP_ALBEDO].color : WHITE;
            ApplyMaterialShaderUniforms(*customShader, final, m_3d.viewMatrix, m_3d.projectionMatrix,
                materialColor, hasTexture);
        } else {
            glUseProgram(m_3d.shader3D);
            if(m_3d.modelLoc >= 0) glUniformMatrix4fv(m_3d.modelLoc, 1, GL_FALSE, final.m);
            if(m_3d.colorLoc >= 0) glUniform4f(m_3d.colorLoc, 1, 1, 1, 1);
            if(m_3d.samplerLoc >= 0) glUniform1i(m_3d.samplerLoc, 0);
        }

        glBindTexture(GL_TEXTURE_2D, texId);
        if (customShader && material && material->maps) {
            BindMaterialMaps(*material, customShader->id);
        }
        glBindVertexArray(mesh.vaoId);

        if (mesh.eboId) {
            glDrawElements(GL_TRIANGLES, (GLsizei)(mesh.triangleCount * 3), GL_UNSIGNED_SHORT, nullptr);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
        }
        glBindVertexArray(0);
    }
}

void QuarkGLRenderer::DrawModelEx(const Model& model, const Mat4& transform, Color tint) {
    if (!m_3d.initialized) Init3DState();
    Mat4 final = ApplyCurrentMatrix(transform * model.transform);

    for(int i = 0; i < model.meshCount; ++i) {
        const Mesh& mesh = model.meshes[i];

        glActiveTexture(GL_TEXTURE0);

        GLuint texId = m_3d.whiteTexture;
        const Material* material = nullptr;
        const Shader* customShader = nullptr;
        bool hasTexture = false;
        if(model.meshMaterial && model.meshMaterial[i] >= 0 && model.meshMaterial[i] < model.materialCount) {
            material = &model.materials[model.meshMaterial[i]];
            customShader = ResolveMaterialShader(material);
            hasTexture = material->maps && material->maps[MATERIAL_MAP_ALBEDO].texture.valid;
            if(hasTexture)
                texId = material->maps[MATERIAL_MAP_ALBEDO].texture.id;
        }

        if (customShader) {
            glUseProgram(customShader->id);
            const Color materialColor = material && material->maps
                ? material->maps[MATERIAL_MAP_ALBEDO].color : WHITE;
            const Color combinedColor{
                static_cast<unsigned char>(materialColor.r * tint.r / 255),
                static_cast<unsigned char>(materialColor.g * tint.g / 255),
                static_cast<unsigned char>(materialColor.b * tint.b / 255),
                static_cast<unsigned char>(materialColor.a * tint.a / 255)};
            ApplyMaterialShaderUniforms(*customShader, final, m_3d.viewMatrix, m_3d.projectionMatrix,
                combinedColor, hasTexture);
        } else {
            glUseProgram(m_3d.shader3D);
            if(m_3d.modelLoc >= 0) glUniformMatrix4fv(m_3d.modelLoc, 1, GL_FALSE, final.m);
            if(m_3d.samplerLoc >= 0) glUniform1i(m_3d.samplerLoc, 0);
            if(m_3d.colorLoc >= 0) glUniform4f(m_3d.colorLoc,
                tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f, tint.a / 255.0f);
        }

        glBindTexture(GL_TEXTURE_2D, texId);
        if (customShader && material && material->maps) {
            BindMaterialMaps(*material, customShader->id);
        }
        glBindVertexArray(mesh.vaoId);

        if (mesh.eboId) {
            glDrawElements(GL_TRIANGLES, (GLsizei)(mesh.triangleCount * 3), GL_UNSIGNED_SHORT, nullptr);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
        }
        glBindVertexArray(0);
    }
}

bool QuarkGLRenderer::UpdateTexture(const ITexture& texture, const void* pixels) {
    if (!pixels || texture.id == 0) return false;
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texture.width, texture.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool QuarkGLRenderer::UpdateTextureRegion(const ITexture& texture, Rectangle region, const void* pixels) {
    if (!pixels || texture.id == 0) return false;
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(region.x), static_cast<GLint>(region.y),
                    static_cast<GLsizei>(region.width), static_cast<GLsizei>(region.height),
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

} // namespace qc
