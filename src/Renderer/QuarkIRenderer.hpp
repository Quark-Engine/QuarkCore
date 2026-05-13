#pragma once

#include "QuarkCore/QuarkCore.hpp"
#include "QuarkFont.hpp"
#include "QuarkTexture.hpp"

namespace qc {

enum class RendererType {
    OpenGL,
    Vulkan
};

class QCAPI IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual void BeginDrawing() = 0;
    virtual void EndDrawing() = 0;

    virtual void ClearBackground(Color color) = 0;

    // Draw 2D
    virtual void DrawRectangle(float x, float y, float width, float height, Color color)                            = 0;
    virtual void DrawRectangle(const Rectangle& rectangle, Color color)                                             = 0;
    virtual void DrawRectangleV(Vec2 position, Vec2 size, Color color)                                              = 0;
    virtual void DrawRectangleLines(Rectangle rectangle, float lineWidth, Color color)                              = 0;
    virtual void DrawRectangleRounded(Rectangle rectangle, float roundness, int segments, Color color)              = 0;
    virtual void DrawCircle(float centerX, float centerY, float radius, Color color)                                = 0;        
    virtual void DrawCircleLines(float centerX, float centerY, float radius, Color color)                           = 0;
    virtual void DrawEllipse(float centerX, float centerY, float radiusH, float radiusV, Color color)               = 0;                    
    virtual void DrawLine(float x1, float y1, float x2, float y2, Color color)                                      = 0;
    virtual void DrawLineV(Vec2 start, Vec2 end, Color color)                                                       = 0;
    virtual void DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color)                                               = 0;
    virtual void DrawPoly(Vec2 center, int sides, float radius, float rotation, Color color)                        = 0;


    // Draw 3D
    virtual void Set3DView(const Mat4& view, const Mat4& projection) = 0;
    virtual void DrawLine3D(Vec3 startPos, Vec3 endPos, Color color) = 0;
    virtual void DrawPlane(Vec3 center, Vec2 size, Color color) = 0;
    virtual void DrawCube(Vec3 position, float width, float height, float length, Color color) = 0;
    virtual void DrawCubeV(Vec3 position, Vec3 size, Color color) = 0;
    virtual void DrawCubeWires(Vec3 position, float width, float height, float length, Color color) = 0;
    virtual void DrawCubeWiresV(Vec3 position, Vec3 size, Color color) = 0;
    virtual void DrawSphere(Vec3 centerPos, float radius, Color color) = 0;
    virtual void DrawSphereEx(Vec3 centerPos, float radius, int rings, int slices, Color color) = 0;
    virtual void DrawSphereWires(Vec3 centerPos, float radius, int rings, int slices, Color color) = 0;
    virtual void DrawCylinder(Vec3 position, float radiusTop, float radiusBottom, float height, int slices, Color color) = 0;
    virtual void DrawCylinderEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius, int sides, Color color) = 0;
    virtual void DrawCylinderWires(Vec3 position, float radiusTop, float radiusBottom, float height, int slices, Color color) = 0;
    virtual void DrawCylinderWiresEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius, int slices, Color color) = 0;
    virtual void DrawGrid(int slices, float spacing) = 0;

    virtual void DrawText(const char* text, int x, int y, int fontSize, Color color)                                = 0;
    virtual void DrawTextEx(IFont font, const char* text, Vec2 position, float fontSize, float spacing, Color tint) = 0;
    virtual Vec2 MeasureTextEx(IFont font, const char* text, float fontSize, float spacing)                         = 0;
    virtual int MeasureText(const char* text, int fontSize)                                                         = 0;

    virtual void DrawTexture(const ITexture& texture, float x, float y, Color tint)                                             = 0;
    virtual void DrawTextureV(const ITexture& texture, Vec2 position, Color tint)                                               = 0;
    virtual void DrawTextureEx(const ITexture& texture, Vec2 position, float rotation, float scale, Color tint)                 = 0;
    virtual void DrawTextureRec(const ITexture& texture, Rectangle source, Vec2 position, Color tint)                           = 0;
    virtual void DrawTexturePro(ITexture texture, Rectangle source, Rectangle dest, Vec2 origin, float rotation, Color tint)    = 0;
    virtual void DrawTextureTiled(ITexture texture, float scale, Vec2 offset, Color tint)                                       = 0;
    virtual void DrawTextureNPatch(ITexture texture, Rectangle source, Rectangle dest, Vec2 origin, float rotation, Color tint) = 0;

    // Camera 2D
    virtual void BeginMode2D(const Camera2D& camera) = 0;
    virtual void EndMode2D()                         = 0;

    // Camera 3D
    virtual void BeginMode3D(const Camera3D& camera) = 0;
    virtual void EndMode3D()                         = 0;

    // Matrix stack & Transformations
    virtual void PushMatrix() = 0;
    virtual void PopMatrix()  = 0;
    virtual void Translate(const Vec3& translation) = 0;
    virtual void Rotate(float angle, const Vec3& axis) = 0;
    virtual void Scale(const Vec3& scale) = 0;
    virtual void MultMatrix(const Mat4& matrix) = 0;
    virtual const float* GetMatrixModelview() = 0;
    virtual const float* GetMatrixProjection() = 0;

    virtual void EnableBackfaceCulling() = 0;
    virtual void DisableBackfaceCulling() = 0;

    // Models
    virtual Model LoadModel(const char* filePath) = 0;
    virtual void UnloadModel(Model& model)        = 0;
    virtual void DrawModel(const Model& model, const Vec3& position, float scale, float rotationX, float rotationY, float rotationZ) = 0;
    virtual void DrawModelEx(const Model& model, const Mat4& transform) = 0;
    
    // Texture
    virtual void BeginTextureMode(IRenderTexture target)                                                = 0;
    virtual void EndTextureMode()                                                                       = 0;
    virtual ITexture LoadTexture(const char* filePath)                                                  = 0;
    virtual ITexture GetRenderTextureTexture(IRenderTexture target)                                     = 0;
    virtual void UnloadTexture(ITexture& texture)                                                       = 0;
    virtual IRenderTexture LoadRenderTexture(int width, int height)                                     = 0;
    virtual void UnloadRenderTexture(IRenderTexture target)                                             = 0;
    virtual ITexture GenCheckerTexture(int width, int height, int cellSize, Color colorA, Color colorB) = 0;
    virtual bool isTextureValid(ITexture& texture)                                                      = 0;
    virtual bool isRenderTextureValid(IRenderTexture& target)                                           = 0;

    // Font
    virtual IFont LoadFont(const char* filePath, int fontSize) = 0;
    virtual void UnloadFont(IFont& font)                       = 0;


    // Shaders
    virtual void BeginShaderMode(const Shader& shader)                                            = 0;
    virtual void EndShaderMode()                                                                  = 0;
    virtual Shader LoadShader(const char* vsFileName, const char* fsFileName)                     = 0;
    virtual Shader LoadShaderFromMemory(const char* vsSource, const char* fsSource)               = 0;
    virtual void UnloadShader(Shader& shader)                                                     = 0;
    virtual bool isShaderValid(Shader& shader)                                                    = 0;
    virtual int GetShaderLocation(const Shader& shader, const char* uniformName)                  = 0;
    virtual int GetShaderAttributeLocation(const Shader& shader, const char* attribName)          = 0;

    virtual void SetShaderValue([[maybe_unused]] const Shader& shader, int locIndex, float value)           = 0;
    virtual void SetShaderValue([[maybe_unused]] const Shader& shader, int locIndex, int value)             = 0;
    virtual void SetShaderValue([[maybe_unused]] const Shader& shader, int locIndex, const Color& value)    = 0;
    virtual void SetShaderValue([[maybe_unused]] const Shader& shader, int locIndex, const qc::Vec2& value) = 0;
    virtual void SetShaderValue([[maybe_unused]] const Shader& shader, int locIndex, const qc::Vec3& value) = 0;

    virtual void SetShaderValueMatrix([[maybe_unused]] const Shader& shader, int locIndex, const float* mat) = 0;
    virtual void SetShaderValueSampler([[maybe_unused]] const Shader& shader, int locIndex, int textureUnit) = 0;



    virtual RendererType GetType() const = 0;
};

}