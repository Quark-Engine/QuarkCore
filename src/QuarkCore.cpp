#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/Quark3D.hpp"
#include "Renderer/QuarkGLRenderer.hpp"
#include "QuarkInternal.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <array>
#include <array>

namespace qc {

QuarkGLRenderer gRenderer;

WindowState gWin;
int   gLastKeyPressed   = 0;
int   gLastCharPressed  = 0;
KeyboardKey gExitKey    = KeyboardKey::Escape;
Vec2  gMousePreviousPosition{};
bool  gCursorHidden     = false;

const char* ToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::None:  return "NONE";
        default:              return "UNKNOWN";
    }
}

std::string FormatTimeNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%H:%M:%S");
    return ss.str();
}

void WriteLog(LogLevel level, const char* type, const std::string& message) {
    if (level < gWin.minimumLogLevel || level == LogLevel::None) return;
    std::cout
        << '[' << FormatTimeNow() << ']'
        << '[' << ToString(level) << ']'
        << '[' << type << "] "
        << message << '\n';
}

void CopyText(char* dst, size_t size, const char* src) {
    if (!dst || size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
#if defined(_MSC_VER)
    strncpy_s(dst, size, src, _TRUNCATE);
#else
    std::strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
#endif
}

static Mat4 BuildTransform(const Vec3& position, const Vec3& axis, float angle, const Vec3& scale) {
    Mat4 translation = Mat4::translation(position.x, position.y, position.z);
    Mat4 scaleMat = Mat4::scale(scale.x, scale.y, scale.z);
    Mat4 rotation = Mat4::identity();
    if ((axis.x != 0.0f || axis.y != 0.0f || axis.z != 0.0f) && angle != 0.0f) {
        Vec3 n = axis.normalized();
        float c = std::cos(angle);
        float s = std::sin(angle);
        float t = 1.0f - c;

        rotation.m[0] = n.x * n.x * t + c;
        rotation.m[1] = n.x * n.y * t + n.z * s;
        rotation.m[2] = n.x * n.z * t - n.y * s;
        rotation.m[3] = 0.0f;

        rotation.m[4] = n.x * n.y * t - n.z * s;
        rotation.m[5] = n.y * n.y * t + c;
        rotation.m[6] = n.y * n.z * t + n.x * s;
        rotation.m[7] = 0.0f;

        rotation.m[8] = n.x * n.z * t + n.y * s;
        rotation.m[9] = n.y * n.z * t - n.x * s;
        rotation.m[10] = n.z * n.z * t + c;
        rotation.m[11] = 0.0f;

        rotation.m[12] = 0.0f;
        rotation.m[13] = 0.0f;
        rotation.m[14] = 0.0f;
        rotation.m[15] = 1.0f;
    }

    return translation * rotation * scaleMat;
}

static Vec3 TransformPoint(const Mat4& transform, const Vec3& point) {
    return Vec3{
        transform.m[0] * point.x + transform.m[4] * point.y + transform.m[8]  * point.z + transform.m[12],
        transform.m[1] * point.x + transform.m[5] * point.y + transform.m[9]  * point.z + transform.m[13],
        transform.m[2] * point.x + transform.m[6] * point.y + transform.m[10] * point.z + transform.m[14]
    };
}

static void DrawModelWireframe(const Model& model, const Mat4& transform, Color color) {
    if (!model.meshes) return;

    for (int i = 0; i < model.meshCount; ++i) {
        const Mesh& mesh = model.meshes[i];
        if (!mesh.vertices) continue;

        const bool hasIndices = mesh.indices != nullptr;
        const int triangleCount = mesh.triangleCount;

        for (int t = 0; t < triangleCount; ++t) {
            int idx0 = hasIndices ? mesh.indices[t * 3 + 0] : t * 3 + 0;
            int idx1 = hasIndices ? mesh.indices[t * 3 + 1] : t * 3 + 1;
            int idx2 = hasIndices ? mesh.indices[t * 3 + 2] : t * 3 + 2;

            Vec3 v0 = TransformPoint(transform, Vec3{
                mesh.vertices[idx0 * 3 + 0],
                mesh.vertices[idx0 * 3 + 1],
                mesh.vertices[idx0 * 3 + 2]
            });
            Vec3 v1 = TransformPoint(transform, Vec3{
                mesh.vertices[idx1 * 3 + 0],
                mesh.vertices[idx1 * 3 + 1],
                mesh.vertices[idx1 * 3 + 2]
            });
            Vec3 v2 = TransformPoint(transform, Vec3{
                mesh.vertices[idx2 * 3 + 0],
                mesh.vertices[idx2 * 3 + 1],
                mesh.vertices[idx2 * 3 + 2]
            });

            gRenderer.DrawLine3D(v0, v1, color);
            gRenderer.DrawLine3D(v1, v2, color);
            gRenderer.DrawLine3D(v2, v0, color);
        }
    }
}

void UpdateInputFromEvents() {
    float mx = 0.f, my = 0.f;
    const SDL_MouseButtonFlags ms = SDL_GetMouseState(&mx, &my);
    gWin.mousePosition = Vec2{mx, my};
    gWin.mouseButtons[static_cast<std::size_t>(MouseButton::Left)]   = (ms & SDL_BUTTON_LMASK) != 0;
    gWin.mouseButtons[static_cast<std::size_t>(MouseButton::Middle)] = (ms & SDL_BUTTON_MMASK) != 0;
    gWin.mouseButtons[static_cast<std::size_t>(MouseButton::Right)]  = (ms & SDL_BUTTON_RMASK) != 0;

    const bool* ks = SDL_GetKeyboardState(nullptr);
    for (int i = 0; i < static_cast<int>(SDL_SCANCODE_COUNT); ++i)
        gWin.currentKeys[static_cast<std::size_t>(i)] = ks[i];
}
void EnsureInitialized() {
    if (gWin.window == nullptr)
        throw std::runtime_error("QuarkCore is not initialized. Call InitWindow() first.");
}

void InitWindow(int width, int height, const char* title) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    gWin.window = SDL_CreateWindow(title, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!gWin.window)
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

    gRenderer.Init(gWin.window, width, height);
    gRenderer.SetTargetFPS(gWin.targetFps);

    WriteLog(LogLevel::Info, "WINDOW", "Window created: " + std::string(title ? title : ""));
}

void CloseWindow() {
    gRenderer.Shutdown();
    if (gWin.window) {
        SDL_DestroyWindow(gWin.window);
        gWin.window = nullptr;
    }
    SDL_Quit();
    WriteLog(LogLevel::Info, "WINDOW", "Window closed");
}

bool WindowShouldClose() {
    if (!gWin.eventsReady) {
        PumpSystemEvents();
    }
    gWin.eventsReady = false;

    if (gRenderer.ShouldClose()) gWin.shouldClose = true;
    return gWin.shouldClose;
}

bool IsWindowReady() {
    return gWin.window != nullptr;
}

int GetScreenWidth()  { return gRenderer.GetScreenWidth();  }
int GetScreenHeight() { return gRenderer.GetScreenHeight(); }

void SetTargetFPS(int fps) {
    gWin.targetFps = fps;
    gRenderer.SetTargetFPS(fps);
}

float GetFrameTime()  { return gRenderer.GetFrameTime();  }
float GetDeltaTime()  { return GetFrameTime(); }

double GetTime() {
    if (!SDL_WasInit(SDL_INIT_VIDEO)) return 0.0;
    return static_cast<double>(SDL_GetTicks()) / 1000.0;
}

void SetLogLevel(LogLevel level) { gWin.minimumLogLevel = level; }

void TraceLog(LogLevel level, const char* type, const char* message) {
    WriteLog(level, type, message ? message : "");
}

const char* TextFormat(const char* fmt, ...) {
    thread_local char buf[4096];
    if (!fmt) { buf[0] = '\0'; return buf; }
    va_list args;
    va_start(args, fmt);
#if defined(_MSC_VER)
    vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
#else
    std::vsnprintf(buf, sizeof(buf), fmt, args);
#endif
    va_end(args);
    return buf;
}

SDL_GLContext GetNativeContext() {
    EnsureInitialized();
    return SDL_GL_GetCurrentContext();
}

bool IsTextInputActive() {
    EnsureInitialized();
    return SDL_TextInputActive(gWin.window);
}

bool IsKeyDown(KeyboardKey key) {
    EnsureInitialized();
    const auto i = static_cast<std::size_t>(key);
    return i < gWin.currentKeys.size() ? gWin.currentKeys[i] : false;
}

bool IsKeyPressed(KeyboardKey key) {
    EnsureInitialized();
    const auto i = static_cast<std::size_t>(key);
    if (i >= gWin.currentKeys.size()) return false;
    return gWin.currentKeys[i] && !gWin.previousKeys[i];
}

bool IsKeyReleased(KeyboardKey key) {
    EnsureInitialized();
    const auto i = static_cast<std::size_t>(key);
    if (i >= gWin.currentKeys.size()) return false;
    return !gWin.currentKeys[i] && gWin.previousKeys[i];
}

bool IsKeyUp(KeyboardKey key) {
    return !IsKeyDown(key);
}

int GetKeyPressed()  { int k = gLastKeyPressed;  gLastKeyPressed  = 0; return k; }
int GetCharPressed() { int c = gLastCharPressed; gLastCharPressed = 0; return c; }

void SetExitKey(KeyboardKey key) { gExitKey = key; }

bool IsMouseButtonDown(MouseButton button) {
    EnsureInitialized();
    const auto i = static_cast<std::size_t>(button);
    return i < gWin.mouseButtons.size() ? gWin.mouseButtons[i] : false;
}

bool IsMouseButtonPressed(MouseButton button) {
    EnsureInitialized();
    const auto i = static_cast<std::size_t>(button);
    if (i >= gWin.mouseButtons.size()) return false;
    return gWin.mouseButtons[i] && !gWin.previousMouseButtons[i];
}

bool IsMouseButtonReleased(MouseButton button) {
    EnsureInitialized();
    const auto i = static_cast<std::size_t>(button);
    if (i >= gWin.mouseButtons.size()) return false;
    return !gWin.mouseButtons[i] && gWin.previousMouseButtons[i];
}

bool IsMouseButtonUp(MouseButton button) { return !IsMouseButtonDown(button); }

Vec2  GetMousePosition()    { EnsureInitialized(); return gWin.mousePosition; }
Vec2  GetMouseWheelMoveV()  { EnsureInitialized(); return gWin.mouseWheel; }
float GetMouseWheelMove()   { EnsureInitialized(); return gWin.mouseWheel.y; }

Vec2 GetMouseDelta() {
    Vec2 delta{ gWin.mousePosition.x - gMousePreviousPosition.x,
                gWin.mousePosition.y - gMousePreviousPosition.y };
    gMousePreviousPosition = gWin.mousePosition;
    return delta;
}

void SetMousePosition(int x, int y) {
    if (gWin.window) {
        SDL_WarpMouseInWindow(gWin.window, static_cast<float>(x), static_cast<float>(y));
        gWin.mousePosition      = Vec2{static_cast<float>(x), static_cast<float>(y)};
        gMousePreviousPosition  = gWin.mousePosition;
    }
}

void DisableCursor()  { if (gWin.window) { SDL_HideCursor(); gCursorHidden = true;  } }
void EnableCursor()   { if (gWin.window) { SDL_ShowCursor(); gCursorHidden = false; } }
bool IsCursorHidden() { return gCursorHidden; }

void SetMouseCursor(MouseCursor cursor) {
    if (!gWin.window) return;
    SDL_SystemCursor sdl;
    switch (cursor) {
        case MouseCursor::Ibeam:        sdl = SDL_SYSTEM_CURSOR_TEXT;        break;
        case MouseCursor::Crosshair:    sdl = SDL_SYSTEM_CURSOR_CROSSHAIR;   break;
        case MouseCursor::PointingHand: sdl = SDL_SYSTEM_CURSOR_POINTER;     break;
        case MouseCursor::ResizeEW:     sdl = SDL_SYSTEM_CURSOR_EW_RESIZE;   break;
        case MouseCursor::ResizeNS:     sdl = SDL_SYSTEM_CURSOR_NS_RESIZE;   break;
        case MouseCursor::ResizeNWSE:   sdl = SDL_SYSTEM_CURSOR_NWSE_RESIZE; break;
        case MouseCursor::ResizeNESW:   sdl = SDL_SYSTEM_CURSOR_NESW_RESIZE; break;
        case MouseCursor::ResizeAll:    sdl = SDL_SYSTEM_CURSOR_MOVE;        break;
        case MouseCursor::NotAllowed:   sdl = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break;
        default:                        sdl = SDL_SYSTEM_CURSOR_DEFAULT;     break;
    }
    SDL_Cursor* c = SDL_CreateSystemCursor(sdl);
    if (c) { SDL_SetCursor(c); SDL_DestroyCursor(c); }
}

bool IsGamepadAvailable(int gamepad) {
    SDL_Joystick* j = SDL_OpenJoystick(gamepad);
    if (j) { SDL_CloseJoystick(j); return true; }
    return false;
}

const char* GetGamepadName(int gamepad) {
    SDL_Joystick* j = SDL_OpenJoystick(gamepad);
    if (!j) return "UNKNOWN";
    const char* n = SDL_GetJoystickName(j);
    SDL_CloseJoystick(j);
    return n ? n : "UNKNOWN";
}

float GetGamepadAxisMovement(int gamepad, int axis) {
    SDL_Joystick* j = SDL_OpenJoystick(gamepad);
    if (!j) return 0.f;
    float v = 0.f;
    if (axis >= 0 && axis < SDL_GetNumJoystickAxes(j))
        v = SDL_GetJoystickAxis(j, axis) / 32768.f;
    SDL_CloseJoystick(j);
    return v;
}

bool IsGamepadButtonPressed(int gamepad, int button) {
    SDL_Joystick* j = SDL_OpenJoystick(gamepad);
    if (!j) return false;
    bool p = SDL_GetJoystickButton(j, button) != 0;
    SDL_CloseJoystick(j);
    return p;
}

void BeginDrawing() { EnsureInitialized(); gRenderer.BeginDrawing(); }
void EndDrawing()   { EnsureInitialized(); gRenderer.EndDrawing();   }

void ClearBackground(Color color) { EnsureInitialized(); gRenderer.ClearBackground(color); }

void DrawRectangle(float x, float y, float w, float h, Color c)    { gRenderer.DrawRectangle(x, y, w, h, c); }
void DrawRectangle(const Rectangle& r, Color c)                     { gRenderer.DrawRectangle(r, c);          }
void DrawRectangleV(Vec2 pos, Vec2 size, Color c)                   { gRenderer.DrawRectangleV(pos, size, c); }
void DrawRectangleLines(Rectangle r, float lw, Color c)             { gRenderer.DrawRectangleLines(r, lw, c); }
void DrawRectangleRounded(Rectangle r, float rn, int seg, Color c)  { gRenderer.DrawRectangleRounded(r, rn, seg, c); }

void DrawCircle(float cx, float cy, float radius, Color c)          { gRenderer.DrawCircle(cx, cy, radius, c);      }
void DrawCircleLines(float cx, float cy, float radius, Color c)     { gRenderer.DrawCircleLines(cx, cy, radius, c); }
void DrawEllipse(float cx, float cy, float rh, float rv, Color c)   { gRenderer.DrawEllipse(cx, cy, rh, rv, c);    }

void DrawLine(float x1, float y1, float x2, float y2, Color c)      { gRenderer.DrawLine(x1, y1, x2, y2, c); }
void DrawLineV(Vec2 start, Vec2 end, Color c)                        { gRenderer.DrawLineV(start, end, c);     }

void DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color c)               { gRenderer.DrawTriangle(v1, v2, v3, c); }
void DrawPoly(Vec2 center, int sides, float r, float rot, Color c)  { gRenderer.DrawPoly(center, sides, r, rot, c); }

Texture2D LoadTexture(const char* filePath) {
    EnsureInitialized();
    ITexture it = gRenderer.LoadTexture(filePath);
    Texture2D t;
    t.id    = it.id;
    t.width = it.width;
    t.height= it.height;
    t.valid = it.valid;
    return t;
}

void UnloadTexture(Texture2D& texture) {
    ITexture it{ texture.id, texture.width, texture.height, texture.valid };
    gRenderer.UnloadTexture(it);
    texture = {};
}

bool IsTextureValid(Texture2D texture) {
    ITexture it{ texture.id, texture.width, texture.height, texture.valid };
    return gRenderer.isTextureValid(it);
}

bool IsTextureReady(Texture2D texture) { return IsTextureValid(texture); }

void DrawTexture(const Texture2D& tex, float x, float y, Color tint) {
    ITexture it{ tex.id, tex.width, tex.height, tex.valid };
    gRenderer.DrawTexture(it, x, y, tint);
}

void DrawTextureV(Texture2D tex, Vec2 pos, Color tint) {
    ITexture it{ tex.id, tex.width, tex.height, tex.valid };
    gRenderer.DrawTextureV(it, pos, tint);
}

void DrawTextureEx(Texture2D tex, Vec2 pos, float rot, float scale, Color tint) {
    ITexture it{ tex.id, tex.width, tex.height, tex.valid };
    gRenderer.DrawTextureEx(it, pos, rot, scale, tint);
}

void DrawTextureRec(Texture2D tex, Rectangle source, Vec2 pos, Color tint) {
    ITexture it{ tex.id, tex.width, tex.height, tex.valid };
    gRenderer.DrawTextureRec(it, source, pos, tint);
}

void DrawTexturePro(Texture2D tex, Rectangle src, Rectangle dst, Vec2 origin, float rot, Color tint) {
    ITexture it{ tex.id, tex.width, tex.height, tex.valid };
    gRenderer.DrawTexturePro(it, src, dst, origin, rot, tint);
}

void DrawTextureTiled(Texture2D tex, float scale, Vec2 offset, Color tint) {
    ITexture it{ tex.id, tex.width, tex.height, tex.valid };
    gRenderer.DrawTextureTiled(it, scale, offset, tint);
}

void DrawTextureNPatch(Texture2D tex, Rectangle src, Rectangle dst, Vec2 origin, float rot, Color tint) {
    ITexture it{ tex.id, tex.width, tex.height, tex.valid };
    gRenderer.DrawTextureNPatch(it, src, dst, origin, rot, tint);
}

Texture2D GenCheckerTexture(int w, int h, int cellSize, Color a, Color b) {
    EnsureInitialized();
    ITexture it = gRenderer.GenCheckerTexture(w, h, cellSize, a, b);
    return Texture2D{ it.id, it.width, it.height, it.valid };
}

RenderTexture2D LoadRenderTexture(int w, int h) {
    EnsureInitialized();
    IRenderTexture ir = gRenderer.LoadRenderTexture(w, h);
    RenderTexture2D rt;
    rt.id       = ir.id;
    rt.depthId  = ir.depthId;
    rt.texture  = Texture2D{ ir.texture.id, ir.texture.width, ir.texture.height, ir.texture.valid };
    return rt;
}

void UnloadRenderTexture(RenderTexture2D target) {
    IRenderTexture ir;
    ir.id      = target.id;
    ir.depthId = target.depthId;
    ir.texture = ITexture{ target.texture.id, target.texture.width, target.texture.height, target.texture.valid };
    gRenderer.UnloadRenderTexture(ir);
}

bool IsRenderTextureValid(RenderTexture2D target) {
    IRenderTexture ir;
    ir.id      = target.id;
    ir.depthId = target.depthId;
    ir.texture = ITexture{ target.texture.id, target.texture.width, target.texture.height, target.texture.valid };
    return gRenderer.isRenderTextureValid(ir);
}

Texture2D GetRenderTextureTexture(RenderTexture2D target) {
    IRenderTexture ir;
    ir.id      = target.id;
    ir.depthId = target.depthId;
    ir.texture = ITexture{ target.texture.id, target.texture.width, target.texture.height, target.texture.valid };
    ITexture it = gRenderer.GetRenderTextureTexture(ir);
    return Texture2D{ it.id, it.width, it.height, it.valid };
}

void BeginTextureMode(RenderTexture2D target) {
    EnsureInitialized();
    IRenderTexture ir;
    ir.id      = target.id;
    ir.depthId = target.depthId;
    ir.texture = ITexture{ target.texture.id, target.texture.width, target.texture.height, target.texture.valid };
    gRenderer.BeginTextureMode(ir);
}

void EndTextureMode() {
    EnsureInitialized();
    gRenderer.EndTextureMode();
}

Font LoadFont(const char* filePath, int fontSize) {
    EnsureInitialized();
    IFont iFont = gRenderer.LoadFont(filePath, fontSize);
    Font f;
    f.textureId  = 0;
    f.baseSize   = fontSize;
    f.valid      = iFont.id != 0;
    f.lineHeight = 0;
    f.ascent     = 0;
    f.descent    = 0;
    f.lineGap    = 0;
    static_assert(sizeof(f._rendererFontId) >= sizeof(uint32_t), "Font needs _rendererFontId field");
    f._rendererFontId = iFont.id;
    return f;
}

void UnloadFont(Font& font) {
    IFont iFont{ font._rendererFontId };
    gRenderer.UnloadFont(iFont);
    font = {};
}

Font GetDefaultFont() {
    EnsureInitialized();
    IFont iFont = gRenderer.LoadFont(nullptr, 32);
    Font f;
    f.valid           = iFont.id != 0;
    f._rendererFontId = iFont.id;
    f.baseSize        = 32;
    return f;
}

void DrawText(const char* text, int x, int y, int fontSize, Color color) {
    EnsureInitialized();
    gRenderer.DrawText(text, x, y, fontSize, color);
}

void DrawTextEx(Font font, const char* text, Vec2 position, float fontSize, float spacing, Color tint) {
    EnsureInitialized();
    IFont iFont{ font._rendererFontId };
    gRenderer.DrawTextEx(iFont, text, position, fontSize, spacing, tint);
}

Vec2 MeasureTextEx(Font font, const char* text, float fontSize, float spacing) {
    IFont iFont{ font._rendererFontId };
    return gRenderer.MeasureTextEx(iFont, text, fontSize, spacing);
}

int MeasureText(const char* text, int fontSize) {
    return gRenderer.MeasureText(text, fontSize);
}

Shader LoadShader(const char* vs, const char* fs) {
    EnsureInitialized();
    return gRenderer.LoadShader(vs, fs);
}

Shader LoadShaderFromMemory(const char* vs, const char* fs) {
    EnsureInitialized();
    return gRenderer.LoadShaderFromMemory(vs, fs);
}

void UnloadShader(Shader& shader)                                    { gRenderer.UnloadShader(shader); }
bool IsShaderValid(const Shader& shader)                             { return gRenderer.isShaderValid(const_cast<Shader&>(shader)); }
bool IsShaderReady(Shader shader)                                    { return IsShaderValid(shader); }

int  GetShaderLocation(const Shader& shader, const char* name)      { return gRenderer.GetShaderLocation(shader, name); }
int  GetShaderLocation(const Shader& shader, ShaderLocationIndex locIndex) { return gRenderer.GetShaderLocation(shader, locIndex); }
int  GetShaderAttributeLocation(const Shader& s, const char* name)  { return gRenderer.GetShaderAttributeLocation(s, name); }

void SetShaderValue(const Shader& s, int loc, float value)          { gRenderer.SetShaderValue(s, loc, value); }
void SetShaderValue(const Shader& s, int loc, int value)            { gRenderer.SetShaderValue(s, loc, value); }
void SetShaderValue(const Shader& s, int loc, const Vec2& value)    { gRenderer.SetShaderValue(s, loc, value); }
void SetShaderValue(const Shader& s, int loc, const Vec3& value)    {
    gRenderer.SetShaderValue(s, loc, value);
}
void SetShaderValue(const Shader& s, int loc, const Color& value) {
    Color v = value;
    gRenderer.SetShaderValue(s, loc, v);
}
void SetShaderValueMatrix(const Shader& s, int loc, const float* m) { gRenderer.SetShaderValueMatrix(s, loc, m); }
void SetShaderValueSampler(const Shader& s, int loc, int unit)      { gRenderer.SetShaderValueSampler(s, loc, unit); }

void BeginShaderMode(const Shader& shader) { gRenderer.BeginShaderMode(shader); }
void EndShaderMode()                       { gRenderer.EndShaderMode(); }

Camera2D CreateCamera2D() {
    Camera2D c{};
    c.zoom = 1.f;
    return c;
}

void BeginMode2D(const Camera2D& camera) { EnsureInitialized(); gRenderer.BeginMode2D(camera); }
void EndMode2D()                         { gRenderer.EndMode2D(); }

Camera2D GetCamera2D() {
    return gRenderer.GetCamera2D();
}

void UpdateCamera2D(Camera2D& camera, float targetX, float targetY, float smoothness) {
    smoothness = std::clamp(smoothness, 0.f, 1.f);
    camera.target.x = camera.target.x * (1.f - smoothness) + targetX * smoothness;
    camera.target.y = camera.target.y * (1.f - smoothness) + targetY * smoothness;
}

Camera3D CreateCamera3D() {
    Camera3D c{};
    c.position = {0.f, 0.f, 10.f};
    c.target   = {0.f, 0.f,  0.f};
    c.up       = {0.f, 1.f,  0.f};
    return c;
}

void BeginMode3D(const Camera3D& camera) { EnsureInitialized(); gRenderer.BeginMode3D(camera); }
void EndMode3D()                         { gRenderer.EndMode3D(); }

void PushMatrix()                        { gRenderer.PushMatrix(); }
void PopMatrix()                         { gRenderer.PopMatrix(); }
void Translate(const Vec3& t)            { gRenderer.Translate(t); }
void Translate(float x, float y, float z){ gRenderer.Translate(Vec3{x,y,z}); }
void Rotate(float angle, const Vec3& ax) { gRenderer.Rotate(angle, ax); }
void Rotate(float angle)                 { gRenderer.Rotate(angle, Vec3{0,0,1}); }
void Scale(const Vec3& s)                { gRenderer.Scale(s); }
void Scale(float s)                      { gRenderer.Scale(Vec3{s,s,s}); }
void MultMatrix(const Mat4& m)           { gRenderer.MultMatrix(m); }
void EnableBackfaceCulling()             { gRenderer.EnableBackfaceCulling(); }
void DisableBackfaceCulling()            { gRenderer.DisableBackfaceCulling(); }
const float* GetMatrixModelview()        { return gRenderer.GetMatrixModelview(); }
const float* GetMatrixProjection()       { return gRenderer.GetMatrixProjection(); }

void Set3DView(const Mat4& view, const Mat4& proj) { gRenderer.Set3DView(view, proj); }

void DrawLine3D(Vec3 start, Vec3 end, Color c)           { gRenderer.DrawLine3D(start, end, c); }
void DrawGrid(int slices, float spacing)                  { gRenderer.DrawGrid(slices, spacing); }
void DrawPlane(Vec3 center, Vec2 size, Color c)           { gRenderer.DrawPlane(center, size, c); }

void DrawCube(Vec3 pos, float w, float h, float l, Color c)       { gRenderer.DrawCube(pos, w, h, l, c); }
void DrawCubeV(Vec3 pos, Vec3 size, Color c)                       { gRenderer.DrawCubeV(pos, size, c); }
void DrawCubeWires(Vec3 pos, float w, float h, float l, Color c)   { gRenderer.DrawCubeWires(pos, w, h, l, c); }
void DrawCubeWiresV(Vec3 pos, Vec3 size, Color c)                  { gRenderer.DrawCubeWiresV(pos, size, c); }

void DrawSphere(Vec3 center, float radius, Color c)                          { gRenderer.DrawSphere(center, radius, c); }
void DrawSphereEx(Vec3 center, float radius, int rings, int slices, Color c) { gRenderer.DrawSphereEx(center, radius, rings, slices, c); }
void DrawSphereWires(Vec3 center, float radius, int rings, int slices, Color c){ gRenderer.DrawSphereWires(center, radius, rings, slices, c); }

void DrawCylinder(Vec3 pos, float rTop, float rBot, float h, int slices, Color c)      { gRenderer.DrawCylinder(pos, rTop, rBot, h, slices, c); }
void DrawCylinderEx(Vec3 s, Vec3 e, float rS, float rE, int sides, Color c)             { gRenderer.DrawCylinderEx(s, e, rS, rE, sides, c); }
void DrawCylinderWires(Vec3 pos, float rTop, float rBot, float h, int slices, Color c)  { gRenderer.DrawCylinderWires(pos, rTop, rBot, h, slices, c); }
void DrawCylinderWiresEx(Vec3 s, Vec3 e, float rS, float rE, int slices, Color c)       { gRenderer.DrawCylinderWiresEx(s, e, rS, rE, slices, c); }

Model LoadModel(const char* filePath) {
    EnsureInitialized();
    return gRenderer.LoadModel(filePath);
}

void UnloadModel(Model& model)  { gRenderer.UnloadModel(model); }

void DrawModel(const Model& model, const Vec3& position, float scale,
               const Vec3& rotationAxis, float rotationAngle, Color tint) {
    Mat4 transform = BuildTransform(position, rotationAxis, rotationAngle, Vec3{scale, scale, scale});
    gRenderer.DrawModelEx(model, transform);
}

void DrawModelEx(const Model& model, const Vec3& position, const Vec3& rotationAxis,
                 float rotationAngle, const Vec3& scale, Color tint) {
    Mat4 transform = BuildTransform(position, rotationAxis, rotationAngle, scale);
    gRenderer.DrawModelEx(model, transform);
}

void DrawModelEx(const Model& model, const Mat4& transform) {
    gRenderer.DrawModelEx(model, transform);
}

void DrawModelWires(const Model& model, const Vec3& position, float scale,
                    const Vec3& rotationAxis, float rotationAngle, Color tint) {
    Mat4 transform = BuildTransform(position, rotationAxis, rotationAngle, Vec3{scale, scale, scale});
    DrawModelWireframe(model, transform, tint);
}

void DrawModelWiresEx(const Model& model, const Vec3& position, const Vec3& rotationAxis,
                      float rotationAngle, const Vec3& scale, Color tint) {
    Mat4 transform = BuildTransform(position, rotationAxis, rotationAngle, scale);
    DrawModelWireframe(model, transform, tint);
}

void DrawBoundingBox(BoundingBox box, Color color) {
    Vec3 vertices[8] = {
        {box.min.x, box.min.y, box.min.z},
        {box.max.x, box.min.y, box.min.z},
        {box.max.x, box.max.y, box.min.z},
        {box.min.x, box.max.y, box.min.z},
        {box.min.x, box.min.y, box.max.z},
        {box.max.x, box.min.y, box.max.z},
        {box.max.x, box.max.y, box.max.z},
        {box.min.x, box.max.y, box.max.z}
    };

    gRenderer.DrawLine3D(vertices[0], vertices[1], color);
    gRenderer.DrawLine3D(vertices[1], vertices[2], color);
    gRenderer.DrawLine3D(vertices[2], vertices[3], color);
    gRenderer.DrawLine3D(vertices[3], vertices[0], color);

    gRenderer.DrawLine3D(vertices[4], vertices[5], color);
    gRenderer.DrawLine3D(vertices[5], vertices[6], color);
    gRenderer.DrawLine3D(vertices[6], vertices[7], color);
    gRenderer.DrawLine3D(vertices[7], vertices[4], color);

    gRenderer.DrawLine3D(vertices[0], vertices[4], color);
    gRenderer.DrawLine3D(vertices[1], vertices[5], color);
    gRenderer.DrawLine3D(vertices[2], vertices[6], color);
    gRenderer.DrawLine3D(vertices[3], vertices[7], color);
}

void DrawBillboard(const Camera3D& camera, Texture2D texture, Vec3 position, float scale, Color tint) {
    Rectangle source{0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)};
    Vec2 size{static_cast<float>(texture.width) * scale, static_cast<float>(texture.height) * scale};
    DrawBillboardRec(camera, texture, source, position, size, tint);
}

void DrawBillboardRec(const Camera3D& camera, Texture2D texture, Rectangle source,
                      Vec3 position, Vec2 size, Color tint) {
    DrawBillboardPro(camera, texture, source, position, camera.up, size, Vec2{0.5f, 0.5f}, 0.0f, tint);
}

void DrawBillboardPro(const Camera3D& camera, Texture2D texture, Rectangle source,
                      Vec3 position, Vec3 up, Vec2 size, Vec2 origin, float rotation, Color tint) {
    Vec3 screenPos = GetWorldToScreen(position, camera);
    Vec3 screenUp = GetWorldToScreen(position + up, camera);
    float angle = std::atan2(screenUp.y - screenPos.y, screenUp.x - screenPos.x) * (180.0f / 3.14159265359f);
    angle += rotation;

    Rectangle dest{
        screenPos.x - size.x * origin.x,
        screenPos.y - size.y * origin.y,
        size.x,
        size.y
    };
    Vec2 originPixels{size.x * origin.x, size.y * origin.y};
    DrawTexturePro(texture, source, dest, originPixels, angle, tint);
}

Vec2 GetWorldToScreen2D(Vec2 position, Camera2D camera) {
    float dx = position.x - camera.target.x;
    float dy = position.y - camera.target.y;
    float cosA = std::cos(camera.rotation * 3.14159265359f / 180.f);
    float sinA = std::sin(camera.rotation * 3.14159265359f / 180.f);
    return Vec2{
        camera.offset.x + (dx * cosA - dy * sinA) * camera.zoom,
        camera.offset.y + (dx * sinA + dy * cosA) * camera.zoom
    };
}

Vec2 GetScreenToWorld2D(Vec2 position, Camera2D camera) {
    float cosA  = std::cos(camera.rotation * 3.14159265359f / 180.f);
    float sinA  = std::sin(camera.rotation * 3.14159265359f / 180.f);
    float scale = camera.zoom;
    float tx    = camera.offset.x - camera.target.x * scale * cosA - camera.target.y * scale * sinA;
    float ty    = camera.offset.y + camera.target.x * scale * sinA - camera.target.y * scale * cosA;
    float x     = (position.x - tx) * scale * cosA - (position.y - ty) * scale * sinA;
    float y     = (position.x - tx) * scale * sinA + (position.y - ty) * scale * cosA;
    return Vec2{ camera.target.x + x / (scale * scale),
                 camera.target.y + y / (scale * scale) };
}

Vec3 GetWorldToScreen(Vec3 position, Camera3D camera) {
    Vec3 forward = (camera.target - camera.position).normalized();
    Vec3 right   = forward.cross(camera.up).normalized();
    Vec3 up      = right.cross(forward);
    Vec3 rel     = position - camera.position;
    float cX = rel.dot(right), cY = rel.dot(up), cZ = rel.dot(forward);

    float sw = static_cast<float>(GetScreenWidth());
    float sh = static_cast<float>(GetScreenHeight());
    if (sw <= 0.f || sh <= 0.f) return Vec3{0.f, 0.f, cZ};

    float aspect    = sw / sh;
    float fovRad    = camera.fovy * 3.14159265359f / 180.f;
    float halfH     = std::tan(fovRad * .5f);
    float halfW     = halfH * aspect;

    float ndcX, ndcY;
    if (camera.projection == CAMERA_ORTHOGRAPHIC) {
        float os = camera.fovy > 0.f ? camera.fovy : 1.f;
        ndcX = cX / (os * aspect);
        ndcY = cY / os;
    } else {
        if (cZ == 0.f) cZ = 1e-6f;
        ndcX = cX / (cZ * halfW);
        ndcY = cY / (cZ * halfH);
    }
    return Vec3{ (ndcX * .5f + .5f) * sw, (.5f - ndcY * .5f) * sh, cZ };
}

Ray GetScreenToWorldRay(Vec2 mouse, Camera3D camera) {
    Ray ray;
    ray.origin = camera.position;
    Vec3 forward = (camera.target - camera.position).normalized();
    Vec3 right   = forward.cross(camera.up).normalized();
    Vec3 up      = right.cross(forward);

    float sw = static_cast<float>(GetScreenWidth());
    float sh = static_cast<float>(GetScreenHeight());
    float aspect  = sw / sh;
    float fovRad  = camera.fovy * 3.14159265359f / 180.f;
    float fovH    = 2.f * std::tan(fovRad / 2.f);
    float fovW    = fovH * aspect;
    float x       = (mouse.x / sw - .5f) * fovW;
    float y       = (.5f - mouse.y / sh) * fovH;

    ray.direction = Vec3{
        forward.x + right.x * x + up.x * y,
        forward.y + right.y * x + up.y * y,
        forward.z + right.z * x + up.z * y
    }.normalized();
    return ray;
}

bool CheckCollisionRecs(Rectangle a, Rectangle b) {
    return !(a.x + a.width < b.x || b.x + b.width < a.x ||
             a.y + a.height < b.y || b.y + b.height < a.y);
}

bool CheckCollisionCircles(Vec2 c1, float r1, Vec2 c2, float r2) {
    float dx = c2.x - c1.x, dy = c2.y - c1.y;
    return std::sqrt(dx*dx + dy*dy) < (r1 + r2);
}

bool CheckCollisionPointRec(Vec2 point, Rectangle rect) {
    return point.x >= rect.x && point.x <= rect.x + rect.width &&
           point.y >= rect.y && point.y <= rect.y + rect.height;
}

bool CheckCollisionPointCircle(Vec2 point, Vec2 center, float radius) {
    float dx = point.x - center.x, dy = point.y - center.y;
    return std::sqrt(dx*dx + dy*dy) <= radius;
}

Color Fade(Color color, float alpha)           { color.a = static_cast<unsigned char>(color.a * alpha); return color; }
Color ColorAlpha(Color color, float alpha)     { color.a = static_cast<unsigned char>(255.f * alpha);   return color; }

Color ColorTint(Color color, Color tint) {
    return Color{
        static_cast<unsigned char>((color.r / 255.f) * (tint.r / 255.f) * 255.f),
        static_cast<unsigned char>((color.g / 255.f) * (tint.g / 255.f) * 255.f),
        static_cast<unsigned char>((color.b / 255.f) * (tint.b / 255.f) * 255.f),
        color.a
    };
}

Color ColorBrightness(Color color, float factor) {
    return Color{
        static_cast<unsigned char>(std::clamp(color.r * factor, 0.f, 255.f)),
        static_cast<unsigned char>(std::clamp(color.g * factor, 0.f, 255.f)),
        static_cast<unsigned char>(std::clamp(color.b * factor, 0.f, 255.f)),
        color.a
    };
}

Color ColorContrast(Color color, float contrast) {
    auto channel = [&](unsigned char ch) -> unsigned char {
        float v = (ch / 255.f - .5f) * contrast + .5f;
        return static_cast<unsigned char>(std::clamp(v * 255.f, 0.f, 255.f));
    };
    return Color{ channel(color.r), channel(color.g), channel(color.b), color.a };
}

Color GetColor(unsigned int hex) {
    return Color{
        static_cast<unsigned char>((hex >> 16) & 0xFF),
        static_cast<unsigned char>((hex >>  8) & 0xFF),
        static_cast<unsigned char>( hex        & 0xFF),
        255
    };
}

void WaitTime(double seconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(seconds * 1000.0)));
}

int  GetRandomValue(int min, int max) {
    if (min > max) std::swap(min, max);
    return min + (std::rand() % (max - min + 1));
}

void SetRandomSeed(unsigned int seed) { std::srand(seed); }

} // namespace qc