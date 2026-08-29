/*
    ========================================================

        Quark Direct3D 11 Renderer
        By Quark Engine Development Team

    --------------------------------------------------------

    Direct3D 11-based 2D/3D rendering backend for Quark Engine.

    This file contains:
        * D3D11 device and swap chain management
        * Render target view creation and resizing
        * 2D primitive rendering pipeline
        * Shader, texture and buffer management
        * Frame presentation and VSync control

    Backend:
        * Direct3D 11 (via d3d11.h)
        * SDL3 (window/surface creation)
        * WRL ComPtr (Microsoft::WRL::ComPtr for COM resource management)

    --------------------------------------------------------

    THIRD-PARTY NOTICE:

        Direct3D and the Direct3D logo are registered
        trademarks of Microsoft Corporation.

        Windows SDK is provided by Microsoft Corporation.
        See: https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/

        Direct3D 11 is a proprietary graphics API developed
        and maintained by Microsoft as part of DirectX.
        See: https://learn.microsoft.com/en-us/windows/win32/direct3d11/

    ========================================================
*/

#ifndef __QUARK_D3D11_RENDERER__
#define __QUARK_D3D11_RENDERER__

#include "../QuarkIRenderer.hpp"

#if defined(_WIN32)
#define NODRAWTEXT
#include "QuarkD3D11CommandContext.hpp"
#include "QuarkD3D11Device.hpp"
#include "QuarkD3D11Pipeline.hpp"
#include "QuarkD3D11Resources.hpp"
#include "QuarkD3D11ShaderCompiler.hpp"
#include "QuarkD3D11SwapChain.hpp"
#include <array>
#include <chrono>
#include <vector>
#include <unordered_map>
#include <string>
#include <fstream>

namespace qc {

class QuarkD3D11Renderer final : public IRenderer {
public:
    QuarkD3D11Renderer() = default;
    ~QuarkD3D11Renderer() override;

    void Init(SDL_Window *window, int width, int height) override;
    void Shutdown() override;
    void BeginDrawing() override;
    void EndDrawing() override;
    void ClearBackground(Color color) override;
    void RefreshViewport() override;

    void DrawRectangle(float x, float y, float width, float height, Color color) override;
    void DrawRectangle(const Rectangle &rectangle, Color color) override;
    void DrawRectangleV(Vec2 position, Vec2 size, Color color) override;
    void DrawRectangleLines(Rectangle rectangle, float lineWidth, Color color) override;
    void DrawRectangleRounded(Rectangle rectangle, float roundness, int segments,
                              Color color) override;
    void DrawCircle(float centerX, float centerY, float radius, Color color) override;
    void DrawCircleLines(float centerX, float centerY, float radius, Color color) override;
    void DrawEllipse(float centerX, float centerY, float radiusH, float radiusV,
                     Color color) override;

    void DrawLine(float x1, float y1, float x2, float y2, Color color) override;
    void DrawLineV(Vec2 start, Vec2 end, Color color) override;
    void DrawTriangle(Vec2, Vec2, Vec2, Color) override;
    void DrawPoly(Vec2 center, int sides, float radius, float rotation, Color color) override;

    void Set3DView(const Mat4 &view, const Mat4 &projection) override;
    void DrawLine3D(Vec3 startPos, Vec3 endPos, Color color) override;
    void DrawPlane(Vec3 center, Vec2 size, Color color) override;
    void DrawCube(Vec3 position, float width, float height, float length, Color color) override;
    void DrawCubeV(Vec3 position, Vec3 size, Color color) override;
    void DrawCubeWires(Vec3 position, float width, float height, float length, Color color) override;
    void DrawCubeWiresV(Vec3 position, Vec3 size, Color color) override;
    void DrawSphere(Vec3 centerPos, float radius, Color color) override;
    void DrawSphereEx(Vec3 centerPos, float radius, int rings, int slices, Color color) override;
    void DrawSphereWires(Vec3 centerPos, float radius, int rings, int slices, Color color) override;
    void DrawCylinder(Vec3 position, float radiusTop, float radiusBottom, float height,
                      int slices, Color color) override;
    void DrawCylinderEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius,
                        int sides, Color color) override;
    void DrawCylinderWires(Vec3 position, float radiusTop, float radiusBottom, float height,
                           int slices, Color color) override;
    void DrawCylinderWiresEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius,
                             int slices, Color color) override;
    void DrawGrid(int slices, float spacing, Color color) override;

    void DrawTexture(const ITexture &, float, float, Color) override;
    void DrawTextureV(const ITexture &, Vec2, Color) override;
    void DrawTextureEx(const ITexture &, Vec2, float, float, Color) override;
    void DrawTextureRec(const ITexture &, Rectangle, Vec2, Color) override;
    void DrawTexturePro(ITexture, Rectangle, Rectangle, Vec2, float, Color) override;
    void DrawTextureTiled(ITexture, float, Vec2, Color) override;
    void DrawTextureNPatch(ITexture, Rectangle, Rectangle, Vec2, float, Color) override;
    ITexture LoadTexture(const char *) override;
    ITexture GetRenderTextureTexture(IRenderTexture) override;
    void UnloadTexture(ITexture &) override;
    IRenderTexture LoadRenderTexture(int, int) override;
    void UnloadRenderTexture(IRenderTexture) override;
    ITexture GenCheckerTexture(int, int, int, Color, Color) override;
    bool isTextureValid(ITexture &) override;
    bool isRenderTextureValid(IRenderTexture &) override;

    void DrawText(const char *text, int x, int y, int fontSize, Color color) override;
    void DrawTextEx(IFont font, const char *text, Vec2 position, float fontSize,
                    float spacing, Color tint) override;
    Vec2 MeasureTextEx(IFont font, const char *text, float fontSize, float spacing) override;
    int MeasureText(const char *text, int fontSize) override;

    IFont LoadFont(const char *filePath, int fontSize) override;
    void UnloadFont(IFont &font) override;

    void BeginShaderMode(const Shader &shader) override;
    void EndShaderMode() override;
    Shader LoadShader(const char *vsFileName, const char *fsFileName) override;
    Shader LoadShaderFromMemory(const char *vsSource, const char *fsSource) override;
    void UnloadShader(Shader &shader) override;
    bool isShaderValid(Shader &shader) override;
    int GetShaderLocation(const Shader &shader, const char *uniformName) override;
    int GetShaderLocation(const Shader &shader, ShaderLocationIndex locIndex) override;
    int GetShaderAttributeLocation(const Shader &shader, const char *attribName) override;
    void SetShaderValue(const Shader &shader, int locIndex, float value) override;
    void SetShaderValue(const Shader &shader, int locIndex, int value) override;
    void SetShaderValue(const Shader &shader, int locIndex, const Color &value) override;
    void SetShaderValue(const Shader &shader, int locIndex, const Vec2 &value) override;
    void SetShaderValue(const Shader &shader, int locIndex, const Vec3 &value) override;
    void SetShaderValue(const Shader &shader, int locIndex, const Vec4 &value) override;
    void SetShaderValueMatrix(const Shader &shader, int locIndex, const float *mat) override;
    void SetShaderValueSampler(const Shader &shader, int locIndex, int textureUnit) override;
    void SetShaderValue(const Shader &shader, int locIndex, const void *value,
                        int uniformType) override;
    void SetShaderValueV(const Shader &shader, int locIndex, const void *value, int uniformType,
                         int count) override;
    void SetShaderValueMatrix(const Shader &shader, int locIndex, const Matrix &mat) override;
    void SetShaderValueTexture(const Shader &shader, int locIndex, const ITexture &texture) override;
    void SetShaderValueTextureUnit(const Shader &shader, int locIndex, const ITexture &texture,
                                   int textureUnit) override;

    void BeginMode2D(const Camera2D &camera) override;
    void EndMode2D() override;
    Camera2D GetCamera2D() const override { return m_commands.GetCamera2D(); }
    void BeginMode3D(const Camera3D &camera) override;
    void EndMode3D() override;
    void PushMatrix() override;
    void PopMatrix() override;
    void Translate(const Vec3 &translation) override;
    void Rotate(float angle, const Vec3 &axis) override;
    void Scale(const Vec3 &scale) override;
    void MultMatrix(const Mat4 &matrix) override;
    const float *GetMatrixModelview() override;
    const float *GetMatrixProjection() override;
    void EnableBackfaceCulling() override;
    void DisableBackfaceCulling() override;

    Model LoadModel(const char *filePath) override;
    void UnloadModel(Model &model) override;
    void DrawModel(const Model &model, const Vec3 &position, float scale,
                   float rotationX, float rotationY, float rotationZ) override;
    void DrawModelEx(const Model &model, const Mat4 &transform) override;
    void DrawModelEx(const Model &model, const Mat4 &transform, Color tint) override;
    void UploadMesh(Mesh &mesh, bool dynamic) override;
    void UpdateMeshBuffer(Mesh &mesh, int index, const void *data, int dataSize, int offset) override;
    void UnloadMesh(Mesh &mesh) override;
    void DrawMesh(const Mesh &mesh, const Material &material, const Mat4 &transform) override;
    void DrawMeshInstanced(const Mesh &mesh, const Material &material, const Mat4 *transforms,
                           int instances) override;

    void BeginTextureMode(IRenderTexture target) override;
    void EndTextureMode() override;
    RendererType GetType() const override { return RendererType::D3D11; }
    bool ShouldClose() const override { return m_shouldClose; }
    void SetTargetFPS(int fps) override { m_targetFps = fps; }
    void SetMSAASamples(int samples);
    void SetTextureFilterMode(TextureFilterMode mode);
    bool SetVSync(bool enabled) override {
        m_vsync = enabled;
        m_vsyncExplicitlySet = true;
        return true;
    }
    float GetFrameTime() const override { return m_frameTime; }
    int GetScreenWidth() const override { return m_width; }
    int GetScreenHeight() const override { return m_height; }
    ID3D11Device *GetD3D11Device() const { return m_device.Get(); }
    ID3D11DeviceContext *GetD3D11ImmediateContext() const { return m_device.Context(); }

private:
    struct GlyphData {
        Rectangle uv{};
        float advanceX = 0.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        int width = 0;
        int height = 0;
    };

    struct FontData {
        ITexture atlasTexture{};
        int baseSize = 0;
        int ascent = 0;
        int descent = 0;
        int lineHeight = 0;
        std::array<GlyphData, 95> glyphs{};
    };

    struct ShaderUniformInfo {
        std::string name;
        uint32_t offset = 0;
        uint32_t size = 0;
    };

    struct ShaderProgramData {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer;
        std::vector<ShaderUniformInfo> uniformInfos;
        std::unordered_map<std::string, int> uniforms;
        std::unordered_map<std::string, int> attributes;
        std::unordered_map<int, std::vector<uint8_t>> uniformValues;
        std::unordered_map<int, int> uniformTypes;
        std::unordered_map<int, uint32_t> textureIds;
        std::unordered_map<int, UINT> textureSlots;
        UINT nextTextureSlot = 1;
        UINT strideBytes = 0;
        UINT positionOffset = 0;
        UINT texCoordOffset = 0xFFFFFFFFu;
        UINT colorOffset = 0xFFFFFFFFu;
        bool hasPosition = false;
        std::vector<uint8_t> constantStaging;
        UINT constantBufferSize = 0;
        bool dirty = false;
    };

    D3D11CommandContext::ShaderOverride BuildShaderOverride(uint32_t shaderId);
    void BuildShaderProgram(ShaderProgramData &program, const char *vsSource, const char *fsSource);
    void RegisterShaderTexture(ShaderProgramData &program, int locIndex, uint32_t textureId);
    void UploadConstantBuffer(ShaderProgramData &program);
    void EnsureConstantCapacity(ShaderProgramData &program, size_t byteCount);
    void StoreUniformValue(ShaderProgramData &program, int locIndex, int uniformType,
                           const void *value, size_t elementBytes, int count);
    ShaderProgramData *GetShaderProgram(uint32_t shaderId);

    bool LoadFontData(const char *filePath, int fontSize, FontData &fontData);
    const FontData *GetFontData(IFont font) const;
    void DrawTextWithFontData(const FontData &fontData, const char *text,
                              Vec2 position, float fontSize, float spacing, Color tint);
    Vec2 MeasureTextWithFontData(const FontData &fontData, const char *text,
                                 float fontSize, float spacing) const;
    uint32_t EnsureDefaultFont();
    void DrawTris3D(const Vec3 *positions, size_t vertexCount, const Mat4 &mvp, Color color);
    void DrawLines3D(const Vec3 *positions, size_t vertexCount, const Mat4 &mvp, Color color);
    Mat4 CurrentMVP() const;

    SDL_Window *m_window = nullptr;
    D3D11Device m_device;
    D3D11SwapChain m_swapChain;
    D3D11Resources m_resources;
    D3D11ShaderCompiler m_shaderCompiler;
    D3D11Pipeline m_pipeline;
    D3D11CommandContext m_commands;
    int m_width = 0;
    int m_height = 0;
    int m_targetFps = 60;
    int m_requestedMsaaSamples = 1;
    TextureFilterMode m_textureFilterMode = TextureFilterMode::Linear;
    bool m_vsync = true;
    bool m_vsyncExplicitlySet = false;
    std::uint64_t m_lastFrameCounter = 0;
    bool m_shouldClose = false;
    float m_frameTime = 0.0f;
    bool m_drawing = false;
    std::chrono::steady_clock::time_point m_lastFrame{};
    std::unordered_map<uint32_t, FontData> m_fonts;
    uint32_t m_nextFontId = 1;
    uint32_t m_defaultFontId = 0;
    std::unordered_map<uint32_t, ShaderProgramData> m_shaderPrograms;
    uint32_t m_nextShaderId = 1;
    uint32_t m_currentShaderId = 0;
    ITexture m_whiteShaderTexture{};
    Mat4 m_viewMatrix{};
    Mat4 m_projectionMatrix{};
    Mat4 m_currentMatrix{};
    Mat4 m_modelviewCapture{};
    std::vector<Mat4> m_matrixStack;
};

} // namespace qc
#endif

#endif // __QUARK_D3D11_RENDERER__
