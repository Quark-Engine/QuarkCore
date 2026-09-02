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
    #if defined(QUARKCORE_STATIC)
        #define QCAPI
    #elif defined(QUARKCORE_BUILD_DLL)
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
struct ID3D11ShaderResourceView;
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
#include "QuarkUtils.hpp"

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
inline constexpr MouseButton MOUSE_BUTTON_MIDDLE = MouseButton::Middle;
inline constexpr MouseButton MOUSE_BUTTON_RIGHT = MouseButton::Right;
inline constexpr MouseButton MOUSE_LEFT_BUTTON = MouseButton::Left;
inline constexpr MouseButton MOUSE_MIDDLE_BUTTON = MouseButton::Middle;
inline constexpr MouseButton MOUSE_RIGHT_BUTTON = MouseButton::Right;

inline constexpr KeyboardKey KEY_NULL = KeyboardKey::Unknown;
inline constexpr KeyboardKey KEY_LEFT = KeyboardKey::Left;
inline constexpr KeyboardKey KEY_RIGHT = KeyboardKey::Right;
inline constexpr KeyboardKey KEY_UP = KeyboardKey::Up;
inline constexpr KeyboardKey KEY_DOWN = KeyboardKey::Down;
inline constexpr KeyboardKey KEY_SPACE = KeyboardKey::Space;
inline constexpr KeyboardKey KEY_ENTER = KeyboardKey::Enter;
inline constexpr KeyboardKey KEY_ESCAPE = KeyboardKey::Escape;
inline constexpr KeyboardKey KEY_BACKSPACE = KeyboardKey::Backspace;
inline constexpr KeyboardKey KEY_LEFT_CONTROL = KeyboardKey::LeftControl;
inline constexpr KeyboardKey KEY_RIGHT_CONTROL = KeyboardKey::RightControl;
inline constexpr KeyboardKey KEY_LEFT_SHIFT = KeyboardKey::LeftShift;
inline constexpr KeyboardKey KEY_RIGHT_SHIFT = KeyboardKey::RightShift;
inline constexpr KeyboardKey KEY_LEFT_ALT = KeyboardKey::LeftAlt;
inline constexpr KeyboardKey KEY_RIGHT_ALT = KeyboardKey::RightAlt;
inline constexpr KeyboardKey KEY_LEFT_SUPER = KeyboardKey::LeftSuper;
inline constexpr KeyboardKey KEY_RIGHT_SUPER = KeyboardKey::RightSuper;

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

enum ConfigFlags {
    FLAG_VSYNC_HINT = 0x00000040,
    FLAG_FULLSCREEN_MODE = 0x00000002,
    FLAG_WINDOW_RESIZABLE = 0x00000004,
    FLAG_BORDERLESS_WINDOWED_MODE = 0x00000008,
    FLAG_HIGHDPI = 0x00000020,
    FLAG_MSAA_4X_HINT = 0x00000080,
    FLAG_WINDOW_UNDECORATED = 0x00000100,
    FLAG_WINDOW_HIDDEN = 0x00000200,
    FLAG_WINDOW_MINIMIZED = 0x00000400,
    FLAG_WINDOW_MAXIMIZED = 0x00000800,
    FLAG_WINDOW_TOPMOST = 0x00001000,
    FLAG_WINDOW_ALWAYS_RUN = 0x00002000,
};

enum TextureFilter {
    TEXTURE_FILTER_POINT = 0,
    TEXTURE_FILTER_BILINEAR = 1,
    TEXTURE_FILTER_TRILINEAR = 2,
    TEXTURE_FILTER_ANISOTROPIC_4X = 3,
    TEXTURE_FILTER_ANISOTROPIC_8X = 4,
    TEXTURE_FILTER_ANISOTROPIC_16X = 5,
};

enum TextureWrap {
    TEXTURE_WRAP_REPEAT = 0,
    TEXTURE_WRAP_CLAMP = 1,
    TEXTURE_WRAP_MIRROR_REPEAT = 2,
    TEXTURE_WRAP_MIRROR_CLAMP = 3,
};

enum BlendMode {
    BLEND_ALPHA = 0,
    BLEND_ADDITIVE = 1,
    BLEND_MULTIPLIED = 2,
    BLEND_ADD_COLORS = 3,
    BLEND_SUBTRACT_COLORS = 4,
    BLEND_MOD_COLOR = 5,
};

enum Gesture {
    GESTURE_NONE = 0,
    GESTURE_TAP = 1,
    GESTURE_DOUBLETAP = 2,
    GESTURE_HOLD = 4,
    GESTURE_DRAG = 8,
    GESTURE_SWIPE_RIGHT = 16,
    GESTURE_SWIPE_LEFT = 32,
    GESTURE_SWIPE_UP = 64,
    GESTURE_SWIPE_DOWN = 128,
    GESTURE_PINCH_IN = 256,
    GESTURE_PINCH_OUT = 512,
};

enum CameraMode {
    CAMERA_CUSTOM = 0,
    CAMERA_FREE = 1,
    CAMERA_ORBITAL = 2,
    CAMERA_FIRST_PERSON = 3,
    CAMERA_THIRD_PERSON = 4,
};

enum FontType {
    FONT_DEFAULT = 0,
    FONT_BITMAP = 1,
    FONT_SDF = 2,
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

struct rAudioBuffer;
struct rAudioProcessor;

typedef void (*AudioCallback)(void* bufferData, unsigned int frames);
typedef unsigned char* (*LoadFileDataCallback)(const char* fileName, int* dataSize);
typedef bool (*SaveFileDataCallback)(const char* fileName, void* data, int dataSize);
typedef char* (*LoadFileTextCallback)(const char* fileName);
typedef bool (*SaveFileTextCallback)(const char* fileName, char* text);

struct Wave {
    unsigned int frameCount = 0;
    unsigned int sampleRate = 0;
    unsigned int sampleSize = 0;
    unsigned int channels = 0;
    void* data = nullptr;
};

struct AudioStream {
    rAudioBuffer* buffer = nullptr;
    rAudioProcessor* processor = nullptr;
    unsigned int sampleRate = 0;
    unsigned int sampleSize = 0;
    unsigned int channels = 0;
};

struct Sound {
    AudioStream stream;
    unsigned int frameCount = 0;
};

struct Music {
    AudioStream stream;
    unsigned int frameCount = 0;
    bool looping = false;
    int ctxType = 0;
    void* ctxData = nullptr;
};

struct VrDeviceInfo {
    int hResolution = 0;
    int vResolution = 0;
    float hScreenSize = 0.0f;
    float vScreenSize = 0.0f;
    float eyeToScreenDistance = 0.0f;
    float lensSeparationDistance = 0.0f;
    float interpupillaryDistance = 0.0f;
    float lensDistortionValues[4] = {};
    float chromaAbCorrection[4] = {};
};

struct VrStereoConfig {
    Matrix projection[2];
    Matrix viewOffset[2];
    float leftLensCenter[2] = {};
    float rightLensCenter[2] = {};
    float leftScreenCenter[2] = {};
    float rightScreenCenter[2] = {};
    float scale[2] = {};
    float scaleIn[2] = {};
};

struct AutomationEvent {
    unsigned int frame = 0;
    unsigned int type = 0;
    int params[4] = {};
};

struct AutomationEventList {
    unsigned int capacity = 0;
    unsigned int count = 0;
    AutomationEvent* events = nullptr;
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
 * @param size New desired size of the memory block, in bytes.
 * @return Pointer to the resized memory block.
 */
QCAPI void*           MemRealloc(void* ptr, unsigned int size);

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
QCAPI ID3D11ShaderResourceView *GetD3D11TextureShaderResourceView(uint32_t textureId);

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
 * @param image Icon image (RGBA 32bit).
 */
QCAPI void SetWindowIcon(Image image);

using TraceLogCallback = void (*)(LogLevel level, const char* message);

/**
 * @brief Check whether the window was resized since the last event pump.
 * @return true if a resize event has occurred.
 */
QCAPI bool IsWindowResized();
/**
 * @brief Check whether a window state flag is active.
 * @param flag SDL window flag bitmask value.
 * @return true if the flag is set.
 */
QCAPI bool IsWindowState(unsigned int flag);
/**
 * @brief Set the current window state using a flag bitmask.
 * @param flags Bitmask of SDL flags to enable.
 */
QCAPI void SetWindowState(unsigned int flags);
/**
 * @brief Clear the current window state using a flag bitmask.
 * @param flags Bitmask of SDL flags to disable.
 */
QCAPI void ClearWindowState(unsigned int flags);
/**
 * @brief Toggle borderless windowed mode.
 */
QCAPI void ToggleBorderlessWindowed();
/**
 * @brief Set the window icon from an array of images.
 * @param images Array of images.
 * @param count Number of images.
 */
QCAPI void SetWindowIcons(Image* images, int count);
/**
 * @brief Move the window to a specific monitor.
 * @param monitor Monitor index.
 */
QCAPI void SetWindowMonitor(int monitor);
/**
 * @brief Set the window opacity.
 * @param opacity Opacity value in the range [0.0, 1.0].
 */
QCAPI void SetWindowOpacity(float opacity);
/**
 * @brief Focus the window.
 */
QCAPI void SetWindowFocused();
/**
 * @brief Get current render width in pixels.
 * @return Render width.
 */
QCAPI int GetRenderWidth();
/**
 * @brief Get current render height in pixels.
 * @return Render height.
 */
QCAPI int GetRenderHeight();
/**
 * @brief Get the number of available monitors.
 * @return Number of monitors.
 */
QCAPI int GetMonitorCount();
/**
 * @brief Get the current monitor index.
 * @return Current monitor index.
 */
QCAPI int GetCurrentMonitor();
/**
 * @brief Get the monitor position in desktop coordinates.
 * @param monitor Monitor index.
 * @return Monitor position as Vec2.
 */
QCAPI Vec2 GetMonitorPosition(int monitor);
/**
 * @brief Get the monitor width.
 * @param monitor Monitor index.
 * @return Width in pixels.
 */
QCAPI int GetMonitorWidth(int monitor);
/**
 * @brief Get the monitor height.
 * @param monitor Monitor index.
 * @return Height in pixels.
 */
QCAPI int GetMonitorHeight(int monitor);
/**
 * @brief Get the monitor physical width.
 * @param monitor Monitor index.
 * @return Physical width in millimetres.
 */
QCAPI int GetMonitorPhysicalWidth(int monitor);
/**
 * @brief Get the monitor physical height.
 * @param monitor Monitor index.
 * @return Physical height in millimetres.
 */
QCAPI int GetMonitorPhysicalHeight(int monitor);
/**
 * @brief Get the name of the monitor.
 * @param monitor Monitor index.
 * @return Monitor name string.
 */
QCAPI const char* GetMonitorName(int monitor);
/**
 * @brief Set the clipboard text.
 * @param text Text to copy.
 */
QCAPI void SetClipboardText(const char* text);
/**
 * @brief Get the clipboard text.
 * @return Clipboard text, or empty string if unavailable.
 */
QCAPI const char* GetClipboardText();
/**
 * @brief Get the clipboard image.
 * @return Empty image if clipboard doesn't contain an image.
 */
QCAPI Image GetClipboardImage();
/**
 * @brief Enable event waiting.
 */
QCAPI void EnableEventWaiting();
/**
 * @brief Disable event waiting.
 */
QCAPI void DisableEventWaiting();
/**
 * @brief Set config flags that apply to the current window.
 * @param flags Flag bitmask.
 */
QCAPI void SetConfigFlags(unsigned int flags);
/**
 * @brief Swap the screen buffer.
 */
QCAPI void SwapScreenBuffer();
/**
 * @brief Poll and process pending input events.
 */
QCAPI void PollInputEvents();
/**
 * @brief Save a screenshot to a file.
 * @param fileName Output image name.
 */
QCAPI void TakeScreenshot(const char* fileName);
/**
 * @brief Open a URL in the default browser.
 * @param url URL string.
 */
QCAPI bool OpenURL(const char* url);
/**
 * @brief Register a custom trace log callback.
 * @param callback Callback function or nullptr to disable.
 */
QCAPI void SetTraceLogCallback(TraceLogCallback callback);
QCAPI float GetCurrentMonitorRefreshRate();
QCAPI Font GetDefaultFont();
QCAPI int GetShaderAttributeLocation(const Shader& shader, const char* attribName);
QCAPI void SetLogLevel(LogLevel level);

/**
 * @brief Get the underlying SDL window.
 *
 * @return Pointer to the SDL window.
 */
QCAPI SDL_Window* GetNativeWindow();

inline SDL_Window* GetWindowHandle(void) { return GetNativeWindow(); }
inline bool SetWindowMinSize(int width, int height) { return SetWindowMinimumSize(width, height); }
inline bool SetWindowMaxSize(int width, int height) { return SetWindowMaximumSize(width, height); }
inline float GetWindowScaleDPI(void) { return GetWindowDisplayScale(); }
inline int GetMonitorRefreshRate(int monitor) {
    (void)monitor;
    return static_cast<int>(std::lround(static_cast<double>(GetCurrentMonitorRefreshRate())));
}
inline Font GetFontDefault(void) { return GetDefaultFont(); }
inline int GetShaderLocationAttrib(const Shader& shader, const char* attribName) {
    return GetShaderAttributeLocation(shader, attribName);
}
inline void SetTraceLogLevel(int logLevel) {
    switch (logLevel) {
        case 0:
        case 1:
        case 2:
            SetLogLevel(LogLevel::Trace);
            break;
        case 3:
            SetLogLevel(LogLevel::Info);
            break;
        case 4:
            SetLogLevel(LogLevel::Warn);
            break;
        case 5:
            SetLogLevel(LogLevel::Error);
            break;
        default:
            SetLogLevel(LogLevel::None);
            break;
    }
}

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
 * @brief Copy one string to another, returns bytes copied.
 * @param dst Destination string.
 * @param src Source string.
 * @return Number of bytes copied.
 */
QCAPI int TextCopy(char* dst, const char* src);

/**
 * @brief Check if two strings are equal.
 * @param text1 First string.
 * @param text2 Second string.
 * @return True if text1 and text2 are equal.
 */
QCAPI bool TextIsEqual(const char* text1, const char* text2);

/**
 * @brief Get text length in bytes, checks for '\0' ending.
 * @param text Text string.
 * @return Length of the text.
 */
QCAPI unsigned int TextLength(const char* text);

/**
 * @brief Get a piece of a text string.
 * @param text Source text.
 * @param position Start position.
 * @param length Desired length.
 * @return Pointer to a static substring buffer.
 */
QCAPI const char* TextSubtext(const char* text, int position, int length);

/**
 * @brief Remove all whitespace from a text string.
 * @param text Text string.
 * @return Pointer to a static buffer without spaces.
 */
QCAPI const char* TextRemoveSpaces(const char* text);

/**
 * @brief Get the text between two markers (allocated, release with MemFree).
 * @param text Source text.
 * @param begin Start marker.
 * @param end End marker.
 * @return Allocated substring, or NULL if markers not found.
 */
QCAPI char* GetTextBetween(const char* text, const char* begin, const char* end);

/**
 * @brief Replace all occurrences of a string within text (allocated, release with MemFree).
 * @param text Source text.
 * @param search String to search.
 * @param replacement Replacement string.
 * @return Allocated result, or NULL if search not found.
 */
QCAPI char* TextReplace(const char* text, const char* search, const char* replacement);

/**
 * @brief Replace occurrences of a string within text, always allocating (release with MemFree).
 * @param text Source text.
 * @param search String to search (may be NULL/empty for a copy).
 * @param replacement Replacement string.
 * @return Always-allocated result.
 */
QCAPI char* TextReplaceAlloc(const char* text, const char* search, const char* replacement);

/**
 * @brief Replace the text between two markers, inclusive (allocated, release with MemFree).
 * @param text Source text.
 * @param begin Start marker.
 * @param end End marker.
 * @param replacement Replacement string.
 * @return Allocated result, or NULL if markers not found.
 */
QCAPI char* TextReplaceBetween(const char* text, const char* begin, const char* end, const char* replacement);

/**
 * @brief Replace the text between two markers, always allocating (release with MemFree).
 * @param text Source text.
 * @param begin Start marker.
 * @param end End marker.
 * @param replacement Replacement string.
 * @return Always-allocated result.
 */
QCAPI char* TextReplaceBetweenAlloc(const char* text, const char* begin, const char* end, const char* replacement);

/**
 * @brief Insert a string into text at a position (allocated, release with MemFree).
 * @param text Source text.
 * @param insert String to insert.
 * @param position Insertion position.
 * @return Allocated result, or NULL on invalid input.
 */
QCAPI char* TextInsert(const char* text, const char* insert, int position);

/**
 * @brief Insert a string into text at a position, always allocating (release with MemFree).
 * @param text Source text.
 * @param insert String to insert.
 * @param position Insertion position.
 * @return Always-allocated result.
 */
QCAPI char* TextInsertAlloc(const char* text, const char* insert, int position);

/**
 * @brief Join a list of strings with a delimiter (allocated, release with MemFree).
 * @param textList Array of strings.
 * @param count Number of strings.
 * @param delimiter Delimiter string.
 * @return Allocated joined string.
 */
QCAPI char* TextJoin(char** textList, int count, const char* delimiter);

/**
 * @brief Split a text string into an array of tokens (release with MemFree per token and the array).
 * @param text Text to split.
 * @param delimiter Delimiter character.
 * @param count Output number of tokens.
 * @return Allocated array of allocated tokens.
 */
QCAPI char** TextSplit(const char* text, char delimiter, int* count);

/**
 * @brief Append text at the current position and advance the position.
 * @param text Destination text (mutable).
 * @param append String to append.
 * @param position Position to append at (in/out).
 */
QCAPI void TextAppend(char* text, const char* append, int* position);

/**
 * @brief Find the index of the first occurrence of a string within text.
 * @param text Text to search in.
 * @param search String to search for.
 * @return Index of the match, or -1 if not found.
 */
QCAPI int TextFindIndex(const char* text, const char* search);

/**
 * @brief Convert text to upper case (static buffer).
 * @param text Text to convert.
 * @return Pointer to a static buffer.
 */
QCAPI const char* TextToUpper(const char* text);

/**
 * @brief Convert text to lower case (static buffer).
 * @param text Text to convert.
 * @return Pointer to a static buffer.
 */
QCAPI const char* TextToLower(const char* text);

/**
 * @brief Convert text to PascalCase (static buffer).
 * @param text Text to convert.
 * @return Pointer to a static buffer.
 */
QCAPI const char* TextToPascal(const char* text);

/**
 * @brief Convert text to snake_case (static buffer).
 * @param text Text to convert.
 * @return Pointer to a static buffer.
 */
QCAPI const char* TextToSnake(const char* text);

/**
 * @brief Convert text to camelCase (static buffer).
 * @param text Text to convert.
 * @return Pointer to a static buffer.
 */
QCAPI const char* TextToCamel(const char* text);

/**
 * @brief Get the integer value from a text string.
 * @param text Text to parse.
 * @return Integer value.
 */
QCAPI int TextToInteger(const char* text);

/**
 * @brief Get the float value from a text string.
 * @param text Text to parse.
 * @return Float value.
 */
QCAPI float TextToFloat(const char* text);

/**
 * @brief Load an array of lines from a text (release with UnloadTextLines).
 * @param text Text to split into lines.
 * @param count Output number of lines.
 * @return Allocated array of allocated line strings.
 */
QCAPI char** LoadTextLines(const char* text, int* count);

/**
 * @brief Unload an array of lines loaded with LoadTextLines.
 * @param text Array of line strings.
 * @param lineCount Number of lines.
 */
QCAPI void UnloadTextLines(char** text, int lineCount);

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

QCAPI bool IsKeyPressedRepeat(int key);
QCAPI const char* GetKeyName(int key);
QCAPI int GetTouchX(void);
QCAPI int GetTouchY(void);
QCAPI Vec2 GetTouchPosition(int index);
QCAPI int GetTouchPointId(int index);
QCAPI int GetTouchPointCount(void);
QCAPI void SetGesturesEnabled(unsigned int flags);
QCAPI bool IsGestureDetected(unsigned int gesture);
QCAPI int GetGestureDetected(void);
QCAPI float GetGestureHoldDuration(void);
QCAPI Vec2 GetGestureDragVector(void);
QCAPI float GetGestureDragAngle(void);
QCAPI Vec2 GetGesturePinchVector(void);
QCAPI float GetGesturePinchAngle(void);
QCAPI void UpdateCamera(Camera3D* camera, int mode);
QCAPI void UpdateCameraPro(Camera3D* camera, Vec2 movement, Vec3 rotation, float zoom);
QCAPI Mat4 GetCameraMatrix(Camera3D camera);
QCAPI Mat4 GetCameraMatrix2D(Camera2D camera);
QCAPI void BeginBlendMode(int mode);
QCAPI void EndBlendMode(void);
QCAPI void BeginScissorMode(int x, int y, int width, int height);
QCAPI void EndScissorMode(void);
QCAPI void SetTextureFilter(Texture2D texture, int filter);
QCAPI void SetTextureWrap(Texture2D texture, int wrap);

QCAPI void InitAudioDevice(void);
QCAPI void CloseAudioDevice(void);
QCAPI bool IsAudioDeviceReady(void);
QCAPI void SetMasterVolume(float volume);
QCAPI float GetMasterVolume(void);
QCAPI Wave LoadWave(const char* fileName);
QCAPI Wave LoadWaveFromMemory(const char* fileType, const unsigned char* fileData, int dataSize);
QCAPI float* LoadWaveSamples(Wave wave);
QCAPI Sound LoadSound(const char* fileName);
QCAPI Sound LoadSoundFromWave(Wave wave);
QCAPI Sound LoadSoundAlias(Sound source);
QCAPI void UpdateSound(Sound sound, const void* data, int frameCount);
QCAPI void UnloadWave(Wave wave);
QCAPI void UnloadSound(Sound sound);
QCAPI void UnloadSoundAlias(Sound alias);
QCAPI bool ExportWave(Wave wave, const char* fileName);
QCAPI bool ExportWaveAsCode(Wave wave, const char* fileName);
QCAPI Wave WaveCopy(Wave wave);
QCAPI void WaveCrop(Wave* wave, int initFrame, int finalFrame);
QCAPI void WaveFormat(Wave* wave, int sampleRate, int sampleSize, int channels);
QCAPI Music LoadMusicStream(const char* fileName);
QCAPI Music LoadMusicStreamFromMemory(const char* fileType, const unsigned char* data, int dataSize);
QCAPI void UnloadMusicStream(Music music);
QCAPI bool IsMusicValid(Music music);
QCAPI void PlayMusicStream(Music music);
QCAPI bool IsMusicStreamPlaying(Music music);
QCAPI void UpdateMusicStream(Music music);
QCAPI void StopMusicStream(Music music);
QCAPI void PauseMusicStream(Music music);
QCAPI void ResumeMusicStream(Music music);
QCAPI void SeekMusicStream(Music music, float position);
QCAPI void SetMusicVolume(Music music, float volume);
QCAPI void SetMusicPitch(Music music, float pitch);
QCAPI void SetMusicPan(Music music, float pan);
QCAPI float GetMusicTimeLength(Music music);
QCAPI float GetMusicTimePlayed(Music music);
QCAPI AudioStream LoadAudioStream(unsigned int sampleRate, unsigned int sampleSize, unsigned int channels);
QCAPI void UnloadAudioStream(AudioStream stream);
QCAPI bool IsAudioStreamValid(AudioStream stream);
QCAPI bool IsAudioStreamPlaying(AudioStream stream);
QCAPI bool IsAudioStreamProcessed(AudioStream stream);
QCAPI void PlayAudioStream(AudioStream stream);
QCAPI void PauseAudioStream(AudioStream stream);
QCAPI void ResumeAudioStream(AudioStream stream);
QCAPI void StopAudioStream(AudioStream stream);
QCAPI void UpdateAudioStream(AudioStream stream, const void* data, int frameCount);
QCAPI void SetAudioStreamBufferSizeDefault(int size);
QCAPI void SetAudioStreamCallback(AudioStream stream, AudioCallback callback);
QCAPI void AttachAudioStreamProcessor(AudioStream stream, AudioCallback processor);
QCAPI void DetachAudioStreamProcessor(AudioStream stream, AudioCallback processor);
QCAPI void AttachAudioMixedProcessor(AudioCallback processor);
QCAPI void DetachAudioMixedProcessor(AudioCallback processor);
QCAPI void SetAudioStreamVolume(AudioStream stream, float volume);
QCAPI void SetAudioStreamPitch(AudioStream stream, float pitch);
QCAPI void SetAudioStreamPan(AudioStream stream, float pan);
QCAPI void PlaySound(Sound sound);
QCAPI void StopSound(Sound sound);
QCAPI void PauseSound(Sound sound);
QCAPI void ResumeSound(Sound sound);
QCAPI bool IsSoundPlaying(Sound sound);
QCAPI void SetSoundVolume(Sound sound, float volume);
QCAPI void SetSoundPitch(Sound sound, float pitch);
QCAPI void SetSoundPan(Sound sound, float pan);

QCAPI unsigned char* LoadFileData(const char* fileName, int* dataSize);
QCAPI void UnloadFileData(unsigned char* data);
QCAPI bool SaveFileData(const char* fileName, const void* data, int dataSize);
QCAPI bool ExportDataAsCode(const unsigned char* data, int dataSize, const char* fileName);
QCAPI char* LoadFileText(const char* fileName);
QCAPI void UnloadFileText(char* text);
QCAPI bool SaveFileText(const char* fileName, const char* text);
QCAPI void SetLoadFileDataCallback(LoadFileDataCallback callback);
QCAPI void SetSaveFileDataCallback(SaveFileDataCallback callback);
QCAPI void SetLoadFileTextCallback(LoadFileTextCallback callback);
QCAPI void SetSaveFileTextCallback(SaveFileTextCallback callback);

QCAPI AutomationEventList LoadAutomationEventList(const char* fileName);
QCAPI void UnloadAutomationEventList(AutomationEventList list);
QCAPI bool ExportAutomationEventList(AutomationEventList list, const char* fileName);
QCAPI void SetAutomationEventList(AutomationEventList* list);
QCAPI void SetAutomationEventBaseFrame(int frame);
QCAPI void StartAutomationEventRecording(void);
QCAPI void StopAutomationEventRecording(void);
QCAPI void PlayAutomationEvent(AutomationEvent event);

QCAPI void BeginVrStereoMode(VrStereoConfig config);
QCAPI void EndVrStereoMode(void);
QCAPI VrStereoConfig LoadVrStereoConfig(VrDeviceInfo device);
QCAPI void UnloadVrStereoConfig(VrStereoConfig config);

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
 * @brief Draw debug text with a hard drop shadow and 1 px outline.
 *
 * @param text Text to draw.
 * @param x X coordinate.
 * @param y Y coordinate.
 * @param fontSize Font size in pixels.
 * @param color Text tint color.
 */
QCAPI void DrawDebugText(const char* text, int x, int y, int fontSize, Color color);

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
 * @brief Draw current FPS as text.
 *
 * @param posX X position.
 * @param posY Y position.
 */
QCAPI void DrawFPS(int posX, int posY);

/**
 * @brief Draw text using a font, with rotation and origin.
 *
 * @param font Font object.
 * @param text Text (UTF-8 encoded) to draw.
 * @param position Screen position.
 * @param origin Rotation origin relative to position.
 * @param rotation Rotation angle in degrees.
 * @param fontSize Font size in pixels.
 * @param spacing Additional character spacing in pixels.
 * @param tint Text tint color.
 */
QCAPI void DrawTextPro(Font font, const char* text, Vec2 position, Vec2 origin,
                float rotation, float fontSize, float spacing, Color tint);

/**
 * @brief Draw a single codepoint (character).
 *
 * @param font Font object.
 * @param codepoint Unicode codepoint to draw.
 * @param position Screen position.
 * @param fontSize Font size in pixels.
 * @param tint Text tint color.
 */
QCAPI void DrawTextCodepoint(Font font, int codepoint, Vec2 position, float fontSize, Color tint);

/**
 * @brief Draw an array of codepoints (characters).
 *
 * @param font Font object.
 * @param codepoints Unicode codepoints to draw.
 * @param codepointCount Number of codepoints.
 * @param position Screen position.
 * @param fontSize Font size in pixels.
 * @param spacing Additional character spacing in pixels.
 * @param tint Text tint color.
 */
QCAPI void DrawTextCodepoints(Font font, const int* codepoints, int codepointCount,
                       Vec2 position, float fontSize, float spacing, Color tint);

/**
 * @brief Set vertical line spacing between lines of text.
 *
 * @param spacing Line spacing in pixels.
 */
QCAPI void SetTextLineSpacing(int spacing);

/**
 * @brief Measure text width/height from an array of codepoints.
 *
 * @param font Font object.
 * @param codepoints Unicode codepoints.
 * @param length Number of codepoints.
 * @param fontSize Font size in pixels.
 * @param spacing Additional character spacing in pixels.
 * @return Text size as Vec2.
 */
QCAPI Vec2 MeasureTextCodepoints(Font font, const int* codepoints, int length,
                          float fontSize, float spacing);

/**
 * @brief Get the atlas index of a codepoint in a font.
 *
 * @param font Font object.
 * @param codepoint Unicode codepoint.
 * @return Index in the font glyph array (fallback to '?' if not found).
 */
QCAPI int GetGlyphIndex(Font font, int codepoint);

/**
 * @brief Get glyph (character) info of a codepoint in a font.
 *
 * @param font Font object.
 * @param codepoint Unicode codepoint.
 * @return GlyphInfo for the codepoint.
 */
QCAPI GlyphInfo GetGlyphInfo(Font font, int codepoint);

/**
 * @brief Get the atlas rectangle of a codepoint in a font.
 *
 * @param font Font object.
 * @param codepoint Unicode codepoint.
 * @return Rectangle in font texture coordinates (pixels).
 */
QCAPI Rectangle GetGlyphAtlasRec(Font font, int codepoint);

/**
 * @brief Load UTF-8 text encoded from an array of codepoints.
 * @param codepoints Array of Unicode codepoints.
 * @param length Number of codepoints in the array.
 * @return Heap-allocated UTF-8 string (release with UnloadUTF8).
 */
QCAPI char* LoadUTF8(const int* codepoints, int length);

/**
 * @brief Unload a UTF-8 string previously allocated with LoadUTF8.
 * @param text UTF-8 string to unload.
 */
QCAPI void UnloadUTF8(char* text);

/**
 * @brief Load all codepoints from a UTF-8 text string.
 * @param text UTF-8 encoded text.
 * @param count Output number of codepoints.
 * @return Heap-allocated array of codepoints (release with UnloadCodepoints).
 */
QCAPI int* LoadCodepoints(const char* text, int* count);

/**
 * @brief Unload a codepoints array previously allocated with LoadCodepoints.
 * @param codepoints Array to unload.
 */
QCAPI void UnloadCodepoints(int* codepoints);

/**
 * @brief Get the total number of codepoints in a UTF-8 encoded string.
 * @param text UTF-8 encoded string.
 * @return Number of codepoints.
 */
QCAPI int GetCodepointCount(const char* text);

/**
 * @brief Get the next codepoint in a UTF-8 encoded string, 0x3f('?') on failure.
 * @param text UTF-8 encoded string.
 * @param codepointSize Output size in bytes of the decoded codepoint.
 * @return Decoded codepoint.
 */
QCAPI int GetCodepoint(const char* text, int* codepointSize);

/**
 * @brief Get the next codepoint in a UTF-8 encoded string, 0x3f('?') on failure.
 * @param text UTF-8 encoded string.
 * @param codepointSize Output size in bytes of the decoded codepoint.
 * @return Decoded codepoint.
 */
QCAPI int GetCodepointNext(const char* text, int* codepointSize);

/**
 * @brief Get the previous codepoint in a UTF-8 encoded string, 0x3f('?') on failure.
 * @param text UTF-8 encoded string.
 * @param codepointSize Output size in bytes of the decoded codepoint.
 * @return Decoded codepoint.
 */
QCAPI int GetCodepointPrevious(const char* text, int* codepointSize);

/**
 * @brief Encode a single codepoint into a UTF-8 byte sequence.
 * @param codepoint Unicode codepoint to encode.
 * @param utf8Size Output size in bytes of the encoded sequence.
 * @return Pointer to a static UTF-8 byte sequence.
 */
QCAPI const char* CodepointToUTF8(int codepoint, int* utf8Size);

/**
 * @brief Load a font from file (rasterized at the default pixel size).
 * 
 * @param fileName Path to the font file (.ttf, .otf, etc.).
 * @return Loaded font object.
 * @return Invalid font (valid=false) on failure.
 */
QCAPI Font LoadFont(const char* fileName);

/**
 * @brief Unload a font and free resources.
 * 
 * @param font Font object to unload.
 */
QCAPI void UnloadFont(Font font);

/**
 * @brief Load a font from file with defined codepoints and generation size.
 *
 * @param fileName Path to the font file (.ttf, .otf, etc.).
 * @param fontSize Font size in pixels height.
 * @param codepoints Pointer to the set of codepoints to load (NULL to load the default ASCII set).
 * @param codepointCount Number of codepoints (0 when codepoints is NULL).
 * @return Loaded font object.
 * @return Invalid font (valid=false) on failure.
 */
QCAPI Font LoadFontEx(const char* fileName, int fontSize, const int* codepoints, int codepointCount);

/**
 * @brief Load a font from an Image (XNA style), where the image holds all glyphs.
 *
 * @param image Image with fonts sprites/characters.
 * @param key Font character color (RGB) to be used as source of the info.
 * @param firstChar Reference character code (ASCII).
 * @return Loaded font object.
 * @return Invalid font (valid=false) on failure.
 */
QCAPI Font LoadFontFromImage(Image image, Color key, int firstChar);

/**
 * @brief Load a font from a memory buffer containing the font file data.
 *
 * @param fileType Font file type/extension, i.e. ".ttf".
 * @param fileData Font file data buffer.
 * @param dataSize Font file data buffer size in bytes.
 * @param fontSize Font size in pixels height.
 * @param codepoints Pointer to the set of codepoints to load (NULL to load the default ASCII set).
 * @param codepointCount Number of codepoints (0 when codepoints is NULL).
 * @return Loaded font object.
 * @return Invalid font (valid=false) on failure.
 */
QCAPI Font LoadFontFromMemory(const char* fileType, const unsigned char* fileData, int dataSize,
                       int fontSize, const int* codepoints, int codepointCount);

/**
 * @brief Check if a font is valid (font data loaded).
 *
 * @param font Font object to validate.
 * @return true if the font is valid.
 */
QCAPI bool IsFontValid(Font font);

/**
 * @brief Load font data (glyph info) for further use.
 *
 * @param fileData Font file data buffer.
 * @param dataSize Font file data buffer size in bytes.
 * @param fontSize Requested font size in pixels.
 * @param codepoints Required codepoints (NULL to load the default ASCII set).
 * @param codepointCount Number of codepoints (0 when codepoints is NULL).
 * @param type Type/font data loading information (FontLoadType enum).
 * @param glyphCount Returned number of loaded glyphs.
 * @return A heap-allocated array of GlyphInfo (must be released with UnloadFontData).
 * @return NULL on failure.
 */
QCAPI GlyphInfo* LoadFontData(const unsigned char* fileData, int dataSize, int fontSize,
                       const int* codepoints, int codepointCount, int type, int* glyphCount);

/**
 * @brief Generate image font atlas using glyph info.
 *
 * @param glyphs Array of GlyphInfo to generate the atlas from.
 * @param glyphRecs Returned array of atlas rectangles (heap-allocated, must be freed).
 * @param glyphCount Number of glyphs.
 * @param fontSize Font size in pixels.
 * @param padding Padding in pixels between glyphs.
 * @param packMethod Packing method (0 = Spritefont/LoadFontData, 1 = LoadFontEx/Donut packing).
 * @return Generated atlas image.
 */
QCAPI Image GenImageFontAtlas(const GlyphInfo* glyphs, Rectangle** glyphRecs, int glyphCount,
                       int fontSize, int padding, int packMethod);

/**
 * @brief Unload font glyph info data (RAM).
 *
 * @param glyphs Array of GlyphInfo to unload.
 * @param glyphCount Number of glyphs in the array.
 */
QCAPI void UnloadFontData(GlyphInfo* glyphs, int glyphCount);

/**
 * @brief Export font data as a code file (C header).
 *
 * @param font Font object to export.
 * @param fileName Path of the output code file.
 * @return true on success.
 */
QCAPI bool ExportFontAsCode(Font font, const char* fileName);

/**
 * @brief Load a texture from a file.
 * @param filePath Path to the texture file.
 * @return Loaded texture.
 * @return Empty texture on failure.
 */
QCAPI Texture2D LoadTexture(const char* filePath);
QCAPI Texture2D LoadTextureFromImage(Image image);
QCAPI TextureCubemap LoadTextureCubemap(Image image, int layout);
QCAPI void UpdateTexture(Texture2D texture, const void* pixels);
QCAPI void UpdateTextureRec(Texture2D texture, Rectangle rec, const void* pixels);
QCAPI void GenTextureMipmaps(Texture2D* texture);
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
QCAPI void UnloadTexture(Texture2D texture);
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
QCAPI void UnloadShader(Shader shader);

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
 * @brief Get a ray from screen coordinates through the camera (3D) in a viewport.
 * @param position Screen position.
 * @param camera 3D camera.
 * @param width Viewport width in pixels.
 * @param height Viewport height in pixels.
 * @return Ray starting from camera position.
 */
QCAPI Ray GetScreenToWorldRayEx(Vec2 position, Camera3D camera, int width, int height);

/**
 * @brief Convert world coordinates to screen coordinates (3D) in a viewport.
 * @param position World position.
 * @param camera 3D camera.
 * @param width Viewport width in pixels.
 * @param height Viewport height in pixels.
 * @return Screen position (x, y).
 */
QCAPI Vec2 GetWorldToScreenEx(Vec3 position, Camera3D camera, int width, int height);

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
 * @brief Set an offset applied to mouse coordinates.
 * @param offsetX Horizontal offset applied to mouse input.
 * @param offsetY Vertical offset applied to mouse input.
 */
QCAPI void SetMouseOffset(int offsetX, int offsetY);

/**
 * @brief Set a scale applied to mouse coordinates.
 * @param scaleX Horizontal mouse scale.
 * @param scaleY Vertical mouse scale.
 */
QCAPI void SetMouseScale(float scaleX, float scaleY);

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
 * @brief Show the mouse cursor (calls SDL_ShowCursor).
 */
QCAPI void ShowCursor();

/**
 * @brief Hide the mouse cursor (calls SDL_HideCursor).
 */
QCAPI void HideCursor();

/**
 * @brief Check if the cursor is on the screen (within the window bounds).
 * @return true if the cursor is inside the window.
 * @return false otherwise.
 */
QCAPI bool IsCursorOnScreen();

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

QCAPI void DrawPixel(int posX, int posY, Color color);
QCAPI void DrawPixelV(Vec2 position, Color color);
/**
 * @brief Draw a line.
 * @param x1 Start X coordinate.
 * @param y1 Start Y coordinate.
 * @param x2 End X coordinate.
 * @param y2 End Y coordinate.
 * @param color Line color.
 */
QCAPI void DrawLine(float x1, float y1, float x2, float y2, Color color);
QCAPI void DrawLineEx(Vec2 startPos, Vec2 endPos, float thick, Color color);
QCAPI void DrawLineStrip(const Vec2* points, int pointCount, Color color);
QCAPI void DrawLineBezier(Vec2 startPos, Vec2 endPos, float thick, Color color);
QCAPI void DrawLineDashed(Vec2 startPos, Vec2 endPos, int dashSize, int spaceSize, Color color);

/**
 * @brief Draw a line using vectors.
 * @param start Start position.
 * @param end End position.
 * @param color Line color.
 */
QCAPI void DrawLineV(Vec2 start, Vec2 end, Color color);

/**
 * @brief Draw rectangle outline.
 * @param posX Rectangle top-left corner X.
 * @param posY Rectangle top-left corner Y.
 * @param width Rectangle width.
 * @param height Rectangle height.
 * @param color Line color.
 */
QCAPI void DrawRectangleLines(int posX, int posY, int width, int height, Color color);
QCAPI void DrawRectangleRec(Rectangle rec, Color color);
QCAPI void DrawRectanglePro(Rectangle rec, Vec2 origin, float rotation, Color color);
QCAPI void DrawRectangleGradientV(int posX, int posY, int width, int height, Color top, Color bottom);
QCAPI void DrawRectangleGradientH(int posX, int posY, int width, int height, Color left, Color right);
QCAPI void DrawRectangleGradientEx(Rectangle rec, Color col1, Color col2, Color col3, Color col4);
QCAPI void DrawRectangleLinesEx(Rectangle rec, float thick, Color color);
QCAPI void DrawRectangleRoundedLines(Rectangle rec, float roundness, int segments, Color color);
QCAPI void DrawRectangleRoundedLinesEx(Rectangle rec, float roundness, int segments, float thick, Color color);

/**
 * @brief Draw a triangle.
 * @param v1 First vertex.
 * @param v2 Second vertex.
 * @param v3 Third vertex.
 * @param color Triangle color.
 */
QCAPI void DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color);
QCAPI void DrawTriangleGradient(Vec2 v1, Vec2 v2, Vec2 v3, Color c1, Color c2, Color c3);
QCAPI void DrawTriangleLines(Vec2 v1, Vec2 v2, Vec2 v3, Color color);
QCAPI void DrawTriangleLinesEx(Vec2 v1, Vec2 v2, Vec2 v3, float thick, Color color);
QCAPI void DrawTriangleFan(const Vec2* points, int pointCount, Color color);
QCAPI void DrawTriangleStrip(const Vec2* points, int pointCount, Color color);

/**
 * @brief Draw circle outline.
 * @param centerX Center X coordinate.
 * @param centerY Center Y coordinate.
 * @param radius Circle radius.
 * @param color Circle color.
 */
QCAPI void DrawCircleLines(float centerX, float centerY, float radius, Color color);
QCAPI void DrawCircleV(Vec2 center, float radius, Color color);
QCAPI void DrawCircleGradient(Vec2 center, float radius, Color inner, Color outer);
QCAPI void DrawCircleSector(Vec2 center, float radius, float startAngle, float endAngle, int segments, Color color);
QCAPI void DrawCircleSectorLines(Vec2 center, float radius, float startAngle, float endAngle, int segments, Color color);
QCAPI void DrawCircleSectorLinesEx(Vec2 center, float radius, float startAngle, float endAngle, int segments, float thick, Color color);
QCAPI void DrawCircleLinesV(Vec2 center, float radius, Color color);
QCAPI void DrawCircleLinesEx(Vec2 center, float radius, float thick, Color color);

/**
 * @brief Draw an ellipse.
 * @param centerX Center X coordinate.
 * @param centerY Center Y coordinate.
 * @param radiusH Horizontal radius.
 * @param radiusV Vertical radius.
 * @param color Ellipse color.
 */
QCAPI void DrawEllipse(float centerX, float centerY, float radiusH, float radiusV, Color color);
QCAPI void DrawEllipseV(Vec2 center, float radiusH, float radiusV, Color color);
QCAPI void DrawEllipseLines(int centerX, int centerY, float radiusH, float radiusV, Color color);
QCAPI void DrawEllipseLinesV(Vec2 center, float radiusH, float radiusV, Color color);
QCAPI void DrawEllipseLinesEx(Vec2 center, float radiusH, float radiusV, float thick, Color color);
QCAPI void DrawRing(Vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, int segments, Color color);
QCAPI void DrawRingLines(Vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, int segments, Color color);
QCAPI void DrawRingLinesEx(Vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, int segments, float thick, Color color);

/**
 * @brief Draw a polygon.
 * @param center Center position.
 * @param sides Number of sides.
 * @param radius Polygon radius.
 * @param rotation Rotation in degrees.
 * @param color Polygon color.
 */
QCAPI void DrawPoly(Vec2 center, int sides, float radius, float rotation, Color color);
QCAPI void DrawPolyLines(Vec2 center, int sides, float radius, float rotation, Color color);
QCAPI void DrawPolyLinesEx(Vec2 center, int sides, float radius, float rotation, float thick, Color color);
QCAPI void SetShapesTexture(Texture2D texture, Rectangle rec);
QCAPI Texture2D GetShapesTexture(void);
QCAPI Rectangle GetShapesTextureRectangle(void);

QCAPI void DrawSplineLinear(const Vec2* points, int pointCount, float thick, Color color);
QCAPI void DrawSplineBasis(const Vec2* points, int pointCount, float thick, Color color);
QCAPI void DrawSplineCatmullRom(const Vec2* points, int pointCount, float thick, Color color);
QCAPI void DrawSplineBezierQuadratic(const Vec2* points, int pointCount, float thick, Color color);
QCAPI void DrawSplineBezierCubic(const Vec2* points, int pointCount, float thick, Color color);
QCAPI void DrawSplineSegmentLinear(Vec2 p1, Vec2 p2, float thick, Color color);
QCAPI void DrawSplineSegmentBasis(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, float thick, Color color);
QCAPI void DrawSplineSegmentCatmullRom(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, float thick, Color color);
QCAPI void DrawSplineSegmentBezierQuadratic(Vec2 p1, Vec2 c2, Vec2 p3, float thick, Color color);
QCAPI void DrawSplineSegmentBezierCubic(Vec2 p1, Vec2 c2, Vec2 c3, Vec2 p4, float thick, Color color);
QCAPI Vec2 GetSplinePointLinear(Vec2 startPos, Vec2 endPos, float t);
QCAPI Vec2 GetSplinePointBasis(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, float t);
QCAPI Vec2 GetSplinePointCatmullRom(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, float t);
QCAPI Vec2 GetSplinePointBezierQuadratic(Vec2 p1, Vec2 c2, Vec2 p3, float t);
QCAPI Vec2 GetSplinePointBezierCubic(Vec2 p1, Vec2 c2, Vec2 c3, Vec2 p4, float t);

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
 * @brief Check if two colors are equal.
 * @param col1 First color.
 * @param col2 Second color.
 * @return true if colors are equal.
 * @return false otherwise.
 */
QCAPI bool ColorIsEqual(Color col1, Color col2);

/**
 * @brief Get the integer (32-bit ARGB) representation of a color.
 * @param color Color to convert.
 * @return Integer value in the form 0xAARRGGBB.
 */
QCAPI int ColorToInt(Color color);

/**
 * @brief Get the normalized (0.0-1.0) RGBA components of a color.
 * @param color Color to convert.
 * @return Normalized vector (x=r, y=g, z=b, w=a).
 */
QCAPI Vec4 ColorNormalize(Color color);

/**
 * @brief Convert a color to HSV space.
 * @param color Color to convert.
 * @return HSV vector (x=hue 0-360, y=saturation 0-1, z=value 0-1).
 */
QCAPI Vec3 ColorToHSV(Color color);

/**
 * @brief Create a color from HSV values.
 * @param hue Hue (0-360).
 * @param saturation Saturation (0-1).
 * @param value Value (0-1).
 * @return Resulting color.
 */
QCAPI Color ColorFromHSV(float hue, float saturation, float value);

/**
 * @brief Blend two colors applying a tint factor to the source.
 * @param dst Destination color (bottom layer).
 * @param src Source color (top layer).
 * @param tint Tint color applied to the source.
 * @return Blended color.
 */
QCAPI Color ColorAlphaBlend(Color dst, Color src, Color tint);

/**
 * @brief Get the linear interpolation between two colors.
 * @param color1 First color.
 * @param color2 Second color.
 * @param factor Interpolation factor (0.0-1.0).
 * @return Interpolated color.
 */
QCAPI Color ColorLerp(Color color1, Color color2, float factor);

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
 * @brief Check collision between circle and rectangle.
 * @param center Circle center.
 * @param radius Circle radius.
 * @param rec Rectangle.
 * @return true if circle and rectangle collide.
 * @return false otherwise.
 */
QCAPI bool CheckCollisionCircleRec(Vec2 center, float radius, Rectangle rec);

/**
 * @brief Check collision between circle and line.
 * @param center Circle center.
 * @param radius Circle radius.
 * @param p1 Line start point.
 * @param p2 Line end point.
 * @return true if circle and line collide.
 * @return false otherwise.
 */
QCAPI bool CheckCollisionCircleLine(Vec2 center, float radius, Vec2 p1, Vec2 p2);

/**
 * @brief Check collision between point and triangle.
 * @param point Point position.
 * @param p1 Triangle vertex 1.
 * @param p2 Triangle vertex 2.
 * @param p3 Triangle vertex 3.
 * @return true if point is in triangle.
 * @return false otherwise.
 */
QCAPI bool CheckCollisionPointTriangle(Vec2 point, Vec2 p1, Vec2 p2, Vec2 p3);

/**
 * @brief Check collision between point and line.
 * @param point Point position.
 * @param p1 Line start point.
 * @param p2 Line end point.
 * @param threshold Detection threshold.
 * @return true if point is on line.
 * @return false otherwise.
 */
QCAPI bool CheckCollisionPointLine(Vec2 point, Vec2 p1, Vec2 p2, int threshold);

/**
 * @brief Check collision between point and polygon.
 * @param point Point position.
 * @param points Polygon vertices.
 * @param pointCount Number of polygon vertices.
 * @return true if point is inside polygon.
 * @return false otherwise.
 */
QCAPI bool CheckCollisionPointPoly(Vec2 point, const Vec2* points, int pointCount);

/**
 * @brief Check collision between two line segments.
 * @param startPos1 First line start point.
 * @param endPos1 First line end point.
 * @param startPos2 Second line start point.
 * @param endPos2 Second line end point.
 * @param collisionPoint Output collision point (may be nullptr).
 * @return true if lines collide.
 * @return false otherwise.
 */
QCAPI bool CheckCollisionLines(Vec2 startPos1, Vec2 endPos1, Vec2 startPos2, Vec2 endPos2, Vec2* collisionPoint);

/**
 * @brief Get the intersection rectangle of two rectangles.
 * @param rec1 First rectangle.
 * @param rec2 Second rectangle.
 * @return Overlapping rectangle.
 */
QCAPI Rectangle GetCollisionRec(Rectangle rec1, Rectangle rec2);

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
 * @brief Generate a random integer sequence.
 * @param count Number of values to generate.
 * @param min Minimum value (inclusive).
 * @param max Maximum value (inclusive).
 * @return Pointer to the generated integers, or nullptr on invalid input.
 */
QCAPI int* LoadRandomSequence(unsigned int count, int min, int max);

/**
 * @brief Unload a random sequence allocated by LoadRandomSequence.
 * @param sequence Sequence pointer to free.
 */
QCAPI void UnloadRandomSequence(int* sequence);

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
 * @return Zero on success, non-zero on failure.
 */
QCAPI int ChangeDirectory(const char* dirPath);
/**
 * @brief Check whether a path identifies a file.
 * @param path Path to inspect.
 * @return True when it is a file.
 */
QCAPI bool IsPathFile(const char* path);
/**
 * @brief Check whether a path identifies a directory.
 * @param path Path to inspect.
 * @return True when it is a directory.
 */
QCAPI bool IsPathDirectory(const char* path);
/**
 * @brief Check whether a path is an absolute path.
 * @param path Path to inspect.
 * @return True when the path is absolute.
 */
QCAPI bool IsPathAbsolute(const char* path);
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
