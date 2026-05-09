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

#include <cstdint>
#include "Quark3D.hpp"

#define QC_VERSION_MAJOR 1
#define QC_VERSION_MINOR 0
#define QC_VERSION_PATCH 0
#define QC_VERSION_STRING "1.0.0"
#define QC_VERSION (QC_VERSION_MAJOR * 10000 + QC_VERSION_MINOR * 100 + QC_VERSION_PATCH)

namespace qc {

/**
 * @brief Texture structure.
 */
struct Texture2D {
    unsigned int id = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
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
 * @brief Shader attribute data type enumeration.
 */
enum class ShaderAttributeDataType {
    Float = 0,
    Vec2,
    Vec3,
    Vec4,
};

/**
 * @brief Shader structure.
 */
struct Shader {
    unsigned int id = 0;               // Program ID
    int* locs = nullptr;               // Uniform locations array
    std::size_t locCount = 0;          // Number of stored locations
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
    D = 7,
    S = 22,
    W = 26,
    Space = 44,
    Escape = 41,
    Left = 80,
    Right = 79,
    Down = 81,
    Up = 82,
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
void InitWindow(int width, int height, const char* title);
/**
 * @brief Check if the window should close.
 *
 * @return true if close was requested.
 * @return false if the application should continue running.
 */
bool WindowShouldClose();
/**
 * @brief Close and destroy the application window.
 */
void CloseWindow();

/**
 * @brief Poll the next available event.
 *
 * @param event Reference to event structure that will receive event data.
 * @return true if an event was received.
 * @return false if no events are available.
 */
bool PollEvent(Event& event);
/**
 * @brief Wait until an event is received.
 *
 * @param event Reference to event structure that will receive event data.
 * @return true if an event was received.
 * @return false on failure.
 */
bool WaitEvent(Event& event);

/**
 * @brief Wait for an event with timeout.
 *
 * @param event Reference to event structure that will receive event data.
 * @param timeoutMs Timeout duration in milliseconds.
 * @return true if an event was received.
 * @return false if timeout was reached.
 */
bool WaitEventTimeout(Event& event, int timeoutMs);
/**
 * @brief Get event type name as string.
 *
 * @param type Event type enum value.
 * @return Pointer to event type name string.
 */
const char* GetEventTypeName(EventType type);

/**
 * @brief Set window title text.
 *
 * @param title New window title.
 * @return true on success.
 * @return false on failure.
 */
bool SetWindowTitle(const char* title);
/**
 * @brief Get current window title.
 *
 * @return Pointer to window title string.
 */
const char* GetWindowTitle();
/**
 * @brief Set window position.
 *
 * @param x Window X position.
 * @param y Window Y position.
 * @return true on success.
 * @return false on failure.
 */
bool SetWindowPosition(int x, int y);
/**
 * @brief Get current window position.
 *
 * @return Window position as IVec2.
 */
IVec2 GetWindowPosition();
/**
 * @brief Set window size.
 *
 * @param width New window width.
 * @param height New window height.
 * @return true on success.
 * @return false on failure.
 */
bool SetWindowSize(int width, int height);
/**
 * @brief Get current window size.
 *
 * @return Window size as IVec2.
 */
IVec2 GetWindowSize();
/**
 * @brief Get current window size in pixels.
 *
 * @return Pixel size as IVec2.
 */
IVec2 GetWindowSizeInPixels();
/**
 * @brief Set minimum allowed window size.
 *
 * @param width Minimum width.
 * @param height Minimum height.
 * @return true on success.
 * @return false on failure.
 */
bool SetWindowMinimumSize(int width, int height);
/**
 * @brief Get minimum window size.
 *
 * @return Minimum size as IVec2.
 */
IVec2 GetWindowMinimumSize();
/**
 * @brief Set maximum allowed window size.
 *
 * @param width Maximum width.
 * @param height Maximum height.
 * @return true on success.
 * @return false on failure.
 */
bool SetWindowMaximumSize(int width, int height);
/**
 * @brief Get maximum window size.
 *
 * @return Maximum size as IVec2.
 */
IVec2 GetWindowMaximumSize();
/**
 * @brief Set window resizable flag.
 *
 * @param resizable Resizable flag.
 * @return true on success.
 * @return false on failure.
 */
bool SetWindowResizable(bool resizable);
/**
 * @brief Set window bordered flag.
 *
 * @param bordered Bordered flag.
 * @return true on success.
 * @return false on failure.
 */
bool SetWindowBordered(bool bordered);
/**
 * @brief Set window fullscreen mode.
 *
 * @param fullscreen Fullscreen flag.
 * @return true on success.
 * @return false on failure.
 */
bool SetWindowFullscreen(bool fullscreen);
/**
 * @brief Toggle window fullscreen mode.
 *
 * @return true on success.
 * @return false on failure.
 */
bool ToggleFullscreen();
/**
 * @brief Show the window.
 *
 * @return true on success.
 * @return false on failure.
 */
bool ShowWindow();
/**
 * @brief Hide the window.
 *
 * @return true on success.
 * @return false on failure.
 */
bool HideWindow();
/**
 * @brief Raise the window.
 *
 * @return true on success.
 * @return false on failure.
 */
bool RaiseWindow();
/**
 * @brief Maximize the window.
 *
 * @return true on success.
 * @return false on failure.
 */
bool MaximizeWindow();
/**
 * @brief Minimize the window.
 *
 * @return true on success.
 * @return false on failure.
 */
bool MinimizeWindow();
/**
 * @brief Restore the window.
 *
 * @return true on success.
 * @return false on failure.
 */
bool RestoreWindow();
/**
 * @brief Sync the window.
 *
 * @return true on success.
 * @return false on failure.
 */
bool SyncWindow();
/**
 * @brief Check if the window is in fullscreen mode.
 *
 * @return true if the window is fullscreen.
 * @return false otherwise.
 */
bool IsWindowFullscreen();
/**
 * @brief Check if the window is hidden.
 *
 * @return true if the window is hidden.
 * @return false otherwise.
 */
bool IsWindowHidden();
/**
 * @brief Check if the window is minimized.
 *
 * @return true if the window is minimized.
 * @return false otherwise.
 */
bool IsWindowMinimized();
/**
 * @brief Check if the window is maximized.
 *
 * @return true if the window is maximized.
 * @return false otherwise.
 */
bool IsWindowMaximized();
/**
 * @brief Check if the window is focused.
 *
 * @return true if the window is focused.
 * @return false otherwise.
 */
bool IsWindowFocused();
/**
 * @brief Check if the window is in focus.
 *
 * @return true if the window is in focus.
 * @return false otherwise.
 */
bool IsWindowMouseFocused();
/**
 * @brief Check if the window is resizable.
 *
 * @return true if the window is resizable.
 * @return false otherwise.
 */
bool IsWindowResizable();
/**
 * @brief Check if the window is borderless.
 *
 * @return true if the window is borderless.
 * @return false otherwise.
 */
bool IsWindowBorderless();
/**
 * @brief Get the display scale of the window.
 *
 * @return Display scale as a float.
 */
float GetWindowDisplayScale();
/**
 * @brief Get the pixel density of the window.
 *
 * @return Pixel density as a float.
 */
float GetWindowPixelDensity();
/**
 * @brief Set the icon for the window.
 *
 * @param filePath Path to the icon file.
 * @return true on success.
 * @return false on failure.
 */
bool SetWindowIcon(const char* filePath);
/**
 * @brief Start text input.
 *
 * @return true on success.
 * @return false on failure.
 */
bool StartTextInput();
/**
 * @brief Stop text input.
 *
 * @return true on success.
 * @return false on failure.
 */
bool StopTextInput();
/**
 * @brief Check if text input is active.
 *
 * @return true if text input is active.
 * @return false otherwise.
 */
bool IsTextInputActive();

/**
 * @brief Set the log level.
 *
 * @param level Log level.
 */
void SetLogLevel(LogLevel level);
/**
 * @brief Trace a log message.
 *
 * @param level Log level.
 * @param logType Log type.
 * @param message Log message.
 */
void TraceLog(LogLevel level, const char* logType, const char* message);
/**
 * @brief Format a text string.
 *
 * @param format Format string (printf-style).
 * @param ... Format arguments.
 * @return Pointer to formatted string.
 */
const char* TextFormat(const char* format, ...);

/**
 * @brief Set the target FPS.
 *
 * @param fps Target FPS.
 */
void SetTargetFPS(int fps);
/**
 * @brief Get the frame time.
 *
 * @return Frame time as a float.
 */
float GetFrameTime();
/**
 * @brief Get the delta time.
 *
 * @return Delta time as a float.
 */
float GetDeltaTime();
/**
 * @brief Get the current FPS.
 *
 * @return Current FPS as an integer.
 */
int GetFPS();
/**
 * @brief Get the current time.
 *
 * @return Current time as a double.
 */
double GetTime();
/**
 * @brief Get the screen width.
 *
 * @return Screen width as an integer.
 */
int GetScreenWidth();
/**
 * @brief Get the screen height.
 *
 * @return Screen height as an integer.
 */
int GetScreenHeight();
/**
 * @brief Get the current monitor refresh rate.
 *
 * @return Refresh rate in Hz as a float.
 */
float GetCurrentMonitorRefreshRate();

/**
 * @brief Check if a key is pressed.
 *
 * @param key Key to check.
 * @return true if the key is pressed.
 * @return false otherwise.
 */
bool IsKeyDown(KeyboardKey key);
/**
 * @brief Check if a key was just pressed.
 *
 * @param key Key to check.
 * @return true if the key was just pressed.
 * @return false otherwise.
 */
bool IsKeyPressed(KeyboardKey key);
/**
 * @brief Check if a mouse button is pressed.
 *
 * @param button Button to check.
 * @return true if the button is pressed.
 * @return false otherwise.
 */
bool IsMouseButtonDown(MouseButton button);
/**
 * @brief Check if a mouse button was just pressed.
 *
 * @param button Button to check.
 * @return true if the button was just pressed.
 * @return false otherwise.
 */
bool IsMouseButtonPressed(MouseButton button);
/**
 * @brief Check if a mouse button was just released.
 *
 * @param button Button to check.
 * @return true if the button was just released.
 * @return false otherwise.
 */
bool IsMouseButtonReleased(MouseButton button);
/**
 * @brief Check if a mouse button is NOT pressed.
 *
 * @param button Button to check.
 * @return true if the button is NOT pressed.
 * @return false otherwise.
 */
bool IsMouseButtonUp(MouseButton button);
/**
 * @brief Get the mouse position.
 *
 * @return Mouse position as a Vec2.
 */
Vec2 GetMousePosition();
/**
 * @brief Get mouse wheel movement for both axes.
 *
 * @return Mouse wheel movement as a Vec2.
 */
Vec2 GetMouseWheelMoveV();
/**
 * @brief Get vertical mouse wheel movement.
 *
 * @return Vertical mouse wheel movement as a float.
 */
float GetMouseWheelMove();

/**
 * @brief Begin drawing.
 *
 * @return true on success.
 * @return false on failure.
 */
void BeginDrawing();
/**
 * @brief End drawing and present the frame.
 *
 * @return true on success.
 * @return false on failure.
 */
void EndDrawing();
/**
 * @brief Clear the background with a color.
 *
 * @param color Clear color.
 * @return true on success.
 * @return false on failure.
 */
void ClearBackground(Color color);

/**
 * @brief Draw a rectangle.
 * @param x X coordinate of the top-left corner.
 * @param y Y coordinate of the top-left corner.
 * @param width Width of the rectangle.
 * @param height Height of the rectangle.
 * @param color Rectangle color.
 */
void DrawRectangle(float x, float y, float width, float height, Color color);
/**
 * @brief Draw a rectangle.
 * @param rectangle Rectangle to draw.
 * @param color Rectangle color.
 */
void DrawRectangle(const Rectangle& rectangle, Color color);
/**
 * @brief Draw a rectangle using vectors.
 * @param position Top-left corner position.
 * @param size Rectangle size.
 * @param color Rectangle color.
 */
void DrawRectangleV(Vec2 position, Vec2 size, Color color);
/**
 * @brief Draw a circle.
 * @param centerX X coordinate of the center.
 * @param centerY Y coordinate of the center.
 * @param radius Circle radius.
 * @param color Circle color.
 */
void DrawCircle(float centerX, float centerY, float radius, Color color);
/**
 * @brief Draw a texture.
 * @param texture Texture to draw.
 * @param x X coordinate of the top-left corner.
 * @param y Y coordinate of the top-left corner.
 * @param tint Tint color.
 */
void DrawTexture(const Texture2D& texture, float x, float y, Color tint = WHITE);

/**
 * @brief Load a texture from a file.
 * @param filePath Path to the texture file.
 * @return Loaded texture.
 * @return Empty texture on failure.
 */
Texture2D LoadTexture(const char* filePath);
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
Texture2D GenCheckerTexture(int width, int height, int cellSize, Color colorA, Color colorB);
/**
 * @brief Unload a texture.
 * @param texture Texture to unload.
 */
void UnloadTexture(Texture2D& texture);

/**
 * @brief Load shader from vertex and fragment source files.
 * @param vsFileName Path to vertex shader file (can be NULL for default vertex shader).
 * @param fsFileName Path to fragment shader file (can be NULL for default fragment shader).
 * @return Loaded shader.
 * @return Empty shader on failure.
 */
Shader LoadShader(const char* vsFileName, const char* fsFileName);

/**
 * @brief Load shader from vertex and fragment source strings.
 * @param vsSource Vertex shader source code string.
 * @param fsSource Fragment shader source code string.
 * @return Loaded shader.
 * @return Empty shader on failure.
 */
Shader LoadShaderFromMemory(const char* vsSource, const char* fsSource);

/**
 * @brief Check if shader is valid.
 * @param shader Shader to check.
 * @return true if shader is valid.
 * @return false otherwise.
 */
bool IsShaderValid(const Shader& shader);

/**
 * @brief Get uniform location in shader.
 * @param shader Shader to query.
 * @param uniformName Uniform name to find.
 * @return Uniform location index (-1 if not found).
 */
int GetShaderLocation(const Shader& shader, const char* uniformName);

/**
 * @brief Get shader attribute location.
 * @param shader Shader to query.
 * @param attribName Attribute name to find.
 * @return Attribute location index (-1 if not found).
 */
int GetShaderAttributeLocation(const Shader& shader, const char* attribName);

/**
 * @brief Set shader float uniform value.
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param value Float value to set.
 */
void SetShaderValue(const Shader& shader, int locIndex, float value);

/**
 * @brief Set shader int uniform value.
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param value Int value to set.
 */
void SetShaderValue(const Shader& shader, int locIndex, int value);

/**
 * @brief Set shader Vec2 uniform value.
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param value Vec2 value to set.
 */
void SetShaderValue(const Shader& shader, int locIndex, const Vec2& value);

/**
 * @brief Set shader Vec3 uniform value.
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param value Vec3 value to set.
 */
void SetShaderValue(const Shader& shader, int locIndex, const qc::Vec3& value);

/**
 * @brief Set shader Vec4 uniform value (color).
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param value Color value to set.
 */
void SetShaderValue(const Shader& shader, int locIndex, const Color& value);

/**
 * @brief Set shader matrix uniform value.
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param mat 4x4 matrix (16 floats).
 */
void SetShaderValueMatrix(const Shader& shader, int locIndex, const float* mat);

/**
 * @brief Set shader sampler2D uniform to texture unit.
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param textureUnit Texture unit index.
 */
void SetShaderValueSampler(const Shader& shader, int locIndex, int textureUnit);

/**
 * @brief Begin shader mode (use shader for subsequent drawing).
 * @param shader Shader to use.
 */
void BeginShaderMode(const Shader& shader);

/**
 * @brief End shader mode (restore default shader).
 */
void EndShaderMode();

/**
 * @brief Unload shader and free resources.
 * @param shader Shader to unload.
 */
void UnloadShader(Shader& shader);

/**
 * @brief Create a default 2D camera.
 * @return Camera2D with default settings.
 */
Camera2D CreateCamera2D();

/**
 * @brief Begin 2D mode with custom camera.
 */
void BeginMode2D(const Camera2D& camera);
void EndMode2D();

/**
 * @brief Create a default 3D camera.
 * @return Camera3D with default settings.
 */
Camera3D CreateCamera3D();

/**
 * @brief Begin 3D mode with custom camera.
 */
void BeginMode3D(const Camera3D& camera);
void EndMode3D();

}  // namespace qc
