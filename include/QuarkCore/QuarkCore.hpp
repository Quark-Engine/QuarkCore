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
        * Vulkan
        * Direct3D 11

    Language:
        * Modern C++

    ========================================================
*/

#ifndef __QUARK_CORE__
#define __QUARK_CORE__

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
#if defined(QC_ENABLE_VULKAN)
#include <vulkan/vulkan.h>
#endif
#if defined(QC_ENABLE_D3D11)
struct ID3D11Device;
struct ID3D11DeviceContext;
#endif

namespace qc {

/** 
 * @brief Renderer type enumeration.
 */
enum class RendererType {
    Auto,
    OpenGL,
    Vulkan,
    D3D11
};

/**
 * @brief Texture structure.
 */
struct Texture {
    unsigned int id = 0;     // Texture id
    int width = 0;           // Texture base width
    int height = 0;          // Texture base height
    int mipmaps = 1;         // Mipmap levels, 1 by default
    int format = 0;          // Data format (PixelFormat type)
    bool valid = false;      // Whether the texture is valid and ready to use
};

using Texture2D = Texture;
using TextureCubemap = Texture;

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

using RenderTexture = RenderTexture2D;

#include "QuarkImage.hpp"

/**
 * @brief Font glyph info structure.
 */
struct GlyphInfo {
    int value = 0;         // Character value (Unicode)
    int offsetX = 0;       // Character offset X when drawing
    int offsetY = 0;       // Character offset Y when drawing
    int advanceX = 0;      // Character advance position X
    Image image;           // Character image data
};

/**
 * @brief Font structure.
 */
struct Font {
    int baseSize = 0;             // Base size (default chars height)
    int glyphCount = 0;           // Number of glyph characters
    int glyphPadding = 0;         // Padding around the glyph characters
    Texture2D texture;            // Texture atlas containing the glyphs
    Rectangle* recs = nullptr;    // Rectangles in texture for the glyphs
    GlyphInfo* glyphs = nullptr;  // Glyphs info data
    bool valid = false;           // Whether the font is valid and ready to use
    uint32_t _rendererFontId = 0; // Internal renderer font handle
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

inline Mat4 GetCameraMat4(const Camera3D& camera) {
    return Mat4::lookAt(camera.position, camera.target, camera.up);
}

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

inline constexpr MouseButton MOUSE_BUTTON_LEFT = MouseButton::Left;
inline constexpr MouseButton MOUSE_LEFT_BUTTON = MouseButton::Left;
inline constexpr KeyboardKey KEY_NULL = KeyboardKey::Unknown;
inline constexpr KeyboardKey KEY_LEFT_CONTROL = KeyboardKey::LeftControl;
inline constexpr KeyboardKey KEY_RIGHT_CONTROL = KeyboardKey::RightControl;

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
QCAPI void InitWindow(int width, int height, const char* title, RendererType rendererType = RendererType::Auto);

enum class TextureFilterMode {
    Nearest,
    Linear
};

/**
 * @brief Allocate a block of memory on the heap.
 * @param size Number of bytes to allocate.
 * @return Pointer to the allocated memory block.
 */
QCAPI void*           MemAlloc(size_t size);

/**
 * @brief Resize a previously allocated block of memory.
 * @param ptr Pointer to the memory block to resize.
 * @param oldSize Previous size of the memory block, in bytes.
 * @param newSize New desired size of the memory block, in bytes.
 * @return Pointer to the resized memory block.
 */
QCAPI void*           MemRealloc(void* ptr, size_t oldSize, size_t newSize);

/**
 * @brief Free a block of memory previously allocated with MemAlloc/MemRealloc.
 * @param ptr Pointer to the memory block to free.
 */
QCAPI void            MemFree(void* ptr);

/**
 * @brief Set the requested multisample anti-aliasing sample count.
 * @param samples Requested sample count.
 */
QCAPI void SetMSAASamples(int samples);
/**
 * @brief Set the default texture filtering mode.
 * @param mode Texture filtering mode.
 */
QCAPI void SetTextureFilterMode(TextureFilterMode mode);

/**
 * @brief Get the currently active rendering backend.
 */
QCAPI RendererType GetCurrentBackend();
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

#if defined(QC_ENABLE_VULKAN)
/**
 * @brief Get the active Vulkan instance.
 * @return Vulkan instance, or VK_NULL_HANDLE if unavailable.
 */
QCAPI VkInstance GetVulkanInstance();
/**
 * @brief Get the active Vulkan physical device.
 * @return Vulkan physical device, or VK_NULL_HANDLE if unavailable.
 */
QCAPI VkPhysicalDevice GetVulkanPhysicalDevice();
/**
 * @brief Get the active Vulkan logical device.
 * @return Vulkan device, or VK_NULL_HANDLE if unavailable.
 */
QCAPI VkDevice GetVulkanDevice();
/**
 * @brief Get the Vulkan graphics queue family index.
 * @return Queue family index, or UINT32_MAX if unavailable.
 */
QCAPI uint32_t GetVulkanGraphicsQueueFamily();
/**
 * @brief Get the Vulkan graphics queue.
 * @return Vulkan queue, or VK_NULL_HANDLE if unavailable.
 */
QCAPI VkQueue GetVulkanGraphicsQueue();
/**
 * @brief Get the descriptor pool used by the Vulkan renderer.
 * @return Vulkan descriptor pool, or VK_NULL_HANDLE if unavailable.
 */
QCAPI VkDescriptorPool GetVulkanDescriptorPool();
/**
 * @brief Get the main Vulkan render pass.
 * @return Vulkan render pass, or VK_NULL_HANDLE if unavailable.
 */
QCAPI VkRenderPass GetVulkanMainRenderPass();
/**
 * @brief Get the minimum swapchain image count.
 * @return Minimum image count.
 */
QCAPI uint32_t GetVulkanMinImageCount();
/**
 * @brief Get the current Vulkan swapchain image count.
 * @return Current image count.
 */
QCAPI uint32_t GetVulkanImageCount();
/**
 * @brief Get the active Vulkan MSAA sample count.
 * @return Vulkan sample count flags.
 */
QCAPI VkSampleCountFlagBits GetVulkanMSAASamples();
/**
 * @brief Get a Vulkan texture descriptor set.
 * @param textureId QuarkCore texture ID.
 * @return Descriptor set, or VK_NULL_HANDLE if unavailable.
 */
QCAPI VkDescriptorSet GetVulkanTextureDescriptorSet(uint32_t textureId);

using VulkanRenderCallback = void(*)(VkCommandBuffer commandBuffer);
/**
 * @brief Set a callback invoked while recording Vulkan rendering commands.
 * @param callback Callback function, or nullptr to clear it.
 */
QCAPI void SetVulkanRenderCallback(VulkanRenderCallback callback);
/**
 * @brief Get the currently registered Vulkan render callback.
 * @return Registered callback, or nullptr if none is set.
 */
QCAPI VulkanRenderCallback GetVulkanRenderCallback();
#endif

#if defined(QC_ENABLE_D3D11)
/**
 * @brief Get the active D3D11 device.
 * @return D3D11 device, or nullptr if unavailable.
 */
QCAPI ID3D11Device *GetD3D11Device();
/**
 * @brief Get the active D3D11 immediate context.
 * @return D3D11 immediate context, or nullptr if unavailable.
 */
QCAPI ID3D11DeviceContext *GetD3D11ImmediateContext();

using D3D11RenderCallback = void(*)(ID3D11DeviceContext *deviceContext);
/**
 * @brief Set a callback invoked before the D3D11 frame is presented.
 * @param callback Callback function, or nullptr to clear it.
 */
QCAPI void SetD3D11RenderCallback(D3D11RenderCallback callback);
/**
 * @brief Get the currently registered D3D11 render callback.
 * @return Registered callback, or nullptr if none is set.
 */
QCAPI D3D11RenderCallback GetD3D11RenderCallback();
#endif

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

using NativeEventCallback = void(*)(const SDL_Event* event);

/**
 * @brief Set a callback that receives each native SDL event.
 * @param callback Callback function, or nullptr to clear it.
 */
QCAPI void SetNativeEventCallback(NativeEventCallback callback);

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
 * @brief Enable or disable vertical synchronization (VSync).
 *
 * @param enabled True to enable VSync, false to disable.
 * @return true on success.
 * @return false on failure.
 */
QCAPI bool SetVSync(bool enabled);
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
inline int GetMouseX() { return static_cast<int>(GetMousePosition().x); }
inline int GetMouseY() { return static_cast<int>(GetMousePosition().y); }
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
 * @brief Release a vertex array object.
 * @param vaoId Renderer vertex array object ID.
 */
QCAPI void UnloadVertexArray(unsigned int vaoId);
/**
 * @brief Release a vertex buffer object.
 * @param vboId Renderer vertex buffer object ID.
 */
QCAPI void UnloadVertexBuffer(unsigned int vboId);

/**
 * @brief Load a shader from vertex and fragment shader files.
 *
 * On the OpenGL backend, specify the paths to the GLSL versions of the shaders
 * (`.vert` and `.frag`). They will be loaded and compiled into an OpenGL shader
 * program automatically.
 * 
 * On the Vulkan backend, specify the paths to the SPIR-V versions of the
 * shaders (`.spv`). They will be loaded and compiled into a Vulkan shader
 * module automatically.
 * 
 * On the Direct3D 11 backend, specify the paths to the HLSL versions of the shaders
 * (`.hlsl`). They will be loaded and compiled into a Direct3D 11 shader
 * program automatically.
 *
 * @param vsFileName Path to the vertex shader file (can be NULL for the default vertex shader).
 * @param fsFileName Path to the fragment shader file (can be NULL for the default fragment shader).
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
 * @brief Enable or disable one of the standard 3D light slots.
 * @param index Light slot index.
 * @param enabled Whether the light should be enabled.
 */
QCAPI void Set3DLightEnabled(int index, bool enabled);

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
 * @brief Set shader Vec4 uniform value.
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param value Vec4 value to set.
 */
QCAPI void SetShaderValue(const Shader& shader, int locIndex, const qc::Vec4& value);

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
 * @brief Set a shader uniform using a raw value and uniform type.
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param value Pointer to the uniform value.
 * @param uniformType Uniform type constant.
 */
QCAPI void SetShaderValue(const Shader& shader, int locIndex, const void* value, int uniformType);
/**
 * @brief Set an array of shader uniform values.
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param value Pointer to the first value.
 * @param uniformType Uniform type constant.
 * @param count Number of values.
 */
QCAPI void SetShaderValueV(const Shader& shader, int locIndex, const void* value, int uniformType, int count);
/**
 * @brief Set a shader matrix uniform from a Matrix object.
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param mat Matrix value.
 */
QCAPI void SetShaderValueMatrix(const Shader& shader, int locIndex, const Matrix& mat);

/**
 * @brief Set shader sampler2D uniform to texture unit.
 * @param shader Shader to modify.
 * @param locIndex Uniform location index.
 * @param textureUnit Texture unit index.
 */
QCAPI void SetShaderValueSampler(const Shader& shader, int locIndex, int textureUnit);
/**
 * @brief Bind a texture to a shader sampler uniform.
 * @param shader Shader to modify.
 * @param locIndex Sampler location index.
 * @param texture Texture to bind.
 */
QCAPI void SetShaderValueTexture(const Shader& shader, int locIndex, const Texture2D& texture);
/**
 * @brief Bind a texture to a shader sampler and explicit texture unit.
 * @param shader Shader to modify.
 * @param locIndex Sampler location index.
 * @param texture Texture to bind.
 * @param textureUnit Texture unit index.
 */
QCAPI void SetShaderValueTextureUnit(const Shader& shader, int locIndex, const Texture2D& texture, int textureUnit);

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
/**
 * @brief End the active 2D camera mode.
 */
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
/**
 * @brief End the active 3D camera mode.
 */
QCAPI void EndMode3D();

/**
 * @brief Save the current renderer transform state.
 */
QCAPI void PushMatrix();
/**
 * @brief Restore the previously saved renderer transform state.
 */
QCAPI void PopMatrix();

/**
 * @brief Apply a 3D translation to the current transform.
 * @param translation Translation vector.
 */
QCAPI void Translate(const Vec3& translation);
/**
 * @brief Apply a 3D translation to the current transform.
 * @param x Translation on the X axis.
 * @param y Translation on the Y axis.
 * @param z Translation on the Z axis.
 */
QCAPI void Translate(float x, float y, float z);
/**
 * @brief Apply an axis-angle rotation to the current transform.
 * @param angle Rotation angle in radians.
 * @param axis Rotation axis.
 */
QCAPI void Rotate(float angle, const Vec3& axis);
/**
 * @brief Apply a rotation around the current 2D rotation axis.
 * @param angle Rotation angle in degrees.
 */
QCAPI void Rotate(float angle);
/**
 * @brief Apply a non-uniform scale to the current transform.
 * @param scale Scale vector.
 */
QCAPI void Scale(const Vec3& scale);
/**
 * @brief Apply a uniform scale to the current transform.
 * @param scale Uniform scale factor.
 */
QCAPI void Scale(float scale);
/**
 * @brief Multiply the current transform by a matrix.
 * @param matrix Matrix to multiply by.
 */
QCAPI void MultMatrix(const Mat4& matrix);

/**
 * @brief Enable back-face culling for subsequent drawing.
 */
QCAPI void EnableBackfaceCulling();
/**
 * @brief Disable back-face culling for subsequent drawing.
 */
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
 * @brief Standard QuarkCore gamepad buttons.
 */
enum GamepadButton {
    GAMEPAD_BUTTON_UNKNOWN = 0,
    GAMEPAD_BUTTON_LEFT_FACE_UP,
    GAMEPAD_BUTTON_LEFT_FACE_RIGHT,
    GAMEPAD_BUTTON_LEFT_FACE_DOWN,
    GAMEPAD_BUTTON_LEFT_FACE_LEFT,
    GAMEPAD_BUTTON_RIGHT_FACE_UP,
    GAMEPAD_BUTTON_RIGHT_FACE_RIGHT,
    GAMEPAD_BUTTON_RIGHT_FACE_DOWN,
    GAMEPAD_BUTTON_RIGHT_FACE_LEFT,
    GAMEPAD_BUTTON_LEFT_TRIGGER_1,
    GAMEPAD_BUTTON_LEFT_TRIGGER_2,
    GAMEPAD_BUTTON_RIGHT_TRIGGER_1,
    GAMEPAD_BUTTON_RIGHT_TRIGGER_2,
    GAMEPAD_BUTTON_MIDDLE_LEFT,
    GAMEPAD_BUTTON_MIDDLE,
    GAMEPAD_BUTTON_MIDDLE_RIGHT,
    GAMEPAD_BUTTON_LEFT_THUMB,
    GAMEPAD_BUTTON_RIGHT_THUMB,
    GAMEPAD_BUTTON_COUNT
};

using GamepadAxis = SDL_GamepadAxis;

/**
 * @brief Return the number of connected gamepads.
 * @return Number of connected gamepads.
 */
QCAPI int GetGamepadCount();
/**
 * @brief Check whether a mapped gamepad button is currently held.
 * @param gamepad Gamepad index.
 * @param button Quark gamepad button.
 * @return True while the button is held.
 */
QCAPI bool IsGamepadButtonDown(int gamepad, int button);
/**
 * @brief Check whether a mapped gamepad button is currently released.
 * @param gamepad Gamepad index.
 * @param button Quark gamepad button.
 * @return True while the button is not held.
 */
QCAPI bool IsGamepadButtonUp(int gamepad, int button);
/**
 * @brief Check whether a mapped gamepad button was released this frame.
 * @param gamepad Gamepad index.
 * @param button Quark gamepad button.
 * @return True if the button was released during the current input update.
 */
QCAPI bool IsGamepadButtonReleased(int gamepad, int button);
/**
 * @brief Return and consume the last gamepad button pressed this frame.
 * @return Quark gamepad button, or GAMEPAD_BUTTON_UNKNOWN if none was pressed.
 */
QCAPI int GetGamepadButtonPressed();
/**
 * @brief Return the number of standard axes supported by a gamepad.
 * @param gamepad Gamepad index.
 * @return Number of standard axes, or zero if unavailable.
 */
QCAPI int GetGamepadAxisCount(int gamepad);
/**
 * @brief Set an axis dead zone in the range [0, 1).
 * @param gamepad Gamepad index.
 * @param axis Standard gamepad axis.
 * @param deadZone Dead zone threshold.
 * @return True when the value was accepted.
 */
QCAPI bool SetGamepadAxisDeadZone(int gamepad, GamepadAxis axis, float deadZone);
/**
 * @brief Return the configured dead zone for a gamepad axis.
 * @param gamepad Gamepad index.
 * @param axis Standard gamepad axis.
 * @return Dead zone threshold.
 */
QCAPI float GetGamepadAxisDeadZone(int gamepad, GamepadAxis axis);
/**
 * @brief Start gamepad vibration.
 * @param gamepad Gamepad index.
 * @param lowFrequency Low-frequency motor strength from 0.0 to 1.0.
 * @param highFrequency High-frequency motor strength from 0.0 to 1.0.
 * @param durationSeconds Vibration duration in seconds.
 */
QCAPI void SetGamepadVibration(int gamepad, float lowFrequency, float highFrequency, float durationSeconds);
/**
 * @brief Add an SDL gamepad mapping string.
 * @param mappings SDL gamepad mapping string.
 * @return SDL mapping result code.
 */
QCAPI int SetGamepadMappings(const char* mappings);
/**
 * @brief Add an SDL gamepad mapping string and report success.
 * @param mapping SDL gamepad mapping string.
 * @return True when the mapping was accepted.
 */
QCAPI bool AddGamepadMapping(const char* mapping);
/**
 * @brief Return the mapping string for a gamepad.
 * @param gamepad Gamepad index.
 * @return Pointer to an internal mapping string, or an empty string.
 */
QCAPI const char* GetGamepadMapping(int gamepad);

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
 * @brief N-Patch layout constants.
 */
#define NPATCH_NINE_PATCH            0    // Npatch layout: 3x3 tiles
#define NPATCH_THREE_PATCH_HORIZONTAL 1   // Npatch layout: 1x3 tiles
#define NPATCH_THREE_PATCH_VERTICAL  2    // Npatch layout: 3x1 tiles

/**
 * @brief N-Patch info structure.
 */
struct NPatchInfo {
    Rectangle source;   // Texture source rectangle
    int left = 0;       // Left border offset
    int top = 0;        // Top border offset
    int right = 0;      // Right border offset
    int bottom = 0;     // Bottom border offset
    int layout = 0;     // Layout of the n-patch: 3x3, 1x3 or 3x1
};

/**
 * @brief Draw a texture (or part of it) that stretches or shrinks nicely.
 * @param texture Texture to draw.
 * @param nPatchInfo N-patch layout info (source rectangle and border offsets).
 * @param dest Destination rectangle.
 * @param origin Origin point.
 * @param rotation Rotation in degrees.
 * @param tint Tint color.
 */
QCAPI void DrawTextureNPatch(Texture2D texture, NPatchInfo nPatchInfo, Rectangle dest, Vec2 origin, float rotation, Color tint);

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

/**
 * @brief Rename an existing file.
 * @param fileName Source path.
 * @param fileRename Destination path.
 * @return Zero on success, non-zero on failure.
 */
QCAPI int FileRename(const char* fileName, const char* fileRename);
/**
 * @brief Remove an existing file.
 * @param fileName File path.
 * @return Zero on success, non-zero on failure.
 */
QCAPI int FileRemove(const char* fileName);
/**
 * @brief Copy a file.
 * @param srcPath Source path.
 * @param dstPath Destination path.
 * @return Zero on success, non-zero on failure.
 */
QCAPI int FileCopy(const char* srcPath, const char* dstPath);
/**
 * @brief Move a file.
 * @param srcPath Source path.
 * @param dstPath Destination path.
 * @return Zero on success, non-zero on failure.
 */
QCAPI int FileMove(const char* srcPath, const char* dstPath);
/**
 * @brief Replace text in an existing file.
 * @param fileName File path.
 * @param search Text to find.
 * @param replacement Replacement text.
 * @return Zero on success, non-zero on failure.
 */
QCAPI int FileTextReplace(const char* fileName, const char* search, const char* replacement);
/**
 * @brief Find text in a file.
 * @param fileName File path.
 * @param search Text to find.
 * @return Matching byte index, or -1 when not found.
 */
QCAPI int FileTextFindIndex(const char* fileName, const char* search);
/**
 * @brief Check whether a file exists.
 * @param fileName File path.
 * @return True when the file exists.
 */
QCAPI bool FileExists(const char* fileName);
/**
 * @brief Check whether a directory exists.
 * @param dirPath Directory path.
 * @return True when the directory exists.
 */
QCAPI bool DirectoryExists(const char* dirPath);
/**
 * @brief Check a file extension.
 * @param fileName File path.
 * @param ext Extension, preferably including '.'.
 * @return True when it matches.
 */
QCAPI bool IsFileExtension(const char* fileName, const char* ext);
/**
 * @brief Get a file size in bytes.
 * @param fileName File path.
 * @return File length, or -1 on failure.
 */
QCAPI int GetFileLength(const char* fileName);
/**
 * @brief Get the last modification time of a file.
 * @param fileName File path.
 * @return Platform file timestamp.
 */
QCAPI long GetFileModTime(const char* fileName);
/**
 * @brief Get the extension portion of a path.
 * @param fileName File path.
 * @return Pointer to a static result string.
 */
QCAPI const char* GetFileExtension(const char* fileName);
/**
 * @brief Get the filename portion of a path.
 * @param filePath File path.
 * @return Pointer to a static result string.
 */
QCAPI const char* GetFileName(const char* filePath);
/**
 * @brief Get a filename without its extension.
 * @param filePath File path.
 * @return Pointer to a static result string.
 */
QCAPI const char* GetFileNameWithoutExt(const char* filePath);
/**
 * @brief Get the directory portion of a path.
 * @param filePath File path.
 * @return Pointer to a static result string.
 */
QCAPI const char* GetDirectoryPath(const char* filePath);
/**
 * @brief Get the parent directory of a path.
 * @param dirPath Directory path.
 * @return Pointer to a static result string.
 */
QCAPI const char* GetPrevDirectoryPath(const char* dirPath);
/**
 * @brief Get the current working directory.
 * @return Pointer to a static result string.
 */
QCAPI const char* GetWorkingDirectory(void);
/**
 * @brief Get the directory containing the application executable.
 * @return Pointer to a static result string.
 */
QCAPI const char* GetApplicationDirectory(void);
/**
 * @brief Create a directory and its missing parents.
 * @param dirPath Directory path.
 * @return Zero on success, non-zero on failure.
 */
QCAPI int MakeDirectory(const char* dirPath);
/**
 * @brief Change the current working directory.
 * @param dirPath Directory path.
 * @return True on success.
 */
QCAPI bool ChangeDirectory(const char* dirPath);
/**
 * @brief Check whether a path identifies a file.
 * @param path Path to inspect.
 * @return True when it is a file.
 */
QCAPI bool IsPathFile(const char* path);
/**
 * @brief Validate a filename for the current platform.
 * @param fileName Filename to validate.
 * @return True when valid.
 */
QCAPI bool IsFileNameValid(const char* fileName);
/**
 * @brief Load files and directories from a directory without recursion.
 * @param dirPath Directory path.
 * @return Allocated path list.
 */
QCAPI FilePathList LoadDirectoryFiles(const char* dirPath);
/**
 * @brief Load directory entries with filtering and optional recursion.
 * @param basePath Base directory path.
 * @param filter Entry filter.
 * @param scanSubdirs Whether to scan subdirectories.
 * @return Allocated path list.
 */
QCAPI FilePathList LoadDirectoryFilesEx(const char* basePath, const char* filter, bool scanSubdirs);
/**
 * @brief Release a directory path list.
 * @param files Path list to release.
 */
QCAPI void UnloadDirectoryFiles(FilePathList files);
/**
 * @brief Check whether a file was dropped onto the window.
 * @return True when a drop is pending.
 */
QCAPI bool IsFileDropped(void);
/**
 * @brief Load paths from the latest file-drop operation.
 * @return Allocated path list.
 */
QCAPI FilePathList LoadDroppedFiles(void);
/**
 * @brief Release a dropped-file path list.
 * @param files Path list to release.
 */
QCAPI void UnloadDroppedFiles(FilePathList files);
/**
 * @brief Count entries in a directory without recursion.
 * @param dirPath Directory path.
 * @return Entry count.
 */
QCAPI unsigned int GetDirectoryFileCount(const char* dirPath);
/**
 * @brief Count filtered directory entries with optional recursion.
 * @param basePath Base directory path.
 * @param filter Entry filter.
 * @param scanSubdirs Whether to scan subdirectories.
 * @return Entry count.
 */
QCAPI unsigned int GetDirectoryFileCountEx(const char* basePath, const char* filter, bool scanSubdirs);

}  // namespace qc

#endif // __QUARK_CORE__
