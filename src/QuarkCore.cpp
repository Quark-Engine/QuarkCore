#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/Quark3D.hpp"
#include "Renderer/QuarkIRenderer.hpp"
#include "Renderer/QuarkGLRenderer.hpp"
#include "Renderer/QuarkVulkan/QuarkVkRenderer.hpp"
#include "QuarkInternal.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
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

QuarkGLRenderer gGLRenderer;
QuarkVkRenderer gVkRenderer;
IRenderer* gRendererPtr = nullptr;

#define gRenderer (*gRendererPtr)

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

void InitWindow(int width, int height, const char* title, RendererType rendererType) {
    TraceLog(LogLevel::Info, "WINDOW", TextFormat("Starting window creation: %s (%dx%d)", title ? title : "Quark", width, height));

    int version = SDL_GetVersion();

    WriteLog(LogLevel::Info, "CORE", "SDL Version: " + std::to_string(SDL_VERSIONNUM_MAJOR(version)) + "." +
                                                       std::to_string(SDL_VERSIONNUM_MINOR(version)) + "." +
                                                       std::to_string(SDL_VERSIONNUM_MICRO(version)));

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());

    if (rendererType == RendererType::OpenGL) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

        TraceLog(LogLevel::Info, "RENDERER", "Backend selected: OpenGL");

        gWin.window = SDL_CreateWindow(
            title,
            width,
            height,
            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
        );

        if (!gWin.window)
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

        gRendererPtr = &gGLRenderer;
        gRenderer.Init(gWin.window, width, height);
        gRenderer.SetTargetFPS(gWin.targetFps);
    }
    else if (rendererType == RendererType::Vulkan) {
        TraceLog(LogLevel::Info, "RENDERER", "Backend selected: Vulkan");

        if (!SDL_Vulkan_LoadLibrary(nullptr))
            throw std::runtime_error(std::string("SDL_Vulkan_LoadLibrary failed: ") + SDL_GetError());

        gWin.window = SDL_CreateWindow(
            title,
            width,
            height,
            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
        );

        if (!gWin.window)
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

        gRendererPtr = &gVkRenderer;
        gRenderer.Init(gWin.window, width, height);
        gRenderer.SetTargetFPS(gWin.targetFps);
    }

    if (!gWin.window)
        throw std::runtime_error(std::string("Window is null after init"));

    WriteLog(LogLevel::Info, "WINDOW", "Window created: " + std::string(title ? title : ""));
}

void CloseWindow() {
    if (gRendererPtr) {
        gRenderer.Shutdown();
        gRendererPtr = nullptr;
    }
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

    if (gRendererPtr && gRenderer.ShouldClose()) gWin.shouldClose = true;
    return gWin.shouldClose;
}

bool IsWindowReady() {
    return gWin.window != nullptr;
}

int GetScreenWidth()  { return gRendererPtr ? gRenderer.GetScreenWidth() : 0; }
int GetScreenHeight() { return gRendererPtr ? gRenderer.GetScreenHeight() : 0; }

void SetTargetFPS(int fps) {
    gWin.targetFps = fps;
    if (gRendererPtr) gRenderer.SetTargetFPS(fps);
}

float GetFrameTime()  { return gRendererPtr ? gRenderer.GetFrameTime() : 0.0f; }
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

void DrawModel(Model model, Vec3 position, float scale, Color tint) {
    Mat4 transform = BuildTransform(position, Vec3{0.0f, 1.0f, 0.0f}, 0.0f, Vec3{scale, scale, scale});
    gRenderer.DrawModelEx(model, transform, tint);
}

void DrawModelEx(Model model, Vec3 position, Vec3 rotationAxis,
                 float rotationAngle, Vec3 scale, Color tint) {
    Mat4 transform = BuildTransform(position, rotationAxis, rotationAngle, scale);
    gRenderer.DrawModelEx(model, transform, tint);
}

void DrawModelEx(Model model, const Mat4& transform) {
    gRenderer.DrawModelEx(model, transform);
}

void DrawModelWires(Model model, Vec3 position, float scale, Color tint) {
    Mat4 transform = BuildTransform(position, Vec3{0.0f, 0.0f, 1.0f}, 0.0f, Vec3{scale, scale, scale});
    DrawModelWireframe(model, transform, tint);
}

void DrawModelWiresEx(Model model, Vec3 position, Vec3 rotationAxis,
                      float rotationAngle, Vec3 scale, Color tint) {
    Mat4 transform = BuildTransform(position, rotationAxis, rotationAngle, scale);
    DrawModelWireframe(model, transform, tint);
}

namespace {

static Mesh CreateMesh(int vertexCount, int triangleCount) {
    Mesh mesh{};
    if (vertexCount <= 0) return mesh;
    mesh.vertexCount = vertexCount;
    mesh.triangleCount = triangleCount;
    mesh.vertices = new float[vertexCount * 3];
    mesh.normals = new float[vertexCount * 3];
    mesh.texcoords = new float[vertexCount * 2];
    mesh.indices = new unsigned short[triangleCount * 3];
    std::fill(mesh.vertices, mesh.vertices + vertexCount * 3, 0.0f);
    std::fill(mesh.normals, mesh.normals + vertexCount * 3, 0.0f);
    std::fill(mesh.texcoords, mesh.texcoords + vertexCount * 2, 0.0f);
    std::fill(mesh.indices, mesh.indices + triangleCount * 3, 0);
    return mesh;
}

static void FreeMeshCpuData(Mesh& mesh) {
    delete[] mesh.vertices; mesh.vertices = nullptr;
    delete[] mesh.texcoords; mesh.texcoords = nullptr;
    delete[] mesh.texcoords2; mesh.texcoords2 = nullptr;
    delete[] mesh.normals; mesh.normals = nullptr;
    delete[] mesh.tangents; mesh.tangents = nullptr;
    delete[] mesh.colors; mesh.colors = nullptr;
    delete[] mesh.indices; mesh.indices = nullptr;
    delete[] mesh.boneIndices; mesh.boneIndices = nullptr;
    delete[] mesh.boneWeights; mesh.boneWeights = nullptr;
    delete[] mesh.animVertices; mesh.animVertices = nullptr;
    delete[] mesh.animNormals; mesh.animNormals = nullptr;
}

static void WriteObjFace(std::ofstream& out, int a, int b, int c, bool hasUV, bool hasNormal) {
    out << "f " << a;
    if (hasUV || hasNormal) out << "/";
    if (hasUV) out << a;
    if (hasNormal) out << "/" << a;
    out << " " << b;
    if (hasUV || hasNormal) out << "/";
    if (hasUV) out << b;
    if (hasNormal) out << "/" << b;
    out << " " << c;
    if (hasUV || hasNormal) out << "/";
    if (hasUV) out << c;
    if (hasNormal) out << "/" << c;
    out << "\n";
}

static Vec3 CalculateTriangleTangent(const Vec3& p0, const Vec3& p1, const Vec3& p2,
                                     const Vec2& uv0, const Vec2& uv1, const Vec2& uv2) {
    Vec3 edge1 = p1 - p0;
    Vec3 edge2 = p2 - p0;
    Vec2 deltaUV1{uv1.x - uv0.x, uv1.y - uv0.y};
    Vec2 deltaUV2{uv2.x - uv0.x, uv2.y - uv0.y};
    float denom = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
    if (std::fabs(denom) < EPSILON) return Vec3{1.0f, 0.0f, 0.0f};
    float inv = 1.0f / denom;
    Vec3 tangent{
        inv * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x),
        inv * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y),
        inv * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z)
    };
    return tangent.normalized();
}

static Vec3 GetHeightSample(const Image& heightmap, int x, int y) {
    if (!heightmap.data || x < 0 || y < 0 || x >= heightmap.width || y >= heightmap.height) return Vec3{0,0,0};
    int stride = heightmap.channels > 0 ? heightmap.channels : 1;
    int idx = (y * heightmap.width + x) * stride;
    float value = static_cast<float>(heightmap.data[idx]) / 255.0f;
    return Vec3{value, value, value};
}

} // namespace

void UploadMesh(Mesh* mesh, bool dynamic) {
    if (!mesh) return;
    gRenderer.UploadMesh(*mesh, dynamic);
}

void UpdateMeshBuffer(Mesh mesh, int index, const void* data, int dataSize, int offset) {
    gRenderer.UpdateMeshBuffer(mesh, index, data, dataSize, offset);
}

void UnloadMesh(Mesh mesh) {
    gRenderer.UnloadMesh(mesh);
}

void DrawMesh(Mesh mesh, Material material, Matrix transform) {
    gRenderer.DrawMesh(mesh, material, transform);
}

void DrawMeshInstanced(Mesh mesh, Material material, const Matrix* transforms, int instances) {
    gRenderer.DrawMeshInstanced(mesh, material, transforms, instances);
}

BoundingBox GetMeshBoundingBox(Mesh mesh) {
    BoundingBox box{};
    if (!mesh.vertices || mesh.vertexCount <= 0) return box;

    box.min = Vec3{mesh.vertices[0], mesh.vertices[1], mesh.vertices[2]};
    box.max = box.min;
    for (int i = 1; i < mesh.vertexCount; ++i) {
        Vec3 p{mesh.vertices[i * 3 + 0], mesh.vertices[i * 3 + 1], mesh.vertices[i * 3 + 2]};
        box.min.x = std::min(box.min.x, p.x);
        box.min.y = std::min(box.min.y, p.y);
        box.min.z = std::min(box.min.z, p.z);
        box.max.x = std::max(box.max.x, p.x);
        box.max.y = std::max(box.max.y, p.y);
        box.max.z = std::max(box.max.z, p.z);
    }
    return box;
}

void GenMeshTangents(Mesh* mesh) {
    if (!mesh || !mesh->vertices || !mesh->normals || !mesh->texcoords || mesh->vertexCount <= 0) return;
    if (!mesh->tangents) mesh->tangents = new float[mesh->vertexCount * 3];
    std::fill(mesh->tangents, mesh->tangents + mesh->vertexCount * 3, 0.0f);

    if (!mesh->indices) return;

    for (int t = 0; t < mesh->triangleCount; ++t) {
        int i0 = mesh->indices[t * 3 + 0];
        int i1 = mesh->indices[t * 3 + 1];
        int i2 = mesh->indices[t * 3 + 2];
        if (i0 >= mesh->vertexCount || i1 >= mesh->vertexCount || i2 >= mesh->vertexCount) continue;

        Vec3 p0{mesh->vertices[i0 * 3 + 0], mesh->vertices[i0 * 3 + 1], mesh->vertices[i0 * 3 + 2]};
        Vec3 p1{mesh->vertices[i1 * 3 + 0], mesh->vertices[i1 * 3 + 1], mesh->vertices[i1 * 3 + 2]};
        Vec3 p2{mesh->vertices[i2 * 3 + 0], mesh->vertices[i2 * 3 + 1], mesh->vertices[i2 * 3 + 2]};
        Vec2 uv0{mesh->texcoords[i0 * 2 + 0], mesh->texcoords[i0 * 2 + 1]};
        Vec2 uv1{mesh->texcoords[i1 * 2 + 0], mesh->texcoords[i1 * 2 + 1]};
        Vec2 uv2{mesh->texcoords[i2 * 2 + 0], mesh->texcoords[i2 * 2 + 1]};

        Vec3 tangent = CalculateTriangleTangent(p0, p1, p2, uv0, uv1, uv2);
        for (int idx : {i0, i1, i2}) {
            mesh->tangents[idx * 3 + 0] += tangent.x;
            mesh->tangents[idx * 3 + 1] += tangent.y;
            mesh->tangents[idx * 3 + 2] += tangent.z;
        }
    }

    for (int i = 0; i < mesh->vertexCount; ++i) {
        Vec3 t{mesh->tangents[i * 3 + 0], mesh->tangents[i * 3 + 1], mesh->tangents[i * 3 + 2]};
        Vec3 n{mesh->normals[i * 3 + 0], mesh->normals[i * 3 + 1], mesh->normals[i * 3 + 2]};
        Vec3 tangent = t.normalized();
        if (tangent.length() > 0.0f) {
            mesh->tangents[i * 3 + 0] = tangent.x;
            mesh->tangents[i * 3 + 1] = tangent.y;
            mesh->tangents[i * 3 + 2] = tangent.z;
        } else {
            mesh->tangents[i * 3 + 0] = 1.0f;
            mesh->tangents[i * 3 + 1] = 0.0f;
            mesh->tangents[i * 3 + 2] = 0.0f;
        }
    }
}

bool ExportMesh(Mesh mesh, const char* fileName) {
    if (!fileName || !mesh.vertices || mesh.vertexCount <= 0) return false;
    std::ofstream out(fileName, std::ios::binary);
    if (!out) return false;

    bool hasUV = mesh.texcoords != nullptr;
    bool hasNormal = mesh.normals != nullptr;

    out << "# Generated by QuarkCore\n";
    for (int i = 0; i < mesh.vertexCount; ++i) {
        out << "v " << mesh.vertices[i * 3 + 0] << " "
            << mesh.vertices[i * 3 + 1] << " "
            << mesh.vertices[i * 3 + 2] << "\n";
    }
    if (hasUV) {
        for (int i = 0; i < mesh.vertexCount; ++i) {
            out << "vt " << mesh.texcoords[i * 2 + 0] << " "
                << mesh.texcoords[i * 2 + 1] << "\n";
        }
    }
    if (hasNormal) {
        for (int i = 0; i < mesh.vertexCount; ++i) {
            out << "vn " << mesh.normals[i * 3 + 0] << " "
                << mesh.normals[i * 3 + 1] << " "
                << mesh.normals[i * 3 + 2] << "\n";
        }
    }

    if (mesh.indices && mesh.triangleCount > 0) {
        for (int t = 0; t < mesh.triangleCount; ++t) {
            int a = mesh.indices[t * 3 + 0] + 1;
            int b = mesh.indices[t * 3 + 1] + 1;
            int c = mesh.indices[t * 3 + 2] + 1;
            if (hasUV && hasNormal) {
                out << "f " << a << "/" << a << "/" << a << " "
                    << b << "/" << b << "/" << b << " "
                    << c << "/" << c << "/" << c << "\n";
            } else if (hasUV) {
                out << "f " << a << "/" << a << " "
                    << b << "/" << b << " "
                    << c << "/" << c << "\n";
            } else if (hasNormal) {
                out << "f " << a << "//" << a << " "
                    << b << "//" << b << " "
                    << c << "//" << c << "\n";
            } else {
                out << "f " << a << " " << b << " " << c << "\n";
            }
        }
    } else {
        for (int i = 0; i + 2 < mesh.vertexCount; i += 3) {
            out << "f " << (i + 1) << " " << (i + 2) << " " << (i + 3) << "\n";
        }
    }

    return true;
}

bool ExportMeshAsCode(Mesh mesh, const char* fileName) {
    if (!fileName || !mesh.vertices || mesh.vertexCount <= 0) return false;
    std::ofstream out(fileName, std::ios::binary);
    if (!out) return false;

    out << "#include \"QuarkCore/Quark3D.hpp\"\n\n";
    out << "static float meshVertices[] = {\n";
    for (int i = 0; i < mesh.vertexCount; ++i) {
        out << "    " << mesh.vertices[i * 3 + 0] << "f, "
            << mesh.vertices[i * 3 + 1] << "f, "
            << mesh.vertices[i * 3 + 2] << "f,\n";
    }
    out << "};\n\n";

    if (mesh.normals) {
        out << "static float meshNormals[] = {\n";
        for (int i = 0; i < mesh.vertexCount; ++i) {
            out << "    " << mesh.normals[i * 3 + 0] << "f, "
                << mesh.normals[i * 3 + 1] << "f, "
                << mesh.normals[i * 3 + 2] << "f,\n";
        }
        out << "};\n\n";
    }

    if (mesh.texcoords) {
        out << "static float meshTexcoords[] = {\n";
        for (int i = 0; i < mesh.vertexCount; ++i) {
            out << "    " << mesh.texcoords[i * 2 + 0] << "f, "
                << mesh.texcoords[i * 2 + 1] << "f,\n";
        }
        out << "};\n\n";
    }

    if (mesh.indices && mesh.triangleCount > 0) {
        out << "static unsigned short meshIndices[] = {\n";
        for (int i = 0; i < mesh.triangleCount * 3; ++i) {
            out << "    " << mesh.indices[i] << ",\n";
        }
        out << "};\n\n";
    }

    out << "Mesh mesh = {};\n";
    out << "mesh.vertexCount = " << mesh.vertexCount << ";\n";
    out << "mesh.triangleCount = " << mesh.triangleCount << ";\n";
    out << "mesh.vertices = meshVertices;\n";
    if (mesh.normals) out << "mesh.normals = meshNormals;\n";
    if (mesh.texcoords) out << "mesh.texcoords = meshTexcoords;\n";
    if (mesh.indices && mesh.triangleCount > 0) out << "mesh.indices = meshIndices;\n";

    return true;
}

Mesh GenMeshPoly(int sides, float radius) {
    if (sides < 3) return Mesh{};
    int vertexCount = sides + 1;
    int triangleCount = sides - 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);
    float angleStep = 2.0f * PI / sides;

    mesh.vertices[0] = 0.0f;
    mesh.vertices[1] = 0.0f;
    mesh.vertices[2] = 0.0f;
    mesh.normals[0] = 0.0f; mesh.normals[1] = 0.0f; mesh.normals[2] = 1.0f;
    mesh.texcoords[0] = 0.5f; mesh.texcoords[1] = 0.5f;

    for (int i = 0; i < sides; ++i) {
        float angle = i * angleStep;
        float x = std::cos(angle) * radius;
        float y = std::sin(angle) * radius;
        mesh.vertices[(i + 1) * 3 + 0] = x;
        mesh.vertices[(i + 1) * 3 + 1] = y;
        mesh.vertices[(i + 1) * 3 + 2] = 0.0f;
        mesh.normals[(i + 1) * 3 + 0] = 0.0f;
        mesh.normals[(i + 1) * 3 + 1] = 0.0f;
        mesh.normals[(i + 1) * 3 + 2] = 1.0f;
        mesh.texcoords[(i + 1) * 2 + 0] = x / (radius * 2.0f) + 0.5f;
        mesh.texcoords[(i + 1) * 2 + 1] = y / (radius * 2.0f) + 0.5f;
    }

    for (int i = 0; i < triangleCount; ++i) {
        mesh.indices[i * 3 + 0] = (unsigned short)0;
        mesh.indices[i * 3 + 1] = (unsigned short)(i + 1);
        mesh.indices[i * 3 + 2] = (unsigned short)(i + 2);
    }

    return mesh;
}

Mesh GenMeshPlane(float width, float length, int resX, int resZ) {
    if (width <= 0.0f || length <= 0.0f || resX <= 0 || resZ <= 0) return Mesh{};
    int vertsX = resX + 1;
    int vertsZ = resZ + 1;
    int vertexCount = vertsX * vertsZ;
    int triangleCount = resX * resZ * 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    for (int z = 0; z < vertsZ; ++z) {
        for (int x = 0; x < vertsX; ++x) {
            int index = z * vertsX + x;
            float fx = ((float)x / resX - 0.5f) * width;
            float fz = ((float)z / resZ - 0.5f) * length;
            mesh.vertices[index * 3 + 0] = fx;
            mesh.vertices[index * 3 + 1] = 0.0f;
            mesh.vertices[index * 3 + 2] = fz;
            mesh.normals[index * 3 + 0] = 0.0f;
            mesh.normals[index * 3 + 1] = 1.0f;
            mesh.normals[index * 3 + 2] = 0.0f;
            mesh.texcoords[index * 2 + 0] = (float)x / resX;
            mesh.texcoords[index * 2 + 1] = (float)z / resZ;
        }
    }

    int idx = 0;
    for (int z = 0; z < resZ; ++z) {
        for (int x = 0; x < resX; ++x) {
            int a = z * vertsX + x;
            int b = a + 1;
            int c = a + vertsX;
            int d = c + 1;
            mesh.indices[idx++] = (unsigned short)a;
            mesh.indices[idx++] = (unsigned short)c;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)c;
            mesh.indices[idx++] = (unsigned short)d;
        }
    }

    return mesh;
}

Mesh GenMeshCube(float width, float height, float length) {
    float hw = width * 0.5f;
    float hh = height * 0.5f;
    float hl = length * 0.5f;
    Mesh mesh = CreateMesh(24, 12);
    const Vec3 positions[24] = {
        {-hw, -hh,  hl}, { hw, -hh,  hl}, { hw,  hh,  hl}, {-hw,  hh,  hl},
        {-hw, -hh, -hl}, {-hw,  hh, -hl}, { hw,  hh, -hl}, { hw, -hh, -hl},
        {-hw,  hh, -hl}, {-hw,  hh,  hl}, { hw,  hh,  hl}, { hw,  hh, -hl},
        {-hw, -hh, -hl}, { hw, -hh, -hl}, { hw, -hh,  hl}, {-hw, -hh,  hl},
        { hw, -hh, -hl}, { hw,  hh, -hl}, { hw,  hh,  hl}, { hw, -hh,  hl},
        {-hw, -hh, -hl}, {-hw, -hh,  hl}, {-hw,  hh,  hl}, {-hw,  hh, -hl}
    };
    const Vec3 normals[24] = {
        {0,0,1},{0,0,1},{0,0,1},{0,0,1},
        {0,0,-1},{0,0,-1},{0,0,-1},{0,0,-1},
        {0,1,0},{0,1,0},{0,1,0},{0,1,0},
        {0,-1,0},{0,-1,0},{0,-1,0},{0,-1,0},
        {1,0,0},{1,0,0},{1,0,0},{1,0,0},
        {-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0}
    };
    const Vec2 uvs[24] = {
        {0,0},{1,0},{1,1},{0,1}, {0,0},{1,0},{1,1},{0,1},
        {0,0},{1,0},{1,1},{0,1}, {0,0},{1,0},{1,1},{0,1},
        {0,0},{1,0},{1,1},{0,1}, {0,0},{1,0},{1,1},{0,1}
    };
    for (int i = 0; i < 24; ++i) {
        mesh.vertices[i * 3 + 0] = positions[i].x;
        mesh.vertices[i * 3 + 1] = positions[i].y;
        mesh.vertices[i * 3 + 2] = positions[i].z;
        mesh.normals[i * 3 + 0] = normals[i].x;
        mesh.normals[i * 3 + 1] = normals[i].y;
        mesh.normals[i * 3 + 2] = normals[i].z;
        mesh.texcoords[i * 2 + 0] = uvs[i].x;
        mesh.texcoords[i * 2 + 1] = uvs[i].y;
    }
    const unsigned short indices[] = {
         0,  1,  2,  0,  2,  3,
         4,  5,  6,  4,  6,  7,
         8,  9, 10,  8, 10, 11,
        12, 13, 14, 12, 14, 15,
        16, 17, 18, 16, 18, 19,
        20, 21, 22, 20, 22, 23
    };
    std::copy(std::begin(indices), std::end(indices), mesh.indices);
    return mesh;
}

Mesh GenMeshSphere(float radius, int rings, int slices) {
    if (radius <= 0.0f || rings < 2 || slices < 3) return Mesh{};
    int vertexCount = (rings + 1) * (slices + 1);
    int triangleCount = rings * slices * 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    int v = 0;
    for (int r = 0; r <= rings; ++r) {
        float phi = PI * r / rings;
        for (int s = 0; s <= slices; ++s) {
            float theta = 2.0f * PI * s / slices;
            float x = std::sin(phi) * std::cos(theta);
            float y = std::cos(phi);
            float z = std::sin(phi) * std::sin(theta);
            mesh.vertices[v * 3 + 0] = x * radius;
            mesh.vertices[v * 3 + 1] = y * radius;
            mesh.vertices[v * 3 + 2] = z * radius;
            mesh.normals[v * 3 + 0] = x;
            mesh.normals[v * 3 + 1] = y;
            mesh.normals[v * 3 + 2] = z;
            mesh.texcoords[v * 2 + 0] = (float)s / slices;
            mesh.texcoords[v * 2 + 1] = (float)r / rings;
            ++v;
        }
    }

    int idx = 0;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < slices; ++s) {
            int a = r * (slices + 1) + s;
            int b = a + slices + 1;
            mesh.indices[idx++] = (unsigned short)a;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)(a + 1);
            mesh.indices[idx++] = (unsigned short)(a + 1);
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)(b + 1);
        }
    }
    return mesh;
}

Mesh GenMeshHemiSphere(float radius, int rings, int slices) {
    if (radius <= 0.0f || rings < 1 || slices < 3) return Mesh{};
    int vertexCount = (rings + 1) * (slices + 1);
    int triangleCount = rings * slices;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    int v = 0;
    for (int r = 0; r <= rings; ++r) {
        float phi = 0.5f * PI * r / rings;
        for (int s = 0; s <= slices; ++s) {
            float theta = 2.0f * PI * s / slices;
            float x = std::sin(phi) * std::cos(theta);
            float y = std::cos(phi);
            float z = std::sin(phi) * std::sin(theta);
            mesh.vertices[v * 3 + 0] = x * radius;
            mesh.vertices[v * 3 + 1] = y * radius;
            mesh.vertices[v * 3 + 2] = z * radius;
            mesh.normals[v * 3 + 0] = x;
            mesh.normals[v * 3 + 1] = y;
            mesh.normals[v * 3 + 2] = z;
            mesh.texcoords[v * 2 + 0] = (float)s / slices;
            mesh.texcoords[v * 2 + 1] = (float)r / rings;
            ++v;
        }
    }

    int idx = 0;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < slices; ++s) {
            int a = r * (slices + 1) + s;
            int b = a + slices + 1;
            mesh.indices[idx++] = (unsigned short)a;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)(a + 1);
            mesh.indices[idx++] = (unsigned short)(a + 1);
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)(b + 1);
        }
    }
    return mesh;
}

Mesh GenMeshCylinder(float radius, float height, int slices) {
    if (radius <= 0.0f || height <= 0.0f || slices < 3) return Mesh{};
    int vertexCount = (slices + 1) * 2 + 2;
    int triangleCount = slices * 4;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    int v = 0;
    for (int i = 0; i <= slices; ++i) {
        float theta = 2.0f * PI * i / slices;
        float x = std::cos(theta) * radius;
        float z = std::sin(theta) * radius;
        mesh.vertices[v * 3 + 0] = x;
        mesh.vertices[v * 3 + 1] = -height * 0.5f;
        mesh.vertices[v * 3 + 2] = z;
        mesh.normals[v * 3 + 0] = x;
        mesh.normals[v * 3 + 1] = 0.0f;
        mesh.normals[v * 3 + 2] = z;
        mesh.texcoords[v * 2 + 0] = (float)i / slices;
        mesh.texcoords[v * 2 + 1] = 0.0f;
        ++v;
        mesh.vertices[v * 3 + 0] = x;
        mesh.vertices[v * 3 + 1] = height * 0.5f;
        mesh.vertices[v * 3 + 2] = z;
        mesh.normals[v * 3 + 0] = x;
        mesh.normals[v * 3 + 1] = 0.0f;
        mesh.normals[v * 3 + 2] = z;
        mesh.texcoords[v * 2 + 0] = (float)i / slices;
        mesh.texcoords[v * 2 + 1] = 1.0f;
        ++v;
    }

    int topCenter = v++;
    int bottomCenter = v++;
    mesh.vertices[topCenter * 3 + 0] = 0.0f;
    mesh.vertices[topCenter * 3 + 1] = height * 0.5f;
    mesh.vertices[topCenter * 3 + 2] = 0.0f;
    mesh.normals[topCenter * 3 + 0] = 0.0f;
    mesh.normals[topCenter * 3 + 1] = 1.0f;
    mesh.normals[topCenter * 3 + 2] = 0.0f;
    mesh.texcoords[topCenter * 2 + 0] = 0.5f;
    mesh.texcoords[topCenter * 2 + 1] = 0.5f;

    mesh.vertices[bottomCenter * 3 + 0] = 0.0f;
    mesh.vertices[bottomCenter * 3 + 1] = -height * 0.5f;
    mesh.vertices[bottomCenter * 3 + 2] = 0.0f;
    mesh.normals[bottomCenter * 3 + 0] = 0.0f;
    mesh.normals[bottomCenter * 3 + 1] = -1.0f;
    mesh.normals[bottomCenter * 3 + 2] = 0.0f;
    mesh.texcoords[bottomCenter * 2 + 0] = 0.5f;
    mesh.texcoords[bottomCenter * 2 + 1] = 0.5f;

    int idx = 0;
    for (int i = 0; i < slices; ++i) {
        int lower0 = i * 2;
        int upper0 = lower0 + 1;
        int lower1 = ((i + 1) % (slices + 1)) * 2;
        int upper1 = lower1 + 1;

        mesh.indices[idx++] = (unsigned short)lower0;
        mesh.indices[idx++] = (unsigned short)upper0;
        mesh.indices[idx++] = (unsigned short)lower1;
        mesh.indices[idx++] = (unsigned short)upper0;
        mesh.indices[idx++] = (unsigned short)upper1;
        mesh.indices[idx++] = (unsigned short)lower1;

        mesh.indices[idx++] = (unsigned short)topCenter;
        mesh.indices[idx++] = (unsigned short)upper1;
        mesh.indices[idx++] = (unsigned short)upper0;

        mesh.indices[idx++] = (unsigned short)bottomCenter;
        mesh.indices[idx++] = (unsigned short)lower0;
        mesh.indices[idx++] = (unsigned short)lower1;
    }
    return mesh;
}

Mesh GenMeshCone(float radius, float height, int slices) {
    if (radius <= 0.0f || height <= 0.0f || slices < 3) return Mesh{};
    int vertexCount = slices + 2;
    int triangleCount = slices * 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    int apex = 0;
    mesh.vertices[0] = 0.0f;
    mesh.vertices[1] = height * 0.5f;
    mesh.vertices[2] = 0.0f;
    mesh.normals[0] = 0.0f;
    mesh.normals[1] = 1.0f;
    mesh.normals[2] = 0.0f;
    mesh.texcoords[0] = 0.5f;
    mesh.texcoords[1] = 0.5f;

    int baseCenter = 1;
    mesh.vertices[3] = 0.0f;
    mesh.vertices[4] = -height * 0.5f;
    mesh.vertices[5] = 0.0f;
    mesh.normals[3] = 0.0f;
    mesh.normals[4] = -1.0f;
    mesh.normals[5] = 0.0f;
    mesh.texcoords[2] = 0.5f;
    mesh.texcoords[3] = 0.5f;

    for (int i = 0; i < slices; ++i) {
        float theta = 2.0f * PI * i / slices;
        float x = std::cos(theta) * radius;
        float z = std::sin(theta) * radius;
        int v = 2 + i;
        mesh.vertices[v * 3 + 0] = x;
        mesh.vertices[v * 3 + 1] = -height * 0.5f;
        mesh.vertices[v * 3 + 2] = z;
        mesh.normals[v * 3 + 0] = x;
        mesh.normals[v * 3 + 1] = radius;
        mesh.normals[v * 3 + 2] = z;
        Vec3 n = Vec3{mesh.normals[v * 3 + 0], mesh.normals[v * 3 + 1], mesh.normals[v * 3 + 2]}.normalized();
        mesh.normals[v * 3 + 0] = n.x;
        mesh.normals[v * 3 + 1] = n.y;
        mesh.normals[v * 3 + 2] = n.z;
        mesh.texcoords[v * 2 + 0] = (std::cos(theta) + 1.0f) * 0.5f;
        mesh.texcoords[v * 2 + 1] = (std::sin(theta) + 1.0f) * 0.5f;
    }

    int idx = 0;
    for (int i = 0; i < slices; ++i) {
        int next = 2 + ((i + 1) % slices);
        mesh.indices[idx++] = (unsigned short)apex;
        mesh.indices[idx++] = (unsigned short)(2 + i);
        mesh.indices[idx++] = (unsigned short)next;
        mesh.indices[idx++] = (unsigned short)baseCenter;
        mesh.indices[idx++] = (unsigned short)next;
        mesh.indices[idx++] = (unsigned short)(2 + i);
    }
    return mesh;
}

Mesh GenMeshTorus(float radius, float size, int radSeg, int sides) {
    if (radius <= 0.0f || size <= 0.0f || radSeg < 3 || sides < 3) return Mesh{};
    int vertexCount = radSeg * sides;
    int triangleCount = radSeg * sides * 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    int v = 0;
    for (int ring = 0; ring < radSeg; ++ring) {
        float u = 2.0f * PI * ring / radSeg;
        Vec3 center{std::cos(u) * radius, 0.0f, std::sin(u) * radius};
        Vec3 ringDir{-std::sin(u), 0.0f, std::cos(u)};
        Vec3 ringUp{0.0f, 1.0f, 0.0f};
        for (int side = 0; side < sides; ++side) {
            float vAngle = 2.0f * PI * side / sides;
            float cx = std::cos(vAngle) * size;
            float cy = std::sin(vAngle) * size;
            Vec3 position = center + ringDir * cx + ringUp * cy;
            Vec3 normal = Vec3{ringDir.x * cx + ringUp.x * cy,
                               ringDir.y * cx + ringUp.y * cy,
                               ringDir.z * cx + ringUp.z * cy}.normalized();
            mesh.vertices[v * 3 + 0] = position.x;
            mesh.vertices[v * 3 + 1] = position.y;
            mesh.vertices[v * 3 + 2] = position.z;
            mesh.normals[v * 3 + 0] = normal.x;
            mesh.normals[v * 3 + 1] = normal.y;
            mesh.normals[v * 3 + 2] = normal.z;
            mesh.texcoords[v * 2 + 0] = (float)ring / radSeg;
            mesh.texcoords[v * 2 + 1] = (float)side / sides;
            ++v;
        }
    }

    int idx = 0;
    for (int ring = 0; ring < radSeg; ++ring) {
        for (int side = 0; side < sides; ++side) {
            int nextRing = (ring + 1) % radSeg;
            int nextSide = (side + 1) % sides;
            int a = ring * sides + side;
            int b = nextRing * sides + side;
            int c = nextRing * sides + nextSide;
            int d = ring * sides + nextSide;
            mesh.indices[idx++] = (unsigned short)a;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)d;
            mesh.indices[idx++] = (unsigned short)d;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)c;
        }
    }
    return mesh;
}

Mesh GenMeshKnot(float radius, float size, int radSeg, int sides) {
    if (radius <= 0.0f || size <= 0.0f || radSeg < 3 || sides < 3) return Mesh{};
    int vertexCount = radSeg * sides;
    int triangleCount = radSeg * sides * 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    auto knotPos = [&](float t) {
        float x = (2.0f + std::cos(3.0f * t)) * std::cos(2.0f * t);
        float y = (2.0f + std::cos(3.0f * t)) * std::sin(2.0f * t);
        float z = std::sin(3.0f * t);
        return Vec3{x, y, z} * radius;
    };

    auto knotTangent = [&](float t) {
        float dx = -2.0f * std::sin(2.0f * t) - 3.0f * std::sin(3.0f * t) * std::cos(2.0f * t) - 2.0f * std::sin(2.0f * t) * std::cos(3.0f * t);
        float dy =  2.0f * std::cos(2.0f * t) + 3.0f * std::sin(3.0f * t) * std::sin(2.0f * t) + 2.0f * std::cos(2.0f * t) * std::cos(3.0f * t);
        float dz =  3.0f * std::cos(3.0f * t);
        return Vec3{dx, dy, dz}.normalized();
    };

    for (int i = 0; i < radSeg; ++i) {
        float u = 2.0f * PI * i / radSeg;
        Vec3 center = knotPos(u);
        Vec3 tangent = knotTangent(u);
        Vec3 normal = Vec3{-tangent.y, tangent.x, 0.0f}.normalized();
        Vec3 binormal = tangent.cross(normal).normalized();
        for (int j = 0; j < sides; ++j) {
            float v = 2.0f * PI * j / sides;
            float cx = std::cos(v) * size;
            float cy = std::sin(v) * size;
            Vec3 position = center + normal * cx + binormal * cy;
            Vec3 n = (normal * cx + binormal * cy).normalized();
            int index = i * sides + j;
            mesh.vertices[index * 3 + 0] = position.x;
            mesh.vertices[index * 3 + 1] = position.y;
            mesh.vertices[index * 3 + 2] = position.z;
            mesh.normals[index * 3 + 0] = n.x;
            mesh.normals[index * 3 + 1] = n.y;
            mesh.normals[index * 3 + 2] = n.z;
            mesh.texcoords[index * 2 + 0] = (float)i / radSeg;
            mesh.texcoords[index * 2 + 1] = (float)j / sides;
        }
    }

    int idx = 0;
    for (int i = 0; i < radSeg; ++i) {
        int nextRing = (i + 1) % radSeg;
        for (int j = 0; j < sides; ++j) {
            int nextSide = (j + 1) % sides;
            int a = i * sides + j;
            int b = nextRing * sides + j;
            int c = nextRing * sides + nextSide;
            int d = i * sides + nextSide;
            mesh.indices[idx++] = (unsigned short)a;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)d;
            mesh.indices[idx++] = (unsigned short)d;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)c;
        }
    }
    return mesh;
}

Mesh GenMeshHeightmap(Image heightmap, Vec3 size) {
    if (!heightmap.data || heightmap.width <= 0 || heightmap.height <= 0) return Mesh{};
    int width = heightmap.width;
    int height = heightmap.height;
    int vertexCount = width * height;
    int triangleCount = (width - 1) * (height - 1) * 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = y * width + x;
            float fx = ((float)x / (width - 1) - 0.5f) * size.x;
            float fz = ((float)y / (height - 1) - 0.5f) * size.z;
            Vec3 sample = GetHeightSample(heightmap, x, y);
            float fy = (sample.x - 0.5f) * size.y;
            mesh.vertices[idx * 3 + 0] = fx;
            mesh.vertices[idx * 3 + 1] = fy;
            mesh.vertices[idx * 3 + 2] = fz;
            mesh.texcoords[idx * 2 + 0] = (float)x / (width - 1);
            mesh.texcoords[idx * 2 + 1] = (float)y / (height - 1);
        }
    }

    int idx = 0;
    for (int y = 0; y < height - 1; ++y) {
        for (int x = 0; x < width - 1; ++x) {
            int a = y * width + x;
            int b = a + 1;
            int c = a + width;
            int d = c + 1;
            mesh.indices[idx++] = (unsigned short)a;
            mesh.indices[idx++] = (unsigned short)c;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)c;
            mesh.indices[idx++] = (unsigned short)d;
        }
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int index = y * width + x;
            Vec3 center{mesh.vertices[index * 3 + 0], mesh.vertices[index * 3 + 1], mesh.vertices[index * 3 + 2]};
            Vec3 left = GetHeightSample(heightmap, x - 1, y);
            Vec3 right = GetHeightSample(heightmap, x + 1, y);
            Vec3 down = GetHeightSample(heightmap, x, y - 1);
            Vec3 up = GetHeightSample(heightmap, x, y + 1);
            float dx = (right.x - left.x) * size.y;
            float dz = (up.x - down.x) * size.y;
            Vec3 normal = Vec3{-dx, 2.0f, -dz}.normalized();
            mesh.normals[index * 3 + 0] = normal.x;
            mesh.normals[index * 3 + 1] = normal.y;
            mesh.normals[index * 3 + 2] = normal.z;
        }
    }
    return mesh;
}

Mesh GenMeshCubicmap(Image cubicmap, Vec3 cubeSize) {
    if (!cubicmap.data || cubicmap.width <= 0 || cubicmap.height <= 0) return Mesh{};
    int width = cubicmap.width;
    int height = cubicmap.height;
    std::vector<Mesh> cubes;
    int channels = cubicmap.channels > 0 ? cubicmap.channels : 1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = (y * width + x) * channels;
            unsigned char alpha = 0;
            if (channels >= 4) {
                alpha = cubicmap.data[idx + 3];
            } else if (channels >= 1) {
                alpha = cubicmap.data[idx];
            }
            if (alpha == 0) continue;
            Mesh cube = GenMeshCube(cubeSize.x, cubeSize.y, cubeSize.z);
            float px = ((float)x - width * 0.5f + 0.5f) * cubeSize.x;
            float pz = ((float)y - height * 0.5f + 0.5f) * cubeSize.z;
            for (int v = 0; v < cube.vertexCount; ++v) {
                cube.vertices[v * 3 + 0] += px;
                cube.vertices[v * 3 + 1] += cubeSize.y * 0.5f;
                cube.vertices[v * 3 + 2] += pz;
            }
            cubes.push_back(std::move(cube));
        }
    }

    if (cubes.empty()) return Mesh{};
    int totalVerts = 0;
    int totalTris = 0;
    for (auto& cube : cubes) {
        totalVerts += cube.vertexCount;
        totalTris += cube.triangleCount;
    }

    Mesh mesh = CreateMesh(totalVerts, totalTris);
    int vOffset = 0;
    int iOffset = 0;
    for (auto& cube : cubes) {
        for (int v = 0; v < cube.vertexCount; ++v) {
            mesh.vertices[(vOffset + v) * 3 + 0] = cube.vertices[v * 3 + 0];
            mesh.vertices[(vOffset + v) * 3 + 1] = cube.vertices[v * 3 + 1];
            mesh.vertices[(vOffset + v) * 3 + 2] = cube.vertices[v * 3 + 2];
            mesh.normals[(vOffset + v) * 3 + 0] = cube.normals[v * 3 + 0];
            mesh.normals[(vOffset + v) * 3 + 1] = cube.normals[v * 3 + 1];
            mesh.normals[(vOffset + v) * 3 + 2] = cube.normals[v * 3 + 2];
            mesh.texcoords[(vOffset + v) * 2 + 0] = cube.texcoords[v * 2 + 0];
            mesh.texcoords[(vOffset + v) * 2 + 1] = cube.texcoords[v * 2 + 1];
        }
        for (int t = 0; t < cube.triangleCount * 3; ++t) {
            mesh.indices[iOffset + t] = (unsigned short)(cube.indices[t] + vOffset);
        }
        vOffset += cube.vertexCount;
        iOffset += cube.triangleCount * 3;
    }
    return mesh;
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

Color ColorFromNormalized(float r, float g, float b, float a) {
    return Color{
        static_cast<unsigned char>(std::clamp(r, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(g, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(b, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(a, 0.0f, 1.0f) * 255.0f)
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