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

#pragma once

#include "../QuarkIRenderer.hpp"

#if defined(_WIN32)
#define NODRAWTEXT
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <vector>

namespace qc {

class QuarkD3D11Renderer final : public IRenderer {
public:
    QuarkD3D11Renderer() = default;
    ~QuarkD3D11Renderer() override;

    void Init(SDL_Window* window, int width, int height) override;
    void Shutdown() override;
    void BeginDrawing() override;
    void EndDrawing() override;
    void ClearBackground(Color color) override;
    void RefreshViewport() override;

    void DrawRectangle(float, float, float, float, Color) override {}
    void DrawRectangle(const Rectangle&, Color) override {}
    void DrawRectangleV(Vec2, Vec2, Color) override {}
    void DrawRectangleLines(Rectangle, float, Color) override {}
    void DrawRectangleRounded(Rectangle, float, int, Color) override {}
    void DrawCircle(float, float, float, Color) override {}
    void DrawCircleLines(float, float, float, Color) override {}
    void DrawEllipse(float, float, float, float, Color) override {}
    void DrawLine(float, float, float, float, Color) override {}
    void DrawLineV(Vec2, Vec2, Color) override {}
    void DrawTriangle(Vec2, Vec2, Vec2, Color) override;
    void DrawPoly(Vec2, int, float, float, Color) override {}

    void Set3DView(const Mat4&, const Mat4&) override {}
    void DrawLine3D(Vec3, Vec3, Color) override {}
    void DrawPlane(Vec3, Vec2, Color) override {}
    void DrawCube(Vec3, float, float, float, Color) override {}
    void DrawCubeV(Vec3, Vec3, Color) override {}
    void DrawCubeWires(Vec3, float, float, float, Color) override {}
    void DrawCubeWiresV(Vec3, Vec3, Color) override {}
    void DrawSphere(Vec3, float, Color) override {}
    void DrawSphereEx(Vec3, float, int, int, Color) override {}
    void DrawSphereWires(Vec3, float, int, int, Color) override {}
    void DrawCylinder(Vec3, float, float, float, int, Color) override {}
    void DrawCylinderEx(Vec3, Vec3, float, float, int, Color) override {}
    void DrawCylinderWires(Vec3, float, float, float, int, Color) override {}
    void DrawCylinderWiresEx(Vec3, Vec3, float, float, int, Color) override {}
    void DrawGrid(int, float) override {}

    void DrawText(const char*, int, int, int, Color) override {}
    void DrawTextEx(IFont, const char*, Vec2, float, float, Color) override {}
    Vec2 MeasureTextEx(IFont, const char*, float, float) override { return {}; }
    int MeasureText(const char*, int) override { return 0; }

    void DrawTexture(const ITexture&, float, float, Color) override {}
    void DrawTextureV(const ITexture&, Vec2, Color) override {}
    void DrawTextureEx(const ITexture&, Vec2, float, float, Color) override {}
    void DrawTextureRec(const ITexture&, Rectangle, Vec2, Color) override {}
    void DrawTexturePro(ITexture, Rectangle, Rectangle, Vec2, float, Color) override {}
    void DrawTextureTiled(ITexture, float, Vec2, Color) override {}
    void DrawTextureNPatch(ITexture, Rectangle, Rectangle, Vec2, float, Color) override {}
    ITexture LoadTexture(const char*) override { return {}; }
    ITexture GetRenderTextureTexture(IRenderTexture) override { return {}; }
    void UnloadTexture(ITexture&) override {}
    IRenderTexture LoadRenderTexture(int, int) override { return {}; }
    void UnloadRenderTexture(IRenderTexture) override {}
    ITexture GenCheckerTexture(int, int, int, Color, Color) override { return {}; }
    bool isTextureValid(ITexture&) override { return false; }
    bool isRenderTextureValid(IRenderTexture&) override { return false; }

    IFont LoadFont(const char*, int) override { return {}; }
    void UnloadFont(IFont&) override {}

    void BeginShaderMode(const Shader&) override {}
    void EndShaderMode() override {}
    Shader LoadShader(const char*, const char*) override { return {}; }
    Shader LoadShaderFromMemory(const char*, const char*) override { return {}; }
    void UnloadShader(Shader&) override {}
    bool isShaderValid(Shader&) override { return false; }
    int GetShaderLocation(const Shader&, const char*) override { return -1; }
    int GetShaderLocation(const Shader&, ShaderLocationIndex) override { return -1; }
    int GetShaderAttributeLocation(const Shader&, const char*) override { return -1; }
    void SetShaderValue(const Shader&, int, float) override {}
    void SetShaderValue(const Shader&, int, int) override {}
    void SetShaderValue(const Shader&, int, const Color&) override {}
    void SetShaderValue(const Shader&, int, const Vec2&) override {}
    void SetShaderValue(const Shader&, int, const Vec3&) override {}
    void SetShaderValue(const Shader&, int, const Vec4&) override {}
    void SetShaderValueMatrix(const Shader&, int, const float*) override {}
    void SetShaderValueSampler(const Shader&, int, int) override {}
    void SetShaderValue(const Shader&, int, const void*, int) override {}
    void SetShaderValueV(const Shader&, int, const void*, int, int) override {}
    void SetShaderValueMatrix(const Shader&, int, const Matrix&) override {}
    void SetShaderValueTexture(const Shader&, int, const ITexture&) override {}
    void SetShaderValueTextureUnit(const Shader&, int, const ITexture&, int) override {}

    void BeginMode2D(const Camera2D&) override {}
    void EndMode2D() override {}
    Camera2D GetCamera2D() const override { return {}; }
    void BeginMode3D(const Camera3D&) override {}
    void EndMode3D() override {}
    void PushMatrix() override {}
    void PopMatrix() override {}
    void Translate(const Vec3&) override {}
    void Rotate(float, const Vec3&) override {}
    void Scale(const Vec3&) override {}
    void MultMatrix(const Mat4&) override {}
    const float* GetMatrixModelview() override { return m_identity.data(); }
    const float* GetMatrixProjection() override { return m_identity.data(); }
    void EnableBackfaceCulling() override {}
    void DisableBackfaceCulling() override {}

    Model LoadModel(const char*) override { return {}; }
    void UnloadModel(Model&) override {}
    void DrawModel(const Model&, const Vec3&, float, float, float, float) override {}
    void DrawModelEx(const Model&, const Mat4&) override {}
    void DrawModelEx(const Model&, const Mat4&, Color) override {}
    void UploadMesh(Mesh&, bool) override {}
    void UpdateMeshBuffer(Mesh&, int, const void*, int, int) override {}
    void UnloadMesh(Mesh&) override {}
    void DrawMesh(const Mesh&, const Material&, const Mat4&) override {}
    void DrawMeshInstanced(const Mesh&, const Material&, const Mat4*, int) override {}

    void BeginTextureMode(IRenderTexture) override {}
    void EndTextureMode() override {}
    RendererType GetType() const override { return RendererType::D3D11; }
    bool ShouldClose() const override { return m_shouldClose; }
    void SetTargetFPS(int fps) override { m_targetFps = fps; }
    bool SetVSync(bool enabled) override { m_vsync = enabled; return true; }
    float GetFrameTime() const override { return m_frameTime; }
    int GetScreenWidth() const override { return m_width; }
    int GetScreenHeight() const override { return m_height; }

private:
    SDL_Window* m_window = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTarget;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_triangleVertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_trianglePixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_triangleInputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_triangleVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_triangleRasterizerState;
    int m_width = 0;
    int m_height = 0;
    int m_targetFps = 60;
    bool m_vsync = true;
    bool m_shouldClose = false;
    float m_frameTime = 0.0f;
    bool m_drawing = false;
    std::chrono::steady_clock::time_point m_lastFrame{};
    std::array<float, 16> m_identity{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
};

} // namespace qc
#endif
