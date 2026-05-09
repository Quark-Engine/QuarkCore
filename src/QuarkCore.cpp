#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/Quark3D.hpp"

#include <SDL3/SDL.h>
#include <GL/glew.h>
#include <png.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <array>
#include <chrono>
#include <cstdarg>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace qc {
namespace {

struct Vertex {
    float x;
    float y;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
};

struct BatchCommand {
    GLuint textureId = 0;
    std::size_t vertexStart = 0;
    std::size_t vertexCount = 0;
    std::size_t indexStart = 0;
    std::size_t indexCount = 0;
};

struct RendererState {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint whiteTexture = 0;
    GLuint currentTexture = 0;
    GLuint currentShaderProgram = 0;
    GLuint defaultShaderProgram = 0;
    GLint screenSizeLocation = -1;
    GLint samplerLocation = -1;
    GLuint ebo = 0;
    int width = 0;
    int height = 0;
    int targetFps = 60;
    float frameTime = 0.0f;
    std::uint64_t lastFrameCounter = 0;
    bool drawing = false;
    bool shouldClose = false;
    LogLevel minimumLogLevel = LogLevel::Trace;
    std::vector<Vertex> batchVertices;
    std::vector<GLuint> batchIndices;
    std::vector<BatchCommand> batchCommands;
    std::array<bool, SDL_SCANCODE_COUNT> currentKeys{};
    std::array<bool, SDL_SCANCODE_COUNT> previousKeys{};
    std::array<bool, 8> mouseButtons{};
    Vec2 mousePosition{};
    std::vector<Event> events;
    std::size_t nextEventIndex = 0;
    bool eventsReady = false;
};

struct ShaderUniformLocations {
    GLint screenSizeLocation = -1;
    GLint samplerLocation = -1;
};

std::unordered_map<GLuint, ShaderUniformLocations> gShaderUniformCache;

struct PngImageData {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

RendererState gRenderer;

constexpr std::size_t kMaxBatchVertices = 8192;
constexpr std::size_t kMaxBatchIndices = kMaxBatchVertices * 3 / 2;

void RefreshViewport();
void UpdateInputFromEvents();
void FlushBatch();
bool LoadPngImage(const char* filePath, PngImageData& image);

[[noreturn]] void Fail(const std::string& message) {
    throw std::runtime_error(message);
}

const char* ToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::None: return "NONE";
        default: return "UNKNOWN";
    }
}

std::string FormatTimeNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &nowTime);
#else
    localtime_r(&nowTime, &localTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%H:%M:%S");
    return stream.str();
}

void WriteLog(LogLevel level, const char* type, const std::string& message) {
    if (level < gRenderer.minimumLogLevel || level == LogLevel::None) {
        return;
    }

    std::cout
        << '[' << FormatTimeNow() << ']'
        << '[' << ToString(level) << ']'
        << '[' << type << "] "
        << message
        << '\n';
}

void CopyText(char* destination, std::size_t capacity, const char* source) {
    if (destination == nullptr || capacity == 0) {
        return;
    }

    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }

#if defined(_MSC_VER)
    strncpy_s(destination, capacity, source, _TRUNCATE);
#else
    std::snprintf(destination, capacity, "%s", source);
#endif
}

EventType TranslateEventType(Uint32 type) {
    switch (type) {
        case SDL_EVENT_QUIT: return EventType::Quit;
        case SDL_EVENT_TERMINATING: return EventType::Terminating;
        case SDL_EVENT_LOW_MEMORY: return EventType::LowMemory;
        case SDL_EVENT_WILL_ENTER_BACKGROUND: return EventType::WillEnterBackground;
        case SDL_EVENT_DID_ENTER_BACKGROUND: return EventType::DidEnterBackground;
        case SDL_EVENT_WILL_ENTER_FOREGROUND: return EventType::WillEnterForeground;
        case SDL_EVENT_DID_ENTER_FOREGROUND: return EventType::DidEnterForeground;
        case SDL_EVENT_LOCALE_CHANGED: return EventType::LocaleChanged;
        case SDL_EVENT_SYSTEM_THEME_CHANGED: return EventType::SystemThemeChanged;
        case SDL_EVENT_DISPLAY_ORIENTATION: return EventType::DisplayOrientation;
        case SDL_EVENT_DISPLAY_ADDED: return EventType::DisplayAdded;
        case SDL_EVENT_DISPLAY_REMOVED: return EventType::DisplayRemoved;
        case SDL_EVENT_DISPLAY_MOVED: return EventType::DisplayMoved;
        case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED: return EventType::DisplayDesktopModeChanged;
        case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED: return EventType::DisplayCurrentModeChanged;
        case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED: return EventType::DisplayContentScaleChanged;
        case SDL_EVENT_DISPLAY_USABLE_BOUNDS_CHANGED: return EventType::DisplayUsableBoundsChanged;
        case SDL_EVENT_WINDOW_SHOWN: return EventType::WindowShown;
        case SDL_EVENT_WINDOW_HIDDEN: return EventType::WindowHidden;
        case SDL_EVENT_WINDOW_EXPOSED: return EventType::WindowExposed;
        case SDL_EVENT_WINDOW_MOVED: return EventType::WindowMoved;
        case SDL_EVENT_WINDOW_RESIZED: return EventType::WindowResized;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: return EventType::WindowPixelSizeChanged;
        case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED: return EventType::WindowMetalViewResized;
        case SDL_EVENT_WINDOW_MINIMIZED: return EventType::WindowMinimized;
        case SDL_EVENT_WINDOW_MAXIMIZED: return EventType::WindowMaximized;
        case SDL_EVENT_WINDOW_RESTORED: return EventType::WindowRestored;
        case SDL_EVENT_WINDOW_MOUSE_ENTER: return EventType::WindowMouseEnter;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE: return EventType::WindowMouseLeave;
        case SDL_EVENT_WINDOW_FOCUS_GAINED: return EventType::WindowFocusGained;
        case SDL_EVENT_WINDOW_FOCUS_LOST: return EventType::WindowFocusLost;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: return EventType::WindowCloseRequested;
        case SDL_EVENT_WINDOW_HIT_TEST: return EventType::WindowHitTest;
        case SDL_EVENT_WINDOW_ICCPROF_CHANGED: return EventType::WindowIccProfileChanged;
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED: return EventType::WindowDisplayChanged;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: return EventType::WindowDisplayScaleChanged;
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED: return EventType::WindowSafeAreaChanged;
        case SDL_EVENT_WINDOW_OCCLUDED: return EventType::WindowOccluded;
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN: return EventType::WindowEnterFullscreen;
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: return EventType::WindowLeaveFullscreen;
        case SDL_EVENT_WINDOW_DESTROYED: return EventType::WindowDestroyed;
        case SDL_EVENT_WINDOW_HDR_STATE_CHANGED: return EventType::WindowHdrStateChanged;
        case SDL_EVENT_KEY_DOWN: return EventType::KeyDown;
        case SDL_EVENT_KEY_UP: return EventType::KeyUp;
        case SDL_EVENT_TEXT_EDITING: return EventType::TextEditing;
        case SDL_EVENT_TEXT_INPUT: return EventType::TextInput;
        case SDL_EVENT_KEYMAP_CHANGED: return EventType::KeymapChanged;
        case SDL_EVENT_KEYBOARD_ADDED: return EventType::KeyboardAdded;
        case SDL_EVENT_KEYBOARD_REMOVED: return EventType::KeyboardRemoved;
        case SDL_EVENT_TEXT_EDITING_CANDIDATES: return EventType::TextEditingCandidates;
        case SDL_EVENT_SCREEN_KEYBOARD_SHOWN: return EventType::ScreenKeyboardShown;
        case SDL_EVENT_SCREEN_KEYBOARD_HIDDEN: return EventType::ScreenKeyboardHidden;
        case SDL_EVENT_MOUSE_MOTION: return EventType::MouseMotion;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: return EventType::MouseButtonDown;
        case SDL_EVENT_MOUSE_BUTTON_UP: return EventType::MouseButtonUp;
        case SDL_EVENT_MOUSE_WHEEL: return EventType::MouseWheel;
        case SDL_EVENT_MOUSE_ADDED: return EventType::MouseAdded;
        case SDL_EVENT_MOUSE_REMOVED: return EventType::MouseRemoved;
        case SDL_EVENT_JOYSTICK_AXIS_MOTION: return EventType::JoystickAxisMotion;
        case SDL_EVENT_JOYSTICK_BALL_MOTION: return EventType::JoystickBallMotion;
        case SDL_EVENT_JOYSTICK_HAT_MOTION: return EventType::JoystickHatMotion;
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN: return EventType::JoystickButtonDown;
        case SDL_EVENT_JOYSTICK_BUTTON_UP: return EventType::JoystickButtonUp;
        case SDL_EVENT_JOYSTICK_ADDED: return EventType::JoystickAdded;
        case SDL_EVENT_JOYSTICK_REMOVED: return EventType::JoystickRemoved;
        case SDL_EVENT_JOYSTICK_BATTERY_UPDATED: return EventType::JoystickBatteryUpdated;
        case SDL_EVENT_JOYSTICK_UPDATE_COMPLETE: return EventType::JoystickUpdateComplete;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: return EventType::GamepadAxisMotion;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN: return EventType::GamepadButtonDown;
        case SDL_EVENT_GAMEPAD_BUTTON_UP: return EventType::GamepadButtonUp;
        case SDL_EVENT_GAMEPAD_ADDED: return EventType::GamepadAdded;
        case SDL_EVENT_GAMEPAD_REMOVED: return EventType::GamepadRemoved;
        case SDL_EVENT_GAMEPAD_REMAPPED: return EventType::GamepadRemapped;
        case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN: return EventType::GamepadTouchpadDown;
        case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION: return EventType::GamepadTouchpadMotion;
        case SDL_EVENT_GAMEPAD_TOUCHPAD_UP: return EventType::GamepadTouchpadUp;
        case SDL_EVENT_GAMEPAD_SENSOR_UPDATE: return EventType::GamepadSensorUpdate;
        case SDL_EVENT_GAMEPAD_UPDATE_COMPLETE: return EventType::GamepadUpdateComplete;
        case SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED: return EventType::GamepadSteamHandleUpdated;
        case SDL_EVENT_FINGER_DOWN: return EventType::FingerDown;
        case SDL_EVENT_FINGER_UP: return EventType::FingerUp;
        case SDL_EVENT_FINGER_MOTION: return EventType::FingerMotion;
        case SDL_EVENT_FINGER_CANCELED: return EventType::FingerCanceled;
        case SDL_EVENT_PINCH_BEGIN: return EventType::PinchBegin;
        case SDL_EVENT_PINCH_UPDATE: return EventType::PinchUpdate;
        case SDL_EVENT_PINCH_END: return EventType::PinchEnd;
        case SDL_EVENT_CLIPBOARD_UPDATE: return EventType::ClipboardUpdate;
        case SDL_EVENT_DROP_FILE: return EventType::DropFile;
        case SDL_EVENT_DROP_TEXT: return EventType::DropText;
        case SDL_EVENT_DROP_BEGIN: return EventType::DropBegin;
        case SDL_EVENT_DROP_COMPLETE: return EventType::DropComplete;
        case SDL_EVENT_DROP_POSITION: return EventType::DropPosition;
        case SDL_EVENT_AUDIO_DEVICE_ADDED: return EventType::AudioDeviceAdded;
        case SDL_EVENT_AUDIO_DEVICE_REMOVED: return EventType::AudioDeviceRemoved;
        case SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED: return EventType::AudioDeviceFormatChanged;
        case SDL_EVENT_SENSOR_UPDATE: return EventType::SensorUpdate;
        case SDL_EVENT_PEN_PROXIMITY_IN: return EventType::PenProximityIn;
        case SDL_EVENT_PEN_PROXIMITY_OUT: return EventType::PenProximityOut;
        case SDL_EVENT_PEN_DOWN: return EventType::PenDown;
        case SDL_EVENT_PEN_UP: return EventType::PenUp;
        case SDL_EVENT_PEN_BUTTON_DOWN: return EventType::PenButtonDown;
        case SDL_EVENT_PEN_BUTTON_UP: return EventType::PenButtonUp;
        case SDL_EVENT_PEN_MOTION: return EventType::PenMotion;
        case SDL_EVENT_PEN_AXIS: return EventType::PenAxis;
        case SDL_EVENT_CAMERA_DEVICE_ADDED: return EventType::CameraDeviceAdded;
        case SDL_EVENT_CAMERA_DEVICE_REMOVED: return EventType::CameraDeviceRemoved;
        case SDL_EVENT_CAMERA_DEVICE_APPROVED: return EventType::CameraDeviceApproved;
        case SDL_EVENT_CAMERA_DEVICE_DENIED: return EventType::CameraDeviceDenied;
        case SDL_EVENT_RENDER_TARGETS_RESET: return EventType::RenderTargetsReset;
        case SDL_EVENT_RENDER_DEVICE_RESET: return EventType::RenderDeviceReset;
        case SDL_EVENT_RENDER_DEVICE_LOST: return EventType::RenderDeviceLost;
        default: return EventType::Unknown;
    }
}

bool CheckWindowCall(bool result, const char* operation) {
    if (!result) {
        WriteLog(LogLevel::Warn, "WINDOW", std::string(operation) + " failed: " + SDL_GetError());
    }

    return result;
}

Event TranslateEvent(const SDL_Event& sdlEvent) {
    Event event{};
    event.type = TranslateEventType(sdlEvent.type);
    event.timestamp = sdlEvent.common.timestamp;

    switch (sdlEvent.type) {
        case SDL_EVENT_DISPLAY_ORIENTATION:
        case SDL_EVENT_DISPLAY_ADDED:
        case SDL_EVENT_DISPLAY_REMOVED:
        case SDL_EVENT_DISPLAY_MOVED:
        case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
        case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
        case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
        case SDL_EVENT_DISPLAY_USABLE_BOUNDS_CHANGED:
            event.which = sdlEvent.display.displayID;
            event.data1 = sdlEvent.display.data1;
            event.data2 = sdlEvent.display.data2;
            break;
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_HIDDEN:
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_MOVED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        case SDL_EVENT_WINDOW_HIT_TEST:
        case SDL_EVENT_WINDOW_ICCPROF_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
        case SDL_EVENT_WINDOW_OCCLUDED:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        case SDL_EVENT_WINDOW_DESTROYED:
        case SDL_EVENT_WINDOW_HDR_STATE_CHANGED:
            event.windowId = sdlEvent.window.windowID;
            event.data1 = sdlEvent.window.data1;
            event.data2 = sdlEvent.window.data2;
            break;
        case SDL_EVENT_KEYBOARD_ADDED:
        case SDL_EVENT_KEYBOARD_REMOVED:
            event.which = sdlEvent.kdevice.which;
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            event.windowId = sdlEvent.key.windowID;
            event.which = sdlEvent.key.which;
            event.scancode = sdlEvent.key.scancode;
            event.key = sdlEvent.key.key;
            event.modifiers = sdlEvent.key.mod;
            event.down = sdlEvent.key.down;
            event.repeat = sdlEvent.key.repeat;
            event.data1 = sdlEvent.key.raw;
            break;
        case SDL_EVENT_TEXT_EDITING:
            event.windowId = sdlEvent.edit.windowID;
            event.data1 = sdlEvent.edit.start;
            event.data2 = sdlEvent.edit.length;
            CopyText(event.text, sizeof(event.text), sdlEvent.edit.text);
            break;
        case SDL_EVENT_TEXT_EDITING_CANDIDATES:
            event.windowId = sdlEvent.edit_candidates.windowID;
            event.data1 = sdlEvent.edit_candidates.num_candidates;
            event.data2 = sdlEvent.edit_candidates.selected_candidate;
            if (sdlEvent.edit_candidates.candidates != nullptr && sdlEvent.edit_candidates.num_candidates > 0) {
                CopyText(event.text, sizeof(event.text), sdlEvent.edit_candidates.candidates[0]);
            }
            break;
        case SDL_EVENT_TEXT_INPUT:
            event.windowId = sdlEvent.text.windowID;
            CopyText(event.text, sizeof(event.text), sdlEvent.text.text);
            break;
        case SDL_EVENT_MOUSE_ADDED:
        case SDL_EVENT_MOUSE_REMOVED:
            event.which = sdlEvent.mdevice.which;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            event.windowId = sdlEvent.motion.windowID;
            event.which = sdlEvent.motion.which;
            event.data1 = static_cast<std::int32_t>(sdlEvent.motion.state);
            event.x = sdlEvent.motion.x;
            event.y = sdlEvent.motion.y;
            event.dx = sdlEvent.motion.xrel;
            event.dy = sdlEvent.motion.yrel;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            event.windowId = sdlEvent.button.windowID;
            event.which = sdlEvent.button.which;
            event.button = sdlEvent.button.button;
            event.down = sdlEvent.button.down;
            event.clicks = sdlEvent.button.clicks;
            event.x = sdlEvent.button.x;
            event.y = sdlEvent.button.y;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            event.windowId = sdlEvent.wheel.windowID;
            event.which = sdlEvent.wheel.which;
            event.x = sdlEvent.wheel.mouse_x;
            event.y = sdlEvent.wheel.mouse_y;
            event.dx = sdlEvent.wheel.x;
            event.dy = sdlEvent.wheel.y;
            event.data1 = sdlEvent.wheel.integer_x;
            event.data2 = sdlEvent.wheel.integer_y;
            break;
        case SDL_EVENT_JOYSTICK_AXIS_MOTION:
            event.which = sdlEvent.jaxis.which;
            event.data1 = sdlEvent.jaxis.axis;
            event.data2 = sdlEvent.jaxis.value;
            break;
        case SDL_EVENT_JOYSTICK_BALL_MOTION:
            event.which = sdlEvent.jball.which;
            event.data1 = sdlEvent.jball.ball;
            event.dx = static_cast<float>(sdlEvent.jball.xrel);
            event.dy = static_cast<float>(sdlEvent.jball.yrel);
            break;
        case SDL_EVENT_JOYSTICK_HAT_MOTION:
            event.which = sdlEvent.jhat.which;
            event.data1 = sdlEvent.jhat.hat;
            event.data2 = sdlEvent.jhat.value;
            break;
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        case SDL_EVENT_JOYSTICK_BUTTON_UP:
            event.which = sdlEvent.jbutton.which;
            event.button = sdlEvent.jbutton.button;
            event.down = sdlEvent.jbutton.down;
            break;
        case SDL_EVENT_JOYSTICK_ADDED:
        case SDL_EVENT_JOYSTICK_REMOVED:
        case SDL_EVENT_JOYSTICK_UPDATE_COMPLETE:
            event.which = sdlEvent.jdevice.which;
            break;
        case SDL_EVENT_JOYSTICK_BATTERY_UPDATED:
            event.which = sdlEvent.jbattery.which;
            event.data1 = sdlEvent.jbattery.state;
            event.data2 = sdlEvent.jbattery.percent;
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            event.which = sdlEvent.gaxis.which;
            event.data1 = sdlEvent.gaxis.axis;
            event.data2 = sdlEvent.gaxis.value;
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            event.which = sdlEvent.gbutton.which;
            event.button = sdlEvent.gbutton.button;
            event.down = sdlEvent.gbutton.down;
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
        case SDL_EVENT_GAMEPAD_REMAPPED:
        case SDL_EVENT_GAMEPAD_UPDATE_COMPLETE:
        case SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED:
            event.which = sdlEvent.gdevice.which;
            break;
        case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
        case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
        case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
            event.which = sdlEvent.gtouchpad.which;
            event.data1 = sdlEvent.gtouchpad.touchpad;
            event.data2 = sdlEvent.gtouchpad.finger;
            event.x = sdlEvent.gtouchpad.x;
            event.y = sdlEvent.gtouchpad.y;
            event.pressure = sdlEvent.gtouchpad.pressure;
            break;
        case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
            event.which = sdlEvent.gsensor.which;
            event.data1 = sdlEvent.gsensor.sensor;
            event.x = sdlEvent.gsensor.data[0];
            event.y = sdlEvent.gsensor.data[1];
            event.dx = sdlEvent.gsensor.data[2];
            break;
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_CANCELED:
            event.windowId = sdlEvent.tfinger.windowID;
            event.which = sdlEvent.tfinger.touchID;
            event.data1 = static_cast<std::int32_t>(sdlEvent.tfinger.fingerID);
            event.x = sdlEvent.tfinger.x;
            event.y = sdlEvent.tfinger.y;
            event.dx = sdlEvent.tfinger.dx;
            event.dy = sdlEvent.tfinger.dy;
            event.pressure = sdlEvent.tfinger.pressure;
            break;
        case SDL_EVENT_PINCH_BEGIN:
        case SDL_EVENT_PINCH_UPDATE:
        case SDL_EVENT_PINCH_END:
            event.windowId = sdlEvent.pinch.windowID;
            event.scale = sdlEvent.pinch.scale;
            break;
        case SDL_EVENT_DROP_FILE:
        case SDL_EVENT_DROP_TEXT:
        case SDL_EVENT_DROP_BEGIN:
        case SDL_EVENT_DROP_COMPLETE:
        case SDL_EVENT_DROP_POSITION:
            event.windowId = sdlEvent.drop.windowID;
            event.x = sdlEvent.drop.x;
            event.y = sdlEvent.drop.y;
            CopyText(event.text, sizeof(event.text), sdlEvent.drop.data);
            break;
        case SDL_EVENT_CLIPBOARD_UPDATE:
            event.down = sdlEvent.clipboard.owner;
            event.data1 = sdlEvent.clipboard.num_mime_types;
            break;
        case SDL_EVENT_AUDIO_DEVICE_ADDED:
        case SDL_EVENT_AUDIO_DEVICE_REMOVED:
        case SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED:
            event.which = sdlEvent.adevice.which;
            event.down = sdlEvent.adevice.recording;
            break;
        case SDL_EVENT_SENSOR_UPDATE:
            event.which = sdlEvent.sensor.which;
            event.x = sdlEvent.sensor.data[0];
            event.y = sdlEvent.sensor.data[1];
            event.dx = sdlEvent.sensor.data[2];
            event.dy = sdlEvent.sensor.data[3];
            break;
        case SDL_EVENT_PEN_PROXIMITY_IN:
        case SDL_EVENT_PEN_PROXIMITY_OUT:
            event.windowId = sdlEvent.pproximity.windowID;
            event.which = sdlEvent.pproximity.which;
            break;
        case SDL_EVENT_PEN_DOWN:
        case SDL_EVENT_PEN_UP:
            event.windowId = sdlEvent.ptouch.windowID;
            event.which = sdlEvent.ptouch.which;
            event.x = sdlEvent.ptouch.x;
            event.y = sdlEvent.ptouch.y;
            event.down = sdlEvent.ptouch.down;
            break;
        case SDL_EVENT_PEN_BUTTON_DOWN:
        case SDL_EVENT_PEN_BUTTON_UP:
            event.windowId = sdlEvent.pbutton.windowID;
            event.which = sdlEvent.pbutton.which;
            event.x = sdlEvent.pbutton.x;
            event.y = sdlEvent.pbutton.y;
            event.button = sdlEvent.pbutton.button;
            event.down = sdlEvent.pbutton.down;
            break;
        case SDL_EVENT_PEN_MOTION:
            event.windowId = sdlEvent.pmotion.windowID;
            event.which = sdlEvent.pmotion.which;
            event.x = sdlEvent.pmotion.x;
            event.y = sdlEvent.pmotion.y;
            event.data1 = static_cast<std::int32_t>(sdlEvent.pmotion.pen_state);
            break;
        case SDL_EVENT_PEN_AXIS:
            event.windowId = sdlEvent.paxis.windowID;
            event.which = sdlEvent.paxis.which;
            event.x = sdlEvent.paxis.x;
            event.y = sdlEvent.paxis.y;
            event.data1 = sdlEvent.paxis.axis;
            event.scale = sdlEvent.paxis.value;
            break;
        case SDL_EVENT_CAMERA_DEVICE_ADDED:
        case SDL_EVENT_CAMERA_DEVICE_REMOVED:
        case SDL_EVENT_CAMERA_DEVICE_APPROVED:
        case SDL_EVENT_CAMERA_DEVICE_DENIED:
            event.which = sdlEvent.cdevice.which;
            break;
        case SDL_EVENT_RENDER_TARGETS_RESET:
        case SDL_EVENT_RENDER_DEVICE_RESET:
        case SDL_EVENT_RENDER_DEVICE_LOST:
            event.windowId = sdlEvent.render.windowID;
            break;
        default:
            break;
    }

    return event;
}

void PumpSystemEvents() {
    gRenderer.previousKeys = gRenderer.currentKeys;
    gRenderer.events.clear();
    gRenderer.nextEventIndex = 0;
    gRenderer.shouldClose = false;

    SDL_Event sdlEvent;
    while (SDL_PollEvent(&sdlEvent)) {
        if (sdlEvent.type == SDL_EVENT_QUIT || sdlEvent.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            gRenderer.shouldClose = true;
        }
        if (sdlEvent.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            RefreshViewport();
        }

        gRenderer.events.push_back(TranslateEvent(sdlEvent));
    }

    UpdateInputFromEvents();
    gRenderer.eventsReady = true;
}

GLuint CompileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE) {
        return shader;
    }

    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(length), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    glDeleteShader(shader);
    Fail("Shader compilation failed: " + log);
}

GLuint CreateProgram() {
    const GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, kVertexShaderSource);
    const GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, kFragmentShaderSource);

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_TRUE) {
        return program;
    }

    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(length), '\0');
    glGetProgramInfoLog(program, length, nullptr, log.data());
    glDeleteProgram(program);
    Fail("Program link failed: " + log);
}

void EnsureInitialized() {
    if (gRenderer.window == nullptr) {
        Fail("QuarkCore is not initialized. Call InitWindow() first.");
    }
}

std::array<float, 4> ToNormalizedColor(Color color) {
    constexpr float inv = 1.0f / 255.0f;
    return {color.r * inv, color.g * inv, color.b * inv, color.a * inv};
}

void RefreshViewport() {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(gRenderer.window, &width, &height);
    gRenderer.width = width;
    gRenderer.height = height;
    glViewport(0, 0, width, height);
}

const ShaderUniformLocations& GetShaderUniformLocations(GLuint program) {
    if (program == 0) {
        static const ShaderUniformLocations emptyLocations;
        return emptyLocations;
    }

    auto it = gShaderUniformCache.find(program);
    if (it != gShaderUniformCache.end()) {
        return it->second;
    }

    ShaderUniformLocations locations;
    locations.screenSizeLocation = glGetUniformLocation(program, "uScreenSize");
    locations.samplerLocation = glGetUniformLocation(program, "uTexture");
    auto inserted = gShaderUniformCache.emplace(program, locations);
    return inserted.first->second;
}

void EnsureBatchSpace(std::size_t vertexCount, std::size_t indexCount) {
    if (gRenderer.batchVertices.size() + vertexCount > kMaxBatchVertices ||
        gRenderer.batchIndices.size() + indexCount > kMaxBatchIndices) {
        FlushBatch();
    }
}

GLuint CreateTextureFromRgba(const std::uint8_t* pixels, int width, int height) {
    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return textureId;
}

void FlushBatch() {
    if (gRenderer.batchCommands.empty()) {
        return;
    }

    std::vector<BatchCommand> sortedCommands = gRenderer.batchCommands;
    std::stable_sort(sortedCommands.begin(), sortedCommands.end(), [](const BatchCommand& a, const BatchCommand& b) {
        return a.textureId < b.textureId;
    });

    std::vector<Vertex> vertexData;
    std::vector<GLuint> indexData;
    vertexData.reserve(gRenderer.batchVertices.size());
    indexData.reserve(gRenderer.batchIndices.size());

    for (const auto& command : sortedCommands) {
        const std::size_t baseVertex = vertexData.size();
        vertexData.insert(
            vertexData.end(),
            gRenderer.batchVertices.begin() + command.vertexStart,
            gRenderer.batchVertices.begin() + command.vertexStart + command.vertexCount
        );

        for (std::size_t i = 0; i < command.indexCount; ++i) {
            const GLuint originalIndex = gRenderer.batchIndices[command.indexStart + i];
            indexData.push_back(static_cast<GLuint>(baseVertex) + (originalIndex - static_cast<GLuint>(command.vertexStart)));
        }
    }

    glUseProgram(gRenderer.currentShaderProgram);
    const ShaderUniformLocations& uniforms = GetShaderUniformLocations(gRenderer.currentShaderProgram);
    if (uniforms.screenSizeLocation >= 0) {
        glUniform2f(uniforms.screenSizeLocation, static_cast<float>(gRenderer.width), static_cast<float>(gRenderer.height));
    }
    if (uniforms.samplerLocation >= 0) {
        glUniform1i(uniforms.samplerLocation, 0);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(gRenderer.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gRenderer.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertexData.size() * sizeof(Vertex)),
        vertexData.data(),
        GL_DYNAMIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gRenderer.ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indexData.size() * sizeof(GLuint)),
        indexData.data(),
        GL_DYNAMIC_DRAW
    );

    GLuint activeTexture = 0;
    std::size_t indexOffset = 0;
    std::size_t groupStart = 0;
    std::size_t groupCount = 0;
    for (const auto& command : sortedCommands) {
        if (command.textureId != activeTexture) {
            if (groupCount > 0) {
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(groupCount), GL_UNSIGNED_INT, reinterpret_cast<const void*>(groupStart * sizeof(GLuint)));
            }
            activeTexture = command.textureId;
            glBindTexture(GL_TEXTURE_2D, activeTexture);
            groupStart = indexOffset;
            groupCount = command.indexCount;
        } else {
            groupCount += command.indexCount;
        }
        indexOffset += command.indexCount;
    }

    if (groupCount > 0) {
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(groupCount), GL_UNSIGNED_INT, reinterpret_cast<const void*>(groupStart * sizeof(GLuint)));
    }

    gRenderer.batchVertices.clear();
    gRenderer.batchIndices.clear();
    gRenderer.batchCommands.clear();
}

void PushVertex(const Vertex& vertex) {
    gRenderer.batchVertices.push_back(vertex);
}

void PushQuad(GLuint textureId, float x, float y, float width, float height, Color color) {
    const GLuint resolvedTexture = textureId != 0 ? textureId : gRenderer.whiteTexture;
    const auto normalized = ToNormalizedColor(color);
    EnsureBatchSpace(4, 6);

    const std::size_t vertexStart = gRenderer.batchVertices.size();
    PushVertex({x, y, 0.0f, 0.0f, normalized[0], normalized[1], normalized[2], normalized[3]});
    PushVertex({x + width, y, 1.0f, 0.0f, normalized[0], normalized[1], normalized[2], normalized[3]});
    PushVertex({x + width, y + height, 1.0f, 1.0f, normalized[0], normalized[1], normalized[2], normalized[3]});
    PushVertex({x, y + height, 0.0f, 1.0f, normalized[0], normalized[1], normalized[2], normalized[3]});

    const GLuint baseIndex = static_cast<GLuint>(vertexStart);
    const std::size_t indexStart = gRenderer.batchIndices.size();
    gRenderer.batchIndices.push_back(baseIndex);
    gRenderer.batchIndices.push_back(baseIndex + 1);
    gRenderer.batchIndices.push_back(baseIndex + 2);
    gRenderer.batchIndices.push_back(baseIndex);
    gRenderer.batchIndices.push_back(baseIndex + 2);
    gRenderer.batchIndices.push_back(baseIndex + 3);

    gRenderer.batchCommands.push_back({resolvedTexture, vertexStart, 4, indexStart, 6});
}

void PushCircle(float centerX, float centerY, float radius, Color color) {
    const GLuint resolvedTexture = gRenderer.whiteTexture;
    const auto normalized = ToNormalizedColor(color);
    constexpr int segments = 48;
    EnsureBatchSpace(static_cast<std::size_t>(segments) + 1, static_cast<std::size_t>(segments) * 3);

    const std::size_t vertexStart = gRenderer.batchVertices.size();
    PushVertex({centerX, centerY, 0.5f, 0.5f, normalized[0], normalized[1], normalized[2], normalized[3]});

    for (int i = 0; i < segments; ++i) {
        const float angle = static_cast<float>(i) / static_cast<float>(segments) * 6.28318530718f;
        const float x = centerX + std::cos(angle) * radius;
        const float y = centerY + std::sin(angle) * radius;
        const float u = 0.5f + std::cos(angle) * 0.5f;
        const float v = 0.5f + std::sin(angle) * 0.5f;
        PushVertex({x, y, u, v, normalized[0], normalized[1], normalized[2], normalized[3]});
    }

    const GLuint baseIndex = static_cast<GLuint>(vertexStart);
    const std::size_t indexStart = gRenderer.batchIndices.size();
    for (int i = 1; i <= segments; ++i) {
        gRenderer.batchIndices.push_back(baseIndex);
        gRenderer.batchIndices.push_back(baseIndex + static_cast<GLuint>(i));
        gRenderer.batchIndices.push_back(baseIndex + static_cast<GLuint>(i % segments) + 1);
    }

    gRenderer.batchCommands.push_back({resolvedTexture, vertexStart, static_cast<std::size_t>(segments) + 1, indexStart, static_cast<std::size_t>(segments) * 3});
}

Texture2D LoadPngTexture(const char* filePath) {
    PngImageData image;
    if (!LoadPngImage(filePath, image)) {
        return {};
    }

    Texture2D texture;
    texture.id = CreateTextureFromRgba(image.pixels.data(), image.width, image.height);
    texture.width = image.width;
    texture.height = image.height;
    texture.valid = true;
    return texture;
}

bool LoadPngImage(const char* filePath, PngImageData& image) {
    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, filePath, "rb") != 0) {
        return false;
    }
#else
    file = std::fopen(filePath, "rb");
    if (file == nullptr) {
        return false;
    }
#endif

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png == nullptr) {
        std::fclose(file);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (info == nullptr) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        std::fclose(file);
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(file);
        return false;
    }

    png_init_io(png, file);
    png_read_info(png, info);

    const png_uint_32 width = png_get_image_width(png, info);
    const png_uint_32 height = png_get_image_height(png, info);
    const png_byte colorType = png_get_color_type(png, info);
    const png_byte bitDepth = png_get_bit_depth(png, info);

    if (bitDepth == 16) {
        png_set_strip_16(png);
    }
    if (colorType == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS) != 0) {
        png_set_tRNS_to_alpha(png);
    }
    if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }

    png_read_update_info(png, info);

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    std::vector<png_bytep> rows(height);
    for (png_uint_32 y = 0; y < height; ++y) {
        rows[y] = pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4U;
    }

    png_read_image(png, rows.data());

    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(file);

    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.pixels = std::move(pixels);
    return true;
}

void UpdateInputFromEvents() {
    gRenderer.previousKeys = gRenderer.currentKeys;

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    const SDL_MouseButtonFlags mouseState = SDL_GetMouseState(&mouseX, &mouseY);
    gRenderer.mousePosition = Vec2{mouseX, mouseY};
    gRenderer.mouseButtons[static_cast<std::size_t>(MouseButton::Left)] = (mouseState & SDL_BUTTON_LMASK) != 0;
    gRenderer.mouseButtons[static_cast<std::size_t>(MouseButton::Middle)] = (mouseState & SDL_BUTTON_MMASK) != 0;
    gRenderer.mouseButtons[static_cast<std::size_t>(MouseButton::Right)] = (mouseState & SDL_BUTTON_RMASK) != 0;

    const bool* keyboardState = SDL_GetKeyboardState(nullptr);
    for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) {
        gRenderer.currentKeys[static_cast<std::size_t>(i)] = keyboardState[i];
    }
}

}  // namespace

void InitWindow(int width, int height, const char* title) {
    WriteLog(LogLevel::Info, "CORE", "Starting QuarkCore " + std::to_string(QC_VERSION_MAJOR) + "." + std::to_string(QC_VERSION_MINOR) + "." + std::to_string(QC_VERSION_PATCH));

    if (gRenderer.window != nullptr) {
        return;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        Fail("SDL_Init failed");
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    gRenderer.window = SDL_CreateWindow(title, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (gRenderer.window == nullptr) {
        const std::string error = SDL_GetError();
        SDL_Quit();
        Fail("SDL_CreateWindow failed: " + error);
    }

    gRenderer.context = SDL_GL_CreateContext(gRenderer.window);
    if (gRenderer.context == nullptr) {
        const std::string error = SDL_GetError();
        SDL_DestroyWindow(gRenderer.window);
        gRenderer.window = nullptr;
        SDL_Quit();
        Fail("SDL_GL_CreateContext failed: " + error);
    }

    if (!SDL_GL_MakeCurrent(gRenderer.window, gRenderer.context)) {
        const std::string error = SDL_GetError();
        CloseWindow();
        Fail("SDL_GL_MakeCurrent failed: " + error);
    }

    SDL_GL_SetSwapInterval(0);

    glewExperimental = GL_TRUE;
    const GLenum glewStatus = glewInit();
    if (glewStatus != GLEW_OK) {
        const std::string error = reinterpret_cast<const char*>(glewGetErrorString(glewStatus));
        CloseWindow();
        Fail("glewInit failed: " + error);
    }

    gRenderer.program = CreateProgram();
    gRenderer.defaultShaderProgram = gRenderer.program;
    gRenderer.currentShaderProgram = gRenderer.program;
    glGenVertexArrays(1, &gRenderer.vao);
    glGenBuffers(1, &gRenderer.vbo);
    glGenBuffers(1, &gRenderer.ebo);
    glBindVertexArray(gRenderer.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gRenderer.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gRenderer.ebo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));

    gRenderer.screenSizeLocation = glGetUniformLocation(gRenderer.program, "uScreenSize");
    gRenderer.samplerLocation = glGetUniformLocation(gRenderer.program, "uTexture");

    const std::array<std::uint8_t, 4> whitePixel{255, 255, 255, 255};
    gRenderer.whiteTexture = CreateTextureFromRgba(whitePixel.data(), 1, 1);
    gRenderer.currentTexture = gRenderer.whiteTexture;
    gRenderer.batchVertices.reserve(kMaxBatchVertices);
    gRenderer.batchIndices.reserve(kMaxBatchIndices);
    gRenderer.batchCommands.reserve(256);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    RefreshViewport();
    gRenderer.lastFrameCounter = SDL_GetPerformanceCounter();
    gRenderer.minimumLogLevel = LogLevel::Trace;
    gRenderer.eventsReady = false;

    int version = SDL_GetVersion();

    WriteLog(LogLevel::Info, "CORE", "SDL Version: " + std::to_string(SDL_VERSIONNUM_MAJOR(version)) + "." +
                                                       std::to_string(SDL_VERSIONNUM_MINOR(version)) + "." +
                                                       std::to_string(SDL_VERSIONNUM_MICRO(version)))
    ;

    WriteLog(LogLevel::Info, "CORE", "OpenGL Version: " + std::string(reinterpret_cast<const char*>(glGetString(GL_VERSION))));
    WriteLog(LogLevel::Info, "CORE", "OpenGL Renderer: " + std::string(reinterpret_cast<const char*>(glGetString(GL_RENDERER))));
    WriteLog(LogLevel::Info, "CORE", "OpenGL Vendor: " + std::string(reinterpret_cast<const char*>(glGetString(GL_VENDOR))));
    WriteLog(LogLevel::Info, "CORE", "OpenGL Shading Language Version: " + std::string(reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION))));

    WriteLog(LogLevel::Info, "CORE", "Window and renderer initialized");
}

bool WindowShouldClose() {
    EnsureInitialized();
    if (!gRenderer.eventsReady) {
        PumpSystemEvents();
    }

    return gRenderer.shouldClose;
}

bool PollEvent(Event& event) {
    EnsureInitialized();
    if (!gRenderer.eventsReady) {
        PumpSystemEvents();
    }

    if (gRenderer.nextEventIndex >= gRenderer.events.size()) {
        return false;
    }

    event = gRenderer.events[gRenderer.nextEventIndex++];
    return true;
}

bool WaitEvent(Event& event) {
    EnsureInitialized();

    SDL_Event sdlEvent;
    if (!SDL_WaitEvent(&sdlEvent)) {
        return false;
    }

    gRenderer.eventsReady = false;
    gRenderer.events.clear();
    gRenderer.nextEventIndex = 0;
    if (sdlEvent.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        RefreshViewport();
    }
    UpdateInputFromEvents();
    event = TranslateEvent(sdlEvent);
    return true;
}

bool WaitEventTimeout(Event& event, int timeoutMs) {
    EnsureInitialized();

    SDL_Event sdlEvent;
    if (!SDL_WaitEventTimeout(&sdlEvent, timeoutMs)) {
        return false;
    }

    gRenderer.eventsReady = false;
    gRenderer.events.clear();
    gRenderer.nextEventIndex = 0;
    if (sdlEvent.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        RefreshViewport();
    }
    UpdateInputFromEvents();
    event = TranslateEvent(sdlEvent);
    return true;
}

const char* GetEventTypeName(EventType type) {
    switch (type) {
        case EventType::None: return "None";
        case EventType::Quit: return "Quit";
        case EventType::WindowResized: return "WindowResized";
        case EventType::WindowPixelSizeChanged: return "WindowPixelSizeChanged";
        case EventType::WindowFocusGained: return "WindowFocusGained";
        case EventType::WindowFocusLost: return "WindowFocusLost";
        case EventType::TextInput: return "TextInput";
        case EventType::TextEditing: return "TextEditing";
        case EventType::KeyDown: return "KeyDown";
        case EventType::KeyUp: return "KeyUp";
        case EventType::MouseMotion: return "MouseMotion";
        case EventType::MouseButtonDown: return "MouseButtonDown";
        case EventType::MouseButtonUp: return "MouseButtonUp";
        case EventType::MouseWheel: return "MouseWheel";
        case EventType::DropFile: return "DropFile";
        case EventType::DropText: return "DropText";
        case EventType::ClipboardUpdate: return "ClipboardUpdate";
        case EventType::WindowShown: return "WindowShown";
        case EventType::WindowHidden: return "WindowHidden";
        case EventType::WindowMoved: return "WindowMoved";
        case EventType::WindowMinimized: return "WindowMinimized";
        case EventType::WindowMaximized: return "WindowMaximized";
        case EventType::WindowRestored: return "WindowRestored";
        case EventType::WindowCloseRequested: return "WindowCloseRequested";
        case EventType::WindowEnterFullscreen: return "WindowEnterFullscreen";
        case EventType::WindowLeaveFullscreen: return "WindowLeaveFullscreen";
        case EventType::FingerDown: return "FingerDown";
        case EventType::FingerUp: return "FingerUp";
        case EventType::FingerMotion: return "FingerMotion";
        case EventType::PinchBegin: return "PinchBegin";
        case EventType::PinchUpdate: return "PinchUpdate";
        case EventType::PinchEnd: return "PinchEnd";
        case EventType::PenDown: return "PenDown";
        case EventType::PenUp: return "PenUp";
        case EventType::RenderDeviceReset: return "RenderDeviceReset";
        case EventType::RenderDeviceLost: return "RenderDeviceLost";
        default: return "Event";
    }
}

void CloseWindow() {
    if (gRenderer.whiteTexture != 0) {
        glDeleteTextures(1, &gRenderer.whiteTexture);
        gRenderer.whiteTexture = 0;
    }
    if (gRenderer.vbo != 0) {
        glDeleteBuffers(1, &gRenderer.vbo);
        gRenderer.vbo = 0;
    }
    if (gRenderer.ebo != 0) {
        glDeleteBuffers(1, &gRenderer.ebo);
        gRenderer.ebo = 0;
    }
    if (gRenderer.vao != 0) {
        glDeleteVertexArrays(1, &gRenderer.vao);
        gRenderer.vao = 0;
    }
    if (gRenderer.program != 0) {
        glDeleteProgram(gRenderer.program);
        gRenderer.program = 0;
    }
    if (gRenderer.context != nullptr) {
        SDL_GL_DestroyContext(gRenderer.context);
        gRenderer.context = nullptr;
    }
    if (gRenderer.window != nullptr) {
        SDL_DestroyWindow(gRenderer.window);
        gRenderer.window = nullptr;
    }

    SDL_Quit();
    gRenderer = {};
}

bool SetWindowTitle(const char* title) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowTitle(gRenderer.window, title != nullptr ? title : ""), "SDL_SetWindowTitle");
}

const char* GetWindowTitle() {
    EnsureInitialized();
    return SDL_GetWindowTitle(gRenderer.window);
}

bool SetWindowPosition(int x, int y) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowPosition(gRenderer.window, x, y), "SDL_SetWindowPosition");
}

IVec2 GetWindowPosition() {
    EnsureInitialized();
    IVec2 position{};
    SDL_GetWindowPosition(gRenderer.window, &position.x, &position.y);
    return position;
}

bool SetWindowSize(int width, int height) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowSize(gRenderer.window, width, height), "SDL_SetWindowSize");
}

IVec2 GetWindowSize() {
    EnsureInitialized();
    IVec2 size{};
    SDL_GetWindowSize(gRenderer.window, &size.x, &size.y);
    return size;
}

IVec2 GetWindowSizeInPixels() {
    EnsureInitialized();
    IVec2 size{};
    SDL_GetWindowSizeInPixels(gRenderer.window, &size.x, &size.y);
    return size;
}

bool SetWindowMinimumSize(int width, int height) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowMinimumSize(gRenderer.window, width, height), "SDL_SetWindowMinimumSize");
}

IVec2 GetWindowMinimumSize() {
    EnsureInitialized();
    IVec2 size{};
    SDL_GetWindowMinimumSize(gRenderer.window, &size.x, &size.y);
    return size;
}

bool SetWindowMaximumSize(int width, int height) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowMaximumSize(gRenderer.window, width, height), "SDL_SetWindowMaximumSize");
}

IVec2 GetWindowMaximumSize() {
    EnsureInitialized();
    IVec2 size{};
    SDL_GetWindowMaximumSize(gRenderer.window, &size.x, &size.y);
    return size;
}

bool SetWindowResizable(bool resizable) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowResizable(gRenderer.window, resizable), "SDL_SetWindowResizable");
}

bool SetWindowBordered(bool bordered) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowBordered(gRenderer.window, bordered), "SDL_SetWindowBordered");
}

bool SetWindowFullscreen(bool fullscreen) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowFullscreen(gRenderer.window, fullscreen), "SDL_SetWindowFullscreen");
}

bool ToggleFullscreen() {
    EnsureInitialized();
    return SetWindowFullscreen(!IsWindowFullscreen());
}

bool ShowWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_ShowWindow(gRenderer.window), "SDL_ShowWindow");
}

bool HideWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_HideWindow(gRenderer.window), "SDL_HideWindow");
}

bool RaiseWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_RaiseWindow(gRenderer.window), "SDL_RaiseWindow");
}

bool MaximizeWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_MaximizeWindow(gRenderer.window), "SDL_MaximizeWindow");
}

bool MinimizeWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_MinimizeWindow(gRenderer.window), "SDL_MinimizeWindow");
}

bool RestoreWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_RestoreWindow(gRenderer.window), "SDL_RestoreWindow");
}

bool SyncWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_SyncWindow(gRenderer.window), "SDL_SyncWindow");
}

bool IsWindowFullscreen() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gRenderer.window) & SDL_WINDOW_FULLSCREEN) != 0;
}

bool IsWindowHidden() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gRenderer.window) & SDL_WINDOW_HIDDEN) != 0;
}

bool IsWindowMinimized() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gRenderer.window) & SDL_WINDOW_MINIMIZED) != 0;
}

bool IsWindowMaximized() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gRenderer.window) & SDL_WINDOW_MAXIMIZED) != 0;
}

bool IsWindowFocused() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gRenderer.window) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

bool IsWindowMouseFocused() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gRenderer.window) & SDL_WINDOW_MOUSE_FOCUS) != 0;
}

bool IsWindowResizable() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gRenderer.window) & SDL_WINDOW_RESIZABLE) != 0;
}

bool IsWindowBorderless() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gRenderer.window) & SDL_WINDOW_BORDERLESS) != 0;
}

float GetWindowDisplayScale() {
    EnsureInitialized();
    return SDL_GetWindowDisplayScale(gRenderer.window);
}

float GetWindowPixelDensity() {
    EnsureInitialized();
    return SDL_GetWindowPixelDensity(gRenderer.window);
}

bool SetWindowIcon(const char* filePath) {
    EnsureInitialized();

    PngImageData image;
    if (!LoadPngImage(filePath, image)) {
        WriteLog(LogLevel::Warn, "WINDOW", std::string("Failed to load window icon: ") + (filePath != nullptr ? filePath : ""));
        return false;
    }

    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        image.width,
        image.height,
        SDL_PIXELFORMAT_RGBA8888,
        image.pixels.data(),
        image.width * 4
    );
    if (surface == nullptr) {
        WriteLog(LogLevel::Warn, "WINDOW", std::string("SDL_CreateSurfaceFrom failed: ") + SDL_GetError());
        return false;
    }

    const bool ok = CheckWindowCall(SDL_SetWindowIcon(gRenderer.window, surface), "SDL_SetWindowIcon");
    SDL_DestroySurface(surface);
    return ok;
}

bool StartTextInput() {
    EnsureInitialized();
    return CheckWindowCall(SDL_StartTextInput(gRenderer.window), "SDL_StartTextInput");
}

bool StopTextInput() {
    EnsureInitialized();
    return CheckWindowCall(SDL_StopTextInput(gRenderer.window), "SDL_StopTextInput");
}

bool IsTextInputActive() {
    EnsureInitialized();
    return SDL_TextInputActive(gRenderer.window);
}

void SetLogLevel(LogLevel level) {
    gRenderer.minimumLogLevel = level;
}

void TraceLog(LogLevel level, const char* type, const char* message) {
    WriteLog(level, type, message != nullptr ? message : "");
}

const char* TextFormat(const char* format, ...) {
    thread_local char buffer[4096];

    if (format == nullptr) {
        buffer[0] = '\0';
        return buffer;
    }

    va_list args;
    va_start(args, format);
#if defined(_MSC_VER)
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
#else
    std::vsnprintf(buffer, sizeof(buffer), format, args);
#endif
    va_end(args);
    return buffer;
}

void SetTargetFPS(int fps) {
    EnsureInitialized();
    gRenderer.targetFps = fps >= 0 ? fps : 0;
}

float GetFrameTime() {
    return gRenderer.frameTime;
}

float GetDeltaTime() {
    return GetFrameTime();
}

int GetFPS() {
    if (gRenderer.frameTime <= 0.0f) {
        return 0;
    }

    return static_cast<int>(std::round(1.0f / gRenderer.frameTime));
}

double GetTime() {
    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        return 0.0;
    }

    return static_cast<double>(SDL_GetTicks()) / 1000.0;
}

int GetScreenWidth() {
    EnsureInitialized();
    return gRenderer.width;
}

int GetScreenHeight() {
    EnsureInitialized();
    return gRenderer.height;
}

float GetCurrentMonitorRefreshRate() {
    EnsureInitialized();

    const SDL_DisplayID displayId = SDL_GetDisplayForWindow(gRenderer.window);
    if (displayId == 0) {
        return 0.0f;
    }

    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(displayId);
    if (mode == nullptr) {
        return 0.0f;
    }

    if (mode->refresh_rate > 0.0f) {
        return mode->refresh_rate;
    }

    if (mode->refresh_rate_numerator > 0 && mode->refresh_rate_denominator > 0) {
        return static_cast<float>(mode->refresh_rate_numerator) /
               static_cast<float>(mode->refresh_rate_denominator);
    }

    return 0.0f;
}

bool IsKeyDown(KeyboardKey key) {
    EnsureInitialized();
    const std::size_t index = static_cast<std::size_t>(key);
    return index < gRenderer.currentKeys.size() ? gRenderer.currentKeys[index] : false;
}

bool IsKeyPressed(KeyboardKey key) {
    EnsureInitialized();
    const std::size_t index = static_cast<std::size_t>(key);
    if (index >= gRenderer.currentKeys.size()) {
        return false;
    }

    return gRenderer.currentKeys[index] && !gRenderer.previousKeys[index];
}

bool IsMouseButtonDown(MouseButton button) {
    EnsureInitialized();
    const std::size_t index = static_cast<std::size_t>(button);
    return index < gRenderer.mouseButtons.size() ? gRenderer.mouseButtons[index] : false;
}

Vec2 GetMousePosition() {
    EnsureInitialized();
    return gRenderer.mousePosition;
}

void BeginDrawing() {
    EnsureInitialized();
    gRenderer.drawing = true;
    gRenderer.currentTexture = gRenderer.whiteTexture;
    gRenderer.batchVertices.clear();
    gRenderer.eventsReady = false;
    RefreshViewport();
}

void EndDrawing() {
    EnsureInitialized();
    if (!gRenderer.drawing) {
        return;
    }

    FlushBatch();
    SDL_GL_SwapWindow(gRenderer.window);

    const std::uint64_t freq = SDL_GetPerformanceFrequency();

    if (gRenderer.targetFps > 0) {
        const std::uint64_t targetTicks = freq / static_cast<std::uint64_t>(gRenderer.targetFps);
        while (true) {
            const std::uint64_t now = SDL_GetPerformanceCounter();
            const std::uint64_t elapsed = now - gRenderer.lastFrameCounter;
            if (elapsed >= targetTicks) {
                break;
            }

            const std::uint64_t remaining = targetTicks - elapsed;
            const Uint32 delayMs = static_cast<Uint32>((remaining * 1000 + freq - 1) / freq);
            SDL_Delay(delayMs > 0 ? delayMs : 0);
        }
    }

    const std::uint64_t frameEnd = SDL_GetPerformanceCounter();
    gRenderer.frameTime = static_cast<float>(frameEnd - gRenderer.lastFrameCounter) / static_cast<float>(freq);
    gRenderer.lastFrameCounter = frameEnd;
    gRenderer.drawing = false;
}

void ClearBackground(Color color) {
    EnsureInitialized();
    const auto normalized = ToNormalizedColor(color);
    glClearColor(normalized[0], normalized[1], normalized[2], normalized[3]);
    glClear(GL_COLOR_BUFFER_BIT);
}

void DrawRectangle(float x, float y, float width, float height, Color color) {
    EnsureInitialized();
    PushQuad(0, x, y, width, height, color);
}

void DrawRectangle(const Rectangle& rectangle, Color color) {
    DrawRectangle(rectangle.x, rectangle.y, rectangle.width, rectangle.height, color);
}

void DrawCircle(float centerX, float centerY, float radius, Color color) {
    EnsureInitialized();
    PushCircle(centerX, centerY, radius, color);
}

void DrawTexture(const Texture2D& texture, float x, float y, Color tint) {
    EnsureInitialized();
    if (!texture.valid || texture.id == 0) {
        return;
    }

    PushQuad(texture.id, x, y, static_cast<float>(texture.width), static_cast<float>(texture.height), tint);
}

Texture2D LoadTexture(const char* filePath) {
    EnsureInitialized();
    Texture2D texture = LoadPngTexture(filePath);
    if (texture.valid) {
        WriteLog(LogLevel::Info, "ASSETS", std::string("Loaded texture: ") + filePath);
    } else {
        WriteLog(LogLevel::Warn, "ASSETS", std::string("Failed to load texture: ") + filePath);
    }
    return texture;
}

Texture2D GenCheckerTexture(int width, int height, int cellSize, Color colorA, Color colorB) {
    EnsureInitialized();
    if (width <= 0 || height <= 0 || cellSize <= 0) {
        WriteLog(LogLevel::Warn, "ASSETS", "Invalid checker texture parameters");
        return {};
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool evenCell = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            const Color color = evenCell ? colorA : colorB;
            const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
            pixels[index + 0] = color.r;
            pixels[index + 1] = color.g;
            pixels[index + 2] = color.b;
            pixels[index + 3] = color.a;
        }
    }

    Texture2D texture;
    texture.id = CreateTextureFromRgba(pixels.data(), width, height);
    texture.width = width;
    texture.height = height;
    texture.valid = true;
    WriteLog(LogLevel::Info, "ASSETS", "Generated checker texture");
    return texture;
}

void UnloadTexture(Texture2D& texture) {
    if (texture.id != 0) {
        glDeleteTextures(1, &texture.id);
    }

    texture = {};
}

namespace {

std::string ReadShaderFile(const char* filePath) {
    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, filePath, "r") != 0) {
        return "";
    }
#else
    file = std::fopen(filePath, "r");
    if (file == nullptr) {
        return "";
    }
#endif

    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    std::string content(static_cast<std::size_t>(size), '\0');
    if (size > 0) {
        std::fread(content.data(), 1, static_cast<std::size_t>(size), file);
    }
    std::fclose(file);

    return content;
}

GLuint CompileShaderCustom(GLenum type, const char* source) {
    if (source == nullptr) {
        return 0;
    }

    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE) {
        return shader;
    }

    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(length), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    glDeleteShader(shader);

    WriteLog(LogLevel::Error, "SHADER", std::string("Compilation failed: ") + log);
    return 0;
}

}  // namespace

Shader LoadShader(const char* vsFileName, const char* fsFileName) {
    EnsureInitialized();

    std::string vsSource;
    std::string fsSource;

    if (vsFileName != nullptr) {
        vsSource = ReadShaderFile(vsFileName);
        if (vsSource.empty()) {
            WriteLog(LogLevel::Error, "SHADER", std::string("Failed to load vertex shader: ") + vsFileName);
            return {};
        }
    } else {
        vsSource = kVertexShaderSource;
    }

    if (fsFileName != nullptr) {
        fsSource = ReadShaderFile(fsFileName);
        if (fsSource.empty()) {
            WriteLog(LogLevel::Error, "SHADER", std::string("Failed to load fragment shader: ") + fsFileName);
            return {};
        }
    } else {
        fsSource = kFragmentShaderSource;
    }

    return LoadShaderFromMemory(vsSource.c_str(), fsSource.c_str());
}

Shader LoadShaderFromMemory(const char* vsSource, const char* fsSource) {
    EnsureInitialized();

    if (vsSource == nullptr || fsSource == nullptr) {
        WriteLog(LogLevel::Error, "SHADER", "Invalid shader sources");
        return {};
    }

    const GLuint vertex = CompileShaderCustom(GL_VERTEX_SHADER, vsSource);
    if (vertex == 0) {
        return {};
    }

    const GLuint fragment = CompileShaderCustom(GL_FRAGMENT_SHADER, fsSource);
    if (fragment == 0) {
        glDeleteShader(vertex);
        return {};
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success != GL_TRUE) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        glDeleteProgram(program);
        WriteLog(LogLevel::Error, "SHADER", std::string("Link failed: ") + log);
        return {};
    }

    Shader shader;
    shader.id = program;
    shader.locs = nullptr;
    shader.locCount = 0;

    WriteLog(LogLevel::Info, "SHADER", "Shader loaded successfully");
    return shader;
}

bool IsShaderValid(const Shader& shader) {
    return shader.id != 0;
}

int GetShaderLocation(const Shader& shader, const char* uniformName) {
    if (shader.id == 0 || uniformName == nullptr) {
        return -1;
    }

    const GLint location = glGetUniformLocation(shader.id, uniformName);
    return location;
}

int GetShaderAttributeLocation(const Shader& shader, const char* attribName) {
    if (shader.id == 0 || attribName == nullptr) {
        return -1;
    }

    const GLint location = glGetAttribLocation(shader.id, attribName);
    return location;
}

void SetShaderValue([[maybe_unused]] const Shader& shader, int locIndex, float value) {
    if (locIndex >= 0) {
        glUniform1f(locIndex, value);
    }
}

void SetShaderValue([[maybe_unused]] const Shader& shader, int locIndex, int value) {
    if (locIndex >= 0) {
        glUniform1i(locIndex, value);
    }
}

void SetShaderValue([[maybe_unused]] const Shader& shader, int locIndex, const Vec2& value) {
    if (locIndex >= 0) {
        glUniform2f(locIndex, value.x, value.y);
    }
}

void SetShaderValue([[maybe_unused]] const Shader& shader, int locIndex, const qc::Vec3& value) {
    if (locIndex >= 0) {
        glUniform3f(locIndex, value.x, value.y, value.z);
    }
}

void SetShaderValue([[maybe_unused]] const Shader& shader, int locIndex, const Color& value) {
    if (locIndex >= 0) {
        const float r = static_cast<float>(value.r) / 255.0f;
        const float g = static_cast<float>(value.g) / 255.0f;
        const float b = static_cast<float>(value.b) / 255.0f;
        const float a = static_cast<float>(value.a) / 255.0f;
        glUniform4f(locIndex, r, g, b, a);
    }
}

void SetShaderValueMatrix([[maybe_unused]] const Shader& shader, int locIndex, const float* mat) {
    if (locIndex >= 0 && mat != nullptr) {
        glUniformMatrix4fv(locIndex, 1, GL_FALSE, mat);
    }
}

void SetShaderValueSampler([[maybe_unused]] const Shader& shader, int locIndex, int textureUnit) {
    if (locIndex >= 0) {
        glUniform1i(locIndex, textureUnit);
    }
}

void BeginShaderMode(const Shader& shader) {
    if (shader.id != 0 && gRenderer.currentShaderProgram != shader.id) {
        FlushBatch();
        gRenderer.currentShaderProgram = shader.id;
        glUseProgram(shader.id);
    }
}

void EndShaderMode() {
    if (gRenderer.currentShaderProgram != gRenderer.defaultShaderProgram) {
        FlushBatch();
        gRenderer.currentShaderProgram = gRenderer.defaultShaderProgram;
        glUseProgram(gRenderer.defaultShaderProgram);
    }
}

void UnloadShader(Shader& shader) {
    if (shader.id != 0) {
        glDeleteProgram(shader.id);
    }

    if (shader.locs != nullptr) {
        delete[] shader.locs;
        shader.locs = nullptr;
    }

    shader.locCount = 0;
    shader.id = 0;
}

namespace {

struct Model3DState {
    std::vector<Model> loadedModels;
    unsigned int nextModelId = 1;
    Mat4 viewMatrix = Mat4::identity();
    Mat4 projectionMatrix = Mat4::identity();
    GLuint shader3D = 0;
    GLint modelLoc = -1;
    GLint viewLoc = -1;
    GLint projLoc = -1;
    GLint samplerLoc = -1;
    GLint lightPosLoc = -1;
    GLuint whiteTexture = 0;
    Vec3 lightPosition{5.0f, 5.0f, 5.0f};
    bool initialized = false;
};

Model3DState g3DState;

const char* kVertex3DShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 vFragPos;
out vec3 vNormal;
out vec2 vTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    vFragPos = vec3(uModel * vec4(aPosition, 1.0));
    vNormal = mat3(uModel) * aNormal;
    vTexCoord = aTexCoord;
    gl_Position = uProjection * uView * vec4(vFragPos, 1.0);
}
)";

const char* kFragment3DShaderSource = R"(
#version 330 core
in vec3 vFragPos;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

uniform sampler2D uTexture;
uniform vec3 uLightPos;

void main() {
    vec3 norm = normalize(vNormal);
    vec3 lightDir = normalize(uLightPos - vFragPos);
    
    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * vec3(1.0, 1.0, 1.0);
    
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * vec3(1.0, 1.0, 1.0);
    
    vec4 texColor = texture(uTexture, vTexCoord);
    vec3 result = (ambient + diffuse) * texColor.rgb;
    FragColor = vec4(result, texColor.a);
}
)";


GLuint Compile3DShader() {
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &kVertex3DShaderSource, nullptr);
    glCompileShader(vertex);

    GLint vStatus;
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &vStatus);
    if (!vStatus) {
        char log[512];
        glGetShaderInfoLog(vertex, 512, nullptr, log);
        WriteLog(LogLevel::Error, "GLSL", TextFormat("Vertex shader error: %s", log));
    }

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &kFragment3DShaderSource, nullptr);
    glCompileShader(fragment);

    GLint fStatus;
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &fStatus);
    if (!fStatus) {
        char log[512];
        glGetShaderInfoLog(fragment, 512, nullptr, log);
        WriteLog(LogLevel::Error, "GLSL", TextFormat("Fragment shader error: %s", log));
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    GLint lStatus;
    glGetProgramiv(program, GL_LINK_STATUS, &lStatus);
    if (!lStatus) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        WriteLog(LogLevel::Error, "GLSL", TextFormat("Program link error: %s", log));
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    return program;
}

void ProcessMesh(const aiMesh* aiMesh, const aiScene* scene, Mesh& mesh) {
    mesh.vertices.clear();
    mesh.indices.clear();

    for (unsigned int i = 0; i < aiMesh->mNumVertices; ++i) {
        Vertex3D vertex{};
        vertex.position = Vec3(aiMesh->mVertices[i].x, aiMesh->mVertices[i].y, aiMesh->mVertices[i].z);

        if (aiMesh->HasNormals()) {
            vertex.normal = Vec3(aiMesh->mNormals[i].x, aiMesh->mNormals[i].y, aiMesh->mNormals[i].z);
            vertex.normal = vertex.normal.normalized();
        } else {
            vertex.normal = Vec3(0.0f, 1.0f, 0.0f);
        }

        if (aiMesh->mTextureCoords[0]) {
            vertex.texCoord.x = aiMesh->mTextureCoords[0][i].x;
            vertex.texCoord.y = aiMesh->mTextureCoords[0][i].y;
        }

        mesh.vertices.push_back(vertex);
    }

    for (unsigned int i = 0; i < aiMesh->mNumFaces; ++i) {
        const aiFace& face = aiMesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; ++j) {
            mesh.indices.push_back(face.mIndices[j]);
        }
    }

    mesh.materialIndex = aiMesh->mMaterialIndex;

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);

    glBindVertexArray(mesh.vao);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex3D), mesh.vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int), mesh.indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, position));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, normal));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, texCoord));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void ProcessMaterials(const aiScene* scene, Model& model, const char* directory) {
    model.materials.clear();

    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        const aiMaterial* mat = scene->mMaterials[i];
        Material material{};

        aiString matName;
        if (mat->Get(AI_MATKEY_NAME, matName) == AI_SUCCESS) {
            material.name = matName.C_Str();
        }

        aiColor3D diffuse(1.0f, 1.0f, 1.0f);
        mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
        material.diffuse = Vec3(diffuse.r, diffuse.g, diffuse.b);

        float shininess = 32.0f;
        mat->Get(AI_MATKEY_SHININESS, shininess);
        material.shininess = shininess;

        if (mat->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
            aiString texPath;
            mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath);
            std::string fullPath = std::string(directory) + "/" + std::string(texPath.C_Str());
            Texture2D tex = LoadTexture(fullPath.c_str());
            if (tex.valid) {
                material.diffuseMap = tex.id;
            }
        }

        model.materials.push_back(material);
    }
}

void ProcessNode(const aiNode* node, const aiScene* scene, Model& model) {
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh* aiMesh = scene->mMeshes[node->mMeshes[i]];
        Mesh mesh{};
        ProcessMesh(aiMesh, scene, mesh);
        model.meshes.push_back(mesh);
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        ProcessNode(node->mChildren[i], scene, model);
    }
}

}  // namespace

Model LoadModel(const char* filePath) {
    Model model{};
    model.id = g3DState.nextModelId++;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filePath,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace |
        aiProcess_GenNormals);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        WriteLog(LogLevel::Error, "ASSIMP", importer.GetErrorString());
        return model;
    }

    std::string fullPath = filePath;
    size_t lastSlash = fullPath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        model.directory = fullPath.substr(0, lastSlash);
    }

    ProcessMaterials(scene, model, model.directory.c_str());

    ProcessNode(scene->mRootNode, scene, model);

    WriteLog(LogLevel::Info, "ASSIMP", TextFormat("Loaded model '%s' with %zu meshes", filePath, model.meshes.size()));
    return model;
}

void UnloadModel(Model& model) {
    for (auto& mesh : model.meshes) {
        if (mesh.vao != 0) glDeleteVertexArrays(1, &mesh.vao);
        if (mesh.vbo != 0) glDeleteBuffers(1, &mesh.vbo);
        if (mesh.ebo != 0) glDeleteBuffers(1, &mesh.ebo);
    }

    for (auto& material : model.materials) {
        if (material.diffuseMap != 0) {
            glDeleteTextures(1, &material.diffuseMap);
        }
    }

    model.meshes.clear();
    model.materials.clear();
    model.id = 0;
}

void Begin3D() {
    if (!g3DState.initialized) {
        g3DState.shader3D = Compile3DShader();
        g3DState.modelLoc = glGetUniformLocation(g3DState.shader3D, "uModel");
        g3DState.viewLoc = glGetUniformLocation(g3DState.shader3D, "uView");
        g3DState.projLoc = glGetUniformLocation(g3DState.shader3D, "uProjection");
        g3DState.samplerLoc = glGetUniformLocation(g3DState.shader3D, "uTexture");
        g3DState.lightPosLoc = glGetUniformLocation(g3DState.shader3D, "uLightPos");
        g3DState.initialized = true;

        std::vector<std::uint8_t> whitePixels(4, 255);
        glGenTextures(1, &g3DState.whiteTexture);
        glBindTexture(GL_TEXTURE_2D, g3DState.whiteTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(g3DState.shader3D);

    if (g3DState.samplerLoc >= 0) {
        glUniform1i(g3DState.samplerLoc, 0);
    }
    
    if (g3DState.lightPosLoc >= 0) {
        glUniform3f(g3DState.lightPosLoc, g3DState.lightPosition.x, g3DState.lightPosition.y, g3DState.lightPosition.z);
    }
}

void End3D() {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(0);
}

void Set3DView(const Mat4& view, const Mat4& projection) {
    g3DState.viewMatrix = view;
    g3DState.projectionMatrix = projection;

    if (g3DState.initialized) {
        glUniformMatrix4fv(g3DState.viewLoc, 1, GL_FALSE, g3DState.viewMatrix.m);
        glUniformMatrix4fv(g3DState.projLoc, 1, GL_FALSE, g3DState.projectionMatrix.m);
    }
}

void DrawModel(const Model& model, const Vec3& position, float scale,
               float rotationX, float rotationY, float rotationZ) {
    Mat4 transform = Mat4::translation(position.x, position.y, position.z);
    transform = transform * Mat4::rotationY(rotationY);
    transform = transform * Mat4::rotationX(rotationX);
    transform = transform * Mat4::rotationZ(rotationZ);
    transform = transform * Mat4::scale(scale, scale, scale);

    DrawModelEx(model, transform);
}

void DrawModelEx(const Model& model, const Mat4& transform) {
    if (g3DState.modelLoc >= 0) {
        glUniformMatrix4fv(g3DState.modelLoc, 1, GL_FALSE, transform.m);
    }

    for (const auto& mesh : model.meshes) {
        glActiveTexture(GL_TEXTURE0);
        
        GLuint textureId = g3DState.whiteTexture;
        if (mesh.materialIndex < model.materials.size()) {
            const Material& mat = model.materials[mesh.materialIndex];
            if (mat.diffuseMap != 0) {
                textureId = mat.diffuseMap;
            }
        }
        glBindTexture(GL_TEXTURE_2D, textureId);

        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }
}

}  // namespace qc
