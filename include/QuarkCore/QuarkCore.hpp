/*
    ========================================================
    
        Quark Core v1.0
        By Quark Engine Development Team

    --------------------------------------------------------

    Core module of Quark Engine.

    This file contains:
        * Basic math structures
        * Window management
        * Input handling
        * Event system
        * Rendering API
        * Texture management
        * Logging and timing utilities

    Backend:
        * SDL3
        * OpenGL

    Language:
        * Modern C++

    ========================================================
*/

#pragma once

#if defined(_WIN32)
    #if defined(QUARKCORE_BUILD_DLL)
        #define QCAPI __declspec(dllexport)
    #else
        #define QCAPI __declspec(dllimport)
    #endif
#else
    #define QCAPI
#endif

#include <SDL3/SDL.h>
#include <cstdint>

namespace qc {

/** 
 * @brief Renderer type enumeration.
 */
enum class RendererType {
    OpenGL,
    Vulkan
};

/**
 * @brief Texture structure.
 */
struct Texture {
    unsigned int id = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
};

using Texture2D = Texture;

} // namespace qc

#include "Quark3D.hpp"

#define QC_VERSION_MAJOR 1
#define QC_VERSION_MINOR 0
#define QC_VERSION_PATCH 0
#define QC_VERSION_STRING "1.0.0"
#define QC_VERSION (QC_VERSION_MAJOR * 10000 + QC_VERSION_MINOR * 100 + QC_VERSION_PATCH)

namespace qc {

struct RendererState;
class IRenderer;
extern IRenderer* gRendererPtr;

/**
 * @brief Render texture structure.
 */
struct RenderTexture2D {
    unsigned int id = 0;               // Framebuffer ID
    Texture2D texture;                 // Color buffer texture
    unsigned int depthId = 0;          // Depth buffer ID
};

/**
 * @brief Font glyph metrics.
 */
struct FontGlyph {
    Rectangle uv{0.0f, 0.0f, 0.0f, 0.0f};
    float advanceX = 0.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    int width = 0;
    int height = 0;
};

/**
 * @brief Font structure.
 */
struct Font {
    unsigned int textureId = 0;
    int baseSize = 0;
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    int lineHeight = 0;
    bool valid = false;
    FontGlyph glyphs[95];
    uint32_t _rendererFontId = 0;
};

/**
 * @brief Shader uniform data type enumeration.
 */
enum class ShaderUniformDataType {
    Float = 0,         // float
    Vec2,              // vec2
    Vec3,              // vec3
    Vec4,              // vec4
    Int,               // int
    IVec2,             // ivec2
    IVec3,             // ivec3
    IVec4,             // ivec4
    Sampler2D,         // sampler2D
};

/**
 * @brief Shader uniform data type constants for C compatibility.
 */
#define SHADER_UNIFORM_FLOAT      0
#define SHADER_UNIFORM_VEC2       1
#define SHADER_UNIFORM_VEC3       2
#define SHADER_UNIFORM_VEC4       3
#define SHADER_UNIFORM_INT        4
#define SHADER_UNIFORM_IVEC2      5
#define SHADER_UNIFORM_IVEC3      6
#define SHADER_UNIFORM_IVEC4      7
#define SHADER_UNIFORM_SAMPLER2D  8

/**
 * @brief Shader attribute data type enumeration.
 */
enum class ShaderAttributeDataType {
    Float = 0,
    Vec2,
    Vec3,
    Vec4,
};

/**
 * @brief Shader location index enumeration.
 */
typedef enum {
    SHADER_LOC_VERTEX_POSITION = 0, // Shader location: vertex attribute: position
    SHADER_LOC_VERTEX_TEXCOORD01,   // Shader location: vertex attribute: texcoord01
    SHADER_LOC_VERTEX_TEXCOORD02,   // Shader location: vertex attribute: texcoord02
    SHADER_LOC_VERTEX_NORMAL,       // Shader location: vertex attribute: normal
    SHADER_LOC_VERTEX_TANGENT,      // Shader location: vertex attribute: tangent
    SHADER_LOC_VERTEX_COLOR,        // Shader location: vertex attribute: color
    SHADER_LOC_MATRIX_MVP,          // Shader location: matrix uniform: model-view-projection
    SHADER_LOC_MATRIX_VIEW,         // Shader location: matrix uniform: view (camera transform)
    SHADER_LOC_MATRIX_PROJECTION,   // Shader location: matrix uniform: projection
    SHADER_LOC_MATRIX_MODEL,        // Shader location: matrix uniform: model (transform)
    SHADER_LOC_MATRIX_NORMAL,       // Shader location: matrix uniform: normal
    SHADER_LOC_VECTOR_VIEW,         // Shader location: vector uniform: view
    SHADER_LOC_COLOR_DIFFUSE,       // Shader location: vector uniform: diffuse color
    SHADER_LOC_COLOR_SPECULAR,      // Shader location: vector uniform: specular color
    SHADER_LOC_COLOR_AMBIENT,       // Shader location: vector uniform: ambient color
    SHADER_LOC_MAP_ALBEDO,          // Shader location: sampler2d texture: albedo (same as: SHADER_LOC_MAP_DIFFUSE)
    SHADER_LOC_MAP_METALNESS,       // Shader location: sampler2d texture: metalness (same as: SHADER_LOC_MAP_SPECULAR)
    SHADER_LOC_MAP_NORMAL,          // Shader location: sampler2d texture: normal
    SHADER_LOC_MAP_ROUGHNESS,       // Shader location: sampler2d texture: roughness
    SHADER_LOC_MAP_OCCLUSION,       // Shader location: sampler2d texture: occlusion
    SHADER_LOC_MAP_EMISSION,        // Shader location: sampler2d texture: emission
    SHADER_LOC_MAP_HEIGHT,          // Shader location: sampler2d texture: heightmap
    SHADER_LOC_MAP_CUBEMAP,         // Shader location: samplerCube texture: cubemap
    SHADER_LOC_MAP_IRRADIANCE,      // Shader location: samplerCube texture: irradiance
    SHADER_LOC_MAP_PREFILTER,       // Shader location: samplerCube texture: prefilter
    SHADER_LOC_MAP_BRDF,            // Shader location: sampler2d texture: brdf
    SHADER_LOC_VERTEX_BONEIDS,      // Shader location: vertex attribute: bone indices
    SHADER_LOC_VERTEX_BONEWEIGHTS,  // Shader location: vertex attribute: bone weights
    SHADER_LOC_MATRIX_BONETRANSFORMS, // Shader location: matrix attribute: bone transforms (animation)
    SHADER_LOC_VERTEX_INSTANCETRANSFORM, // Shader location: vertex attribute: instance transforms
    SHADER_LOC_COUNT                // Total number of shader locations
} ShaderLocationIndex;

/**
 * @brief Shader structure.
 */
struct Shader {
    unsigned int id = 0;               // Program ID
    int locs[SHADER_LOC_COUNT] = {};   // Uniform locations array
};

/**
 * @brief Camera projection type.
 */
enum CameraProjection {
    CAMERA_PERSPECTIVE = 0,
    CAMERA_ORTHOGRAPHIC
};

/**
 * @brief 2D Camera for orthographic projection.
 * 
 * Controls view transformation for 2D rendering with pan and zoom.
 */
struct Camera2D {
    Vec2 offset{0.0f, 0.0f};          // Camera screen offset (center of viewport)
    Vec2 target{0.0f, 0.0f};          // Target position to look at
    float rotation{0.0f};              // Camera rotation in degrees
    float zoom{1.0f};                  // Zoom level (1.0 = default)
};

/**
 * @brief 3D Camera for perspective projection.
 * 
 * Controls view and projection transformation for 3D rendering.
 */
struct Camera3D {
    Vec3 position{0.0f, 0.0f, 10.0f}; // Camera position in 3D space
    Vec3 target{0.0f, 0.0f, 0.0f};    // Target position to look at
    Vec3 up{0.0f, 1.0f, 0.0f};        // Camera up vector
    float fovy{45.0f};                 // Camera field-of-view Y in degrees
    int projection{0};                 // Camera projection: CAMERA_PERSPECTIVE or CAMERA_ORTHOGRAPHIC
};

using Camera = Camera3D;

/**
 * @brief Default vertex shader source code.
 * Implements basic 2D rendering with texture and color attributes.
 */
inline constexpr const char* kVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPosition;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec4 aColor;

out vec2 vTexCoord;
out vec4 vColor;

uniform vec2 uScreenSize;

void main() {
    vec2 normalized = vec2(
        (aPosition.x / uScreenSize.x) * 2.0 - 1.0,
        1.0 - (aPosition.y / uScreenSize.y) * 2.0
    );

    vTexCoord = aTexCoord;
    vColor = aColor;
    gl_Position = vec4(normalized, 0.0, 1.0);
}
)";

/**
 * @brief Default fragment shader source code.
 * Implements basic textured rendering with color modulation.
 */
inline constexpr const char* kFragmentShaderSource = R"(
#version 330 core
in vec2 vTexCoord;
in vec4 vColor;

out vec4 fragColor;

uniform sampler2D uTexture;

void main() {
    fragColor = texture(uTexture, vTexCoord) * vColor;
}
)";

/**
 * @brief Log level type enumeration.
 */
enum class LogLevel {
    Trace = 0,
    Info,
    Warn,
    Error,
    None,
};

/**
 * @brief Keyboard key type enumeration.
 */
enum class KeyboardKey {
    Unknown = 0,

    A = 4,
    B = 5,
    C = 6,
    D = 7,
    E = 8,
    F = 9,
    G = 10,
    H = 11,
    I = 12,
    J = 13,
    K = 14,
    L = 15,
    M = 16,
    N = 17,
    O = 18,
    P = 19,
    Q = 20,
    R = 21,
    S = 22,
    T = 23,
    U = 24,
    V = 25,
    W = 26,
    X = 27,
    Y = 28,
    Z = 29,

    Num1 = 30,
    Num2 = 31,
    Num3 = 32,
    Num4 = 33,
    Num5 = 34,
    Num6 = 35,
    Num7 = 36,
    Num8 = 37,
    Num9 = 38,
    Num0 = 39,

    Enter = 40,
    Escape = 41,
    Backspace = 42,
    Tab = 43,
    Space = 44,

    Minus = 45,
    Equals = 46,
    LeftBracket = 47,
    RightBracket = 48,
    Backslash = 49,
    NonUSHash = 50,
    Semicolon = 51,
    Apostrophe = 52,
    Grave = 53,
    Comma = 54,
    Period = 55,
    Slash = 56,

    CapsLock = 57,

    F1 = 58,
    F2 = 59,
    F3 = 60,
    F4 = 61,
    F5 = 62,
    F6 = 63,
    F7 = 64,
    F8 = 65,
    F9 = 66,
    F10 = 67,
    F11 = 68,
    F12 = 69,

    PrintScreen = 70,
    ScrollLock = 71,
    Pause = 72,
    Insert = 73,
    Home = 74,
    PageUp = 75,
    Delete = 76,
    End = 77,
    PageDown = 78,

    Right = 79,
    Left = 80,
    Down = 81,
    Up = 82,

    NumLock = 83,

    KeypadDivide = 84,
    KeypadMultiply = 85,
    KeypadMinus = 86,
    KeypadPlus = 87,
    KeypadEnter = 88,

    Keypad1 = 89,
    Keypad2 = 90,
    Keypad3 = 91,
    Keypad4 = 92,
    Keypad5 = 93,
    Keypad6 = 94,
    Keypad7 = 95,
    Keypad8 = 96,
    Keypad9 = 97,
    Keypad0 = 98,
    KeypadPeriod = 99,

    NonUSBackslash = 100,
    Application = 101,
    Power = 102,
    KeypadEquals = 103,

    F13 = 104,
    F14 = 105,
    F15 = 106,
    F16 = 107,
    F17 = 108,
    F18 = 109,
    F19 = 110,
    F20 = 111,
    F21 = 112,
    F22 = 113,
    F23 = 114,
    F24 = 115,

    Execute = 116,
    Help = 117,
    Menu = 118,
    Select = 119,
    Stop = 120,
    Again = 121,
    Undo = 122,
    Cut = 123,
    Copy = 124,
    Paste = 125,
    Find = 126,
    Mute = 127,
    VolumeUp = 128,
    VolumeDown = 129,

    LeftControl = 224,
    LeftShift = 225,
    LeftAlt = 226,
    LeftSuper = 227,

    RightControl = 228,
    RightShift = 229,
    RightAlt = 230,
    RightSuper = 231
};

/**
 * @brief Mouse button type enumeration.
 */
enum class MouseButton {
    Left = 1,
    Middle = 2,
    Right = 3,
};

/**
 * @brief Mouse cursor type enumeration.
 */
enum class MouseCursor {
    Default = 0,
    Arrow,
    Ibeam,
    Crosshair,
    PointingHand,
    ResizeEW,
    ResizeNS,
    ResizeNWSE,
    ResizeNESW,
    ResizeAll,
    NotAllowed,
};

/**
 * @brief Event type enumeration.
 */
enum class EventType {
    None = 0,
    Quit,
    Terminating,
    LowMemory,
    WillEnterBackground,
    DidEnterBackground,
    WillEnterForeground,
    DidEnterForeground,
    LocaleChanged,
    SystemThemeChanged,
    DisplayOrientation,
    DisplayAdded,
    DisplayRemoved,
    DisplayMoved,
    DisplayDesktopModeChanged,
    DisplayCurrentModeChanged,
    DisplayContentScaleChanged,
    DisplayUsableBoundsChanged,
    WindowShown,
    WindowHidden,
    WindowExposed,
    WindowMoved,
    WindowResized,
    WindowPixelSizeChanged,
    WindowMetalViewResized,
    WindowMinimized,
    WindowMaximized,
    WindowRestored,
    WindowMouseEnter,
    WindowMouseLeave,
    WindowFocusGained,
    WindowFocusLost,
    WindowCloseRequested,
    WindowHitTest,
    WindowIccProfileChanged,
    WindowDisplayChanged,
    WindowDisplayScaleChanged,
    WindowSafeAreaChanged,
    WindowOccluded,
    WindowEnterFullscreen,
    WindowLeaveFullscreen,
    WindowDestroyed,
    WindowHdrStateChanged,
    KeyDown,
    KeyUp,
    TextEditing,
    TextInput,
    KeymapChanged,
    KeyboardAdded,
    KeyboardRemoved,
    TextEditingCandidates,
    ScreenKeyboardShown,
    ScreenKeyboardHidden,
    MouseMotion,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
    MouseAdded,
    MouseRemoved,
    JoystickAxisMotion,
    JoystickBallMotion,
    JoystickHatMotion,
    JoystickButtonDown,
    JoystickButtonUp,
    JoystickAdded,
    JoystickRemoved,
    JoystickBatteryUpdated,
    JoystickUpdateComplete,
    GamepadAxisMotion,
    GamepadButtonDown,
    GamepadButtonUp,
    GamepadAdded,
    GamepadRemoved,
    GamepadRemapped,
    GamepadTouchpadDown,
    GamepadTouchpadMotion,
    GamepadTouchpadUp,
    GamepadSensorUpdate,
    GamepadUpdateComplete,
    GamepadSteamHandleUpdated,
    FingerDown,
    FingerUp,
    FingerMotion,
    FingerCanceled,
    PinchBegin,
    PinchUpdate,
    PinchEnd,
    ClipboardUpdate,
    DropFile,
    DropText,
    DropBegin,
    DropComplete,
    DropPosition,
    AudioDeviceAdded,
    AudioDeviceRemoved,
    AudioDeviceFormatChanged,
    SensorUpdate,
    PenProximityIn,
    PenProximityOut,
    PenDown,
    PenUp,
    PenButtonDown,
    PenButtonUp,
    PenMotion,
    PenAxis,
    CameraDeviceAdded,
    CameraDeviceRemoved,
    CameraDeviceApproved,
    CameraDeviceDenied,
    RenderTargetsReset,
    RenderDeviceReset,
    RenderDeviceLost,
    Unknown,
};

/**
 * @brief Event structure.
 */
struct Event {
    EventType type = EventType::None;
    SDL_Event nativeEvent{};
    std::uint64_t timestamp = 0;
    std::uint32_t windowId = 0;
    std::uint64_t which = 0;
    std::int32_t data1 = 0;
    std::int32_t data2 = 0;
    float x = 0.0f;
    float y = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    float pressure = 0.0f;
    float scale = 0.0f;
    std::uint32_t key = 0;
    std::uint32_t scancode = 0;
    std::uint32_t modifiers = 0;
    std::uint32_t button = 0;
    std::uint32_t clicks = 0;
    bool down = false;
    bool repeat = false;
    char text[256]{};
};

/**
 * @brief Initialize the main application window.
 *
 * @param width Window width in pixels.
 * @param height Window height in pixels.
 * @param title Window title text.
 */
QCAPI void InitWindow(int width, int height, const char* title, RendererType rendererType);
/**
 * @brief Check if the window should close.
 *
 * @return true if close was requested.
 * @return false if the application should continue running.
 */
QCAPI bool WindowShouldClose();
/**
 * @brief Close and destroy the application window.
 */
QCAPI void CloseWindow();

/**
 * @brief Poll the next available event.
 *
 * @param event Reference to event structure that will receive event data.
 * @return true if an event was received.
 * @return false if no events are available.
 */
QCAPI bool PollEvent(Event& event);
/**
 * @brief Wait until an event is received.
 *
 * @param event Reference to event structure that will receive event data.
 * @return true if an event was received.
 * @return false on failure.
 */
QCAPI bool WaitEvent(Event& event);

/**
 * @brief Wait for an event with timeout.
 *
 * @param event Reference to event structure that will receive event data.
 * @param timeoutMs Timeout duration in milliseconds.
 * @return true if an event was received.
 * @return false if timeout was reached.
 */
QCAPI bool WaitEventTimeout(Event& event, int timeoutMs);

/**
 * @brief Get event type name as string.
 *
 * @param type Event type enum value.
 * @return Pointer to event type name string.
 */
QCAPI const char* GetEventTypeName(EventType type);

/**
 * @brief Set window title text.
 *
 * @param title New window title.
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool SetWindowTitle(const char* title);
/**
 * @brief Get current window title.
 *
 * @return Pointer to window title string.
 */
QCAPI const char* GetWindowTitle();
/**
 * @brief Set window position.
 *
 * @param x Window X position.
 * @param y Window Y position.
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool SetWindowPosition(int x, int y);
/**
 * @brief Get current window position.
 *
 * @return Window position as IVec2.
 */
QCAPI IVec2 GetWindowPosition();
/**
 * @brief Set window size.
 *
 * @param width New window width.
 * @param height New window height.
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool SetWindowSize(int width, int height);
/**
 * @brief Get current window size.
 *
 * @return Window size as IVec2.
 */
QCAPI IVec2 GetWindowSize();
/**
 * @brief Get current window size in pixels.
 *
 * @return Pixel size as IVec2.
 */
QCAPI IVec2 GetWindowSizeInPixels();
/**
 * @brief Set minimum allowed window size.
 *
 * @param width Minimum width.
 * @param height Minimum height.
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool SetWindowMinimumSize(int width, int height);
/**
 * @brief Get minimum window size.
 *
 * @return Minimum size as IVec2.
 */
QCAPI IVec2 GetWindowMinimumSize();
/**
 * @brief Set maximum allowed window size.
 *
 * @param width Maximum width.
 * @param height Maximum height.
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool SetWindowMaximumSize(int width, int height);
/**
 * @brief Get maximum window size.
 *
 * @return Maximum size as IVec2.
 */
QCAPI IVec2 GetWindowMaximumSize();
/**
 * @brief Set window resizable flag.
 *
 * @param resizable Resizable flag.
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool SetWindowResizable(bool resizable);
/**
 * @brief Set window bordered flag.
 *
 * @param bordered Bordered flag.
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool SetWindowBordered(bool bordered);
/**
 * @brief Set window fullscreen mode.
 *
 * @param fullscreen Fullscreen flag.
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool SetWindowFullscreen(bool fullscreen);
/**
 * @brief Toggle window fullscreen mode.
 *
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool ToggleFullscreen();
/**
 * @brief Show the window.
 *
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool ShowWindow();
/**
 * @brief Hide the window.
 *
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool HideWindow();
/**
 * @brief Raise the window.
 *
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool RaiseWindow();
/**
 * @brief Maximize the window.
 *
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool MaximizeWindow();
/**
 * @brief Minimize the window.
 *
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool MinimizeWindow();
/**
 * @brief Restore the window.
 *
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool RestoreWindow();
/**
 * @brief Sync the window.
 *
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool SyncWindow();
/**
 * @brief Check if the window is in fullscreen mode.
 *
 * @return true if the window is fullscreen.
 * @return false otherwise.
 */
QCAPI bool IsWindowFullscreen();
/**
 * @brief Check if the window is hidden.
 *
 * @return true if the window is hidden.
 * @return false otherwise.
 */
QCAPI bool IsWindowHidden();
/**
 * @brief Check if the window is minimized.
 *
 * @return true if the window is minimized.
 * @return false otherwise.
 */
QCAPI bool IsWindowMinimized();
/**
 * @brief Check if the window is maximized.
 *
 * @return true if the window is maximized.
 * @return false otherwise.
 */
QCAPI bool IsWindowMaximized();
/**
 * @brief Check if the window is focused.
 *
 * @return true if the window is focused.
 * @return false otherwise.
 */
QCAPI bool IsWindowFocused();
/**
 * @brief Check if the window is in focus.
 *
 * @return true if the window is in focus.
 * @return false otherwise.
 */
QCAPI bool IsWindowMouseFocused();
/**
 * @brief Check if the window is resizable.
 *
 * @return true if the window is resizable.
 * @return false otherwise.
 */
QCAPI bool IsWindowResizable();
/**
 * @brief Check if the window is borderless.
 *
 * @return true if the window is borderless.
 * @return false otherwise.
 */
QCAPI bool IsWindowBorderless();
/**
 * @brief Get the display scale of the window.
 *
 * @return Display scale as a float.
 */
QCAPI float GetWindowDisplayScale();
/**
 * @brief Get the pixel density of the window.
 *
 * @return Pixel density as a float.
 */
QCAPI float GetWindowPixelDensity();
/**
 * @brief Set the icon for the window.
 *
 * @param filePath Path to the icon file.
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool SetWindowIcon(const char* filePath);

/**
 * @brief Get the underlying SDL window.
 *
 * @return Pointer to the SDL window.
 */
QCAPI SDL_Window* GetNativeWindow();

/**
 * @brief Get the underlying SDL GL context.
 *
 * @return Pointer to the SDL GL context.
 */
QCAPI SDL_GLContext GetNativeContext();

/**
 * @brief Get the underlying SDL event.
 *
 * @return SDL_Event structure with event data.
*/
QCAPI SDL_Event GetNativeEvent();

/**
 * @brief Start text input.
 *
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool StartTextInput();
/**
 * @brief Stop text input.
 *
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool StopTextInput();
/**
 * @brief Check if text input is active.
 *
 * @return true if text input is active.
 * @return false otherwise.
 */
QCAPI bool IsTextInputActive();

/**
 * @brief Set the log level.
 *
 * @param level Log level.
 */
QCAPI void SetLogLevel(LogLevel level);
/**
 * @brief Trace a log message.
 *
 * @param level Log level.
 * @param logType Log type.
 * @param message Log message.
 */
QCAPI void TraceLog(LogLevel level, const char* logType, const char* message);
/**
 * @brief Format a text string.
 *
 * @param format Format string (printf-style).
 * @param ... Format arguments.
 * @return Pointer to formatted string.
 */
QCAPI const char* TextFormat(const char* format, ...);

/**
 * @brief Set the target FPS.
 *
 * @param fps Target FPS.
 */
QCAPI void SetTargetFPS(int fps);
/**
 * @brief Get the frame time.
 *
 * @return Frame time as a float.
 */
QCAPI float GetFrameTime();
/**
 * @brief Get the delta time.
 *
 * @return Delta time as a float.
 */
QCAPI float GetDeltaTime();
/**
 * @brief Get the current FPS.
 *
 * @return Current FPS as an integer.
 */
QCAPI int GetFPS();
/**
 * @brief Get the current time.
 *
 * @return Current time as a double.
 */
QCAPI double GetTime();
/**
 * @brief Get the screen width.
 *
 * @return Screen width as an integer.
 */
QCAPI int GetScreenWidth();
/**
 * @brief Get the screen height.
 *
 * @return Screen height as an integer.
 */
QCAPI int GetScreenHeight();
/**
 * @brief Get the current monitor refresh rate.
 *
 * @return Refresh rate in Hz as a float.
 */
QCAPI float GetCurrentMonitorRefreshRate();

/**
 * @brief Check if a key is pressed.
 *
 * @param key Key to check.
 * @return true if the key is pressed.
 * @return false otherwise.
 */
QCAPI bool IsKeyDown(KeyboardKey key);
/**
 * @brief Check if a key was just pressed.
 *
 * @param key Key to check.
 * @return true if the key was just pressed.
 * @return false otherwise.
 */
QCAPI bool IsKeyPressed(KeyboardKey key);
/**
 * @brief Check if a mouse button is pressed.
 *
 * @param button Button to check.
 * @return true if the button is pressed.
 * @return false otherwise.
 */
QCAPI bool IsMouseButtonDown(MouseButton button);
/**
 * @brief Check if a mouse button was just pressed.
 *
 * @param button Button to check.
 * @return true if the button was just pressed.
 * @return false otherwise.
 */
QCAPI bool IsMouseButtonPressed(MouseButton button);
/**
 * @brief Check if a mouse button was just released.
 *
 * @param button Button to check.
 * @return true if the button was just released.
 * @return false otherwise.
 */
QCAPI bool IsMouseButtonReleased(MouseButton button);
/**
 * @brief Check if a mouse button is NOT pressed.
 *
 * @param button Button to check.
 * @return true if the button is NOT pressed.
 * @return false otherwise.
 */
QCAPI bool IsMouseButtonUp(MouseButton button);
/**
 * @brief Get the mouse position.
 *
 * @return Mouse position as a Vec2.
 */
QCAPI Vec2 GetMousePosition();
/**
 * @brief Get mouse wheel movement for both axes.
 *
 * @return Mouse wheel movement as a Vec2.
 */
QCAPI Vec2 GetMouseWheelMoveV();
/**
 * @brief Get vertical mouse wheel movement.
 *
 * @return Vertical mouse wheel movement as a float.
 */
QCAPI float GetMouseWheelMove();

/**
 * @brief Begin drawing.
 *
 * @return true on success.
 * @return false on failure.
 */
QCAPI void BeginDrawing();
/**
 * @brief End drawing and present the frame.
 *
 * @return true on success.
 * @return false on failure.
 */
QCAPI void EndDrawing();
/**
 * @brief Clear the background with a color.
 *
 * @param color Clear color.
 * @return true on success.
 * @return false on failure.
 */
QCAPI void ClearBackground(Color color);

/**
 * @brief Draw a rectangle.
 * @param x X coordinate of the top-left corner.
 * @param y Y coordinate of the top-left corner.
 * @param width Width of the rectangle.
 * @param height Height of the rectangle.
 * @param color Rectangle color.
 */
QCAPI void DrawRectangle(float x, float y, float width, float height, Color color);
/**
 * @brief Draw a rectangle.
 * @param rectangle Rectangle to draw.
 * @param color Rectangle color.
 */
QCAPI void DrawRectangle(const Rectangle& rectangle, Color color);
/**
 * @brief Draw a rectangle using vectors.
 * @param position Top-left corner position.
 * @param size Rectangle size.
 * @param color Rectangle color.
 */
QCAPI void DrawRectangleV(Vec2 position, Vec2 size, Color color);
/**
 * @brief Draw a circle.
 * @param centerX X coordinate of the center.
 * @param centerY Y coordinate of the center.
 * @param radius Circle radius.
 * @param color Circle color.
 */
QCAPI void DrawCircle(float centerX, float centerY, float radius, Color color);
/**
 * @brief Draw a texture.
 * @param texture Texture to draw.
 * @param x X coordinate of the top-left corner.
 * @param y Y coordinate of the top-left corner.
 * @param tint Tint color.
 */
QCAPI void DrawTexture(const Texture2D& texture, float x, float y, Color tint = WHITE);

/**
 * @brief Draw a part of a texture (transformed).
 * @param texture Texture to draw.
 * @param source Source rectangle in pixels.
 * @param dest Destination rectangle in pixels.
 * @param origin Origin point for rotation/scale.
 * @param rotation Rotation in degrees.
 * @param tint Tint color.
 */
QCAPI void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle dest, Vec2 origin, float rotation, Color tint = WHITE);

/**
 * @brief Get the default font.
 * 
 * @return Default font object.
 */
QCAPI Font GetDefaultFont();

/**
 * @brief Draw text using the default font.
 *
 * @param text Text to draw.
 * @param x X coordinate.
 * @param y Y coordinate.
 * @param fontSize Font size in pixels.
 * @param color Text tint color.
 */
QCAPI void DrawText(const char* text, int x, int y, int fontSize, Color color);

/**
 * @brief Draw text with a custom font.
 *
 * @param font Font object.
 * @param text Text to draw.
 * @param position Screen position.
 * @param fontSize Font size in pixels.
 * @param spacing Additional character spacing in pixels.
 * @param tint Text tint color.
 */
QCAPI void DrawTextEx(Font font, const char* text, Vec2 position,
                float fontSize, float spacing, Color tint);

/**
 * @brief Measure text with a custom font.
 *
 * @param font Font object.
 * @param text Text to measure.
 * @param fontSize Font size in pixels.
 * @param spacing Additional character spacing in pixels.
 * @return Text size as Vec2.
 */
QCAPI Vec2 MeasureTextEx(Font font, const char* text,
                   float fontSize, float spacing);

/**
 * @brief Measure text using the default font.
 *
 * @param text Text to measure.
 * @param fontSize Font size in pixels.
 * @return Text width in pixels.
 */
QCAPI int MeasureText(const char* text, int fontSize);

/**
 * @brief Load a font from file.
 * 
 * @param filePath Path to the font file (.ttf, .otf, etc.).
 * @param fontSize Font size in pixels.
 * @return Loaded font object.
 * @return Invalid font (valid=false) on failure.
 */
QCAPI Font LoadFont(const char* filePath, int fontSize);

/**
 * @brief Unload a font and free resources.
 * 
 * @param font Font object to unload.
 */
QCAPI void UnloadFont(Font& font);

/**
 * @brief Load a texture from a file.
 * @param filePath Path to the texture file.
 * @return Loaded texture.
 * @return Empty texture on failure.
 */
QCAPI Texture2D LoadTexture(const char* filePath);
/**
 * @brief Load a render texture.
 * @param width Texture width.
 * @param height Texture height.
 * @return Loaded render texture.
 */
QCAPI RenderTexture2D LoadRenderTexture(int width, int height);
/**
 * @brief Unload a render texture.
 * @param target Render texture to unload.
 */
QCAPI void UnloadRenderTexture(RenderTexture2D target);
/**
 * @brief Generate a checker texture.
 * @param width Texture width.
 * @param height Texture height.
 * @param cellSize Cell size.
 * @param colorA Color A.
 * @param colorB Color B.
 * @return Generated texture.
 * @return Empty texture on failure.
 */
QCAPI Texture2D GenCheckerTexture(int width, int height, int cellSize, Color colorA, Color colorB);
/**
 * @brief Unload a texture.
 * @param texture Texture to unload.
 */
QCAPI void UnloadTexture(Texture2D& texture);

/**
 * @brief Load shader from vertex and fragment source files.
 * @param vsFileName Path to vertex shader file (can be NULL for default vertex shader).
 * @param fsFileName Path to fragment shader file (can be NULL for default fragment shader).
 * @return Loaded shader.
 * @return Empty shader on failure.
 */
QCAPI Shader LoadShader(const char* vsFileName, const char* fsFileName);

/**
 * @brief Load shader from vertex and fragment source strings.
 * @param vsSource Vertex shader source code string.
 * @param fsSource Fragment shader source code string.
 * @return Loaded shader.
 * @return Empty shader on failure.
 */
QCAPI Shader LoadShaderFromMemory(const char* vsSource, const char* fsSource);

/**
 * @brief Check if shader is valid.
 * @param shader Shader to check.
 * @return true if shader is valid.
 * @return false otherwise.
 */
QCAPI bool IsShaderValid(const Shader& shader);

/**
 * @brief Get uniform location in shader.
 * @param shader Shader to query.
 * @param uniformName Uniform name to find.
 * @return Uniform location index (-1 if not found).
 */
QCAPI int GetShaderLocation(const Shader& shader, const char* uniformName);

/**
 * @brief Get shader location using predefined index.
 * @param shader Shader to query.
 * @param locIndex Predefined shader location index.
 * @return Uniform or attribute location index (-1 if not found).
 */
QCAPI int GetShaderLocation(const Shader& shader, ShaderLocationIndex locIndex);

/**
 * @brief Get shader attribute location.
 * @param shader Shader to query.
 * @param attribName Attribute name to find.
 * @return Attribute location index (-1 if not found).
 */
QCAPI int GetShaderAttributeLocation(const Shader& shader, const char* attribName);

/**
 * @brief Set shader float uniform value.
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param value Float value to set.
 */
QCAPI void SetShaderValue(const Shader& shader, int locIndex, float value);

/**
 * @brief Set shader int uniform value.
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param value Int value to set.
 */
QCAPI void SetShaderValue(const Shader& shader, int locIndex, int value);

/**
 * @brief Set shader Vec2 uniform value.
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param value Vec2 value to set.
 */
QCAPI void SetShaderValue(const Shader& shader, int locIndex, const Vec2& value);

/**
 * @brief Set shader Vec3 uniform value.
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param value Vec3 value to set.
 */
QCAPI void SetShaderValue(const Shader& shader, int locIndex, const qc::Vec3& value);

/**
 * @brief Set shader Vec4 uniform value (color).
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param value Color value to set.
 */
QCAPI void SetShaderValue(const Shader& shader, int locIndex, const Color& value);

/**
 * @brief Set shader matrix uniform value.
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param mat 4x4 matrix (16 floats).
 */
QCAPI void SetShaderValueMatrix(const Shader& shader, int locIndex, const float* mat);

/**
 * @brief Set shader sampler2D uniform to texture unit.
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param textureUnit Texture unit index.
 */
QCAPI void SetShaderValueSampler(const Shader& shader, int locIndex, int textureUnit);

/**
 * @brief Begin shader mode (use shader for subsequent drawing).
 * @param shader Shader to use.
 */
QCAPI void BeginShaderMode(const Shader& shader);

/**
 * @brief End shader mode (restore default shader).
 */
QCAPI void EndShaderMode();

/**
 * @brief Unload shader and free resources.
 * @param shader Shader to unload.
 */
QCAPI void UnloadShader(Shader& shader);

/**
 * @brief Create a default 2D camera.
 * @return Camera2D with default settings.
 */
QCAPI Camera2D CreateCamera2D();

/**
 * @brief Begin 2D mode with custom camera.
 */
QCAPI void BeginMode2D(const Camera2D& camera);
QCAPI void EndMode2D();

/**
 * @brief Begin drawing to render texture.
 * @param target Target render texture.
 */
QCAPI void BeginTextureMode(RenderTexture2D target);
/**
 * @brief End drawing to render texture.
 */
QCAPI void EndTextureMode();

/**
 * @brief Create a default 3D camera.
 * @return Camera3D with default settings.
 */
QCAPI Camera3D CreateCamera3D();

/**
 * @brief Begin 3D mode with custom camera.
 */
QCAPI void BeginMode3D(const Camera3D& camera);
QCAPI void EndMode3D();

QCAPI void PushMatrix();
QCAPI void PopMatrix();

QCAPI void Translate(const Vec3& translation);
QCAPI void Translate(float x, float y, float z);
QCAPI void Rotate(float angle, const Vec3& axis);
QCAPI void Rotate(float angle);
QCAPI void Scale(const Vec3& scale);
QCAPI void Scale(float scale);
QCAPI void MultMatrix(const Mat4& matrix);

QCAPI void EnableBackfaceCulling();
QCAPI void DisableBackfaceCulling();

/**
 * @brief Convert screen coordinates to world coordinates (2D).
 * @param position Screen position.
 * @param camera 2D camera.
 * @return World position.
 */
QCAPI Vec2 GetScreenToWorld2D(Vec2 position, Camera2D camera);

/**
 * @brief Convert world coordinates to screen coordinates (2D).
 * @param position World position.
 * @param camera 2D camera.
 * @return Screen position.
 */
QCAPI Vec2 GetWorldToScreen2D(Vec2 position, Camera2D camera);

/**
 * @brief Convert world coordinates to screen coordinates (3D).
 * @param position World position.
 * @param camera 3D camera.
 * @return Screen position (as Vec3, z component is depth).
 */
QCAPI Vec3 GetWorldToScreen(Vec3 position, Camera3D camera);

/**
 * @brief Get a ray from screen coordinates through the camera (3D).
 * @param mousePosition Screen mouse position.
 * @param camera 3D camera.
 * @return Ray starting from camera position.
 */
QCAPI Ray GetScreenToWorldRay(Vec2 mousePosition, Camera3D camera);

/**
 * @brief Get the current modelview matrix.
 * @return Pointer to the 4x4 modelview matrix (16 floats).
 */
QCAPI const float* GetMatrixModelview();

/**
 * @brief Get the current projection matrix.
 * @return Pointer to the 4x4 projection matrix (16 floats).
 */
QCAPI const float* GetMatrixProjection();

/**
 * @brief Check if a key was just released.
 * @param key Key to check.
 * @return true if the key was just released.
 * @return false otherwise.
 */
QCAPI bool IsKeyReleased(KeyboardKey key);

/**
 * @brief Check if a key is NOT pressed.
 * @param key Key to check.
 * @return true if the key is NOT pressed.
 * @return false otherwise.
 */
QCAPI bool IsKeyUp(KeyboardKey key);

/**
 * @brief Get the last key pressed.
 * @return Key code of the last pressed key, or 0 if no key was pressed this frame.
 */
QCAPI int GetKeyPressed();

/**
 * @brief Get the last character pressed.
 * @return Character code of the last pressed character.
 */
QCAPI int GetCharPressed();

/**
 * @brief Set the key that exits the application.
 * @param key Exit key.
 */
QCAPI void SetExitKey(KeyboardKey key);

/**
 * @brief Get mouse movement delta for this frame.
 * @return Mouse delta movement.
 */
QCAPI Vec2 GetMouseDelta();

/**
 * @brief Set mouse position.
 * @param x Mouse X coordinate.
 * @param y Mouse Y coordinate.
 */
QCAPI void SetMousePosition(int x, int y);

/**
 * @brief Hide the mouse cursor.
 */
QCAPI void DisableCursor();

/**
 * @brief Show the mouse cursor.
 */
QCAPI void EnableCursor();

/**
 * @brief Check if the cursor is hidden.
 * @return true if cursor is hidden.
 * @return false otherwise.
 */
QCAPI bool IsCursorHidden();

/**
 * @brief Set the mouse cursor type.
 * @param cursor Cursor type.
 */
QCAPI void SetMouseCursor(MouseCursor cursor);

/**
 * @brief Check if a gamepad is available.
 * @param gamepad Gamepad index.
 * @return true if gamepad is available.
 * @return false otherwise.
 */
QCAPI bool IsGamepadAvailable(int gamepad);

/**
 * @brief Get gamepad name.
 * @param gamepad Gamepad index.
 * @return Gamepad name string.
 */
QCAPI const char* GetGamepadName(int gamepad);

/**
 * @brief Get gamepad axis movement value.
 * @param gamepad Gamepad index.
 * @param axis Gamepad axis.
 * @return Axis value (-1.0 to 1.0).
 */
QCAPI float GetGamepadAxisMovement(int gamepad, int axis);

/**
 * @brief Check if a gamepad button was just pressed.
 * @param gamepad Gamepad index.
 * @param button Gamepad button.
 * @return true if button was just pressed.
 * @return false otherwise.
 */
QCAPI bool IsGamepadButtonPressed(int gamepad, int button);

/**
 * @brief Check if a texture is valid.
 * @param texture Texture to check.
 * @return true if texture is valid.
 * @return false otherwise.
 */
QCAPI bool IsTextureValid(Texture2D texture);

/**
 * @brief Draw a texture at position.
 * @param texture Texture to draw.
 * @param position Position to draw at.
 * @param tint Tint color.
 */
QCAPI void DrawTextureV(Texture2D texture, Vec2 position, Color tint);

/**
 * @brief Draw a texture at position with rotation and scale.
 * @param texture Texture to draw.
 * @param position Position to draw at.
 * @param rotation Rotation in degrees.
 * @param scale Scale factor.
 * @param tint Tint color.
 */
QCAPI void DrawTextureEx(Texture2D texture, Vec2 position, float rotation, float scale, Color tint);

/**
 * @brief Draw part of a texture at position.
 * @param texture Texture to draw.
 * @param source Source rectangle in texture.
 * @param position Destination position.
 * @param tint Tint color.
 */
QCAPI void DrawTextureRec(Texture2D texture, Rectangle source, Vec2 position, Color tint);

/**
 * @brief Draw a tiled texture.
 * @param texture Texture to draw.
 * @param scale Texture scale.
 * @param offset Texture offset.
 * @param tint Tint color.
 */
QCAPI void DrawTextureTiled(Texture2D texture, float scale, Vec2 offset, Color tint);

/**
 * @brief Draw a textured polygon (n-patch).
 * @param texture Texture to draw.
 * @param source Source rectangle.
 * @param dest Destination rectangle.
 * @param origin Origin point.
 * @param rotation Rotation in degrees.
 * @param tint Tint color.
 */
QCAPI void DrawTextureNPatch(Texture2D texture, Rectangle source, Rectangle dest, Vec2 origin, float rotation, Color tint);

/**
 * @brief Check if a render texture is valid.
 * @param target Render texture to check.
 * @return true if render texture is valid.
 * @return false otherwise.
 */
QCAPI bool IsRenderTextureValid(RenderTexture2D target);

/**
 * @brief Get the color texture from a render texture.
 * @param target Render texture.
 * @return Color texture.
 */
QCAPI Texture2D GetRenderTextureTexture(RenderTexture2D target);

/**
 * @brief Draw a line.
 * @param x1 Start X coordinate.
 * @param y1 Start Y coordinate.
 * @param x2 End X coordinate.
 * @param y2 End Y coordinate.
 * @param color Line color.
 */
QCAPI void DrawLine(float x1, float y1, float x2, float y2, Color color);

/**
 * @brief Draw a line using vectors.
 * @param start Start position.
 * @param end End position.
 * @param color Line color.
 */
QCAPI void DrawLineV(Vec2 start, Vec2 end, Color color);

/**
 * @brief Draw rectangle outline.
 * @param rectangle Rectangle to outline.
 * @param lineWidth Line width.
 * @param color Line color.
 */
QCAPI void DrawRectangleLines(Rectangle rectangle, float lineWidth, Color color);

/**
 * @brief Draw a triangle.
 * @param v1 First vertex.
 * @param v2 Second vertex.
 * @param v3 Third vertex.
 * @param color Triangle color.
 */
QCAPI void DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color);

/**
 * @brief Draw circle outline.
 * @param centerX Center X coordinate.
 * @param centerY Center Y coordinate.
 * @param radius Circle radius.
 * @param color Circle color.
 */
QCAPI void DrawCircleLines(float centerX, float centerY, float radius, Color color);

/**
 * @brief Draw an ellipse.
 * @param centerX Center X coordinate.
 * @param centerY Center Y coordinate.
 * @param radiusH Horizontal radius.
 * @param radiusV Vertical radius.
 * @param color Ellipse color.
 */
QCAPI void DrawEllipse(float centerX, float centerY, float radiusH, float radiusV, Color color);

/**
 * @brief Draw a polygon.
 * @param center Center position.
 * @param sides Number of sides.
 * @param radius Polygon radius.
 * @param rotation Rotation in degrees.
 * @param color Polygon color.
 */
QCAPI void DrawPoly(Vec2 center, int sides, float radius, float rotation, Color color);

/**
 * @brief Draw a rounded rectangle.
 * @param rectangle Rectangle bounds.
 * @param roundness Roundness value (0.0 to 1.0).
 * @param segments Number of segments for corners.
 * @param color Rectangle color.
 */
QCAPI void DrawRectangleRounded(Rectangle rectangle, float roundness, int segments, Color color);

/**
 * @brief Fade a color by alpha.
 * @param color Color to fade.
 * @param alpha Alpha value (0.0 to 1.0).
 * @return Faded color.
 */
QCAPI Color Fade(Color color, float alpha);

/**
 * @brief Apply alpha to a color.
 * @param color Color to modify.
 * @param alpha Alpha value.
 * @return Color with alpha applied.
 */
QCAPI Color ColorAlpha(Color color, float alpha);

/**
 * @brief Tint a color by another color.
 * @param color Color to tint.
 * @param tint Tint color.
 * @return Tinted color.
 */
QCAPI Color ColorTint(Color color, Color tint);

/**
 * @brief Adjust color brightness.
 * @param color Color to adjust.
 * @param factor Brightness factor.
 * @return Adjusted color.
 */
QCAPI Color ColorBrightness(Color color, float factor);

/**
 * @brief Adjust color contrast.
 * @param color Color to adjust.
 * @param contrast Contrast factor.
 * @return Adjusted color.
 */
QCAPI Color ColorContrast(Color color, float contrast);

/**
 * @brief Get color from hex value.
 * @param hexValue Hex color value (0xRRGGBB).
 * @return Color.
 */
QCAPI Color GetColor(unsigned int hexValue);

/**
 * @brief Create color from normalized values (0.0-1.0 range).
 * @param r Red component (0.0-1.0).
 * @param g Green component (0.0-1.0).
 * @param b Blue component (0.0-1.0).
 * @param a Alpha component (0.0-1.0).
 * @return Color with values converted to 0-255 range.
 */
QCAPI Color ColorFromNormalized(float r, float g, float b, float a = 1.0f);

/**
 * @brief Check collision between two rectangles.
 * @param a First rectangle.
 * @param b Second rectangle.
 * @return true if rectangles collide.
 * @return false otherwise.
 */
QCAPI bool CheckCollisionRecs(Rectangle a, Rectangle b);

/**
 * @brief Check collision between two circles.
 * @param center1 Center of first circle.
 * @param radius1 Radius of first circle.
 * @param center2 Center of second circle.
 * @param radius2 Radius of second circle.
 * @return true if circles collide.
 * @return false otherwise.
 */
QCAPI bool CheckCollisionCircles(Vec2 center1, float radius1, Vec2 center2, float radius2);

/**
 * @brief Check collision between point and rectangle.
 * @param point Point position.
 * @param rect Rectangle.
 * @return true if point is in rectangle.
 * @return false otherwise.
 */
QCAPI bool CheckCollisionPointRec(Vec2 point, Rectangle rect);

/**
 * @brief Check collision between point and circle.
 * @param point Point position.
 * @param center Circle center.
 * @param radius Circle radius.
 * @return true if point is in circle.
 * @return false otherwise.
 */
QCAPI bool CheckCollisionPointCircle(Vec2 point, Vec2 center, float radius);

/**
 * @brief Wait for a specified time duration.
 * @param seconds Time to wait in seconds.
 */
QCAPI void WaitTime(double seconds);

/**
 * @brief Get a random integer value.
 * @param min Minimum value (inclusive).
 * @param max Maximum value (inclusive).
 * @return Random integer value.
 */
QCAPI int GetRandomValue(int min, int max);

/**
 * @brief Set the random number generator seed.
 * @param seed Random seed value.
 */
QCAPI void SetRandomSeed(unsigned int seed);

/**
 * @brief Check if the window is ready for drawing.
 * @return true if window is ready.
 * @return false otherwise.
 */
QCAPI bool IsWindowReady();

/**
 * @brief Check if a texture is ready for use.
 * @param texture Texture to check.
 * @return true if texture is ready.
 * @return false otherwise.
 */
QCAPI bool IsTextureReady(Texture2D texture);

/**
 * @brief Check if a shader is ready for use.
 * @param shader Shader to check.
 * @return true if shader is ready.
 * @return false otherwise.
 */
QCAPI bool IsShaderReady(Shader shader);

/**
 * @brief File path list structure.
 */
struct FilePathList {
    unsigned int count = 0;           // Filepaths entries count
    char** paths = nullptr;           // Filepaths entries
};

QCAPI int FileRename(const char* fileName, const char* fileRename); // Rename file (if exists)
QCAPI int FileRemove(const char* fileName);                         // Remove file (if exists)
QCAPI int FileCopy(const char* srcPath, const char* dstPath);       // Copy file from one path to another, dstPath created if it doesn't exist
QCAPI int FileMove(const char* srcPath, const char* dstPath);       // Move file from one directory to another, dstPath created if it doesn't exist
QCAPI int FileTextReplace(const char* fileName, const char* search, const char* replacement); // Replace text in an existing file
QCAPI int FileTextFindIndex(const char* fileName, const char* search); // Find text in existing file
QCAPI bool FileExists(const char* fileName);                        // Check if file exists
QCAPI bool DirectoryExists(const char* dirPath);                    // Check if a directory path exists
QCAPI bool IsFileExtension(const char* fileName, const char* ext);  // Check file extension (recommended include point: .png, .wav)
QCAPI int GetFileLength(const char* fileName);                      // Get file length in bytes (NOTE: GetFileSize() conflicts with windows.h)
QCAPI long GetFileModTime(const char* fileName);                    // Get file modification time (last write time)
QCAPI const char* GetFileExtension(const char* fileName);           // Get pointer to extension for a filename string (includes dot: '.png')
QCAPI const char* GetFileName(const char* filePath);                // Get pointer to filename for a path string
QCAPI const char* GetFileNameWithoutExt(const char* filePath);      // Get filename string without extension (uses static string)
QCAPI const char* GetDirectoryPath(const char* filePath);           // Get full path for a given fileName with path (uses static string)
QCAPI const char* GetPrevDirectoryPath(const char* dirPath);        // Get previous directory path for a given path (uses static string)
QCAPI const char* GetWorkingDirectory(void);                        // Get current working directory (uses static string)
QCAPI const char* GetApplicationDirectory(void);                    // Get the directory of the running application (uses static string)
QCAPI int MakeDirectory(const char* dirPath);                       // Create directories (including full path requested), returns 0 on success
QCAPI bool ChangeDirectory(const char* dirPath);                    // Change working directory, return true on success
QCAPI bool IsPathFile(const char* path);                            // Check if a given path is a file or a directory
QCAPI bool IsFileNameValid(const char* fileName);                   // Check if fileName is valid for the platform/OS
QCAPI FilePathList LoadDirectoryFiles(const char* dirPath);         // Load directory filepaths, files and directories, no subdirs scan
QCAPI FilePathList LoadDirectoryFilesEx(const char* basePath, const char* filter, bool scanSubdirs); // Load directory filepaths with extension filtering and subdir scan; some filters available: `*.*`,`FILES*`,`DIRS*`
QCAPI void UnloadDirectoryFiles(FilePathList files);                // Unload filepaths
QCAPI bool IsFileDropped(void);                                     // Check if a file has been dropped into window
QCAPI FilePathList LoadDroppedFiles(void);                          // Load dropped filepaths
QCAPI void UnloadDroppedFiles(FilePathList files);                  // Unload dropped filepaths
QCAPI unsigned int GetDirectoryFileCount(const char* dirPath);      // Get the file count in a directory
QCAPI unsigned int GetDirectoryFileCountEx(const char* basePath, const char* filter, bool scanSubdirs);

}  // namespace qc
