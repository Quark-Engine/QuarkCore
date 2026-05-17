#include "QuarkGLRenderer.hpp"
#include "../QuarkInternal.hpp"

#include <png.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/DefaultLogger.hpp>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
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
uniform vec3 uLightPos;
uniform vec4 uColor;

void main() {
    vec3 norm     = normalize(vNormal);
    vec3 lightDir = normalize(uLightPos - vFragPos);
    float ambient = 0.3;
    float diff    = max(dot(norm, lightDir), 0.0);
    vec4  tex     = texture(uTexture, vTexCoord);
    vec3  result  = (ambient + diff) * tex.rgb * uColor.rgb;
    FragColor     = vec4(result, tex.a * uColor.a);
}
)";

namespace qc {

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

static bool PngSafeInit(png_structp png, FILE* f) {
    if (setjmp(png_jmpbuf(png))) return false;

    png_init_io(png, f);
    return true;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
QCAPI bool LoadPngImage(const char* path, PngImageData& out) {
    FILE* f = nullptr;

#if defined(_MSC_VER)
    if (fopen_s(&f, path, "rb") != 0) return false;
#else
    f = fopen(path, "rb");
    if (!f) return false;
#endif

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        fclose(f);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        fclose(f);
        return false;
    }

    if (!PngSafeInit(png, f)) {
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(f);
        return false;
    }

    png_read_info(png, info);
    png_uint_32 w = png_get_image_width(png, info), h = png_get_image_height(png, info);
    png_byte ct = png_get_color_type(png, info), bd = png_get_bit_depth(png, info);

    if(bd == 16) png_set_strip_16(png);
    if(ct == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if(ct == PNG_COLOR_TYPE_GRAY && bd < 8) png_set_expand_gray_1_2_4_to_8(png);

    if(png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if(ct == PNG_COLOR_TYPE_RGB || ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

    if(ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);

    png_read_update_info(png, info);
    out.pixels.resize((size_t)w * h * 4);
    std::vector<png_bytep> rows(h);
    for(png_uint_32 y = 0; y < h ; ++y) rows[y] = out.pixels.data() + (size_t)y * w * 4;

    png_read_image(png, rows.data());

    png_destroy_read_struct(&png, &info, nullptr);
    fclose(f);
    out.width  = (int)w;
    out.height = (int)h;
    return true;
}

namespace {

static bool FileExists(const char* p) {
    if(!p) return false;
    std::ifstream f(p, std::ios::binary); return f.good();
}

static const char* DefaultFontPath() {
#if defined(_WIN32)
    static const char* paths[] = {
        "C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/verdana.ttf", nullptr};
#elif defined(__APPLE__)
    static const char* paths[] = {
        "/System/Library/Fonts/SFNS.ttf", "/Library/Fonts/Arial.ttf", "/Library/Fonts/Helvetica.ttf", nullptr};
#else
    static const char* paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",nullptr};
#endif

    for(int i = 0; paths[i]; ++i) if(FileExists(paths[i])) return paths[i];
    return nullptr;
}

} // namespace

QuarkGLRenderer::~QuarkGLRenderer() {
    this->Shutdown();
}

void QuarkGLRenderer::Init(SDL_Window* window, int width, int height) {
    m_window = window;
    m_width  = width;
    m_height = height;
    m_context = SDL_GL_CreateContext(window);
    if (!m_context)
        throw std::runtime_error(std::string("SDL_GL_CreateContext: ") + SDL_GetError());
        
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
        throw std::runtime_error("glewInit failed");

    InitGL();

    m_lastFrameCounter = SDL_GetPerformanceCounter();
}

void QuarkGLRenderer::Shutdown() {
    if (m_window == nullptr) {
        return;
    }

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
    RefreshViewport();
}

void QuarkGLRenderer::EndDrawing() {
    FlushBatch();
    SDL_GL_SwapWindow(m_window);

    const std::uint64_t freq = SDL_GetPerformanceFrequency();

    if (m_targetFps > 0) {
        const std::uint64_t targetTicks = freq / static_cast<std::uint64_t>(m_targetFps);
        while (true) {
            const std::uint64_t now     = SDL_GetPerformanceCounter();
            const std::uint64_t elapsed = now - m_lastFrameCounter;
            if (elapsed >= targetTicks) break;
            const std::uint64_t remaining = targetTicks - elapsed;
            if (remaining > freq / 500) SDL_Delay(1);
        }
    }

    const std::uint64_t frameEnd = SDL_GetPerformanceCounter();
    m_frameTime          = static_cast<float>(frameEnd - m_lastFrameCounter)
                           / static_cast<float>(freq);
    m_lastFrameCounter   = frameEnd;
    m_drawing            = false;
}

void QuarkGLRenderer::ClearBackground(Color c) {
    auto n = ToNormColor(c);
    glClearColor(n[0], n[1], n[2], n[3]);
    glClear(GL_COLOR_BUFFER_BIT);
}

std::array<float,4> QuarkGLRenderer::ToNormColor(Color c) {
    constexpr float inv = 1.f / 255.f;
    return{ c.r * inv, c.g * inv, c.b * inv, c.a * inv };
}

GLuint QuarkGLRenderer::CreateTextureFromRgba(const uint8_t* px, int w, int h) {
    GLuint id = 0;

    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);

    glBindTexture(GL_TEXTURE_2D, 0);

    return id;
}

GLuint QuarkGLRenderer::CompileGLShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);

    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);

    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);

        glDeleteShader(s);
        throw std::runtime_error(std::string("Shader compile: ") + log);
    }

    return s;
}

GLuint QuarkGLRenderer::CreateDefaultProgram() {
    GLuint vs = CompileGLShader(GL_VERTEX_SHADER,   kVS2D);
    GLuint fs = CompileGLShader(GL_FRAGMENT_SHADER, kFS2D);
    GLuint p  = glCreateProgram();

    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);

    if (!ok) {
        char log[512];

        glGetProgramInfoLog(p, 512, nullptr, log);
        glDeleteProgram(p);

        throw std::runtime_error(std::string("Program link: ") + log);
    }
    return p;
}

void QuarkGLRenderer::FlushBatch() {
    if (m_batchVertices.empty()) return;

    glUseProgram(m_currentShader);

    if (m_currentShader == m_defaultShader) {
        GLint sLoc = glGetUniformLocation(m_currentShader, "uScreenSize");
        GLint tLoc = glGetUniformLocation(m_currentShader, "uTexture");
        if (sLoc >= 0) glUniform2f(sLoc, (float)m_width, (float)m_height);
        if (tLoc >= 0) glUniform1i(tLoc, 0);
    } 
    
    else {
        GLint sLoc = glGetUniformLocation(m_currentShader, "uScreenSize");
        if (sLoc >= 0) glUniform2f(sLoc, (float)m_width, (float)m_height);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_currentTexture ? m_currentTexture : m_whiteTexture);
    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
        (GLsizeiptr)(m_batchVertices.size() * sizeof(BatchVertex)),
        m_batchVertices.data(), GL_DYNAMIC_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_batchVertices.size());

    m_batchVertices.clear();
}

void QuarkGLRenderer::EnsureBatchTexture(GLuint id) {
    GLuint r = id ? id : m_whiteTexture;

    if (!m_currentTexture) m_currentTexture=r;

    if (r != m_currentTexture || m_batchVertices.size() >= kMaxBatchVertices) {
        FlushBatch();
        m_currentTexture = r;
    }
}

void QuarkGLRenderer::PushVertex(const BatchVertex& vtx) {
    if (m_batchVertices.size() >= kMaxBatchVertices)
        FlushBatch();

    BatchVertex v = vtx;

    if (m_camera2DActive) {
        Vec2 s = qc::GetWorldToScreen2D({v.x, v.y}, m_camera2D);
        v.x = s.x; v.y = s.y;
    }

    m_batchVertices.push_back(v);
}

void QuarkGLRenderer::PushQuad(GLuint tex, float x, float y, float w, float h, Color col) {
    EnsureBatchTexture(tex);
    auto n = ToNormColor(col);

    PushVertex({x, y, 0, 0, n[0], n[1], n[2], n[3]});
    PushVertex({x + w, y, 1, 0, n[0], n[1], n[2], n[3]});
    PushVertex({x + w, y + h, 1, 1, n[0], n[1], n[2], n[3]});
    PushVertex({x, y, 0, 0, n[0], n[1], n[2], n[3]});
    PushVertex({x + w, y + h, 1, 1, n[0], n[1], n[2], n[3]});
    PushVertex({x, y + h, 0, 1, n[0], n[1], n[2], n[3]});
}

void QuarkGLRenderer::PushTexturedQuad(GLuint tex, Rectangle uv,
                                        float x, float y, float w, float h, Color col) {
    EnsureBatchTexture(tex);
    auto n = ToNormColor(col);

    float u0 = uv.x, v0 = uv.y, u1 = uv.x + uv.width, v1 = uv.y + uv.height;

    PushVertex({x, y, u0, v0, n[0], n[1], n[2], n[3]});
    PushVertex({x + w, y, u1, v0, n[0], n[1], n[2], n[3]});
    PushVertex({x + w, y + h, u1, v1, n[0], n[1], n[2], n[3]});
    PushVertex({x, y,  u0, v0, n[0], n[1], n[2], n[3]});
    PushVertex({x + w, y + h, u1, v1, n[0], n[1], n[2], n[3]});
    PushVertex({x,  y + h, u0, v1, n[0], n[1], n[2], n[3]});
}

void QuarkGLRenderer::PushCircleImpl(float cx, float cy, float r, Color col) {
    EnsureBatchTexture(0);
    auto n = ToNormColor(col);

    constexpr int seg = 48;
    for(int i = 0; i < seg; ++i) {
        float a0 = (float)i / seg * 6.28318530718f, a1 = (float)(i + 1) / seg * 6.28318530718f;
        PushVertex({cx, cy, 0.5f, 0.5f, n[0], n[1], n[2], n[3]});
        PushVertex({cx + cosf(a0) * r, cy + sinf(a0) * r, 1, 0, n[0], n[1], n[2], n[3]});
        PushVertex({cx + cosf(a1) * r, cy + sinf(a1) * r, 0, 1, n[0], n[1], n[2], n[3]});
    }
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
    FlushBatch();

    glBegin(GL_LINES);
    glColor4ub(c.r, c.g, c.b, c.a);
    glVertex2f(s.x, s.y);
    glVertex2f(e.x, e.y);
    glEnd();
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
    FlushBatch();

    glBegin(GL_TRIANGLES);
    glColor4ub(col.r, col.g, col.b, col.a);
    glVertex2f(a.x, a.y);
    glVertex2f(b.x, b.y);
    glVertex2f(c.x, c.y);
    glEnd();
}

void QuarkGLRenderer::DrawCircleLines(float cx, float cy, float r, Color c) {
    FlushBatch();

    glBegin(GL_LINE_LOOP);
    glColor4ub(c.r, c.g, c.b, c.a);
    for(int i = 0; i < 36; ++i) {
        float a = i / 36.f * 6.28318530718f;
        glVertex2f(cx + r * cosf(a), cy + r * sinf(a));
    }
    glEnd();
}

void QuarkGLRenderer::DrawEllipse(float cx, float cy, float rh, float rv, Color c) {
    FlushBatch();

    glBegin(GL_POLYGON);
    glColor4ub(c.r, c.g, c.b, c.a);
    for(int i = 0; i < 36; ++i) {
        float a = i / 36.f * 6.28318530718f;
        glVertex2f(cx + rh * cosf(a), cy + rv * sinf(a));
    }
    glEnd();
}

void QuarkGLRenderer::DrawPoly(Vec2 cen, int sides, float r, float rot, Color c) {
    if(sides < 3) return;

    FlushBatch();

    glBegin(GL_POLYGON);
    glColor4ub(c.r, c.g, c.b, c.a);
    for(int i = 0; i < sides; ++i) {
        float a = i / (float)sides * 6.28318530718f + rot * PI / 180.f;
        glVertex2f(cen.x + r * cosf(a), cen.y + r * sinf(a));
    }
    glEnd();
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

void QuarkGLRenderer::DrawTextureNPatch(ITexture t, Rectangle src, Rectangle dst,
                                         Vec2 origin, float rot, Color tint) {
    DrawTexturePro(t, src, dst, origin, rot, tint);
}

ITexture QuarkGLRenderer::LoadTexture(const char* path) {
    TraceLog(LogLevel::Trace, "TEXTURE", TextFormat("Loading texture from: %s", path));

    PngImageData img;
    ITexture t{};

    if(LoadPngImage(path,img)) {
        t.id = CreateTextureFromRgba(img.pixels.data(), img.width, img.height);
        t.width = img.width;
        t.height = img.height;
        t.valid = true;

        TraceLog(LogLevel::Info, "TEXTURE", TextFormat("Texture loaded successfully: %s (%dx%d)", path, t.width, t.height));
    }
    else {
        TraceLog(LogLevel::Error, "TEXTURE", TextFormat("Failed to load texture: %s", path));
    }

    return t;
}

void QuarkGLRenderer::UnloadTexture(ITexture& t) {
    if(t.id) glDeleteTextures(1, &t.id);
    t = {};
}

bool QuarkGLRenderer::isTextureValid(ITexture& t) {
    return t.valid && t.id != 0;
}

ITexture QuarkGLRenderer::GetRenderTextureTexture(IRenderTexture rt) {
    return rt.texture;
}

IRenderTexture QuarkGLRenderer::LoadRenderTexture(int w, int h) {
    IRenderTexture rt{};

    glGenFramebuffers(1, &rt.id);
    glBindFramebuffer(GL_FRAMEBUFFER, rt.id);

    glGenTextures(1, &rt.texture.id);
    glBindTexture(GL_TEXTURE_2D, rt.texture.id);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt.texture.id, 0);
    glGenRenderbuffers(1, &rt.depthId);
    glBindRenderbuffer(GL_RENDERBUFFER, rt.depthId);

    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);

    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rt.depthId);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    rt.texture.width = w;
    rt.texture.height = h;
    rt.texture.valid = true;
    return rt;
}

void QuarkGLRenderer::UnloadRenderTexture(IRenderTexture rt) {
    if(rt.id)
        glDeleteFramebuffers(1, &rt.id);
    if(rt.depthId)
        glDeleteRenderbuffers(1, &rt.depthId);
    if(rt.texture.id)
        glDeleteTextures(1, &rt.texture.id);
}

bool QuarkGLRenderer::isRenderTextureValid(IRenderTexture& rt) {
    return rt.id && rt.texture.valid;
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
    t.valid = true;
    return t;
}

void QuarkGLRenderer::BeginTextureMode(IRenderTexture rt) {
    FlushBatch();

    glBindFramebuffer(GL_FRAMEBUFFER, rt.id);
    m_currentFbo = rt.id;
    m_width = rt.texture.width;
    m_height = rt.texture.height;

    glViewport(0, 0, m_width, m_height);
}

void QuarkGLRenderer::EndTextureMode() {
    FlushBatch();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_currentFbo = 0;

    RefreshViewport();
}

bool QuarkGLRenderer::LoadFontInternal(const char* filePath, int pointSize, FontData& out) {
    FT_Library ft = nullptr;
    if (FT_Init_FreeType(&ft) != 0) return false;

    FT_Face face = nullptr;
    if (FT_New_Face(ft, filePath, 0, &face) != 0) {
        FT_Done_FreeType(ft);
        return false;
    }

    FT_Select_Charmap(face, FT_ENCODING_UNICODE);
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)pointSize);

    constexpr int AW = 1024, AH = 1024;
    std::vector<uint8_t> atlas((size_t)AW * AH * 4, 0);
    int penX = 1, penY = 1, rowH = 0;

    for (unsigned char c = 32; c < 127; ++c) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER|FT_LOAD_TARGET_NORMAL) != 0) continue;

        FT_GlyphSlot slot = face->glyph;
        int gw = (int)slot->bitmap.width;
        int gh = (int)slot->bitmap.rows;

        if (penX + gw + 1 > AW) {
            penX = 1;
            penY += rowH + 1;
            rowH = 0;
        }

        if (penY + gh + 1 > AH) {
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
        GlyphData& g = out.glyphs[c - 32];
        g.uv = Rectangle{(float)penX / AW, (float)penY / AH,
                               gw > 0 ? (float)gw / AW : 0.f, gh > 0 ? (float)gh / AH : 0.f};
        g.advanceX = (float)slot->advance.x / 64.f;
        g.offsetX = (float)slot->bitmap_left;
        g.offsetY = (float)slot->bitmap_top;
        g.width = gw;
        g.height = gh;
        penX += gw + 1;
        rowH  = std::max(rowH, gh);
    }

    out.atlasTexture = CreateTextureFromRgba(atlas.data(), AW, AH);
    out.baseSize     = pointSize;
    out.ascent       = (int)(face->size->metrics.ascender  / 64);
    out.descent      = (int)(face->size->metrics.descender / 64);
    out.lineHeight   = (int)(face->size->metrics.height    / 64);
    out.lineGap      = out.lineHeight - (out.ascent - out.descent);

    FT_Done_Face(face);
    FT_Done_FreeType(ft);
    return true;
}

uint32_t QuarkGLRenderer::EnsureDefaultFont() {
    if (m_defaultFontId != 0) return m_defaultFontId;

    const char* path = DefaultFontPath();
    if (!path) return 0;

    FontData fd{};
    if (!LoadFontInternal(path, 32, fd)) return 0;

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

    for (const char* c = text; *c; ++c) {
        if (*c == '\n') {
            x = pos.x;
            y += lineHeight;
            first = true;
            continue;
        }

        unsigned char uc = (unsigned char) * c;
        if (uc < 32 || uc >= 127) continue;

        const GlyphData& g = fd.glyphs[uc - 32];

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

    float scale = (float)fontSize/fd.baseSize;
    float lh = (float)fd.lineHeight * scale;

    float x = 0, maxW = 0;
    bool first = true;

    for (const char* c = text; *c; ++c) {
        if(*c == '\n') {
            maxW = std::max(maxW, x);
            x = 0;
            first = true;
            continue;
        }

        unsigned char uc = (unsigned char)*c;
        if(uc < 32 || uc >= 127) continue;

        if(!first) x += spacing;
        first = false;
        x += fd.glyphs[uc - 32].advanceX * scale;
    }

    int newlines = (int)std::count(text, text + strlen(text), '\n');
    return {std::max(maxW, x), lh * (1 + newlines)};
}

IFont QuarkGLRenderer::LoadFont(const char* filePath, int fontSize) {
    if (filePath == nullptr) {
        TraceLog(LogLevel::Info, "FONT", "Loading default system font...");

        IFont handle{};
        handle.id = this->EnsureDefaultFont();

        if (handle.id) TraceLog(LogLevel::Info, "FONT", "Default font loaded");
        return handle;
    }

    TraceLog(LogLevel::Trace, "FONT", TextFormat("Loading font: %s (size: %d)", filePath, fontSize));

    FontData fd{};
    if (!LoadFontInternal(filePath, fontSize, fd)) return IFont{};

    uint32_t id = m_nextFontId++;
    m_fonts[id] = std::move(fd);

    IFont handle{};
    handle.id = id;

    TraceLog(LogLevel::Info, "FONT", TextFormat("Font loaded successfully: %s", filePath));
    return handle;
}

void QuarkGLRenderer::UnloadFont(IFont& font) {
    auto it = m_fonts.find(font.id);
    if (it != m_fonts.end()) {
        if (it->second.atlasTexture)
            glDeleteTextures(1, &it->second.atlasTexture);

        if (font.id == m_defaultFontId) m_defaultFontId = 0;

        m_fonts.erase(it);
    }

    font.id = 0;
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
    if(sh.id) {
        m_currentShader = sh.id;
        glUseProgram(sh.id);
    }
}

void QuarkGLRenderer::EndShaderMode() {
    FlushBatch();
    m_currentShader = m_defaultShader;
    glUseProgram(m_defaultShader);
}

Shader QuarkGLRenderer::LoadShader(const char* vsFileName, const char* fsFileName) {
    std::string vsSource, fsSource;
    if (vsFileName) {
        std::ifstream vsFile(vsFileName);
        if (vsFile.is_open()) {
            vsSource.assign((std::istreambuf_iterator<char>(vsFile)),
                            (std::istreambuf_iterator<char>()));
        } else {
            TraceLog(LogLevel::Error, "SHADER", TextFormat("Failed to open vertex shader file: %s", vsFileName));
            return Shader{};
        }
    }

    if (fsFileName) {
        std::ifstream fsFile(fsFileName);
        if (fsFile.is_open()) {
            fsSource.assign((std::istreambuf_iterator<char>(fsFile)),
                            (std::istreambuf_iterator<char>()));
        } else {
            TraceLog(LogLevel::Error, "SHADER", TextFormat("Failed to open fragment shader file: %s", fsFileName));
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
            glDeleteShader(vs);
            return Shader{};
        }
    }

    GLuint p = glCreateProgram();
    if (vs) glAttachShader(p, vs);
    if (fs) glAttachShader(p, fs);
    glLinkProgram(p);

    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);

    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, 512, nullptr, log);
        TraceLog(LogLevel::Error, "SHADER", TextFormat("Program link error: %s", log));
        glDeleteProgram(p);
        return Shader{};
    }

    Shader result{p};
    for (int i = 0; i < SHADER_LOC_COUNT; ++i) {
        result.locs[i] = GetShaderLocation(result, static_cast<ShaderLocationIndex>(i));
    }
    return result;
}

void QuarkGLRenderer::UnloadShader(Shader& sh) {
    if (sh.id) {
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

void QuarkGLRenderer::SetShaderValue(const Shader&,int loc,float v) {
    if(loc >= 0)
        glUniform1f(loc, v);
}

void QuarkGLRenderer::SetShaderValue(const Shader&,int loc,int v) {
    if(loc >= 0)
        glUniform1i(loc, v);
}

void QuarkGLRenderer::SetShaderValue(const Shader&,int loc,const Color& c) {
    if(loc >= 0)
        glUniform4f(loc, c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f);
}

void QuarkGLRenderer::SetShaderValue(const Shader&,int loc,const Vec2& v) {
    if(loc >= 0)
        glUniform2f(loc, v.x, v.y);
}

void QuarkGLRenderer::SetShaderValue(const Shader& /*shader*/, int locIndex, const Vec3& value) {
    if (locIndex >= 0)
        glUniform3f(locIndex, value.x, value.y, value.z);
}

void QuarkGLRenderer::SetShaderValue(const Shader&, int locIndex, const Vec4& value) {
    if (locIndex >= 0)
        glUniform4f(locIndex, value.x, value.y, value.z, value.w);
}

void QuarkGLRenderer::SetShaderValueMatrix(const Shader&,int loc,const float* m) {
    if(loc >= 0&& m)
        glUniformMatrix4fv(loc, 1, GL_FALSE, m);
}

void QuarkGLRenderer::SetShaderValueSampler(const Shader&,int loc,int unit) { 
    if(loc >= 0)
        glUniform1i(loc, unit);
}

void QuarkGLRenderer::BeginMode2D(const Camera2D& cam) {
    m_camera2D = cam;
    m_camera2DActive = true;
}

void QuarkGLRenderer::EndMode2D() {
    m_camera2DActive = false;
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

    Mat4 id = Mat4::identity();
    if (m_3d.modelLoc >= 0) glUniformMatrix4fv(m_3d.modelLoc, 1, GL_FALSE, id.m);

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

    Mat4 id = Mat4::identity();
    if (m_3d.modelLoc >= 0) glUniformMatrix4fv(m_3d.modelLoc, 1, GL_FALSE, id.m);

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
    Mat4 t = ApplyCurrentMatrix(
        Mat4::translation(
            c.x,
            c.y,
            c.z
        ) * Mat4::scale(
            size.x,
            1.0f,
            size.y
        )
    );

    if (m_3d.modelLoc >= 0)
        glUniformMatrix4fv(m_3d.modelLoc, 1, GL_FALSE, t.m);

    if (m_3d.colorLoc >= 0)
        glUniform4f(
            m_3d.colorLoc,
            color.r / 255.f,
            color.g / 255.f,
            color.b / 255.f,
            color.a / 255.f
        );

    glBindTexture(GL_TEXTURE_2D, m_3d.whiteTexture);
    glBindVertexArray(m_3d.planeVAO);

    glDrawElements(
        GL_TRIANGLES,
        m_3d.planeIndexCount,
        GL_UNSIGNED_INT,
        nullptr
    );

    glBindVertexArray(0);
}

void QuarkGLRenderer::DrawCube(Vec3 pos,float w,float h,float l,Color color){
    Mat4 t = ApplyCurrentMatrix(
        Mat4::translation(
            pos.x,
            pos.y,
            pos.z
        ) * Mat4::scale(
            w,
            h,
            l
        )
    );

    if (m_3d.modelLoc >= 0)
        glUniformMatrix4fv(m_3d.modelLoc, 1, GL_FALSE, t.m);

    if (m_3d.colorLoc >= 0)
        glUniform4f(
            m_3d.colorLoc,
            color.r / 255.f,
            color.g / 255.f,
            color.b / 255.f,
            color.a / 255.f
        );

    glBindTexture(GL_TEXTURE_2D, m_3d.whiteTexture);
    glBindVertexArray(m_3d.cubeVAO);

    glDrawElements(
        GL_TRIANGLES,
        m_3d.cubeIndexCount,
        GL_UNSIGNED_INT,
        nullptr
    );

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
    Mat4 t = ApplyCurrentMatrix(
        Mat4::translation(
            pos.x,
            pos.y,
            pos.z
        ) * Mat4::scale(
            r,
            r,
            r
        )
    );

    if (m_3d.modelLoc >= 0)
        glUniformMatrix4fv(m_3d.modelLoc, 1, GL_FALSE, t.m);

    if (m_3d.colorLoc >= 0)
        glUniform4f(
            m_3d.colorLoc,
            color.r / 255.f,
            color.g / 255.f,
            color.b / 255.f,
            color.a / 255.f
        );

    glBindTexture(GL_TEXTURE_2D, m_3d.whiteTexture);
    glBindVertexArray(m_3d.sphereVAO);

    glDrawElements(
        GL_TRIANGLES,
        m_3d.sphereIndexCount,
        GL_UNSIGNED_INT,
        0
    );

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

void QuarkGLRenderer::DrawGrid(int slices,float spacing) {
    float half = (float)slices * spacing / 2;
    for(int i = 0; i <= slices; ++i){
        float f =- half + (float)i * spacing;

        DrawLine3D({f, 0, -half}, {f, 0, half}, DARKGRAY);
        DrawLine3D({-half, 0, f}, {half, 0, f}, DARKGRAY);
    }
}

Model QuarkGLRenderer::LoadModel(const char* filePath) {
    TraceLog(LogLevel::Trace, "MODEL", TextFormat("Loading model: %s", filePath));
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filePath, aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        TraceLog(LogLevel::Error, "MODEL", TextFormat("Failed to load model %s: %s", filePath, importer.GetErrorString()));
        return Model{};
    }

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
        if (AI_SUCCESS == material->GetTexture(aiTextureType_DIFFUSE, 0, &path)) {
            std::string texturePath = filePath;
            size_t lastSlash = texturePath.find_last_of("/\\");
            if (lastSlash != std::string::npos) {
                texturePath = texturePath.substr(0, lastSlash + 1);
            } else {
                texturePath = "";
            }
            texturePath += path.C_Str();
            ITexture loadedTex = this->LoadTexture(texturePath.c_str());
            mat.maps[MATERIAL_MAP_ALBEDO].texture.id = loadedTex.id;
            mat.maps[MATERIAL_MAP_ALBEDO].texture.width = loadedTex.width;
            mat.maps[MATERIAL_MAP_ALBEDO].texture.height = loadedTex.height;
            mat.maps[MATERIAL_MAP_ALBEDO].texture.valid = loadedTex.valid;
        }
    }

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
    }

    TraceLog(LogLevel::Info, "MODEL", TextFormat("Model loaded successfully: %s (%d meshes, %d materials)", filePath, model.meshCount, model.materialCount));
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
        mesh = {};
    }

    delete[] model.meshes;
    model.meshes = nullptr;

    for (int i = 0; i < model.materialCount; ++i) {
        Material& mat = model.materials[i];

        if (mat.maps && mat.maps[MATERIAL_MAP_ALBEDO].texture.valid) {
            ITexture tempTex;
            tempTex.id = mat.maps[MATERIAL_MAP_ALBEDO].texture.id;
            this->UnloadTexture(tempTex);
        }

        delete[] mat.maps;
        mat = {};
    }
    delete[] model.materials;
    model.materials = nullptr;

    delete[] model.meshMaterial;
    model.meshMaterial = nullptr;

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
}

void QuarkGLRenderer::UpdateMeshBuffer(Mesh& mesh, int index, const void* data, int dataSize, int offset) {
    if (!data || dataSize <= 0) return;

    if (index == 0 && mesh.vboId) {
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId);
        glBufferSubData(GL_ARRAY_BUFFER, offset, dataSize, data);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    } else if (index == 1 && mesh.eboId) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.eboId);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, offset, dataSize, data);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
}

void QuarkGLRenderer::UnloadMesh(Mesh& mesh) {
    if (mesh.vaoId)
        glDeleteVertexArrays(1, &mesh.vaoId);
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

void QuarkGLRenderer::DrawMesh(const Mesh& mesh, const Material& material, const Mat4& transform) {
    if (!mesh.vaoId) return;
    if (!m_3d.initialized) Init3DState();

    glUseProgram(m_3d.shader3D);
    if (m_3d.modelLoc >= 0) glUniformMatrix4fv(m_3d.modelLoc, 1, GL_FALSE, transform.m);
    if (m_3d.colorLoc >= 0) glUniform4f(m_3d.colorLoc, 1.0f, 1.0f, 1.0f, 1.0f);

    GLuint texId = m_3d.whiteTexture;
    if (material.maps && material.maps[MATERIAL_MAP_ALBEDO].texture.valid) {
        texId = material.maps[MATERIAL_MAP_ALBEDO].texture.id;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texId);
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
    Mat4 final = ApplyCurrentMatrix(transform);

    if(m_3d.modelLoc >= 0) glUniformMatrix4fv(m_3d.modelLoc, 1, GL_FALSE, final.m);
    if(m_3d.colorLoc >= 0) glUniform4f(m_3d.colorLoc, 1, 1, 1, 1);

    for(int i = 0; i < model.meshCount; ++i) {
        const Mesh& mesh = model.meshes[i];

        glActiveTexture(GL_TEXTURE0);
        GLuint texId = m_3d.whiteTexture;

        if(model.meshMaterial && model.meshMaterial[i] >= 0 && model.meshMaterial[i] < model.materialCount) {
            const Material& mat = model.materials[model.meshMaterial[i]];
            if(mat.maps && mat.maps[MATERIAL_MAP_ALBEDO].texture.valid)
                texId = mat.maps[MATERIAL_MAP_ALBEDO].texture.id;
        }

        glBindTexture(GL_TEXTURE_2D, texId);
        glBindVertexArray(mesh.vaoId);

        glDrawElements(GL_TRIANGLES, (GLsizei)(mesh.triangleCount * 3), GL_UNSIGNED_SHORT, nullptr);
        glBindVertexArray(0);
    }
}

void QuarkGLRenderer::DrawModelEx(const Model& model, const Mat4& transform, Color tint) {
    Mat4 final = ApplyCurrentMatrix(transform);

    if(m_3d.modelLoc >= 0) glUniformMatrix4fv(m_3d.modelLoc, 1, GL_FALSE, final.m);

    if(m_3d.colorLoc >= 0) glUniform4f(m_3d.colorLoc,
        tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f, tint.a / 255.0f);

    for(int i = 0; i < model.meshCount; ++i) {
        const Mesh& mesh = model.meshes[i];

        glActiveTexture(GL_TEXTURE0);

        GLuint texId = m_3d.whiteTexture;
        if(model.meshMaterial && model.meshMaterial[i] >= 0 && model.meshMaterial[i] < model.materialCount) {
            const Material& mat = model.materials[model.meshMaterial[i]];
            if(mat.maps && mat.maps[MATERIAL_MAP_ALBEDO].texture.valid)
                texId = mat.maps[MATERIAL_MAP_ALBEDO].texture.id;
        }

        glBindTexture(GL_TEXTURE_2D, texId);
        glBindVertexArray(mesh.vaoId);

        glDrawElements(GL_TRIANGLES, (GLsizei)(mesh.triangleCount * 3), GL_UNSIGNED_SHORT, nullptr);
        glBindVertexArray(0);
    }
}

} // namespace qc