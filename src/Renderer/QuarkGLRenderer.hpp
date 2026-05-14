#pragma once

#include "QuarkIRenderer.hpp"
#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/Quark3D.hpp"
#include "QuarkFont.hpp"

#include <GL/glew.h>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace qc {

class QCAPI QuarkGLRenderer final : public IRenderer {
public:
    QuarkGLRenderer() = default;
    ~QuarkGLRenderer() override;

    void Init(SDL_Window* window, int width, int height);
    void Shutdown();

    void BeginDrawing() override;
    void EndDrawing() override;
    void ClearBackground(Color color) override;

    void DrawRectangle(float x, float y, float width, float height, Color color) override;
    void DrawRectangle(const Rectangle& rectangle, Color color) override;
    void DrawRectangleV(Vec2 position, Vec2 size, Color color) override;
    void DrawRectangleLines(Rectangle rectangle, float lineWidth, Color color) override;
    void DrawRectangleRounded(Rectangle rectangle, float roundness, int segments, Color color) override;
    void DrawCircle(float centerX, float centerY, float radius, Color color) override;
    void DrawCircleLines(float centerX, float centerY, float radius, Color color) override;
    void DrawEllipse(float centerX, float centerY, float radiusH, float radiusV, Color color) override;
    void DrawLine(float x1, float y1, float x2, float y2, Color color) override;
    void DrawLineV(Vec2 start, Vec2 end, Color color) override;
    void DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color) override;
    void DrawPoly(Vec2 center, int sides, float radius, float rotation, Color color) override;

    void Set3DView(const Mat4& view, const Mat4& projection) override;
    void DrawLine3D(Vec3 startPos, Vec3 endPos, Color color) override;
    void DrawPlane(Vec3 center, Vec2 size, Color color) override;
    void DrawCube(Vec3 position, float width, float height, float length, Color color) override;
    void DrawCubeV(Vec3 position, Vec3 size, Color color) override;
    void DrawCubeWires(Vec3 position, float width, float height, float length, Color color) override;
    void DrawCubeWiresV(Vec3 position, Vec3 size, Color color) override;
    void DrawSphere(Vec3 centerPos, float radius, Color color) override;
    void DrawSphereEx(Vec3 centerPos, float radius, int rings, int slices, Color color) override;
    void DrawSphereWires(Vec3 centerPos, float radius, int rings, int slices, Color color) override;
    void DrawCylinder(Vec3 position, float radiusTop, float radiusBottom, float height, int slices, Color color) override;
    void DrawCylinderEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius, int sides, Color color) override;
    void DrawCylinderWires(Vec3 position, float radiusTop, float radiusBottom, float height, int slices, Color color) override;
    void DrawCylinderWiresEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius, int slices, Color color) override;
    void DrawGrid(int slices, float spacing) override;

    void DrawText(const char* text, int x, int y, int fontSize, Color color) override;
    void DrawTextEx(IFont font, const char* text, Vec2 position, float fontSize, float spacing, Color tint) override;
    Vec2 MeasureTextEx(IFont font, const char* text, float fontSize, float spacing) override;
    int  MeasureText(const char* text, int fontSize) override;

    void           DrawTexture(const ITexture& texture, float x, float y, Color tint) override;
    void           DrawTextureV(const ITexture& texture, Vec2 position, Color tint) override;
    void           DrawTextureEx(const ITexture& texture, Vec2 position, float rotation, float scale, Color tint) override;
    void           DrawTextureRec(const ITexture& texture, Rectangle source, Vec2 position, Color tint) override;
    void           DrawTexturePro(ITexture texture, Rectangle source, Rectangle dest, Vec2 origin, float rotation, Color tint) override;
    void           DrawTextureTiled(ITexture texture, float scale, Vec2 offset, Color tint) override;
    void           DrawTextureNPatch(ITexture texture, Rectangle source, Rectangle dest, Vec2 origin, float rotation, Color tint) override;
    ITexture       LoadTexture(const char* filePath) override;
    ITexture       GetRenderTextureTexture(IRenderTexture target) override;
    void           UnloadTexture(ITexture& texture) override;
    bool           isTextureValid(ITexture& texture) override;
    IRenderTexture LoadRenderTexture(int width, int height) override;
    void           UnloadRenderTexture(IRenderTexture target) override;
    bool           isRenderTextureValid(IRenderTexture& target) override;
    ITexture       GenCheckerTexture(int width, int height, int cellSize, Color colorA, Color colorB) override;

    void BeginTextureMode(IRenderTexture target) override;
    void EndTextureMode() override;

    IFont LoadFont(const char* filePath, int fontSize) override;
    void  UnloadFont(IFont& font) override;

    void   BeginShaderMode(const Shader& shader) override;
    void   EndShaderMode() override;
    Shader LoadShader(const char* vsFileName, const char* fsFileName) override;
    Shader LoadShaderFromMemory(const char* vsSource, const char* fsSource) override;
    void   UnloadShader(Shader& shader) override;
    bool   isShaderValid(Shader& shader) override;
    int    GetShaderLocation(const Shader& shader, const char* uniformName) override;
    int    GetShaderLocation(const Shader& shader, ShaderLocationIndex locIndex) override;
    int    GetShaderAttributeLocation(const Shader& shader, const char* attribName) override;
    void   SetShaderValue(const Shader& shader, int locIndex, float value) override;
    void   SetShaderValue(const Shader& shader, int locIndex, int value) override;
    void   SetShaderValue(const Shader& shader, int locIndex, const Color& value) override;
    void   SetShaderValue(const Shader& shader, int locIndex, const qc::Vec2& value) override;
    void   SetShaderValue(const Shader& shader, int locIndex, const qc::Vec3& value) override;
    void   SetShaderValueMatrix(const Shader& shader, int locIndex, const float* mat) override;
    void   SetShaderValueSampler(const Shader& sfhader, int locIndex, int textureUnit) override;

    void BeginMode2D(const Camera2D& camera) override;
    void EndMode2D() override;
    void BeginMode3D(const Camera3D& camera) override;
    void EndMode3D() override;
    Camera2D GetCamera2D() const { return m_camera2D; }

    void         PushMatrix() override;
    void         PopMatrix() override;
    void         Translate(const Vec3& translation) override;
    void         Rotate(float angle, const Vec3& axis) override;
    void         Scale(const Vec3& scale) override;
    void         MultMatrix(const Mat4& matrix) override;
    const float* GetMatrixModelview() override;
    const float* GetMatrixProjection() override;
    void         EnableBackfaceCulling() override;
    void         DisableBackfaceCulling() override;

    Model LoadModel(const char* filePath) override;
    void  UnloadModel(Model& model) override;
    void  DrawModel(const Model& model, const Vec3& position, float scale,
                    float rotationX, float rotationY, float rotationZ) override;
    void  DrawModelEx(const Model& model, const Mat4& transform) override;

    RendererType GetType() const override { return RendererType::OpenGL; }

    int  GetScreenWidth()  const { return m_width; }
    int  GetScreenHeight() const { return m_height; }
    void SetTargetFPS(int fps)   { m_targetFps = fps; }
    float GetFrameTime()   const { return m_frameTime; }
    bool ShouldClose()     const { return m_shouldClose; }
    void SetShouldClose(bool v)  { m_shouldClose = v; }

    void   RefreshViewport();

private:

    struct BatchVertex {
        float x, y, u, v, r, g, b, a;
    };

    struct Vertex3D {
        Vec3 position;
        Vec3 normal;
        Vec2 texCoord;
    };

    struct GlyphData {
        Rectangle uv{};
        float     advanceX = 0.f;
        float     offsetX  = 0.f;
        float     offsetY  = 0.f;
        int       width    = 0;
        int       height   = 0;
    };

    struct FontData {
        GLuint    atlasTexture = 0;
        int       baseSize    = 0;
        int       ascent      = 0;
        int       descent     = 0;
        int       lineHeight  = 0;
        int       lineGap     = 0;
        GlyphData glyphs[95]{};
    };

    struct Model3DState {
        bool   initialized    = false;
        GLuint shader3D       = 0;
        GLint  modelLoc       = -1, viewLoc    = -1, projLoc = -1;
        GLint  samplerLoc     = -1, lightPosLoc = -1, colorLoc = -1;
        GLuint whiteTexture   = 0;

        GLuint planeVAO  = 0, planeVBO  = 0, planeEBO  = 0; int planeIndexCount  = 0;
        GLuint cubeVAO   = 0, cubeVBO   = 0, cubeEBO   = 0; int cubeIndexCount   = 0;
        GLuint sphereVAO = 0, sphereVBO = 0, sphereEBO = 0; int sphereIndexCount = 0;

        GLuint lineVAO = 0, lineVBO = 0;
        GLuint triVAO  = 0, triVBO  = 0;
        std::vector<Vertex3D> lineVertices;
        std::vector<Vertex3D> triVertices;
        Color currentLineColor{ 255,255,255,255 };
        Color currentTriColor { 255,255,255,255 };
        Vec3  lightPosition{ 5.f, 5.f, 5.f };
        Mat4  viewMatrix       = Mat4::identity();
        Mat4  projectionMatrix = Mat4::identity();
    };

    void   InitGL();
    static std::array<float,4> ToNormColor(Color c);
    GLuint CreateTextureFromRgba(const std::uint8_t* pixels, int w, int h);
    GLuint CompileGLShader(GLenum type, const char* source);
    GLuint CreateDefaultProgram();

    static constexpr std::size_t kMaxBatchVertices = 8192;
    void FlushBatch();
    void EnsureBatchTexture(GLuint textureId);
    void PushVertex(const BatchVertex& vtx);
    void PushQuad(GLuint tex, float x, float y, float w, float h, Color color);
    void PushCircleImpl(float cx, float cy, float r, Color color);
    void PushTexturedQuad(GLuint tex, Rectangle uv,
                          float x, float y, float w, float h, Color color);

    bool            LoadFontInternal(const char* filePath, int pointSize, FontData& out);
    uint32_t        EnsureDefaultFont();
    const FontData* GetFontData(IFont font) const;
    void DrawTextWithFontData(const FontData& fd, const char* text,
                              Vec2 pos, float fontSize, float spacing, Color tint);
    Vec2 MeasureTextWithFontData(const FontData& fd, const char* text,
                                 float fontSize, float spacing) const;

    void   Init3DState();
    void   Init3DGeometry();
    GLuint Compile3DShader();
    void   FlushLines3D();
    void   FlushTriangles3D();
    void   DrawTriangle3DImpl(Vertex3D v1, Vertex3D v2, Vertex3D v3, Color color);
    Vec3   TransformPoint(const Mat4& m, const Vec3& p) const;
    Mat4   ApplyCurrentMatrix(const Mat4& t) const;

    SDL_Window*   m_window   = nullptr;
    SDL_GLContext m_context  = nullptr;
    int           m_width    = 0;
    int           m_height   = 0;
    int           m_targetFps = 60;
    float         m_frameTime = 0.f;
    bool          m_drawing  = false;
    bool          m_shouldClose = false;
    std::uint64_t m_lastFrameCounter = 0;

    GLuint m_program        = 0;
    GLuint m_vao            = 0;
    GLuint m_vbo            = 0;
    GLuint m_whiteTexture   = 0;
    GLuint m_currentTexture = 0;
    GLuint m_currentShader  = 0;
    GLuint m_defaultShader  = 0;
    GLuint m_currentFbo     = 0;
    std::vector<BatchVertex> m_batchVertices;

    Camera2D m_camera2D{};
    bool     m_camera2DActive = false;

    Mat4              m_currentMatrix = Mat4::identity();
    std::vector<Mat4> m_matrixStack;

    Model3DState m_3d;

    std::unordered_map<uint32_t, FontData> m_fonts;
    uint32_t m_nextFontId    = 1;
    uint32_t m_defaultFontId = 0;
};

} // namespace qc