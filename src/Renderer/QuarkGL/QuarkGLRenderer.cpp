#include "QuarkGLRenderer.hpp"
#include "../DebugFont.h"
#include "../DefaultFont.h"
#include "../QuarkAssimpTexture.hpp"
#include "../../QuarkInternal.hpp"
#include "../../QuarkModelAnim.hpp"
#include "QuarkCore/QuarkImage.hpp"

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

static const char* VS_3D = R"(
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

static const char* FS_3D = R"(
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
namespace qci
{
    static Mat4 TransposeMat4(const Mat4& matrix)
    {
        Mat4 result{};
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                result.m[row * 4 + col] = matrix.m[col * 4 + row];
            }
        }
        return result;
    }

    struct GLShaderUniformCache
    {
        GLint m_Texture0Loc = -1;
        GLint m_UseTextureLoc = -1;
        GLint m_UseInstancingLoc = -1;
        GLint m_aShadowMapLocs[4] = {-1, -1, -1, -1};
        GLint m_aPbrMapLocs[5] = {-1, -1, -1, -1, -1};
    };

    static std::unordered_map<GLuint, GLShaderUniformCache>& GetShaderUniformCacheMap()
    {
        static std::unordered_map<GLuint, GLShaderUniformCache> s_cache;
        return s_cache;
    }

    static const GLShaderUniformCache& GetGLShaderUniformCache(GLuint program)
    {
        auto& cacheMap = GetShaderUniformCacheMap();
        auto it = cacheMap.find(program);
        if (it != cacheMap.end())
        {
            return it->second;
        }

        GLShaderUniformCache locs{};
        locs.m_Texture0Loc = glGetUniformLocation(program, "texture0");
        locs.m_UseTextureLoc = glGetUniformLocation(program, "useTexture");
        locs.m_UseInstancingLoc = glGetUniformLocation(program, "useInstancing");
        static const char* SHADOW_NAMES[4] = {
            "shadowMaps[0]", "shadowMaps[1]", "shadowMaps[2]", "shadowMaps[3]"};
        for (int i = 0; i < 4; ++i)
        {
            locs.m_aShadowMapLocs[i] = glGetUniformLocation(program, SHADOW_NAMES[i]);
        }
        static const char* PBR_NAMES[5] = {
            "metalnessMap", "normalMap", "roughnessMap", "occlusionMap", "emissionMap"};
        for (int i = 0; i < 5; ++i)
        {
            locs.m_aPbrMapLocs[i] = glGetUniformLocation(program, PBR_NAMES[i]);
        }

        auto [insertedIt, inserted] = cacheMap.emplace(program, locs);
        return insertedIt->second;
    }

    static void InvalidateGLShaderUniformCache(GLuint program)
    {
        if (program != 0)
        {
            GetShaderUniformCacheMap().erase(program);
        }
    }

    static void ApplyMaterialShaderUniforms(const Shader& shader, const Mat4& model,
                                            const Mat4& view, const Mat4& projection,
                                            Color tint, bool useTexture)
    {
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

        const GLShaderUniformCache& cachedLocs = GetGLShaderUniformCache(shader.id);
        if (cachedLocs.m_Texture0Loc >= 0)
            glUniform1i(cachedLocs.m_Texture0Loc, 0);
        if (cachedLocs.m_UseTextureLoc >= 0)
            glUniform1i(cachedLocs.m_UseTextureLoc, useTexture ? 1 : 0);
    }

    static const Shader* ResolveMaterialShader(const Material* pMaterial)
    {
        if (pMaterial && pMaterial->shader && pMaterial->shader->id != 0)
            return pMaterial->shader;
        return nullptr;
    }

    static GLenum ToGLTextureWrapMode(int wrap)
    {
        switch (wrap)
        {
        case TEXTURE_WRAP_CLAMP:
            return GL_CLAMP_TO_EDGE;
        case TEXTURE_WRAP_MIRROR_REPEAT:
            return GL_MIRRORED_REPEAT;
        case TEXTURE_WRAP_MIRROR_CLAMP:
        {
            int majorVersion = 0;
            int minorVersion = 0;
            const bool coreSupport =
                SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &majorVersion) &&
                SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minorVersion) &&
                (majorVersion > 4 || (majorVersion == 4 && minorVersion >= 4));
            if (coreSupport || GLAD_GL_ARB_texture_mirror_clamp_to_edge)
            {
                return GL_MIRROR_CLAMP_TO_EDGE;
            }

            static bool s_Warned = false;
            if (!s_Warned)
            {
                TraceLog(LogLevel::Warn, "OPENGL",
                         "MIRROR_CLAMP requires OpenGL 4.4 or ARB_texture_mirror_clamp_to_edge; falling back to CLAMP_TO_EDGE.");
                s_Warned = true;
            }
            return GL_CLAMP_TO_EDGE;
        }
        case TEXTURE_WRAP_REPEAT:
        default:
            return GL_REPEAT;
        }
    }

    template <typename Fn>
    static void WithShaderProgram(const Shader& shader, Fn&& fn)
    {
        if (shader.id == 0)
            return;

        GLint previousProgram = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);

        if (static_cast<GLuint>(previousProgram) != shader.id)
        {
            glUseProgram(shader.id);
        }

        fn();

        if (static_cast<GLuint>(previousProgram) != shader.id)
        {
            glUseProgram(static_cast<GLuint>(previousProgram));
        }
    }

    static const char* s_apShaderLocationNames[SHADER_LOC_COUNT] = {
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
#pragma warning(disable : 4611)
#endif

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    QuarkGLRenderer::~QuarkGLRenderer()
    {
        this->Shutdown();
    }

    void QuarkGLRenderer::Init(SDL_Window* pWindow, int width, int height)
    {
        m_pWindow = pWindow;
        m_Width = width;
        m_Height = height;
        m_Device.Init(pWindow, width, height);
        m_Context = m_Device.GetContext();

        const char* pVendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        const char* pRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* pVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        const char* pGlsl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));

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

        TraceLog(LogLevel::Info, "OPENGL", TextFormat("GPU / Renderer: %s", pRenderer ? pRenderer : "Unknown"));
        TraceLog(LogLevel::Info, "OPENGL", TextFormat("Vendor: %s", pVendor ? pVendor : "Unknown"));
        TraceLog(LogLevel::Info, "OPENGL", TextFormat("OpenGL Version: %s", pVersion ? pVersion : "Unknown"));
        TraceLog(LogLevel::Info, "OPENGL", TextFormat("GLSL Version: %s", pGlsl ? pGlsl : "Unknown"));
        TraceLog(LogLevel::Info, "OPENGL", TextFormat("Context Config: Initial Viewport %dx%d, Extensions Count: %d", width, height, numExtensions));
        TraceLog(LogLevel::Trace, "OPENGL", TextFormat("Limits: Max 2D Texture: %d, Max 3D Texture: %d, Max CubeMap: %d, Max Array Layers: %d", maxTexSize, max3DTexSize, maxCubeMapSize, maxArrayLayers));
        TraceLog(LogLevel::Trace, "OPENGL", TextFormat("Limits: Max MSAA Samples: %dx, Max Vertex Attribs: %d, Max Combined Texture Units: %d", maxSamples, maxAttribs, maxCombinedTexUnits));
        TraceLog(LogLevel::Trace, "OPENGL", TextFormat("Limits: Max UBO Bindings: %d, Max Vertex Uniforms: %d, Max Fragment Uniforms: %d", maxUBO, maxVertUniforms, maxFragUniforms));
        TraceLog(LogLevel::Trace, "OPENGL", TextFormat("Limits: Max Color Attachments: %d, Max Renderbuffer: %d, Max Viewport: %dx%d", maxColorAttach, maxRenderbufferSize, maxViewportDims[0], maxViewportDims[1]));

        InitGL();
        if (m_VsyncExplicitlySet)
        {
            SDL_GL_SetSwapInterval(m_Vsync ? 1 : 0);
            TraceLog(LogLevel::Info, "OPENGL", TextFormat("VSync configured: %s (Swap Interval: %d)", m_Vsync ? "ON" : "OFF", m_Vsync ? 1 : 0));
        }

        m_LastFrameCounter = SDL_GetPerformanceCounter();
        TraceLog(LogLevel::Info, "OPENGL", "OpenGL renderer initialized successfully.");
    }

    void QuarkGLRenderer::Shutdown()
    {
        if (m_pWindow == nullptr)
        {
            return;
        }

        TraceLog(LogLevel::Info, "OPENGL", "Shutting down OpenGL renderer...");
        m_Font.Clear();
        m_Batch.Shutdown();
        m_Device.Shutdown();

        for (auto& [id, fd] : m_Fonts)
            if (fd.m_AtlasTexture)
                glDeleteTextures(1, &fd.m_AtlasTexture);
        m_Fonts.clear();
        m_DefaultFontId = 0;

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

        if (m_Program)
        {
            glDeleteProgram(m_Program);
            m_Program = 0;
        }

        if (m_WhiteTexture)
        {
            glDeleteTextures(1, &m_WhiteTexture);
            m_WhiteTexture = 0;
        }

        if (m_3D.m_Shader3D)
        {
            glDeleteProgram(m_3D.m_Shader3D);
            m_3D.m_Shader3D = 0;
        }

        if (m_3D.m_WhiteTexture)
        {
            glDeleteTextures(1, &m_3D.m_WhiteTexture);
            m_3D.m_WhiteTexture = 0;
        }

        if (m_3D.m_BlackTexture)
        {
            glDeleteTextures(1, &m_3D.m_BlackTexture);
            m_3D.m_BlackTexture = 0;
        }

        if (m_3D.m_FlatNormalTexture)
        {
            glDeleteTextures(1, &m_3D.m_FlatNormalTexture);
            m_3D.m_FlatNormalTexture = 0;
        }

        for (const auto& [_, cachedTexture] : m_TextureCache)
        {
            if (cachedTexture.m_Texture.m_Id)
            {
                glDeleteTextures(1, &cachedTexture.m_Texture.m_Id);
            }
        }
        m_TextureCache.clear();
        m_TextureCacheKeys.clear();

        auto del = [](GLuint& va, GLuint& vb, GLuint& eb)
        {
            if (va)
            {
                glDeleteVertexArrays(1, &va);
                va = 0;
            }

            if (vb)
            {
                glDeleteBuffers(1, &vb);
                vb = 0;
            }

            if (eb)
            {
                glDeleteBuffers(1, &eb);
                eb = 0;
            }
        };
        del(m_3D.m_PlaneVAO, m_3D.planeVBO, m_3D.planeEBO);
        del(m_3D.m_CubeVAO, m_3D.cubeVBO, m_3D.cubeEBO);
        del(m_3D.m_SphereVAO, m_3D.sphereVBO, m_3D.sphereEBO);
        if (m_3D.m_LineVAO)
        {
            glDeleteVertexArrays(1, &m_3D.m_LineVAO);
            m_3D.m_LineVAO = 0;
        }

        if (m_3D.lineVBO)
        {
            glDeleteBuffers(1, &m_3D.lineVBO);
            m_3D.lineVBO = 0;
        }

        if (m_3D.m_TriVAO)
        {
            glDeleteVertexArrays(1, &m_3D.m_TriVAO);
            m_3D.m_TriVAO = 0;
        }

        if (m_3D.triVBO)
        {
            glDeleteBuffers(1, &m_3D.triVBO);
            m_3D.triVBO = 0;
        }

        if (m_Context)
        {
            SDL_GL_DestroyContext(m_Context);
            m_Context = nullptr;
        }

        m_pWindow = nullptr;
        TraceLog(LogLevel::Info, "OPENGL", "OpenGL renderer shut down successfully.");
    }

    void QuarkGLRenderer::InitGL()
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#if !defined(__ANDROID__)
        int multisampleBuffers = 0;
        SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &multisampleBuffers);
        if (multisampleBuffers > 0)
        {
            glEnable(GL_MULTISAMPLE);
        }
        else
        {
            glDisable(GL_MULTISAMPLE);
        }
#endif

        m_Program = CreateDefaultProgram();
        m_DefaultShader = m_Program;
        m_CurrentShader = m_Program;

        glGenVertexArrays(1, &m_Vao);
        glGenBuffers(1, &m_Vbo);
        glBindVertexArray(m_Vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_Vbo);
        glBufferData(GL_ARRAY_BUFFER, MAX_BATCH_VERTICES * sizeof(BatchVertex), nullptr, GL_DYNAMIC_DRAW);
        // layout: vec2 pos, vec2 uv, vec4 color
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), reinterpret_cast<void*>(offsetof(BatchVertex, m_X)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), reinterpret_cast<void*>(offsetof(BatchVertex, m_U)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), reinterpret_cast<void*>(offsetof(BatchVertex, m_R)));
        glBindVertexArray(0);

        const uint8_t white[4] = {255, 255, 255, 255};
        glGenTextures(1, &m_WhiteTexture);
        glBindTexture(GL_TEXTURE_2D, m_WhiteTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);

        m_CurrentTexture = m_WhiteTexture;
        m_Batch.Init(m_Program, m_WhiteTexture, m_Width, m_Height);
        m_Batch.SetCamera(m_Camera2D, m_Camera2DActive);
        RefreshViewport();
    }

    void QuarkGLRenderer::RefreshViewport()
    {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(m_pWindow, &w, &h);

        m_Width = w;
        m_Height = h;
        glViewport(0, 0, w, h);
    }

    void QuarkGLRenderer::BeginDrawing()
    {
        m_Drawing = true;
        m_CurrentTexture = m_WhiteTexture;
        m_vBatchVertices.clear();
        m_Batch.Begin(m_DefaultShader, m_WhiteTexture, m_Width, m_Height);
        m_Batch.SetCamera(m_Camera2D, m_Camera2DActive);
        m_Device.BeginDrawing();
    }

    void QuarkGLRenderer::EndDrawing()
    {
        FlushBatch();
        m_Device.EndDrawing();
        m_FrameTime = m_Device.GetFrameTime();
        m_ShouldClose = m_Device.ShouldClose();
    }

    void QuarkGLRenderer::SetTargetFPS(int fps)
    {
        m_TargetFps = fps;
        m_Device.SetTargetFPS(fps);
        if (!m_VsyncExplicitlySet && m_Context)
        {
            if (fps == 0)
                SDL_GL_SetSwapInterval(0);
            else
                SDL_GL_SetSwapInterval(1);
        }
    }

    bool QuarkGLRenderer::SetVSync(bool enabled)
    {
        m_Vsync = enabled;
        m_VsyncExplicitlySet = true;
        if (!m_Device.SetVSync(enabled))
        {
            TraceLog(LogLevel::Warn, "RENDERER", (std::string("SDL_GL_SetSwapInterval failed: ") + SDL_GetError()).c_str());
            return false;
        }
        return true;
    }

    void QuarkGLRenderer::ClearBackground(Color c)
    {
        m_Device.ClearBackground(c);
    }

    std::array<float, 4> QuarkGLRenderer::ToNormColor(Color c)
    {
        return QuarkGLDevice::ToNormColor(c);
    }

    GLuint QuarkGLRenderer::CreateTextureFromRgba(const uint8_t* pPx, int w, int h)
    {
        return QuarkGLTexture::CreateTextureFromRgba(pPx, w, h);
    }

    GLuint QuarkGLRenderer::CompileGLShader(GLenum type, const char* pSrc)
    {
        return QuarkGLShader::CompileGLShader(type, pSrc);
    }

    GLuint QuarkGLRenderer::CreateDefaultProgram()
    {
        TraceLog(LogLevel::Trace, "SHADER", "[OpenGL] Creating default 2D shader program...");
        const GLuint program = m_Shader.CreateDefaultProgram();
        TraceLog(LogLevel::Info, "SHADER", TextFormat("[OpenGL] Default 2D shader program created (ID: %u)", program));
        return program;
    }

    void QuarkGLRenderer::FlushBatch()
    {
        m_Batch.Flush();
    }

    void QuarkGLRenderer::EnsureBatchTexture(GLuint id)
    {
        m_Batch.EnsureBatchTexture(id);
    }

    void QuarkGLRenderer::PushVertex(const BatchVertex& vtx)
    {
        m_Batch.PushVertex({vtx.m_X, vtx.m_Y, vtx.m_U, vtx.m_V, vtx.m_R, vtx.m_G, vtx.m_B, vtx.m_A});
    }

    void QuarkGLRenderer::PushQuad(GLuint tex, float x, float y, float w, float h, Color col)
    {
        m_Batch.PushQuad(tex, x, y, w, h, col);
    }

    void QuarkGLRenderer::PushTexturedQuad(GLuint tex, Rectangle uv,
                                           float x, float y, float w, float h, Color col)
    {
        m_Batch.PushTexturedQuad(tex, uv, x, y, w, h, col);
    }

    void QuarkGLRenderer::PushCircleImpl(float cx, float cy, float r, Color col)
    {
        m_Batch.PushCircleImpl(cx, cy, r, col);
    }

    void QuarkGLRenderer::DrawRectangle(float x, float y, float w, float h, Color c)
    {
        PushQuad(0, x, y, w, h, c);
    }

    void QuarkGLRenderer::DrawRectangle(const Rectangle& r, Color c)
    {
        PushQuad(0, r.x, r.y, r.width, r.height, c);
    }

    void QuarkGLRenderer::DrawRectangleV(Vec2 p, Vec2 s, Color c)
    {
        PushQuad(0, p.x, p.y, s.x, s.y, c);
    }

    void QuarkGLRenderer::DrawCircle(float cx, float cy, float r, Color c)
    {
        PushCircleImpl(cx, cy, r, c);
    }

    void QuarkGLRenderer::DrawLine(float x1, float y1, float x2, float y2, Color c)
    {
        DrawLineV({x1, y1}, {x2, y2}, c);
    }

    void QuarkGLRenderer::DrawLineV(Vec2 s, Vec2 e, Color c)
    {
        float dx = e.x - s.x;
        float dy = e.y - s.y;
        float length = sqrtf(dx * dx + dy * dy);
        if (length <= 0)
            return;

        float angle = atan2f(dy, dx) * 180.0f / PI;
        ITexture white = {m_WhiteTexture, 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, true};
        DrawTexturePro(white, {0, 0, 1, 1}, {s.x, s.y, length, 1.0f}, {0, 0.5f}, angle, c);
    }

    void QuarkGLRenderer::DrawRectangleLines(Rectangle r, float, Color c)
    {
        DrawLine(r.x, r.y, r.x + r.width, r.y, c);
        DrawLine(r.x + r.width, r.y, r.x + r.width, r.y + r.height, c);
        DrawLine(r.x + r.width, r.y + r.height, r.x, r.y + r.height, c);
        DrawLine(r.x, r.y + r.height, r.x, r.y, c);
    }

    void QuarkGLRenderer::DrawRectangleRounded(Rectangle r, float rnd, int, Color c)
    {
        float rad = rnd * std::min(r.width, r.height) / 2.f;

        DrawCircle(r.x + rad, r.y + rad, rad, c);
        DrawCircle(r.x + r.width - rad, r.y + rad, rad, c);
        DrawCircle(r.x + r.width - rad, r.y + r.height - rad, rad, c);
        DrawCircle(r.x + rad, r.y + r.height - rad, rad, c);

        PushQuad(0, r.x + rad, r.y, r.width - 2 * rad, r.height, c);
        PushQuad(0, r.x, r.y + rad, r.width, r.height - 2 * rad, c);
    }

    void QuarkGLRenderer::DrawTriangle(Vec2 a, Vec2 b, Vec2 c, Color col)
    {
        EnsureBatchTexture(0);
        auto n = ToNormColor(col);
        PushVertex({a.x, a.y, 0, 0, n[0], n[1], n[2], n[3]});
        PushVertex({b.x, b.y, 0, 0, n[0], n[1], n[2], n[3]});
        PushVertex({c.x, c.y, 0, 0, n[0], n[1], n[2], n[3]});
    }

    void QuarkGLRenderer::DrawCircleLines(float cx, float cy, float r, Color c)
    {
        constexpr int segments = 36;
        for (int i = 0; i < segments; i++)
        {
            float a1 = (float)i / segments * 2.0f * PI;
            float a2 = (float)(i + 1) / segments * 2.0f * PI;
            DrawLineV({cx + r * cosf(a1), cy + r * sinf(a1)}, {cx + r * cosf(a2), cy + r * sinf(a2)}, c);
        }
    }

    void QuarkGLRenderer::DrawEllipse(float cx, float cy, float rh, float rv, Color c)
    {
        EnsureBatchTexture(0);
        auto n = ToNormColor(c);
        constexpr int segments = 36;
        for (int i = 0; i < segments; i++)
        {
            float a1 = (float)i / segments * 2.0f * PI;
            float a2 = (float)(i + 1) / segments * 2.0f * PI;
            PushVertex({cx, cy, 0.5f, 0.5f, n[0], n[1], n[2], n[3]});
            PushVertex({cx + rh * cosf(a1), cy + rv * sinf(a1), 1, 0, n[0], n[1], n[2], n[3]});
            PushVertex({cx + rh * cosf(a2), cy + rv * sinf(a2), 0, 1, n[0], n[1], n[2], n[3]});
        }
    }

    void QuarkGLRenderer::DrawPoly(Vec2 cen, int sides, float r, float rot, Color c)
    {
        if (sides < 3)
            return;
        EnsureBatchTexture(0);
        auto n = ToNormColor(c);
        for (int i = 0; i < sides; i++)
        {
            float a1 = (float)i / sides * 2.0f * PI + rot * PI / 180.0f;
            float a2 = (float)(i + 1) / sides * 2.0f * PI + rot * PI / 180.0f;
            PushVertex({cen.x, cen.y, 0.5f, 0.5f, n[0], n[1], n[2], n[3]});
            PushVertex({cen.x + r * cosf(a1), cen.y + r * sinf(a1), 1, 0, n[0], n[1], n[2], n[3]});
            PushVertex({cen.x + r * cosf(a2), cen.y + r * sinf(a2), 0, 1, n[0], n[1], n[2], n[3]});
        }
    }

    void QuarkGLRenderer::DrawTexture(const ITexture& t, float x, float y, Color tint)
    {
        if (!t.m_Id)
            return;
        PushQuad(t.m_Id, x, y, (float)t.m_Width, (float)t.m_Height, tint);
    }

    void QuarkGLRenderer::DrawTextureV(const ITexture& t, Vec2 p, Color tint)
    {
        DrawTexture(t, p.x, p.y, tint);
    }

    void QuarkGLRenderer::DrawTextureRec(const ITexture& t, Rectangle src, Vec2 pos, Color tint)
    {
        ITexture copy = t;
        DrawTexturePro(copy, src, {pos.x, pos.y, src.width, src.height}, {0, 0}, 0, tint);
    }

    void QuarkGLRenderer::DrawTextureEx(const ITexture& t, Vec2 pos, float rot, float scale, Color tint)
    {
        ITexture copy = t;
        Rectangle src{0, 0, (float)t.m_Width, (float)t.m_Height};
        Rectangle dst{pos.x, pos.y, (float)t.m_Width * scale, (float)t.m_Height * scale};
        DrawTexturePro(copy, src, dst, {(float)t.m_Width * scale / 2, (float)t.m_Height * scale / 2}, rot, tint);
    }

    void QuarkGLRenderer::DrawTexturePro(ITexture t, Rectangle src, Rectangle dst,
                                         Vec2 origin, float rotation, Color tint)
    {
        if (!t.m_Id)
            return;
        EnsureBatchTexture(t.m_Id);

        auto n = ToNormColor(tint);

        float tw = (float)t.m_Width, th = (float)t.m_Height;
        float u0 = src.x / tw, v0 = src.y / th, u1 = (src.x + src.width) / tw, v1 = (src.y + src.height) / th;
        Vec2 v[4] = {{-origin.x, -origin.y}, {dst.width - origin.x, -origin.y}, {dst.width - origin.x, dst.height - origin.y}, {-origin.x, dst.height - origin.y}};

        if (rotation != 0)
        {
            float rad = rotation * PI / 180.f, cA = cosf(rad), sA = sinf(rad);
            for (auto& p : v)
            {
                float rx = p.x * cA - p.y * sA;
                float ry = p.x * sA + p.y * cA;
                p.x = rx;
                p.y = ry;
            }
        }

        for (auto& p : v)
        {
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

    void QuarkGLRenderer::DrawTextureTiled(ITexture t, float scale, Vec2 off, Color tint)
    {
        if (!t.m_Id)
            return;

        int tx = (int)ceilf(m_Width / (t.m_Width * scale)) + 1;
        int ty = (int)ceilf(m_Height / (t.m_Height * scale)) + 1;

        for (int y = -1; y < ty; ++y)
            for (int x = -1; x < tx; ++x)
                DrawTexture(t, off.x + x * t.m_Width * scale, off.y + y * t.m_Height * scale, tint);
    }

    void QuarkGLRenderer::DrawTextureNPatch(ITexture t, NPatchInfo np, Rectangle dst,
                                            Vec2 origin, float rot, Color tint)
    {
        if (!t.m_Id)
            return;

        const float srcW = np.source.width > 0.f ? np.source.width : 1.f;
        const float srcH = np.source.height > 0.f ? np.source.height : 1.f;
        (void)srcW;
        (void)srcH;

        const float dLeft = static_cast<float>(np.left);
        const float dTop = static_cast<float>(np.top);
        const float dRight = static_cast<float>(np.right);
        const float dBottom = static_cast<float>(np.bottom);
        const float dMiddleW = dst.width - dLeft - dRight;
        const float dMiddleH = dst.height - dTop - dBottom;

        const float sLeft = np.source.x + dLeft;
        const float sTop = np.source.y + dTop;
        const float sRight = np.source.x + np.source.width - dRight;
        const float sBottom = np.source.y + np.source.height - dBottom;

        auto patch = [&](float sx, float sy, float sw, float sh,
                         float dx, float dy, float dw, float dh)
        {
            if (sw <= 0.f || sh <= 0.f || dw <= 0.f || dh <= 0.f)
                return;
            Rectangle src{sx, sy, sw, sh};
            Rectangle dpt{dst.x + dx, dst.y + dy, dw, dh};
            DrawTexturePro(t, src, dpt, origin, rot, tint);
        };

        if (np.layout == NPATCH_THREE_PATCH_HORIZONTAL)
        {
            // 1x3: left, center (stretch), right
            patch(np.source.x, np.source.y, dLeft, np.source.height, 0.f, 0.f, dLeft, dst.height);
            patch(sLeft, np.source.y, np.source.width - dLeft - dRight, np.source.height,
                  dLeft, 0.f, dMiddleW, dst.height);
            patch(sRight, np.source.y, dRight, np.source.height, dLeft + dMiddleW, 0.f, dRight, dst.height);
            return;
        }

        if (np.layout == NPATCH_THREE_PATCH_VERTICAL)
        {
            // 3x1: top, center (stretch), bottom
            patch(np.source.x, np.source.y, np.source.width, dTop, 0.f, 0.f, dst.width, dTop);
            patch(np.source.x, sTop, np.source.width, np.source.height - dTop - dBottom,
                  0.f, dTop, dst.width, dMiddleH);
            patch(np.source.x, sBottom, np.source.width, dBottom, 0.f, dTop + dMiddleH, dst.width, dBottom);
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

    ITexture QuarkGLRenderer::LoadTexture(const char* pPath)
    {
        return m_Texture.LoadTexture(pPath);
    }

    ITexture QuarkGLRenderer::LoadTextureFromImage(const Image& image)
    {
        return m_Texture.LoadTextureFromImage(image);
    }

    ITexture QuarkGLRenderer::LoadTextureCubemap(const unsigned char* pRgbaFaces, int faceSize)
    {
        return m_Texture.LoadTextureCubemap(pRgbaFaces, faceSize);
    }

    void QuarkGLRenderer::UnloadTexture(ITexture& t)
    {
        m_Texture.UnloadTexture(t);
    }

    bool QuarkGLRenderer::isTextureValid(ITexture& t)
    {
        return m_Texture.IsTextureValid(t);
    }

    ITexture QuarkGLRenderer::GetRenderTextureTexture(IRenderTexture rt)
    {
        return m_Texture.GetRenderTextureTexture(rt);
    }

    IRenderTexture QuarkGLRenderer::LoadRenderTexture(int w, int h)
    {
        return m_Texture.LoadRenderTexture(w, h);
    }

    void QuarkGLRenderer::UnloadRenderTexture(IRenderTexture rt)
    {
        m_Texture.UnloadRenderTexture(rt);
    }

    bool QuarkGLRenderer::isRenderTextureValid(IRenderTexture& rt)
    {
        return m_Texture.IsRenderTextureValid(rt);
    }

    namespace
    {
        void FlipRowsRgba(void* pData, int width, int height)
        {
            const int rowBytes = width * 4;
            std::vector<uint8_t> vTmp(rowBytes);
            uint8_t* pPixels = static_cast<uint8_t*>(pData);
            for (int y = 0; y < height / 2; ++y)
            {
                uint8_t* pTop = pPixels + static_cast<size_t>(y) * rowBytes;
                uint8_t* pBottom = pPixels + static_cast<size_t>(height - 1 - y) * rowBytes;
                std::memcpy(vTmp.data(), pTop, rowBytes);
                std::memcpy(pTop, pBottom, rowBytes);
                std::memcpy(pBottom, vTmp.data(), rowBytes);
            }
        }
    } // anonymous

    Image QuarkGLRenderer::ReadTextureImage(const ITexture& t)
    {
        if (!t.m_Valid || t.m_Id == 0 || t.m_Width <= 0 || t.m_Height <= 0)
            return Image{};

        FlushBatch();

        const size_t bytes = static_cast<size_t>(t.m_Width) * t.m_Height * 4;
        void* pBuf = MemAlloc(bytes);
        if (!pBuf)
            return Image{};

        glBindTexture(GL_TEXTURE_2D, t.m_Id);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pBuf);
        glBindTexture(GL_TEXTURE_2D, 0);

        FlipRowsRgba(pBuf, t.m_Width, t.m_Height);

        Image img{};
        img.data = pBuf;
        img.width = t.m_Width;
        img.height = t.m_Height;
        img.mipmaps = 1;
        img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        TraceLog(LogLevel::Info, "IMAGE", TextFormat("[OpenGL] Read texture pixels back to CPU: %ux%u (ID: %u)", t.m_Width, t.m_Height, t.m_Id));
        return img;
    }

    Image QuarkGLRenderer::ReadScreenImage()
    {
        if (m_Width <= 0 || m_Height <= 0)
            return Image{};

        FlushBatch();

        const int w = m_Width;
        const int h = m_Height;
        const size_t bytes = static_cast<size_t>(w) * h * 4;
        void* pBuf = MemAlloc(bytes);
        if (!pBuf)
            return Image{};

        glBindFramebuffer(GL_FRAMEBUFFER, m_CurrentFbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        if (m_CurrentFbo != 0)
        {
            glReadBuffer(GL_COLOR_ATTACHMENT0);
        }
        else
        {
            glReadBuffer(GL_BACK);
        }
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pBuf);

        FlipRowsRgba(pBuf, w, h);

        Image img{};
        img.data = pBuf;
        img.width = w;
        img.height = h;
        img.mipmaps = 1;
        img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        TraceLog(LogLevel::Info, "IMAGE", TextFormat("[OpenGL] Read backbuffer pixels to CPU: %dx%d", w, h));
        return img;
    }

    ITexture QuarkGLRenderer::GenCheckerTexture(int w, int h, int cell, Color ca, Color cb)
    {
        std::vector<uint8_t> vPx((size_t)w * h * 4);

        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                Color c = ((x / cell + y / cell) % 2 == 0) ? ca : cb;
                size_t i = ((size_t)y * w + x) * 4;

                vPx[i] = c.r;
                vPx[i + 1] = c.g;
                vPx[i + 2] = c.b;
                vPx[i + 3] = c.a;
            }

        ITexture t{};
        t.m_Id = CreateTextureFromRgba(vPx.data(), w, h);
        t.m_Width = w;
        t.m_Height = h;
        t.m_Mipmaps = 1;
        t.m_Format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        t.m_Valid = true;
        TraceLog(LogLevel::Info, "TEXTURE", TextFormat("[OpenGL] Generated checker texture: %dx%d (Cell: %dpx, ID: %u)", w, h, cell, t.m_Id));
        return t;
    }

    void QuarkGLRenderer::BeginTextureMode(IRenderTexture rt)
    {
        FlushBatch();

        glBindFramebuffer(GL_FRAMEBUFFER, rt.m_Id);
        m_CurrentFbo = rt.m_Id;
        m_Width = rt.m_Texture.m_Width;
        m_Height = rt.m_Texture.m_Height;
        m_Batch.SetScreenSize(m_Width, m_Height);
        m_Batch.SetCamera(m_Camera2D, m_Camera2DActive);

        glViewport(0, 0, m_Width, m_Height);
    }

    void QuarkGLRenderer::EndTextureMode()
    {
        FlushBatch();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        m_CurrentFbo = 0;

        RefreshViewport();
        m_Batch.SetScreenSize(m_Width, m_Height);
        m_Batch.SetCamera(m_Camera2D, m_Camera2DActive);
    }

    static std::vector<int> DefaultCodepoints()
    {
        std::vector<int> vCps;
        vCps.reserve(96);
        for (int c = 32; c <= 126; ++c)
            vCps.push_back(c);
        return vCps;
    }

    static int DecodeUTF8(const char*& p)
    {
        const unsigned char lead = static_cast<unsigned char>(*p);
        int cp = 0, seq = 0;
        if ((lead & 0x80) == 0)
        {
            cp = lead;
            seq = 1;
        }
        else if ((lead & 0xE0) == 0xC0)
        {
            cp = lead & 0x1F;
            seq = 2;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            cp = lead & 0x0F;
            seq = 3;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            cp = lead & 0x07;
            seq = 4;
        }
        else
        {
            cp = lead;
            seq = 1;
        }
        for (int k = 1; k < seq && p[k] != '\0'; ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(p[k]) & 0x3F);
        p += seq;
        return cp;
    }

    bool QuarkGLRenderer::LoadFontInternal(const char* pFilePath, const unsigned char* pFileData, int dataSize,
                                           int pointSize, const int* pCodepoints, int codepointCount, FontData& out)
    {
        TraceLog(LogLevel::Trace, "FONT", TextFormat("[OpenGL] FreeType initializing font: %s (size: %d pt)", pFilePath ? pFilePath : "<memory>", pointSize));
        FT_Library ft = nullptr;
        if (FT_Init_FreeType(&ft) != 0)
        {
            TraceLog(LogLevel::Error, "FONT", "[OpenGL] Failed to initialize FreeType library");
            return false;
        }

        std::string cleanFilePath = pFilePath ? pFilePath : "";
        bool syntheticItalic = false;
        const std::string italicTag = "|italic";
        if (cleanFilePath.size() > italicTag.size() &&
            cleanFilePath.compare(cleanFilePath.size() - italicTag.size(), italicTag.size(), italicTag) == 0)
        {
            cleanFilePath.erase(cleanFilePath.size() - italicTag.size());
            syntheticItalic = true;
        }

        FT_Face face = nullptr;
        if (pFileData != nullptr)
        {
            if (FT_New_Memory_Face(ft, pFileData, (FT_Long)dataSize, 0, &face) != 0)
            {
                TraceLog(LogLevel::Error, "FONT", "[OpenGL] FreeType failed to open font from memory");
                FT_Done_FreeType(ft);
                return false;
            }
        }
        else if (FT_New_Face(ft, cleanFilePath.c_str(), 0, &face) != 0)
        {
            TraceLog(LogLevel::Error, "FONT", TextFormat("[OpenGL] FreeType failed to open font file: %s", pFilePath ? pFilePath : "<null>"));
            FT_Done_FreeType(ft);
            return false;
        }

        FT_Select_Charmap(face, FT_ENCODING_UNICODE);
        FT_Set_Pixel_Sizes(face, 0, (FT_UInt)pointSize);
        if (syntheticItalic)
        {
            FT_Matrix italicMatrix = {0x10000L, 0x0366AL, 0, 0x10000L};
            FT_Set_Transform(face, &italicMatrix, nullptr);
        }

        std::vector<int> vCps;
        const int* pCpsPtr = pCodepoints;
        int cpsCount = codepointCount;
        if (pCpsPtr == nullptr || cpsCount <= 0)
        {
            vCps = DefaultCodepoints();
            pCpsPtr = vCps.data();
            cpsCount = (int)vCps.size();
        }

        constexpr int AW = 1024, AH = 1024;
        std::vector<uint8_t> vAtlas((size_t)AW * AH * 4, 0);
        int penX = 1, penY = 1, rowH = 0;
        int renderedGlyphs = 0;
        out.m_vGlyphs.clear();
        out.m_vGlyphs.reserve((size_t)cpsCount);

        for (int i = 0; i < cpsCount; ++i)
        {
            const int cp = pCpsPtr[i];
            if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0)
                continue;

            FT_GlyphSlot slot = face->glyph;
            int gw = (int)slot->bitmap.width;
            int gh = (int)slot->bitmap.rows;

            if (penX + gw + 1 > AW)
            {
                penX = 1;
                penY += rowH + 1;
                rowH = 0;
            }

            if (penY + gh + 1 > AH)
            {
                TraceLog(LogLevel::Warn, "FONT", TextFormat("[OpenGL] Font atlas overflow (%dx%d) for font: %s", AW, AH, pFilePath ? pFilePath : "<memory>"));
                FT_Done_Face(face);
                FT_Done_FreeType(ft);
                return false;
            }

            for (int row = 0; row < gh; ++row)
                for (int col = 0; col < gw; ++col)
                {
                    size_t dst = ((penY + row) * AW + (penX + col)) * 4;
                    uint8_t alpha = slot->bitmap.buffer[row * slot->bitmap.pitch + col];

                    vAtlas[dst] = 255;
                    vAtlas[dst + 1] = 255;
                    vAtlas[dst + 2] = 255;
                    vAtlas[dst + 3] = alpha;
                }

            GlyphData g;
            g.m_Value = cp;
            g.m_Uv = Rectangle{(float)penX / AW, (float)penY / AH,
                             gw > 0 ? (float)gw / AW : 0.f, gh > 0 ? (float)gh / AH : 0.f};
            g.m_Rec = Rectangle{(float)penX, (float)penY, gw > 0 ? (float)gw : 0.f, gh > 0 ? (float)gh : 0.f};
            g.m_AdvanceX = (float)slot->advance.x / 64.f;
            g.m_OffsetX = (float)slot->bitmap_left;
            g.m_OffsetY = (float)slot->bitmap_top;
            g.m_Width = gw;
            g.m_Height = gh;

            if (gw > 0 && gh > 0)
            {
                unsigned char* pGdata = static_cast<unsigned char*>(std::malloc((size_t)gw * gh * 4));
                if (pGdata)
                {
                    for (int row = 0; row < gh; ++row)
                    {
                        for (int col = 0; col < gw; ++col)
                        {
                            unsigned char alpha = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
                            pGdata[((size_t)row * gw + col) * 4 + 0] = 255;
                            pGdata[((size_t)row * gw + col) * 4 + 1] = 255;
                            pGdata[((size_t)row * gw + col) * 4 + 2] = 255;
                            pGdata[((size_t)row * gw + col) * 4 + 3] = alpha;
                        }
                    }
                    g.m_Image = Image{pGdata, gw, gh, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                }
            }
            penX += gw + 1;
            rowH = std::max(rowH, gh);
            renderedGlyphs++;
            out.m_vGlyphs.push_back(g);
        }

        out.m_AtlasTexture = CreateTextureFromRgba(vAtlas.data(), AW, AH);
        out.m_AtlasWidth = AW;
        out.m_AtlasHeight = AH;
        out.m_BaseSize = pointSize;
        out.m_GlyphCount = renderedGlyphs;
        out.m_Ascent = (int)(face->size->metrics.ascender / 64);
        out.m_Descent = (int)(face->size->metrics.descender / 64);
        out.m_LineHeight = (int)(face->size->metrics.height / 64);
        out.m_LineGap = out.m_LineHeight - (out.m_Ascent - out.m_Descent);

        const char* pFamily = face->family_name ? face->family_name : "Unknown";
        const char* pStyle = face->style_name ? face->style_name : "Regular";
        TraceLog(LogLevel::Info, "FONT", TextFormat("[OpenGL] Font rasterized: %s (%s %s, %d glyphs, Atlas: %dx%d, Ascent: %d, Descent: %d, LineHeight: %d)", pFilePath ? pFilePath : "<in-memory>", pFamily, pStyle, renderedGlyphs, AW, AH, out.m_Ascent, out.m_Descent, out.m_LineHeight));

        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return true;
    }

    int QuarkGLRenderer::FindGlyph(const FontData& fd, int codepoint)
    {
        for (int i = 0; i < (int)fd.m_vGlyphs.size(); ++i)
        {
            if (fd.m_vGlyphs[i].m_Value == codepoint)
                return i;
        }
        for (int i = 0; i < (int)fd.m_vGlyphs.size(); ++i)
        {
            if (fd.m_vGlyphs[i].m_Value == 63)
                return i;
        }
        return -1;
    }

    uint32_t QuarkGLRenderer::EnsureDefaultFont()
    {
        if (m_DefaultFontId != 0)
            return m_DefaultFontId;

        FontData fd{};
        if (!LoadFontInternal(nullptr, pixel_ttf, static_cast<int>(pixel_ttf_len), 32, nullptr, 0, fd))
            return 0;

        uint32_t id = m_NextFontId++;
        m_Fonts[id] = std::move(fd);
        m_DefaultFontId = id;
        return id;
    }

    const QuarkGLRenderer::FontData* QuarkGLRenderer::GetFontData(IFont font) const
    {
        auto it = m_Fonts.find(font.m_Id);
        return it != m_Fonts.end() ? &it->second : nullptr;
    }

    void QuarkGLRenderer::DrawTextWithFontData(const FontData& fd, const char* pText,
                                               Vec2 pos, float fontSize, float spacing, Color tint)
    {
        if (!pText)
            return;

        const float scale = fontSize / (float)fd.m_BaseSize;
        const float lineHeight = (float)fd.m_LineHeight * scale;
        const float baseline = (float)fd.m_Ascent * scale;
        float x = pos.x, y = pos.y;

        bool first = true;

        for (const char* c = pText; *c;)
        {
            if (*c == '\n')
            {
                x = pos.x;
                y += lineHeight;
                first = true;
                ++c;
                continue;
            }

            const int cp = DecodeUTF8(c);
            const int idx = FindGlyph(fd, cp);
            if (idx < 0)
            {
                continue;
            }

            const GlyphData& g = fd.m_vGlyphs[(size_t)idx];

            if (!first)
                x += spacing;
            first = false;

            float gx = x + g.m_OffsetX * scale;
            float gy = y + baseline - g.m_OffsetY * scale;
            float gw = (float)g.m_Width * scale, gh = (float)g.m_Height * scale;
            if (gw > 0 && gh > 0)
                PushTexturedQuad(fd.m_AtlasTexture, g.m_Uv, gx, gy, gw, gh, tint);

            x += g.m_AdvanceX * scale;
        }
    }

    Vec2 QuarkGLRenderer::MeasureTextWithFontData(const FontData& fd, const char* pText, float fontSize, float spacing) const
    {
        if (!pText)
            return {};

        float scale = (float)fontSize / (float)fd.m_BaseSize;
        float lh = (float)fd.m_LineHeight * scale;

        float x = 0, maxW = 0;
        bool first = true;
        int lines = 1;

        for (const char* c = pText; *c;)
        {
            if (*c == '\n')
            {
                maxW = std::max(maxW, x);
                x = 0;
                first = true;
                ++lines;
                ++c;
                continue;
            }

            const int cp = DecodeUTF8(c);
            const int idx = FindGlyph(fd, cp);
            if (idx < 0)
                continue;

            if (!first)
                x += spacing;
            first = false;
            x += fd.m_vGlyphs[(size_t)idx].m_AdvanceX * scale;
        }

        return {std::max(maxW, x), lh * (float)lines};
    }

    IFont QuarkGLRenderer::LoadFont(const char* pFilePath, int fontSize,
                                    const int* pCodepoints, int codepointCount)
    {
        if (!pFilePath)
        {
            IFont handle{};
            handle.m_Id = EnsureDefaultFont();
            return handle;
        }

        FontData fd{};
        if (!LoadFontInternal(pFilePath, nullptr, 0, fontSize, pCodepoints, codepointCount, fd))
        {
            return IFont{};
        }

        const uint32_t id = m_NextFontId++;
        m_Fonts[id] = std::move(fd);
        IFont handle{};
        handle.m_Id = id;
        return handle;
    }

    IFont QuarkGLRenderer::LoadFontFromMemory(const char* pFileType, const unsigned char* pFileData, int dataSize,
                                              int fontSize, const int* pCodepoints, int codepointCount)
    {
        if (!pFileData || dataSize <= 0)
        {
            return IFont{};
        }

        FontData fd{};
        if (!LoadFontInternal(pFileType, pFileData, dataSize, fontSize, pCodepoints, codepointCount, fd))
        {
            return IFont{};
        }

        const uint32_t id = m_NextFontId++;
        m_Fonts[id] = std::move(fd);
        IFont handle{};
        handle.m_Id = id;
        return handle;
    }

    void QuarkGLRenderer::UnloadFont(IFont& font)
    {
        auto it = m_Fonts.find(font.m_Id);
        if (it != m_Fonts.end())
        {
            GLuint atlasId = it->second.m_AtlasTexture;
            if (it->second.m_AtlasTexture)
                glDeleteTextures(1, &it->second.m_AtlasTexture);

            for (GlyphData& g : it->second.m_vGlyphs)
            {
                std::free(g.m_Image.data);
                g.m_Image = Image{};
            }

            if (font.m_Id == m_DefaultFontId)
                m_DefaultFontId = 0;

            m_Fonts.erase(it);
            TraceLog(LogLevel::Info, "FONT", TextFormat("[OpenGL] Font unloaded (Font ID: %u, Atlas ID: %u)", font.m_Id, atlasId));
        }

        font.m_Id = 0;
    }

    void QuarkGLRenderer::FillFont(IFont font, Font& out)
    {
        const FontData* pFd = GetFontData(font);
        if (!pFd)
        {
            uint32_t id = EnsureDefaultFont();
            pFd = (id != 0) ? GetFontData(IFont{id}) : nullptr;
        }
        if (!pFd)
        {
            out.valid = false;
            return;
        }

        out.baseSize = pFd->m_BaseSize;
        out.glyphCount = pFd->m_GlyphCount;
        out.glyphPadding = 2;
        out.valid = true;
        out._rendererFontId = font.m_Id;
        out.texture = Texture2D{pFd->m_AtlasTexture, pFd->m_AtlasWidth, pFd->m_AtlasHeight, 1,
                                PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, true};

        delete[] out.recs;
        delete[] out.glyphs;
        out.recs = new Rectangle[pFd->m_GlyphCount];
        out.glyphs = new GlyphInfo[pFd->m_GlyphCount];

        for (int i = 0; i < pFd->m_GlyphCount; ++i)
        {
            const GlyphData& g = pFd->m_vGlyphs[(size_t)i];
            out.recs[i] = g.m_Rec;
            out.glyphs[i].value = g.m_Value;
            out.glyphs[i].offsetX = (int)g.m_OffsetX;
            out.glyphs[i].offsetY = (int)g.m_OffsetY;
            out.glyphs[i].advanceX = (int)g.m_AdvanceX;
            out.glyphs[i].image = g.m_Image;
        }
    }

    void QuarkGLRenderer::DrawDebugText(const char* pText, int x, int y, int fontSize, Color color)
    {
        if (!pText || !*pText || fontSize <= 0)
            return;

        static std::unordered_map<int, IFont> s_debugFonts;
        auto it = s_debugFonts.find(fontSize);
        if (it == s_debugFonts.end() || m_Fonts.find(it->second.m_Id) == m_Fonts.end())
        {
            FontData fontData{};
            if (!LoadFontInternal(nullptr, tahoma_ttf, static_cast<int>(tahoma_ttf_len), fontSize,
                                  nullptr, 0, fontData))
                return;

            IFont font{};
            font.m_Id = m_NextFontId++;
            m_Fonts[font.m_Id] = std::move(fontData);
            s_debugFonts[fontSize] = font;
            it = s_debugFonts.find(fontSize);
        }

        int winW = 0, winH = 0;
        SDL_GetWindowSizeInPixels(m_pWindow, &winW, &winH);
        if (winW > 0 && winH > 0)
        {
            m_Width = winW;
            m_Height = winH;
        }

        m_Batch.SetScreenSize(m_Width, m_Height);
        m_Batch.SetCurrentShader(m_DefaultShader);
        m_Batch.SetCamera({}, false);

        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, m_Width, m_Height);
        glBindSampler(0, 0);

        const Color shadow = {0, 0, 0, 255};
        const std::array<Vec2, 9> offsets = {
            Vec2{-1.f, -1.f}, Vec2{0.f, -1.f}, Vec2{1.f, -1.f},
            Vec2{-1.f, 0.f}, Vec2{0.f, 0.f}, Vec2{1.f, 0.f},
            Vec2{-1.f, 1.f}, Vec2{0.f, 1.f}, Vec2{1.f, 1.f}};

        for (const Vec2& delta : offsets)
        {
            if (delta.x == 0.f && delta.y == 0.f)
                continue;
            DrawTextEx(it->second, pText, Vec2{static_cast<float>(x) + delta.x, static_cast<float>(y) + delta.y}, static_cast<float>(fontSize), 0.f, shadow);
        }

        DrawTextEx(it->second, pText, Vec2{static_cast<float>(x) + 1.f, static_cast<float>(y) + 1.f}, static_cast<float>(fontSize), 0.f, shadow);
        DrawTextEx(it->second, pText, Vec2{static_cast<float>(x), static_cast<float>(y)}, static_cast<float>(fontSize), 0.f, color);

        FlushBatch();
    }

    void QuarkGLRenderer::DrawText(const char* pText, int x, int y, int fontSize, Color color)
    {
        uint32_t id = EnsureDefaultFont();
        if (!id)
            return;

        DrawTextWithFontData(m_Fonts[id], pText, {(float)x, (float)y}, (float)fontSize, 0.f, color);
    }

    void QuarkGLRenderer::DrawTextEx(IFont font, const char* pText, Vec2 pos,
                                     float fontSize, float spacing, Color tint)
    {
        const FontData* pFd = GetFontData(font);
        if (!pFd)
            return;

        DrawTextWithFontData(*pFd, pText, pos, fontSize, spacing, tint);
    }

    Vec2 QuarkGLRenderer::MeasureTextEx(IFont font, const char* pText,
                                        float fontSize, float spacing)
    {
        const FontData* pFd = GetFontData(font);
        if (!pFd)
            return {};

        return MeasureTextWithFontData(*pFd, pText, fontSize, spacing);
    }

    int QuarkGLRenderer::MeasureText(const char* pText, int fontSize)
    {
        uint32_t id = EnsureDefaultFont();
        if (!id)
            return 0;

        return (int)std::round(MeasureTextWithFontData(m_Fonts[id], pText, (float)fontSize, 0.f).x);
    }

    void QuarkGLRenderer::BeginShaderMode(const Shader& sh)
    {
        if (sh.id)
        {
            m_CurrentShader = sh.id;
            m_Batch.SetCurrentShader(sh.id);
            glUseProgram(sh.id);
        }
    }

    void QuarkGLRenderer::EndShaderMode()
    {
        FlushBatch();
        m_CurrentShader = m_DefaultShader;
        m_Batch.SetCurrentShader(m_DefaultShader);
        glUseProgram(m_DefaultShader);
    }

    Shader QuarkGLRenderer::LoadShader(const char* pVsFileName, const char* pFsFileName)
    {
        TraceLog(LogLevel::Trace, "SHADER", TextFormat("[OpenGL] Loading shader files: VS='%s', FS='%s'", pVsFileName ? pVsFileName : "<none>", pFsFileName ? pFsFileName : "<none>"));
        std::string vsSource, fsSource;
        if (pVsFileName)
        {
            std::ifstream vsFile(pVsFileName);
            if (vsFile.is_open())
            {
                vsSource.assign((std::istreambuf_iterator<char>(vsFile)),
                                (std::istreambuf_iterator<char>()));
                TraceLog(LogLevel::Trace, "SHADER", TextFormat("[OpenGL] Read vertex shader file '%s' (%zu bytes)", pVsFileName, vsSource.size()));
            }
            else
            {
                TraceLog(LogLevel::Error, "SHADER", TextFormat("[OpenGL] Failed to open vertex shader file: %s", pVsFileName));
                return Shader{};
            }
        }

        if (pFsFileName)
        {
            std::ifstream fsFile(pFsFileName);
            if (fsFile.is_open())
            {
                fsSource.assign((std::istreambuf_iterator<char>(fsFile)),
                                (std::istreambuf_iterator<char>()));
                TraceLog(LogLevel::Trace, "SHADER", TextFormat("[OpenGL] Read fragment shader file '%s' (%zu bytes)", pFsFileName, fsSource.size()));
            }
            else
            {
                TraceLog(LogLevel::Error, "SHADER", TextFormat("[OpenGL] Failed to open fragment shader file: %s", pFsFileName));
                return Shader{};
            }
        }

        return LoadShaderFromMemory(vsSource.empty() ? nullptr : vsSource.c_str(),
                                    fsSource.empty() ? nullptr : fsSource.c_str());
    }

    Shader QuarkGLRenderer::LoadShaderFromMemory(const char* pVsSource, const char* pFsSource)
    {
        GLuint vs = 0, fs = 0;
        if (pVsSource)
        {
            vs = CompileGLShader(GL_VERTEX_SHADER, pVsSource);
            if (vs == 0)
                return Shader{};
        }
        if (pFsSource)
        {
            fs = CompileGLShader(GL_FRAGMENT_SHADER, pFsSource);
            if (fs == 0)
            {
                if (vs)
                    glDeleteShader(vs);
                return Shader{};
            }
        }

        GLuint p = glCreateProgram();
        if (vs)
            glAttachShader(p, vs);
        if (fs)
            glAttachShader(p, fs);
        glLinkProgram(p);

        if (vs)
            glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);

        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok)
        {
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
        for (int i = 0; i < SHADER_LOC_COUNT; ++i)
        {
            result.locs[i] = GetShaderLocation(result, static_cast<ShaderLocationIndex>(i));
            if (result.locs[i] != -1)
                foundLocCount++;
        }
        TraceLog(LogLevel::Info, "SHADER", TextFormat("[OpenGL] Shader program linked successfully (ID: %u, Active Uniforms: %d, Active Attributes: %d, Standard Locs: %d/%d)", p, numUniforms, numAttribs, foundLocCount, SHADER_LOC_COUNT));
        return result;
    }

    void QuarkGLRenderer::UnloadShader(Shader& sh)
    {
        InvalidateGLShaderUniformCache(sh.id);
        if (sh.id)
        {
            TraceLog(LogLevel::Info, "SHADER", TextFormat("[OpenGL] Shader program unloaded (ID: %u)", sh.id));
            glDeleteProgram(sh.id);
            sh.id = 0;
        }
    }

    bool QuarkGLRenderer::isShaderValid(Shader& sh)
    {
        return sh.id != 0;
    }

    int QuarkGLRenderer::GetShaderLocation(const Shader& sh, const char* pName)
    {
        return sh.id ? glGetUniformLocation(sh.id, pName) : -1;
    }

    int QuarkGLRenderer::GetShaderLocation(const Shader& sh, ShaderLocationIndex locIndex)
    {
        if (!sh.id || locIndex >= SHADER_LOC_COUNT)
            return -1;

        if (locIndex <= SHADER_LOC_VERTEX_COLOR || locIndex >= SHADER_LOC_VERTEX_BONEIDS)
        {
            return glGetAttribLocation(sh.id, s_apShaderLocationNames[locIndex]);
        }
        else
        {
            return glGetUniformLocation(sh.id, s_apShaderLocationNames[locIndex]);
        }
    }

    int QuarkGLRenderer::GetShaderAttributeLocation(const Shader& sh, const char* pName)
    {
        return sh.id ? glGetAttribLocation(sh.id, pName) : -1;
    }

    void QuarkGLRenderer::SetShaderValue(const Shader& shader, int loc, float v)
    {
        if (loc < 0)
            return;
        WithShaderProgram(shader, [&]()
                          {
                              glUniform1f(loc, v);
                          });
    }

    void QuarkGLRenderer::SetShaderValue(const Shader& shader, int loc, int v)
    {
        if (loc < 0)
            return;
        WithShaderProgram(shader, [&]()
                          {
                              glUniform1i(loc, v);
                          });
    }

    void QuarkGLRenderer::SetShaderValue(const Shader& shader, int loc, const Color& c)
    {
        if (loc < 0)
            return;
        WithShaderProgram(shader, [&]()
                          {
                              glUniform4f(loc, c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f);
                          });
    }

    void QuarkGLRenderer::SetShaderValue(const Shader& shader, int loc, const Vec2& v)
    {
        if (loc < 0)
            return;
        WithShaderProgram(shader, [&]()
                          {
                              glUniform2f(loc, v.x, v.y);
                          });
    }

    void QuarkGLRenderer::SetShaderValue(const Shader& shader, int locIndex, const Vec3& value)
    {
        if (locIndex < 0)
            return;
        WithShaderProgram(shader, [&]()
                          {
                              glUniform3f(locIndex, value.x, value.y, value.z);
                          });
    }

    void QuarkGLRenderer::SetShaderValue(const Shader& shader, int locIndex, const Vec4& value)
    {
        if (locIndex < 0)
            return;
        WithShaderProgram(shader, [&]()
                          {
                              glUniform4f(locIndex, value.x, value.y, value.z, value.w);
                          });
    }

    void QuarkGLRenderer::SetShaderValueMatrix(const Shader& shader, int loc, const float* pM)
    {
        if (loc < 0 || !pM)
            return;
        WithShaderProgram(shader, [&]()
                          {
                              glUniformMatrix4fv(loc, 1, GL_FALSE, pM);
                          });
    }

    void QuarkGLRenderer::SetShaderValueSampler(const Shader& shader, int loc, int unit)
    {
        if (loc < 0)
            return;
        WithShaderProgram(shader, [&]()
                          {
                              glUniform1i(loc, unit);
                          });
    }

    void QuarkGLRenderer::SetShaderValue(const Shader& s, int loc, const void* pValue, int uniformType)
    {
        if (loc < 0 || !pValue)
            return;
        WithShaderProgram(s, [&]()
                          {
                              switch (uniformType)
                              {
                              case SHADER_UNIFORM_FLOAT:
                                  glUniform1f(loc, *reinterpret_cast<const float*>(pValue));
                                  break;
                              case SHADER_UNIFORM_VEC2:
                                  glUniform2fv(loc, 1, reinterpret_cast<const float*>(pValue));
                                  break;
                              case SHADER_UNIFORM_VEC3:
                                  glUniform3fv(loc, 1, reinterpret_cast<const float*>(pValue));
                                  break;
                              case SHADER_UNIFORM_VEC4:
                                  glUniform4fv(loc, 1, reinterpret_cast<const float*>(pValue));
                                  break;
                              case SHADER_UNIFORM_INT:
                                  glUniform1i(loc, *reinterpret_cast<const int*>(pValue));
                                  break;
                              case SHADER_UNIFORM_IVEC2:
                                  glUniform2iv(loc, 1, reinterpret_cast<const int*>(pValue));
                                  break;
                              case SHADER_UNIFORM_IVEC3:
                                  glUniform3iv(loc, 1, reinterpret_cast<const int*>(pValue));
                                  break;
                              case SHADER_UNIFORM_IVEC4:
                                  glUniform4iv(loc, 1, reinterpret_cast<const int*>(pValue));
                                  break;
                              case SHADER_UNIFORM_SAMPLER2D:
                                  glUniform1i(loc, *reinterpret_cast<const int*>(pValue));
                                  break;
                              default:
                                  break;
                              }
                          });
    }

    void QuarkGLRenderer::SetShaderValueV(const Shader& s, int loc, const void* pValue, int uniformType, int count)
    {
        if (loc < 0 || !pValue || count <= 0)
            return;
        WithShaderProgram(s, [&]()
                          {
                              switch (uniformType)
                              {
                              case SHADER_UNIFORM_FLOAT:
                                  glUniform1fv(loc, count, reinterpret_cast<const float*>(pValue));
                                  break;
                              case SHADER_UNIFORM_VEC2:
                                  glUniform2fv(loc, count, reinterpret_cast<const float*>(pValue));
                                  break;
                              case SHADER_UNIFORM_VEC3:
                                  glUniform3fv(loc, count, reinterpret_cast<const float*>(pValue));
                                  break;
                              case SHADER_UNIFORM_VEC4:
                                  glUniform4fv(loc, count, reinterpret_cast<const float*>(pValue));
                                  break;
                              case SHADER_UNIFORM_INT:
                                  glUniform1iv(loc, count, reinterpret_cast<const int*>(pValue));
                                  break;
                              case SHADER_UNIFORM_IVEC2:
                                  glUniform2iv(loc, count, reinterpret_cast<const int*>(pValue));
                                  break;
                              case SHADER_UNIFORM_IVEC3:
                                  glUniform3iv(loc, count, reinterpret_cast<const int*>(pValue));
                                  break;
                              case SHADER_UNIFORM_IVEC4:
                                  glUniform4iv(loc, count, reinterpret_cast<const int*>(pValue));
                                  break;
                              case SHADER_UNIFORM_SAMPLER2D:
                                  glUniform1iv(loc, count, reinterpret_cast<const int*>(pValue));
                                  break;
                              default:
                                  break;
                              }
                          });
    }

    void QuarkGLRenderer::SetShaderValueMatrix(const Shader& s, int loc, const Matrix& mat)
    {
        if (loc < 0)
            return;
        WithShaderProgram(s, [&]()
                          {
                              glUniformMatrix4fv(loc, 1, GL_FALSE, mat.m);
                          });
    }

    void QuarkGLRenderer::SetShaderValueTexture(const Shader& s, int loc, const ITexture& texture)
    {
        if (loc < 0)
            return;
        glActiveTexture(GL_TEXTURE0);
        const GLenum target = m_Texture.IsCubemap(texture.m_Id) ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        glBindTexture(target, texture.m_Id);
        SetShaderValueSampler(s, loc, 0);
    }

    void QuarkGLRenderer::SetShaderValueTextureUnit(const Shader& s, int loc, const ITexture& texture, int textureUnit)
    {
        if (loc < 0 || textureUnit < 0)
            return;
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        const GLenum target = m_Texture.IsCubemap(texture.m_Id) ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        glBindTexture(target, texture.m_Id);
        SetShaderValueSampler(s, loc, textureUnit);
        glActiveTexture(GL_TEXTURE0);
    }

    void QuarkGLRenderer::SetTextureFilterMode(TextureFilter mode)
    {
        g_TextureFilter = mode;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        (mode == TEXTURE_FILTER_POINT) ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        (mode == TEXTURE_FILTER_POINT) ? GL_NEAREST : GL_LINEAR);
    }

    void QuarkGLRenderer::SetTextureFilter(TextureFilter filter)
    {
        const bool point = (filter == TEXTURE_FILTER_POINT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, point ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, point ? GL_NEAREST : GL_LINEAR);
    }

    void QuarkGLRenderer::SetTextureWrap(int wrap)
    {
        const GLenum glWrap = ToGLTextureWrapMode(wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, glWrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, glWrap);
    }

    void QuarkGLRenderer::SetTextureWrap(uint32_t textureId, int wrap)
    {
        if (textureId == 0)
        {
            return;
        }

        const GLenum target = m_Texture.IsCubemap(textureId) ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        GLint previousBinding = 0;
        glGetIntegerv(target == GL_TEXTURE_CUBE_MAP
                          ? GL_TEXTURE_BINDING_CUBE_MAP
                          : GL_TEXTURE_BINDING_2D,
                      &previousBinding);

        glBindTexture(target, textureId);
        const GLenum glWrap = ToGLTextureWrapMode(wrap);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, glWrap);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, glWrap);
        if (target == GL_TEXTURE_CUBE_MAP)
        {
            glTexParameteri(target, GL_TEXTURE_WRAP_R, glWrap);
        }
        glBindTexture(target, static_cast<GLuint>(previousBinding));
    }

    void QuarkGLRenderer::BeginScissorMode(int x, int y, int width, int height)
    {
        FlushBatch();
        glEnable(GL_SCISSOR_TEST);
        glScissor(x, m_Height - (y + height), width, height);
    }

    void QuarkGLRenderer::EndScissorMode()
    {
        FlushBatch();
        glDisable(GL_SCISSOR_TEST);
    }

    void QuarkGLRenderer::SetBlendMode(int mode)
    {
        m_Batch.Flush();
        glEnable(GL_BLEND);

        switch (mode)
        {
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

    void QuarkGLRenderer::BeginMode2D(const Camera2D& cam)
    {
        m_Camera2D = cam;
        m_Camera2DActive = true;
        m_Batch.SetCamera(cam, true);
    }

    void QuarkGLRenderer::EndMode2D()
    {
        m_Camera2DActive = false;
        m_Batch.SetCamera(m_Camera2D, false);
    }

    void QuarkGLRenderer::BeginMode3D(const Camera3D& camera)
    {
        Init3DState();
        Init3DGeometry();

        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        glUseProgram(m_3D.m_Shader3D);

        Mat4 view = Mat4::lookAt(camera.position, camera.target, camera.up);
        float asp = (float)m_Width / (float)m_Height;
        Mat4 proj = Mat4::perspective(camera.fovy * PI / 180.f, asp, 0.1f, 1000.f);

        Set3DView(view, proj);

        if (m_3D.m_SamplerLoc >= 0)
            glUniform1i(m_3D.m_SamplerLoc, 0);
        if (m_3D.colorLoc >= 0)
            glUniform4f(m_3D.colorLoc, 1, 1, 1, 1);
        if (m_3D.lightPosLoc >= 0)
            glUniform3f(m_3D.lightPosLoc,
                        m_3D.m_LightPosition.x, m_3D.m_LightPosition.y, m_3D.m_LightPosition.z);
    }

    void QuarkGLRenderer::EndMode3D()
    {
        FlushLines3D();
        FlushTriangles3D();

        m_vMatrixStack.clear();
        m_CurrentMatrix = Mat4::identity();

        glDisable(GL_DEPTH_TEST);
        glUseProgram(0);
    }

    void QuarkGLRenderer::PushMatrix()
    {
        m_vMatrixStack.push_back(m_CurrentMatrix);
    }

    void QuarkGLRenderer::PopMatrix()
    {
        if (!m_vMatrixStack.empty())
        {
            m_CurrentMatrix = m_vMatrixStack.back();
            m_vMatrixStack.pop_back();
        }
        else
            m_CurrentMatrix = Mat4::identity();
    }

    void QuarkGLRenderer::Translate(const Vec3& t)
    {
        m_CurrentMatrix = m_CurrentMatrix * Mat4::translation(t.x, t.y, t.z);
    }

    void QuarkGLRenderer::Rotate(float angle, const Vec3& axis)
    {
        Vec3 a = axis;
        float len = a.length();
        if (len <= 0)
            return;
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

        m_CurrentMatrix = m_CurrentMatrix * r;
    }

    void QuarkGLRenderer::Scale(const Vec3& s)
    {
        m_CurrentMatrix = m_CurrentMatrix * Mat4::scale(s.x, s.y, s.z);
    }

    void QuarkGLRenderer::MultMatrix(const Mat4& m)
    {
        m_CurrentMatrix = m_CurrentMatrix * m;
    }

    const float* QuarkGLRenderer::GetMatrixModelview()
    {
        return m_3D.m_ViewMatrix.m;
    }

    const float* QuarkGLRenderer::GetMatrixProjection()
    {
        return m_3D.m_ProjectionMatrix.m;
    }

    void QuarkGLRenderer::EnableBackfaceCulling()
    {
        glEnable(GL_CULL_FACE);

        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
    }

    void QuarkGLRenderer::DisableBackfaceCulling()
    {
        glDisable(GL_CULL_FACE);
    }

    void QuarkGLRenderer::Init3DState()
    {
        if (m_3D.m_Initialized)
            return;

        m_3D.m_Shader3D = Compile3DShader();

        m_3D.m_ModelLoc = glGetUniformLocation(m_3D.m_Shader3D, "uModel");
        m_3D.viewLoc = glGetUniformLocation(m_3D.m_Shader3D, "uView");
        m_3D.projLoc = glGetUniformLocation(m_3D.m_Shader3D, "uProjection");
        m_3D.m_SamplerLoc = glGetUniformLocation(m_3D.m_Shader3D, "uTexture");
        m_3D.lightPosLoc = glGetUniformLocation(m_3D.m_Shader3D, "uLightPos");
        m_3D.colorLoc = glGetUniformLocation(m_3D.m_Shader3D, "uColor");

        m_3D.m_Initialized = true;

        const uint8_t white[4] = {255, 255, 255, 255};
        glGenTextures(1, &m_3D.m_WhiteTexture);

        glBindTexture(GL_TEXTURE_2D, m_3D.m_WhiteTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        const uint8_t black[4] = {0, 0, 0, 255};
        glGenTextures(1, &m_3D.m_BlackTexture);

        glBindTexture(GL_TEXTURE_2D, m_3D.m_BlackTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        const uint8_t flatNormal[4] = {128, 128, 255, 255};
        glGenTextures(1, &m_3D.m_FlatNormalTexture);

        glBindTexture(GL_TEXTURE_2D, m_3D.m_FlatNormalTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, flatNormal);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    GLuint QuarkGLRenderer::Compile3DShader()
    {
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &VS_3D, nullptr);
        glCompileShader(vs);

        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &FS_3D, nullptr);
        glCompileShader(fs);

        GLuint p = glCreateProgram();

        glAttachShader(p, vs);
        glAttachShader(p, fs);
        glLinkProgram(p);

        glDeleteShader(vs);
        glDeleteShader(fs);

        return p;
    }

    void QuarkGLRenderer::Init3DGeometry()
    {
        if (m_3D.m_PlaneVAO != 0)
            return;

        auto setup = [](GLuint& vao, GLuint& vbo, GLuint& ebo,
                        const float* vd, size_t vs, const unsigned int* id, size_t is)
        {
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
        float pv[] = {-0.5f, 0, -0.5f, 0, 1, 0, 0, 0, 0.5f, 0, -0.5f, 0, 1, 0, 1, 0,
                      0.5f, 0, 0.5f, 0, 1, 0, 1, 1, -0.5f, 0, 0.5f, 0, 1, 0, 0, 1};
        unsigned int pi[] = {0, 1, 2, 0, 2, 3};
        m_3D.m_PlaneIndexCount = 6;
        setup(m_3D.m_PlaneVAO, m_3D.planeVBO, m_3D.planeEBO, pv, sizeof(pv), pi, sizeof(pi));

        // Cube
        float cv[] = {
            -0.5f, -0.5f, 0.5f, 0, 0, 1, 0, 0, 0.5f, -0.5f, 0.5f, 0, 0, 1, 1, 0, 0.5f, 0.5f, 0.5f, 0, 0, 1, 1, 1, -0.5f, 0.5f, 0.5f, 0, 0, 1, 0, 1,
            -0.5f, -0.5f, -0.5f, 0, 0, -1, 0, 0, -0.5f, 0.5f, -0.5f, 0, 0, -1, 1, 0, 0.5f, 0.5f, -0.5f, 0, 0, -1, 1, 1, 0.5f, -0.5f, -0.5f, 0, 0, -1, 0, 1,
            -0.5f, 0.5f, -0.5f, 0, 1, 0, 0, 0, -0.5f, 0.5f, 0.5f, 0, 1, 0, 1, 0, 0.5f, 0.5f, 0.5f, 0, 1, 0, 1, 1, 0.5f, 0.5f, -0.5f, 0, 1, 0, 0, 1,
            -0.5f, -0.5f, -0.5f, 0, -1, 0, 0, 0, 0.5f, -0.5f, -0.5f, 0, -1, 0, 1, 0, 0.5f, -0.5f, 0.5f, 0, -1, 0, 1, 1, -0.5f, -0.5f, 0.5f, 0, -1, 0, 0, 1,
            0.5f, -0.5f, -0.5f, 1, 0, 0, 0, 0, 0.5f, 0.5f, -0.5f, 1, 0, 0, 1, 0, 0.5f, 0.5f, 0.5f, 1, 0, 0, 1, 1, 0.5f, -0.5f, 0.5f, 1, 0, 0, 0, 1,
            -0.5f, -0.5f, -0.5f, -1, 0, 0, 0, 0, -0.5f, -0.5f, 0.5f, -1, 0, 0, 1, 0, -0.5f, 0.5f, 0.5f, -1, 0, 0, 1, 1, -0.5f, 0.5f, -0.5f, -1, 0, 0, 0, 1};
        unsigned int ci[] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11, 12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};
        m_3D.m_CubeIndexCount = 36;
        setup(m_3D.m_CubeVAO, m_3D.cubeVBO, m_3D.cubeEBO, cv, sizeof(cv), ci, sizeof(ci));

        // Sphere
        std::vector<float> vSv;
        std::vector<unsigned int> vSi;

        const int R = 16, S = 16;
        for (int r = 0; r <= R; ++r)
        {
            float phi = PI * r / R;
            for (int s = 0; s <= S; ++s)
            {
                float th = 2 * PI * s / S;
                float x = sinf(phi) * cosf(th), y = cosf(phi), z = sinf(phi) * sinf(th);
                vSv.insert(vSv.end(), {x, y, z, x, y, z, (float)s / S, (float)r / R});
            }
        }
        for (int r = 0; r < R; ++r)
        {
            for (int s = 0; s < S; ++s)
            {
                vSi.push_back(r * (S + 1) + s);
                vSi.push_back((r + 1) * (S + 1) + s);
                vSi.push_back((r + 1) * (S + 1) + (s + 1));

                vSi.push_back(r * (S + 1) + s);
                vSi.push_back((r + 1) * (S + 1) + (s + 1));
                vSi.push_back(r * (S + 1) + (s + 1));
            }
        }

        m_3D.m_SphereIndexCount = static_cast<int>(vSi.size());

        setup(
            m_3D.m_SphereVAO,
            m_3D.sphereVBO,
            m_3D.sphereEBO,
            vSv.data(),
            vSv.size() * 4,
            vSi.data(),
            vSi.size() * 4);

        // Line / Triangle dynamic VAOs
        auto dynVao = [](GLuint& vao, GLuint& vbo)
        {
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
                (void*)offsetof(Vertex3D, position));

            glEnableVertexAttribArray(1);
            glVertexAttribPointer(
                1,
                3,
                GL_FLOAT,
                GL_FALSE,
                sizeof(Vertex3D),
                (void*)offsetof(Vertex3D, normal));

            glEnableVertexAttribArray(2);
            glVertexAttribPointer(
                2,
                2,
                GL_FLOAT,
                GL_FALSE,
                sizeof(Vertex3D),
                (void*)offsetof(Vertex3D, texCoord));

            glBindVertexArray(0);
        };

        dynVao(m_3D.m_LineVAO, m_3D.lineVBO);
        dynVao(m_3D.m_TriVAO, m_3D.triVBO);
    }

    void QuarkGLRenderer::Set3DView(const Mat4& view, const Mat4& proj)
    {
        m_3D.m_ViewMatrix = view;
        m_3D.m_ProjectionMatrix = proj;

        if (m_3D.m_Initialized)
        {
            glUniformMatrix4fv(m_3D.viewLoc, 1, GL_FALSE, m_3D.m_ViewMatrix.m);
            glUniformMatrix4fv(m_3D.projLoc, 1, GL_FALSE, m_3D.m_ProjectionMatrix.m);
        }
    }

    Vec3 QuarkGLRenderer::TransformPoint(const Mat4& m, const Vec3& p) const
    {
        float x =
            m.m[0] * p.x +
            m.m[4] * p.y +
            m.m[8] * p.z +
            m.m[12];

        float y =
            m.m[1] * p.x +
            m.m[5] * p.y +
            m.m[9] * p.z +
            m.m[13];

        float z =
            m.m[2] * p.x +
            m.m[6] * p.y +
            m.m[10] * p.z +
            m.m[14];

        float w =
            m.m[3] * p.x +
            m.m[7] * p.y +
            m.m[11] * p.z +
            m.m[15];

        if (w != 0.0f)
        {
            x /= w;
            y /= w;
            z /= w;
        }

        return {
            x,
            y,
            z};
    }

    Mat4 QuarkGLRenderer::ApplyCurrentMatrix(const Mat4& t) const
    {
        return m_CurrentMatrix * t;
    }

    void QuarkGLRenderer::FlushLines3D()
    {
        if (m_3D.m_vLineVertices.empty())
            return;

        glUseProgram(m_3D.m_Shader3D);
        if (m_3D.viewLoc >= 0)
            glUniformMatrix4fv(m_3D.viewLoc, 1, GL_FALSE, m_3D.m_ViewMatrix.m);
        if (m_3D.projLoc >= 0)
            glUniformMatrix4fv(m_3D.projLoc, 1, GL_FALSE, m_3D.m_ProjectionMatrix.m);

        Mat4 id = Mat4::identity();
        if (m_3D.m_ModelLoc >= 0)
            glUniformMatrix4fv(m_3D.m_ModelLoc, 1, GL_FALSE, id.m);
        if (m_3D.colorLoc >= 0)
        {
            glUniform4f(m_3D.colorLoc,
                        m_3D.m_CurrentLineColor.r / 255.f,
                        m_3D.m_CurrentLineColor.g / 255.f,
                        m_3D.m_CurrentLineColor.b / 255.f,
                        m_3D.m_CurrentLineColor.a / 255.f);
        }

        glBindTexture(GL_TEXTURE_2D, m_3D.m_WhiteTexture);
        glBindVertexArray(m_3D.m_LineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_3D.lineVBO);

        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(m_3D.m_vLineVertices.size() * sizeof(Vertex3D)),
                     m_3D.m_vLineVertices.data(),
                     GL_DYNAMIC_DRAW);

        glDrawArrays(GL_LINES, 0, (GLsizei)m_3D.m_vLineVertices.size());
        RecordDebugRenderDraw(m_3D.m_vLineVertices.size());

        glBindVertexArray(0);
        m_3D.m_vLineVertices.clear();
    }

    void QuarkGLRenderer::FlushTriangles3D()
    {
        if (m_3D.m_vTriVertices.empty())
            return;

        glUseProgram(m_3D.m_Shader3D);
        if (m_3D.viewLoc >= 0)
            glUniformMatrix4fv(m_3D.viewLoc, 1, GL_FALSE, m_3D.m_ViewMatrix.m);
        if (m_3D.projLoc >= 0)
            glUniformMatrix4fv(m_3D.projLoc, 1, GL_FALSE, m_3D.m_ProjectionMatrix.m);

        Mat4 id = Mat4::identity();
        if (m_3D.m_ModelLoc >= 0)
            glUniformMatrix4fv(m_3D.m_ModelLoc, 1, GL_FALSE, id.m);
        if (m_3D.colorLoc >= 0)
        {
            glUniform4f(m_3D.colorLoc,
                        m_3D.m_CurrentTriColor.r / 255.f,
                        m_3D.m_CurrentTriColor.g / 255.f,
                        m_3D.m_CurrentTriColor.b / 255.f,
                        m_3D.m_CurrentTriColor.a / 255.f);
        }

        glBindTexture(GL_TEXTURE_2D, m_3D.m_WhiteTexture);
        glBindVertexArray(m_3D.m_TriVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_3D.triVBO);

        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(m_3D.m_vTriVertices.size() * sizeof(Vertex3D)),
                     m_3D.m_vTriVertices.data(),
                     GL_DYNAMIC_DRAW);

        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_3D.m_vTriVertices.size());
        RecordDebugRenderDraw(m_3D.m_vTriVertices.size());

        glBindVertexArray(0);
        m_3D.m_vTriVertices.clear();
    }

    void QuarkGLRenderer::DrawTriangle3DImpl(Vertex3D v1, Vertex3D v2, Vertex3D v3, Color color)
    {
        if (!m_3D.m_vTriVertices.empty() &&
            (color.r != m_3D.m_CurrentTriColor.r ||
             color.g != m_3D.m_CurrentTriColor.g ||
             color.b != m_3D.m_CurrentTriColor.b ||
             color.a != m_3D.m_CurrentTriColor.a))
        {
            FlushTriangles3D();
        }

        m_3D.m_CurrentTriColor = color;

        if (m_3D.colorLoc >= 0)
            glUniform4f(
                m_3D.colorLoc,
                color.r / 255.f,
                color.g / 255.f,
                color.b / 255.f,
                color.a / 255.f);

        m_3D.m_vTriVertices.push_back({TransformPoint(m_CurrentMatrix, v1.position),
                                    v1.normal,
                                    v1.texCoord});

        m_3D.m_vTriVertices.push_back({TransformPoint(m_CurrentMatrix, v2.position),
                                    v2.normal,
                                    v2.texCoord});

        m_3D.m_vTriVertices.push_back({TransformPoint(m_CurrentMatrix, v3.position),
                                    v3.normal,
                                    v3.texCoord});
    }

    void QuarkGLRenderer::DrawLine3D(Vec3 s, Vec3 e, Color color)
    {
        if (!m_3D.m_vLineVertices.empty() &&
            (color.r != m_3D.m_CurrentLineColor.r ||
             color.g != m_3D.m_CurrentLineColor.g ||
             color.b != m_3D.m_CurrentLineColor.b ||
             color.a != m_3D.m_CurrentLineColor.a))
        {
            FlushLines3D();
        }

        m_3D.m_CurrentLineColor = color;

        if (m_3D.colorLoc >= 0)
            glUniform4f(
                m_3D.colorLoc,
                color.r / 255.f,
                color.g / 255.f,
                color.b / 255.f,
                color.a / 255.f);

        m_3D.m_vLineVertices.push_back({TransformPoint(m_CurrentMatrix, s),
                                     {0, 1, 0},
                                     {0, 0}});

        m_3D.m_vLineVertices.push_back({TransformPoint(m_CurrentMatrix, e),
                                     {0, 1, 0},
                                     {0, 0}});
    }

    void QuarkGLRenderer::DrawPlane(Vec3 c, Vec2 size, Color color)
    {
        FlushLines3D();
        FlushTriangles3D();

        const Mat4 transform = ApplyCurrentMatrix(
            Mat4::translation(c.x, c.y, c.z) * Mat4::scale(size.x, 1.0f, size.y));
        QuarkGL3D::ApplyDrawState(m_3D, transform, color, m_3D.m_WhiteTexture);

        glBindVertexArray(m_3D.m_PlaneVAO);
        glDrawElements(GL_TRIANGLES, m_3D.m_PlaneIndexCount, GL_UNSIGNED_INT, nullptr);
        RecordDebugRenderDraw(static_cast<std::uint64_t>(m_3D.m_PlaneIndexCount));
        glBindVertexArray(0);
    }

    void QuarkGLRenderer::DrawCube(Vec3 pos, float w, float h, float l, Color color)
    {
        FlushLines3D();
        FlushTriangles3D();

        const Mat4 transform = ApplyCurrentMatrix(
            Mat4::translation(pos.x, pos.y, pos.z) * Mat4::scale(w, h, l));
        QuarkGL3D::ApplyDrawState(m_3D, transform, color, m_3D.m_WhiteTexture);

        glBindVertexArray(m_3D.m_CubeVAO);
        glDrawElements(GL_TRIANGLES, m_3D.m_CubeIndexCount, GL_UNSIGNED_INT, nullptr);
        RecordDebugRenderDraw(static_cast<std::uint64_t>(m_3D.m_CubeIndexCount));
        glBindVertexArray(0);
    }

    void QuarkGLRenderer::DrawCubeV(Vec3 p, Vec3 s, Color c)
    {
        DrawCube(p, s.x, s.y, s.z, c);
    }

    void QuarkGLRenderer::DrawCubeWires(Vec3 pos, float w, float h, float l, Color color)
    {
        float hw = w * 0.5f;
        float hh = h * 0.5f;
        float hl = l * 0.5f;

        Vec3 v[8] = {
            pos + Vec3{-hw, -hh, -hl},
            pos + Vec3{hw, -hh, -hl},
            pos + Vec3{hw, hh, -hl},
            pos + Vec3{-hw, hh, -hl},

            pos + Vec3{-hw, -hh, hl},
            pos + Vec3{hw, -hh, hl},
            pos + Vec3{hw, hh, hl},
            pos + Vec3{-hw, hh, hl}};

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

    void QuarkGLRenderer::DrawCubeWiresV(Vec3 p, Vec3 s, Color c)
    {
        DrawCubeWires(p, s.x, s.y, s.z, c);
    }

    void QuarkGLRenderer::DrawSphere(Vec3 pos, float r, Color color)
    {
        FlushLines3D();
        FlushTriangles3D();

        const Mat4 transform = ApplyCurrentMatrix(
            Mat4::translation(pos.x, pos.y, pos.z) * Mat4::scale(r, r, r));
        QuarkGL3D::ApplyDrawState(m_3D, transform, color, m_3D.m_WhiteTexture);

        glBindVertexArray(m_3D.m_SphereVAO);
        glDrawElements(GL_TRIANGLES, m_3D.m_SphereIndexCount, GL_UNSIGNED_INT, nullptr);
        RecordDebugRenderDraw(static_cast<std::uint64_t>(m_3D.m_SphereIndexCount));
        glBindVertexArray(0);
    }

    void QuarkGLRenderer::DrawSphereEx(Vec3 c, float r, int rings, int slices, Color color)
    {
        for (int ri = 0; ri < rings; ++ri)
        {
            for (int si = 0; si < slices; ++si)
            {
                float phi1 = PI * ri / rings;
                float phi2 = PI * (ri + 1) / rings;

                float theta1 = 2.0f * PI * si / slices;
                float theta2 = 2.0f * PI * (si + 1) / slices;

                Vec3 a = {
                    r * sinf(phi1) * cosf(theta1),
                    r * cosf(phi1),
                    r * sinf(phi1) * sinf(theta1)};

                Vec3 b = {
                    r * sinf(phi1) * cosf(theta2),
                    r * cosf(phi1),
                    r * sinf(phi1) * sinf(theta2)};

                Vec3 d = {
                    r * sinf(phi2) * cosf(theta1),
                    r * cosf(phi2),
                    r * sinf(phi2) * sinf(theta1)};

                Vec3 e = {
                    r * sinf(phi2) * cosf(theta2),
                    r * cosf(phi2),
                    r * sinf(phi2) * sinf(theta2)};

                DrawTriangle3DImpl(
                    {c + a, a.normalized(), {0, 0}},
                    {c + b, b.normalized(), {0, 0}},
                    {c + e, e.normalized(), {0, 0}},
                    color);

                DrawTriangle3DImpl(
                    {c + a, a.normalized(), {0, 0}},
                    {c + e, e.normalized(), {0, 0}},
                    {c + d, d.normalized(), {0, 0}},
                    color);
            }
        }
    }

    void QuarkGLRenderer::DrawSphereWires(Vec3 c, float r, int rings, int slices, Color color)
    {
        for (int ri = 0; ri <= rings; ++ri)
        {
            float phi = PI * ri / rings;

            for (int si = 0; si < slices; ++si)
            {
                float t1 = 2 * PI * si / slices, t2 = 2 * PI * (si + 1) / slices;
                DrawLine3D(c + Vec3{r * sinf(phi) * cosf(t1), r * cosf(phi), r * sinf(phi) * sinf(t1)},
                           c + Vec3{r * sinf(phi) * cosf(t2), r * cosf(phi), r * sinf(phi) * sinf(t2)}, color);
            }
        }
        for (int si = 0; si < slices; ++si)
        {
            float th = 2 * PI * si / slices;

            for (int ri = 0; ri < rings; ++ri)
            {
                float p1 = PI * ri / rings, p2 = PI * (ri + 1) / rings;
                DrawLine3D(c + Vec3{r * sinf(p1) * cosf(th), r * cosf(p1), r * sinf(p1) * sinf(th)},
                           c + Vec3{r * sinf(p2) * cosf(th), r * cosf(p2), r * sinf(p2) * sinf(th)}, color);
            }
        }
    }

    void QuarkGLRenderer::DrawCylinder(Vec3 pos, float rTop, float rBot, float h, int sl, Color color)
    {
        DrawCylinderEx(pos + Vec3{0, -h / 2, 0}, pos + Vec3{0, h / 2, 0}, rBot, rTop, sl, color);
    }

    void QuarkGLRenderer::DrawCylinderEx(Vec3 s, Vec3 e, float rs, float re, int sides, Color color)
    {
        if (sides < 3)
            return;

        Vec3 dir = e - s;
        float len = dir.length();
        if (len < 1e-6f)
            return;
        dir = dir * (1 / len);

        Vec3 up{0, 1, 0};
        if (fabsf(dir.dot(up)) > 0.99f)
            up = {1, 0, 0};
        Vec3 xd = dir.cross(up).normalized(), yd = dir.cross(xd).normalized();

        for (int i = 0; i < sides; ++i)
        {
            float a1 = 2 * PI * i / sides, a2 = 2 * PI * (i + 1) / sides;
            Vec3 p1 = s + xd * cosf(a1) * rs + yd * sinf(a1) * rs, p2 = s + xd * cosf(a2) * rs + yd * sinf(a2) * rs;
            Vec3 p3 = e + xd * cosf(a2) * re + yd * sinf(a2) * re, p4 = e + xd * cosf(a1) * re + yd * sinf(a1) * re;

            DrawTriangle3DImpl({p1, (p1 - s).normalized(), {0, 0}}, {p2, (p2 - s).normalized(), {0, 0}}, {p3, (p3 - e).normalized(), {0, 0}}, color);
            DrawTriangle3DImpl({p1, (p1 - s).normalized(), {0, 0}}, {p3, (p3 - e).normalized(), {0, 0}}, {p4, (p4 - e).normalized(), {0, 0}}, color);
            DrawTriangle3DImpl({s, dir * -1, {0, 0}}, {p2, dir * -1, {0, 0}}, {p1, dir * -1, {0, 0}}, color);
            DrawTriangle3DImpl({e, dir, {0, 0}}, {p3, dir, {0, 0}}, {p4, dir, {0, 0}}, color);
        }
    }

    void QuarkGLRenderer::DrawCylinderWires(Vec3 pos, float rTop, float rBot, float h, int sl, Color color)
    {
        DrawCylinderWiresEx(pos + Vec3{0, -h / 2, 0}, pos + Vec3{0, h / 2, 0}, rBot, rTop, sl, color);
    }

    void QuarkGLRenderer::DrawCylinderWiresEx(Vec3 s, Vec3 e, float rs, float re, int sl, Color color)
    {
        if (sl < 3)
            return;

        Vec3 dir = e - s;
        float len = dir.length();
        if (len < 1e-6f)
            return;
        dir = dir * (1 / len);

        Vec3 up{0, 1, 0};
        if (fabsf(dir.dot(up)) > 0.99f)
            up = {1, 0, 0};

        Vec3 xd = dir.cross(up).normalized(), yd = dir.cross(xd).normalized();

        for (int i = 0; i < sl; ++i)
        {
            float a1 = 2 * PI * i / sl, a2 = 2 * PI * (i + 1) / sl;
            Vec3 p1 = s + xd * cosf(a1) * rs + yd * sinf(a1) * rs, p2 = s + xd * cosf(a2) * rs + yd * sinf(a2) * rs;
            Vec3 p3 = e + xd * cosf(a1) * re + yd * sinf(a1) * re, p4 = e + xd * cosf(a2) * re + yd * sinf(a2) * re;

            DrawLine3D(p1, p2, color);
            DrawLine3D(p3, p4, color);
            DrawLine3D(p1, p3, color);
        }
    }

    void QuarkGLRenderer::DrawGrid(int slices, float spacing, Color color)
    {
        float half = (float)slices * spacing / 2;
        for (int i = 0; i <= slices; ++i)
        {
            float f = -half + (float)i * spacing;

            DrawLine3D({f, 0, -half}, {f, 0, half}, color);
            DrawLine3D({-half, 0, f}, {half, 0, f}, color);
        }
    }

    static void ensure_mesh_tangents(Mesh& mesh);

    Model QuarkGLRenderer::LoadModel(const char* pFilePath)
    {
        TraceLog(LogLevel::Info, "MODEL", TextFormat("[OpenGL] Loading 3D model: %s", pFilePath ? pFilePath : "<null>"));
        Assimp::Importer importer;
        const aiScene* pScene = nullptr;
        try
        {
            pScene = importer.ReadFile(pFilePath, aiProcess_Triangulate | aiProcess_GenNormals |
                                                    aiProcess_FlipUVs | aiProcess_PopulateArmatureData);
        }
        catch (const std::exception& error)
        {
            TraceLog(LogLevel::Error, "MODEL", TextFormat("[OpenGL] Exception while loading model %s: %s", pFilePath ? pFilePath : "<null>", error.what()));
            return Model{};
        }

        if (!pScene || pScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !pScene->mRootNode)
        {
            TraceLog(LogLevel::Error, "MODEL", TextFormat("[OpenGL] Failed to load model %s: %s", pFilePath ? pFilePath : "<null>", importer.GetErrorString()));
            return Model{};
        }

        TraceLog(LogLevel::Trace, "MODEL", TextFormat("[OpenGL] Assimp scene parsed: %u meshes, %u materials, %u textures, %u animations", pScene->mNumMeshes, pScene->mNumMaterials, pScene->mNumTextures, pScene->mNumAnimations));

        Model model{};
        model.meshCount = pScene->mNumMeshes;
        model.meshes = new Mesh[model.meshCount];
        model.materialCount = pScene->mNumMaterials;
        model.materials = new Material[model.materialCount];
        model.meshMaterial = new int[model.meshCount];

        for (unsigned int i = 0; i < pScene->mNumMaterials; ++i)
        {
            aiMaterial* pMaterial = pScene->mMaterials[i];
            Material& mat = model.materials[i];
            mat = {};
            mat.maps = new MaterialMap[12];
            mat.maps[MATERIAL_MAP_ALBEDO].color = WHITE;

            aiColor4D baseColor{};
            if (AI_SUCCESS == aiGetMaterialColor(pMaterial, AI_MATKEY_BASE_COLOR, &baseColor) ||
                AI_SUCCESS == aiGetMaterialColor(pMaterial, AI_MATKEY_COLOR_DIFFUSE, &baseColor))
            {
                mat.maps[MATERIAL_MAP_ALBEDO].color = Color{
                    static_cast<unsigned char>(std::clamp(baseColor.r * 255.0f, 0.0f, 255.0f)),
                    static_cast<unsigned char>(std::clamp(baseColor.g * 255.0f, 0.0f, 255.0f)),
                    static_cast<unsigned char>(std::clamp(baseColor.b * 255.0f, 0.0f, 255.0f)),
                    static_cast<unsigned char>(std::clamp(baseColor.a * 255.0f, 0.0f, 255.0f))};
            }

            aiString path;
            std::string materialDirectory = pFilePath ? pFilePath : "";
            const size_t lastSlash = materialDirectory.find_last_of("/\\");
            if (lastSlash != std::string::npos)
            {
                materialDirectory = materialDirectory.substr(0, lastSlash + 1);
            }
            else
            {
                materialDirectory = "";
            }

            const std::array<std::pair<int, aiTextureType>, 7> textureTypes = {{{MATERIAL_MAP_ALBEDO, aiTextureType_BASE_COLOR},
                                                                                {MATERIAL_MAP_ALBEDO, aiTextureType_DIFFUSE},
                                                                                {MATERIAL_MAP_METALNESS, aiTextureType_METALNESS},
                                                                                {MATERIAL_MAP_NORMAL, aiTextureType_NORMALS},
                                                                                {MATERIAL_MAP_ROUGHNESS, aiTextureType_DIFFUSE_ROUGHNESS},
                                                                                {MATERIAL_MAP_OCCLUSION, aiTextureType_AMBIENT_OCCLUSION},
                                                                                {MATERIAL_MAP_EMISSION, aiTextureType_EMISSION_COLOR}}};
            for (const auto& [mapIndex, textureType] : textureTypes)
            {
                if (mat.maps[mapIndex].texture.valid)
                    continue;
                if (AI_SUCCESS != pMaterial->GetTexture(textureType, 0, &path))
                    continue;

                const std::string textureReference = path.C_Str();
                ITexture loadedTex{};
                std::string texturePath = materialDirectory + textureReference;
                if (!textureReference.empty() && textureReference[0] == '*')
                {
                    Image embeddedImage{};
                    std::vector<unsigned char> vRawPixels;
                    bool ownsImage = false;
                    if (DecodeEmbeddedAssimpTexture(*pScene, path, embeddedImage, vRawPixels, ownsImage))
                    {
                        loadedTex = this->LoadTextureFromImage(embeddedImage);
                        ReleaseEmbeddedAssimpTexture(embeddedImage, ownsImage);
                    }
                    else
                    {
                        TraceLog(LogLevel::Error, "MODEL",
                                 TextFormat("[OpenGL] Failed to decode embedded texture: %s",
                                            textureReference.c_str()));
                    }
                    texturePath = std::string("embedded ") + textureReference;
                }
                else
                {
                    loadedTex = this->LoadTexture(texturePath.c_str());
                }

                TraceLog(LogLevel::Trace, "MODEL",
                         TextFormat("[OpenGL] Model material #%u loading map %d texture: %s",
                                    i, mapIndex, texturePath.c_str()));
                mat.maps[mapIndex].texture.id = loadedTex.m_Id;
                mat.maps[mapIndex].texture.width = loadedTex.m_Width;
                mat.maps[mapIndex].texture.height = loadedTex.m_Height;
                mat.maps[mapIndex].texture.mipmaps = loadedTex.m_Mipmaps;
                mat.maps[mapIndex].texture.format = loadedTex.m_Format;
                mat.maps[mapIndex].texture.valid = loadedTex.m_Valid;
            }
        }

        qcPopulateModelSkeleton(pScene, model);

        int totalVertices = 0;
        int totalTriangles = 0;

        for (unsigned int i = 0; i < pScene->mNumMeshes; ++i)
        {
            aiMesh* pMesh = pScene->mMeshes[i];
            Mesh& qMesh = model.meshes[i];
            qMesh = {};

            std::vector<float> vVertices;
            std::vector<unsigned short> vIndices;

            for (unsigned int j = 0; j < pMesh->mNumVertices; ++j)
            {
                vVertices.push_back(pMesh->mVertices[j].x);
                vVertices.push_back(pMesh->mVertices[j].y);
                vVertices.push_back(pMesh->mVertices[j].z);

                if (pMesh->HasNormals())
                {
                    vVertices.push_back(pMesh->mNormals[j].x);
                    vVertices.push_back(pMesh->mNormals[j].y);
                    vVertices.push_back(pMesh->mNormals[j].z);
                }
                else
                {
                    vVertices.push_back(0.0f);
                    vVertices.push_back(0.0f);
                    vVertices.push_back(0.0f);
                }

                if (pMesh->HasTextureCoords(0))
                {
                    vVertices.push_back(pMesh->mTextureCoords[0][j].x);
                    vVertices.push_back(pMesh->mTextureCoords[0][j].y);
                }
                else
                {
                    vVertices.push_back(0.0f);
                    vVertices.push_back(0.0f);
                }
            }

            for (unsigned int j = 0; j < pMesh->mNumFaces; ++j)
            {
                aiFace face = pMesh->mFaces[j];
                for (unsigned int k = 0; k < face.mNumIndices; ++k)
                {
                    vIndices.push_back(static_cast<unsigned short>(face.mIndices[k]));
                }
            }

            qMesh.vertexCount = pMesh->mNumVertices;
            qMesh.triangleCount = pMesh->mNumFaces;
            totalVertices += qMesh.vertexCount;
            totalTriangles += qMesh.triangleCount;

            qMesh.vertices = new float[qMesh.vertexCount * 3];
            qMesh.normals = new float[qMesh.vertexCount * 3];
            qMesh.texcoords = new float[qMesh.vertexCount * 2];
            qMesh.indices = new unsigned short[qMesh.triangleCount * 3];

            qMesh.boneIndices = new unsigned char[static_cast<size_t>(qMesh.vertexCount) * 4u]{};
            qMesh.boneWeights = new float[static_cast<size_t>(qMesh.vertexCount) * 4u]{};
            for (unsigned int boneIndex = 0; boneIndex < pMesh->mNumBones; ++boneIndex)
            {
                const aiBone* pBone = pMesh->mBones[boneIndex];
                const int skeletonBoneIndex = qcFindSkeletonBoneIndex(pScene, model, pBone);
                if (skeletonBoneIndex < 0 || skeletonBoneIndex > 255)
                {
                    TraceLog(LogLevel::Warn, "MODEL",
                             TextFormat("[OpenGL] Mesh bone '%s' is not present in the model skeleton or exceeds the 8-bit bone index range",
                                        pBone->mName.C_Str()));
                    continue;
                }
                qcSetMeshBoneOffset(qMesh, static_cast<unsigned int>(skeletonBoneIndex),
                                    model.skeleton.boneCount, pBone->mOffsetMatrix);
                for (unsigned int weightIndex = 0; weightIndex < pBone->mNumWeights; ++weightIndex)
                {
                    const aiVertexWeight& weight = pBone->mWeights[weightIndex];
                    const unsigned int vertexIndex = weight.mVertexId;
                    float* pDstWeights = qMesh.boneWeights + static_cast<size_t>(vertexIndex) * 4u;
                    unsigned char* pDstBones = qMesh.boneIndices + static_cast<size_t>(vertexIndex) * 4u;
                    for (int slot = 0; slot < 4; ++slot)
                    {
                        if (pDstWeights[slot] <= 0.0f)
                        {
                            pDstBones[slot] = static_cast<unsigned char>(skeletonBoneIndex);
                            pDstWeights[slot] = weight.mWeight;
                            break;
                        }
                    }
                }
            }

            for (int vertexIndex = 0; vertexIndex < qMesh.vertexCount; ++vertexIndex)
            {
                const int packedBase = vertexIndex * 8;
                qMesh.vertices[vertexIndex * 3 + 0] = vVertices[packedBase + 0];
                qMesh.vertices[vertexIndex * 3 + 1] = vVertices[packedBase + 1];
                qMesh.vertices[vertexIndex * 3 + 2] = vVertices[packedBase + 2];
                qMesh.normals[vertexIndex * 3 + 0] = vVertices[packedBase + 3];
                qMesh.normals[vertexIndex * 3 + 1] = vVertices[packedBase + 4];
                qMesh.normals[vertexIndex * 3 + 2] = vVertices[packedBase + 5];
                qMesh.texcoords[vertexIndex * 2 + 0] = vVertices[packedBase + 6];
                qMesh.texcoords[vertexIndex * 2 + 1] = vVertices[packedBase + 7];
            }

            std::copy(vIndices.begin(), vIndices.end(), qMesh.indices);

            ensure_mesh_tangents(qMesh);

            vVertices.clear();
            vVertices.reserve(static_cast<size_t>(qMesh.vertexCount) * 11u);
            for (int vertexIndex = 0; vertexIndex < qMesh.vertexCount; ++vertexIndex)
            {
                vVertices.push_back(qMesh.vertices[vertexIndex * 3 + 0]);
                vVertices.push_back(qMesh.vertices[vertexIndex * 3 + 1]);
                vVertices.push_back(qMesh.vertices[vertexIndex * 3 + 2]);
                vVertices.push_back(qMesh.normals[vertexIndex * 3 + 0]);
                vVertices.push_back(qMesh.normals[vertexIndex * 3 + 1]);
                vVertices.push_back(qMesh.normals[vertexIndex * 3 + 2]);
                vVertices.push_back(qMesh.texcoords[vertexIndex * 2 + 0]);
                vVertices.push_back(qMesh.texcoords[vertexIndex * 2 + 1]);
                vVertices.push_back(qMesh.tangents[vertexIndex * 3 + 0]);
                vVertices.push_back(qMesh.tangents[vertexIndex * 3 + 1]);
                vVertices.push_back(qMesh.tangents[vertexIndex * 3 + 2]);
            }

            glGenVertexArrays(1, &qMesh.vaoId);
            glGenBuffers(1, &qMesh.vboId);
            glGenBuffers(1, &qMesh.eboId);

            glBindVertexArray(qMesh.vaoId);

            glBindBuffer(GL_ARRAY_BUFFER, qMesh.vboId);
            glBufferData(GL_ARRAY_BUFFER, vVertices.size() * sizeof(float), vVertices.data(), GL_STATIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, qMesh.eboId);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, vIndices.size() * sizeof(unsigned short), vIndices.data(), GL_STATIC_DRAW);

            // Position
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)0);
            // Normal
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(3 * sizeof(float)));
            // TexCoords
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(6 * sizeof(float)));
            // Tangent
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), (void*)(8 * sizeof(float)));

            glBindVertexArray(0);

            model.meshMaterial[i] = pMesh->mMaterialIndex;

            const char* pMeshName = pMesh->mName.length > 0 ? pMesh->mName.C_Str() : "unnamed";
            TraceLog(LogLevel::Trace, "MODEL", TextFormat("[OpenGL] Mesh #%u ('%s'): %d vertices, %d triangles, VAO: %u, Material: #%d", i, pMeshName, qMesh.vertexCount, qMesh.triangleCount, qMesh.vaoId, model.meshMaterial[i]));
        }

        TraceLog(LogLevel::Info, "MODEL", TextFormat("[OpenGL] Model loaded successfully: %s (%d meshes, %d materials, %d total vertices, %d total triangles)", pFilePath ? pFilePath : "<null>", model.meshCount, model.materialCount, totalVertices, totalTriangles));
        return model;
    }

    void QuarkGLRenderer::UnloadModel(Model& model)
    {
        for (int i = 0; i < model.meshCount; ++i)
        {
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
            delete[] mesh.boneOffsets;
            delete[] mesh.animVertices;
            delete[] mesh.animNormals;
            delete[] mesh.bindVertices;
            delete[] mesh.bindNormals;
            mesh = {};
        }

        delete[] model.meshes;
        model.meshes = nullptr;

        for (int i = 0; i < model.materialCount; ++i)
        {
            Material& mat = model.materials[i];

            if (mat.maps)
            {
                for (int mapIndex = 0; mapIndex <= MATERIAL_MAP_BRDF; ++mapIndex)
                {
                    const Texture2D& mapTexture = mat.maps[mapIndex].texture;
                    if (!mapTexture.valid)
                    {
                        continue;
                    }

                    ITexture texture{
                        mapTexture.id,
                        mapTexture.width,
                        mapTexture.height,
                        mapTexture.mipmaps,
                        mapTexture.format,
                        mapTexture.valid};
                    this->UnloadTexture(texture);
                }
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

    static void ensure_mesh_tangents(Mesh& mesh)
    {
        if (!mesh.vertices || mesh.vertexCount <= 0)
            return;
        if (!mesh.tangents)
            mesh.tangents = new float[static_cast<size_t>(mesh.vertexCount) * 3u];

        for (int i = 0; i < mesh.vertexCount * 3; ++i)
            mesh.tangents[i] = 0.0f;

        const bool indexed = mesh.indices && mesh.triangleCount > 0;
        const int triangleCount = mesh.triangleCount > 0
                                      ? mesh.triangleCount
                                      : static_cast<int>(mesh.vertexCount / 3);

        for (int pT = 0; pT < triangleCount; ++pT)
        {
            const int ia = indexed ? mesh.indices[pT * 3 + 0] : pT * 3 + 0;
            const int ib = indexed ? mesh.indices[pT * 3 + 1] : pT * 3 + 1;
            const int ic = indexed ? mesh.indices[pT * 3 + 2] : pT * 3 + 2;
            if (ia < 0 || ib < 0 || ic < 0 ||
                ia >= mesh.vertexCount || ib >= mesh.vertexCount || ic >= mesh.vertexCount)
                continue;

            const float* pV0 = mesh.vertices + ia * 3;
            const float* pV1 = mesh.vertices + ib * 3;
            const float* pV2 = mesh.vertices + ic * 3;

            const float e1x = pV1[0] - pV0[0], e1y = pV1[1] - pV0[1], e1z = pV1[2] - pV0[2];
            const float e2x = pV2[0] - pV0[0], e2y = pV2[1] - pV0[1], e2z = pV2[2] - pV0[2];

            float tx = 0.0f, ty = 0.0f, tz = 0.0f;
            if (mesh.texcoords)
            {
                const float u0 = mesh.texcoords[ia * 2 + 0];
                const float vt0 = mesh.texcoords[ia * 2 + 1];
                const float u1 = mesh.texcoords[ib * 2 + 0];
                const float vt1 = mesh.texcoords[ib * 2 + 1];
                const float u2 = mesh.texcoords[ic * 2 + 0];
                const float vt2 = mesh.texcoords[ic * 2 + 1];

                const float du1 = u1 - u0, dv1 = vt1 - vt0;
                const float du2 = u2 - u0, dv2 = vt2 - vt0;
                const float determinant = du1 * dv2 - du2 * dv1;
                if (fabsf(determinant) > 1e-8f)
                {
                    const float r = 1.0f / determinant;
                    tx = (e1x * dv2 - e2x * dv1) * r;
                    ty = (e1y * dv2 - e2y * dv1) * r;
                    tz = (e1z * dv2 - e2z * dv1) * r;
                }
            }
            if (tx == 0.0f && ty == 0.0f && tz == 0.0f)
                tx = 1.0f;

            const int idx[3] = {ia, ib, ic};
            for (int k = 0; k < 3; ++k)
            {
                float* pDst = mesh.tangents + idx[k] * 3;
                pDst[0] += tx;
                pDst[1] += ty;
                pDst[2] += tz;
            }
        }

        for (int i = 0; i < mesh.vertexCount; ++i)
        {
            float* pT = mesh.tangents + i * 3;
            const float* pN = mesh.normals ? mesh.normals + i * 3 : nullptr;
            if (pN)
            {
                const float nx = pN[0], ny = pN[1], nz = pN[2];
                const float nn = sqrtf(nx * nx + ny * ny + nz * nz);
                if (nn > 1e-9f)
                {
                    const float inv = 1.0f / nn;
                    const float anx = nx * inv, any = ny * inv, anz = nz * inv;
                    const float d = pT[0] * anx + pT[1] * any + pT[2] * anz;
                    pT[0] -= anx * d;
                    pT[1] -= any * d;
                    pT[2] -= anz * d;
                }
            }
            const float total = sqrtf(pT[0] * pT[0] + pT[1] * pT[1] + pT[2] * pT[2]);
            if (total > 1e-8f)
            {
                const float inv = 1.0f / total;
                pT[0] *= inv;
                pT[1] *= inv;
                pT[2] *= inv;
            }
            else
            {
                pT[0] = 1.0f;
                pT[1] = 0.0f;
                pT[2] = 0.0f;
            }
        }
    }

    void QuarkGLRenderer::UploadMesh(Mesh& mesh, bool dynamic)
    {
        if (!mesh.vertices || mesh.vertexCount <= 0)
            return;

        ensure_mesh_tangents(mesh);

        if (mesh.vaoId)
            glDeleteVertexArrays(1, &mesh.vaoId);
        if (mesh.vboId)
            glDeleteBuffers(1, &mesh.vboId);
        if (mesh.eboId)
            glDeleteBuffers(1, &mesh.eboId);

        glGenVertexArrays(1, &mesh.vaoId);
        glGenBuffers(1, &mesh.vboId);

        if (mesh.indices && mesh.triangleCount > 0)
            glGenBuffers(1, &mesh.eboId);

        std::vector<float> vVertexData;
        vVertexData.reserve(mesh.vertexCount * 11);
        for (int i = 0; i < mesh.vertexCount; ++i)
        {
            vVertexData.push_back(mesh.vertices[i * 3 + 0]);
            vVertexData.push_back(mesh.vertices[i * 3 + 1]);
            vVertexData.push_back(mesh.vertices[i * 3 + 2]);

            if (mesh.normals)
            {
                vVertexData.push_back(mesh.normals[i * 3 + 0]);
                vVertexData.push_back(mesh.normals[i * 3 + 1]);
                vVertexData.push_back(mesh.normals[i * 3 + 2]);
            }
            else
            {
                vVertexData.push_back(0.0f);
                vVertexData.push_back(0.0f);
                vVertexData.push_back(0.0f);
            }

            if (mesh.texcoords)
            {
                vVertexData.push_back(mesh.texcoords[i * 2 + 0]);
                vVertexData.push_back(mesh.texcoords[i * 2 + 1]);
            }
            else
            {
                vVertexData.push_back(0.0f);
                vVertexData.push_back(0.0f);
            }

            vVertexData.push_back(mesh.tangents[i * 3 + 0]);
            vVertexData.push_back(mesh.tangents[i * 3 + 1]);
            vVertexData.push_back(mesh.tangents[i * 3 + 2]);
        }

        glBindVertexArray(mesh.vaoId);

        glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizei>(vVertexData.size() * sizeof(float)),
                     vVertexData.data(),
                     dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);

        if (mesh.eboId)
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.eboId);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizei>(mesh.triangleCount * 3 * sizeof(unsigned short)),
                         mesh.indices, dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
        }

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), reinterpret_cast<void*>(0));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 11 * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));

        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 11 * sizeof(float), reinterpret_cast<void*>(8 * sizeof(float)));

        glBindVertexArray(0);
        TraceLog(LogLevel::Trace, "MESH", TextFormat("[OpenGL] Uploaded mesh to GPU (VAO: %u, VBO: %u, EBO: %u, %d vertices, %d triangles, dynamic: %s)", mesh.vaoId, mesh.vboId, mesh.eboId, mesh.vertexCount, mesh.triangleCount, dynamic ? "yes" : "no"));
    }

    void QuarkGLRenderer::UpdateMeshBuffer(Mesh& mesh, int index, const void* pData, int dataSize, int offset)
    {
        if (!pData || dataSize <= 0)
            return;

        if ((index == 0 || index == 1 || index == 2) && mesh.vboId)
        {
            ensure_mesh_tangents(mesh);

            std::vector<float> vVertexData;
            vVertexData.reserve(mesh.vertexCount * 11);
            for (int i = 0; i < mesh.vertexCount; ++i)
            {
                vVertexData.push_back(mesh.vertices ? mesh.vertices[i * 3 + 0] : 0.0f);
                vVertexData.push_back(mesh.vertices ? mesh.vertices[i * 3 + 1] : 0.0f);
                vVertexData.push_back(mesh.vertices ? mesh.vertices[i * 3 + 2] : 0.0f);

                vVertexData.push_back(mesh.normals ? mesh.normals[i * 3 + 0] : 0.0f);
                vVertexData.push_back(mesh.normals ? mesh.normals[i * 3 + 1] : 0.0f);
                vVertexData.push_back(mesh.normals ? mesh.normals[i * 3 + 2] : 0.0f);

                vVertexData.push_back(mesh.texcoords ? mesh.texcoords[i * 2 + 0] : 0.0f);
                vVertexData.push_back(mesh.texcoords ? mesh.texcoords[i * 2 + 1] : 0.0f);

                vVertexData.push_back(mesh.tangents ? mesh.tangents[i * 3 + 0] : 1.0f);
                vVertexData.push_back(mesh.tangents ? mesh.tangents[i * 3 + 1] : 0.0f);
                vVertexData.push_back(mesh.tangents ? mesh.tangents[i * 3 + 2] : 0.0f);
            }

            glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId);
            glBufferSubData(
                GL_ARRAY_BUFFER,
                0,
                static_cast<GLsizei>(vVertexData.size() * sizeof(float)),
                vVertexData.data());
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        else if (index == 6 && mesh.eboId)
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.eboId);
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, offset, dataSize, pData);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        }
    }

    void QuarkGLRenderer::UnloadMesh(Mesh& mesh)
    {
        if (mesh.vaoId)
        {
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
        delete[] mesh.boneOffsets;
        mesh.boneOffsets = nullptr;
        mesh.boneOffsetCount = 0;
        delete[] mesh.animVertices;
        mesh.animVertices = nullptr;
        delete[] mesh.animNormals;
        mesh.animNormals = nullptr;

        mesh.vertexCount = 0;
        mesh.triangleCount = 0;
    }

    void QuarkGLRenderer::BindMaterialMaps(const Material& material, GLuint shaderProgram)
    {
        if (!material.maps)
            return;
        const GLShaderUniformCache& cachedLocs = GetGLShaderUniformCache(shaderProgram);

        for (int shadowIndex = 0; shadowIndex < 4; ++shadowIndex)
        {
            const GLint shadowMapLoc = cachedLocs.m_aShadowMapLocs[shadowIndex];
            if (shadowMapLoc < 0)
                continue;

            const int mapIndex = MATERIAL_MAP_HEIGHT + shadowIndex;
            glActiveTexture(GL_TEXTURE1 + shadowIndex);
            glBindTexture(GL_TEXTURE_2D, material.maps[mapIndex].texture.valid
                                             ? material.maps[mapIndex].texture.id
                                             : m_3D.m_WhiteTexture);
            glUniform1i(shadowMapLoc, 1 + shadowIndex);
        }

        const struct
        {
            int slot;
            int mapIndex;
            GLuint fallback;
        } pbrMaps[5] = {
            {5, MATERIAL_MAP_METALNESS, m_3D.m_WhiteTexture},
            {6, MATERIAL_MAP_NORMAL, m_3D.m_FlatNormalTexture},
            {7, MATERIAL_MAP_ROUGHNESS, m_3D.m_WhiteTexture},
            {8, MATERIAL_MAP_OCCLUSION, m_3D.m_WhiteTexture},
            {9, MATERIAL_MAP_EMISSION, m_3D.m_BlackTexture},
        };
        for (int i = 0; i < 5; ++i)
        {
            const GLint location = cachedLocs.m_aPbrMapLocs[i];
            if (location < 0)
                continue;

            const MaterialMap& map = material.maps[pbrMaps[i].mapIndex];
            glActiveTexture(GL_TEXTURE0 + pbrMaps[i].slot);
            glBindTexture(GL_TEXTURE_2D, map.texture.valid ? map.texture.id : pbrMaps[i].fallback);
            glUniform1i(location, pbrMaps[i].slot);
        }

        glActiveTexture(GL_TEXTURE0);
    }

    void QuarkGLRenderer::DrawMesh(const Mesh& mesh, const Material& material, const Mat4& transform)
    {
        if (!mesh.vaoId)
            return;
        if (!m_3D.m_Initialized)
            Init3DState();

        GLuint texId = m_3D.m_WhiteTexture;
        const bool hasTexture = material.maps && material.maps[MATERIAL_MAP_ALBEDO].texture.valid;
        const Color materialColor = material.maps
                                        ? material.maps[MATERIAL_MAP_ALBEDO].color
                                        : WHITE;
        if (hasTexture)
        {
            texId = material.maps[MATERIAL_MAP_ALBEDO].texture.id;
        }

        const Shader* pCustomShader = ResolveMaterialShader(&material);
        if (pCustomShader)
        {
            glUseProgram(pCustomShader->id);
            ApplyMaterialShaderUniforms(*pCustomShader, transform, m_3D.m_ViewMatrix, m_3D.m_ProjectionMatrix, materialColor, hasTexture);
        }
        else
        {
            QuarkGL3D::ApplyDrawState(m_3D, transform, materialColor, texId);
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texId);
        if (pCustomShader && material.maps)
        {
            BindMaterialMaps(material, pCustomShader->id);
        }
        glBindVertexArray(mesh.vaoId);

        if (mesh.eboId)
        {
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.triangleCount * 3), GL_UNSIGNED_SHORT, nullptr);
            RecordDebugRenderDraw(static_cast<std::uint64_t>(mesh.triangleCount) * 3);
        }
        else
        {
            glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
            RecordDebugRenderDraw(static_cast<std::uint64_t>(mesh.vertexCount));
        }

        glBindVertexArray(0);
    }

    void QuarkGLRenderer::DrawMeshInstanced(const Mesh& mesh, const Material& material, const Mat4* pTransforms, int instances)
    {
        if (!pTransforms || instances <= 0)
            return;
        if (instances == 1)
        {
            DrawMesh(mesh, material, pTransforms[0]);
            return;
        }
        if (!mesh.vaoId)
            return;
        if (!m_3D.m_Initialized)
            Init3DState();

        GLuint texId = m_3D.m_WhiteTexture;
        const bool hasTexture = material.maps && material.maps[MATERIAL_MAP_ALBEDO].texture.valid;
        const Color materialColor = material.maps
                                        ? material.maps[MATERIAL_MAP_ALBEDO].color
                                        : WHITE;
        if (hasTexture)
        {
            texId = material.maps[MATERIAL_MAP_ALBEDO].texture.id;
        }

        const Shader* pCustomShader = ResolveMaterialShader(&material);
        if (pCustomShader)
        {
            glUseProgram(pCustomShader->id);
            const GLint useInstancingLoc = GetGLShaderUniformCache(pCustomShader->id).m_UseInstancingLoc;
            if (useInstancingLoc >= 0)
            {
                ApplyMaterialShaderUniforms(*pCustomShader, pTransforms[0], m_3D.m_ViewMatrix, m_3D.m_ProjectionMatrix, materialColor, hasTexture);
                glUniform1i(useInstancingLoc, 1);

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, texId);
                if (material.maps)
                {
                    BindMaterialMaps(material, pCustomShader->id);
                }

                if (m_3D.m_InstanceVBO == 0)
                {
                    glGenBuffers(1, &m_3D.m_InstanceVBO);
                }
                const size_t requiredBytes = static_cast<size_t>(instances) * sizeof(Mat4);
                glBindBuffer(GL_ARRAY_BUFFER, m_3D.m_InstanceVBO);
                if (requiredBytes > m_3D.m_InstanceVBOCapacity)
                {
                    m_3D.m_InstanceVBOCapacity = requiredBytes * 2;
                    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_3D.m_InstanceVBOCapacity), nullptr, GL_STREAM_DRAW);
                }
                glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(requiredBytes), pTransforms);

                glBindVertexArray(mesh.vaoId);
                for (GLuint col = 0; col < 4; ++col)
                {
                    const GLuint loc = 4u + col;
                    glEnableVertexAttribArray(loc);
                    glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, sizeof(Mat4),
                                          reinterpret_cast<const void*>(sizeof(float) * 4u * col));
                    glVertexAttribDivisor(loc, 1);
                }

                if (mesh.eboId)
                {
                    glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(mesh.triangleCount * 3),
                                            GL_UNSIGNED_SHORT, nullptr, static_cast<GLsizei>(instances));
                    RecordDebugRenderDraw(static_cast<std::uint64_t>(mesh.triangleCount) * 3u * static_cast<std::uint64_t>(instances));
                }
                else
                {
                    glDrawArraysInstanced(GL_TRIANGLES, 0, mesh.vertexCount, static_cast<GLsizei>(instances));
                    RecordDebugRenderDraw(static_cast<std::uint64_t>(mesh.vertexCount) * static_cast<std::uint64_t>(instances));
                }

                for (GLuint col = 0; col < 4; ++col)
                {
                    const GLuint loc = 4u + col;
                    glVertexAttribDivisor(loc, 0);
                    glDisableVertexAttribArray(loc);
                }
                glBindVertexArray(0);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glUniform1i(useInstancingLoc, 0);
                return;
            }

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texId);
            if (material.maps)
            {
                BindMaterialMaps(material, pCustomShader->id);
            }
            glBindVertexArray(mesh.vaoId);
            for (int i = 0; i < instances; ++i)
            {
                ApplyMaterialShaderUniforms(*pCustomShader, pTransforms[i], m_3D.m_ViewMatrix, m_3D.m_ProjectionMatrix, materialColor, hasTexture);
                if (mesh.eboId)
                {
                    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.triangleCount * 3), GL_UNSIGNED_SHORT, nullptr);
                    RecordDebugRenderDraw(static_cast<std::uint64_t>(mesh.triangleCount) * 3u);
                }
                else
                {
                    glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
                    RecordDebugRenderDraw(static_cast<std::uint64_t>(mesh.vertexCount));
                }
            }
            glBindVertexArray(0);
            return;
        }

        for (int i = 0; i < instances; ++i)
        {
            DrawMesh(mesh, material, pTransforms[i]);
        }
    }

    void QuarkGLRenderer::DrawModel(const Model& model, const Vec3& pos, float scale,
                                    float rx, float ry, float rz)
    {
        Mat4 t = Mat4::translation(pos.x, pos.y, pos.z) * Mat4::rotationY(ry) * Mat4::rotationX(rx) * Mat4::rotationZ(rz) * Mat4::scale(scale, scale, scale);

        DrawModelEx(model, t);
    }

    void QuarkGLRenderer::DrawModelEx(const Model& model, const Mat4& transform)
    {
        if (!m_3D.m_Initialized)
            Init3DState();
        Mat4 final = ApplyCurrentMatrix(transform * model.transform);

        for (int i = 0; i < model.meshCount; ++i)
        {
            const Mesh& mesh = model.meshes[i];

            glActiveTexture(GL_TEXTURE0);
            GLuint texId = m_3D.m_WhiteTexture;
            const Material* pMaterial = nullptr;
            const Shader* pCustomShader = nullptr;
            bool hasTexture = false;

            if (model.meshMaterial && model.meshMaterial[i] >= 0 && model.meshMaterial[i] < model.materialCount)
            {
                pMaterial = &model.materials[model.meshMaterial[i]];
                pCustomShader = ResolveMaterialShader(pMaterial);
                hasTexture = pMaterial->maps && pMaterial->maps[MATERIAL_MAP_ALBEDO].texture.valid;
                if (hasTexture)
                    texId = pMaterial->maps[MATERIAL_MAP_ALBEDO].texture.id;
            }
            const Color materialColor = pMaterial && pMaterial->maps
                                            ? pMaterial->maps[MATERIAL_MAP_ALBEDO].color
                                            : WHITE;

            if (pCustomShader)
            {
                glUseProgram(pCustomShader->id);
                ApplyMaterialShaderUniforms(*pCustomShader, final, m_3D.m_ViewMatrix, m_3D.m_ProjectionMatrix,
                                            materialColor, hasTexture);
            }
            else
            {
                glUseProgram(m_3D.m_Shader3D);
                if (m_3D.m_ModelLoc >= 0)
                    glUniformMatrix4fv(m_3D.m_ModelLoc, 1, GL_FALSE, final.m);
                if (m_3D.viewLoc >= 0)
                    glUniformMatrix4fv(m_3D.viewLoc, 1, GL_FALSE, m_3D.m_ViewMatrix.m);
                if (m_3D.projLoc >= 0)
                    glUniformMatrix4fv(m_3D.projLoc, 1, GL_FALSE, m_3D.m_ProjectionMatrix.m);
                if (m_3D.colorLoc >= 0)
                    glUniform4f(m_3D.colorLoc,
                                materialColor.r / 255.0f, materialColor.g / 255.0f,
                                materialColor.b / 255.0f, materialColor.a / 255.0f);
                if (m_3D.m_SamplerLoc >= 0)
                    glUniform1i(m_3D.m_SamplerLoc, 0);
            }

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBindSampler(0, 0);
            glBindTexture(GL_TEXTURE_2D, texId);
            if (pCustomShader && pMaterial && pMaterial->maps)
            {
                BindMaterialMaps(*pMaterial, pCustomShader->id);
            }
            glBindVertexArray(mesh.vaoId);

            if (mesh.eboId)
            {
                glDrawElements(GL_TRIANGLES, (GLsizei)(mesh.triangleCount * 3), GL_UNSIGNED_SHORT, nullptr);
                RecordDebugRenderDraw(static_cast<std::uint64_t>(mesh.triangleCount) * 3);
            }
            else
            {
                glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
                RecordDebugRenderDraw(static_cast<std::uint64_t>(mesh.vertexCount));
            }
            glBindVertexArray(0);
        }
    }

    void QuarkGLRenderer::DrawModelEx(const Model& model, const Mat4& transform, Color tint)
    {
        if (!m_3D.m_Initialized)
            Init3DState();
        Mat4 final = ApplyCurrentMatrix(transform * model.transform);

        for (int i = 0; i < model.meshCount; ++i)
        {
            const Mesh& mesh = model.meshes[i];

            glActiveTexture(GL_TEXTURE0);

            GLuint texId = m_3D.m_WhiteTexture;
            const Material* pMaterial = nullptr;
            const Shader* pCustomShader = nullptr;
            bool hasTexture = false;
            if (model.meshMaterial && model.meshMaterial[i] >= 0 && model.meshMaterial[i] < model.materialCount)
            {
                pMaterial = &model.materials[model.meshMaterial[i]];
                pCustomShader = ResolveMaterialShader(pMaterial);
                hasTexture = pMaterial->maps && pMaterial->maps[MATERIAL_MAP_ALBEDO].texture.valid;
                if (hasTexture)
                    texId = pMaterial->maps[MATERIAL_MAP_ALBEDO].texture.id;
            }
            const Color materialColor = pMaterial && pMaterial->maps
                                            ? pMaterial->maps[MATERIAL_MAP_ALBEDO].color
                                            : WHITE;
            const Color combinedColor{
                static_cast<unsigned char>(materialColor.r * tint.r / 255),
                static_cast<unsigned char>(materialColor.g * tint.g / 255),
                static_cast<unsigned char>(materialColor.b * tint.b / 255),
                static_cast<unsigned char>(materialColor.a * tint.a / 255)};

            if (pCustomShader)
            {
                glUseProgram(pCustomShader->id);
                ApplyMaterialShaderUniforms(*pCustomShader, final, m_3D.m_ViewMatrix, m_3D.m_ProjectionMatrix,
                                            combinedColor, hasTexture);
            }
            else
            {
                glUseProgram(m_3D.m_Shader3D);
                if (m_3D.m_ModelLoc >= 0)
                    glUniformMatrix4fv(m_3D.m_ModelLoc, 1, GL_FALSE, final.m);
                if (m_3D.viewLoc >= 0)
                    glUniformMatrix4fv(m_3D.viewLoc, 1, GL_FALSE, m_3D.m_ViewMatrix.m);
                if (m_3D.projLoc >= 0)
                    glUniformMatrix4fv(m_3D.projLoc, 1, GL_FALSE, m_3D.m_ProjectionMatrix.m);
                if (m_3D.m_SamplerLoc >= 0)
                    glUniform1i(m_3D.m_SamplerLoc, 0);
                if (m_3D.colorLoc >= 0)
                    glUniform4f(m_3D.colorLoc,
                                combinedColor.r / 255.0f, combinedColor.g / 255.0f,
                                combinedColor.b / 255.0f, combinedColor.a / 255.0f);
            }

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBindSampler(0, 0);
            glBindTexture(GL_TEXTURE_2D, texId);
            if (pCustomShader && pMaterial && pMaterial->maps)
            {
                BindMaterialMaps(*pMaterial, pCustomShader->id);
            }
            glBindVertexArray(mesh.vaoId);

            if (mesh.eboId)
            {
                glDrawElements(GL_TRIANGLES, (GLsizei)(mesh.triangleCount * 3), GL_UNSIGNED_SHORT, nullptr);
                RecordDebugRenderDraw(static_cast<std::uint64_t>(mesh.triangleCount) * 3);
            }
            else
            {
                glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
                RecordDebugRenderDraw(static_cast<std::uint64_t>(mesh.vertexCount));
            }
            glBindVertexArray(0);
        }
    }

    bool QuarkGLRenderer::UpdateTexture(const ITexture& texture, const void* pPixels)
    {
        if (!pPixels || texture.m_Id == 0)
            return false;
        glBindTexture(GL_TEXTURE_2D, texture.m_Id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texture.m_Width, texture.m_Height, GL_RGBA, GL_UNSIGNED_BYTE, pPixels);
        glBindTexture(GL_TEXTURE_2D, 0);
        return true;
    }

    bool QuarkGLRenderer::UpdateTextureRegion(const ITexture& texture, Rectangle region, const void* pPixels)
    {
        if (!pPixels || texture.m_Id == 0)
            return false;
        glBindTexture(GL_TEXTURE_2D, texture.m_Id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(region.x), static_cast<GLint>(region.y),
                        static_cast<GLsizei>(region.width), static_cast<GLsizei>(region.height),
                        GL_RGBA, GL_UNSIGNED_BYTE, pPixels);
        glBindTexture(GL_TEXTURE_2D, 0);
        return true;
    }
} // qci
