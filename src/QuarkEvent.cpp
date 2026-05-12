#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/Quark3D.hpp"

namespace qc {
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

Event TranslateEvent(const SDL_Event& sdlEvent) {
    Event event{};
    event.nativeEvent = sdlEvent;
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
    gRenderer.mouseWheel = { 0.0f, 0.0f };
    gRenderer.events.clear();
    gRenderer.nextEventIndex = 0;
    gRenderer.shouldClose = false;
    gLastKeyPressed = 0;
    gLastCharPressed = 0;

    SDL_Event sdlEvent;
    while (SDL_PollEvent(&sdlEvent)) {
        gRenderer.nativeEvent = sdlEvent;
        if (sdlEvent.type == SDL_EVENT_QUIT || sdlEvent.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            gRenderer.shouldClose = true;
        }
        if (sdlEvent.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            RefreshViewport();
        }
        if (sdlEvent.type == SDL_EVENT_MOUSE_WHEEL) {
            gRenderer.mouseWheel.x += sdlEvent.wheel.x;
            gRenderer.mouseWheel.y += sdlEvent.wheel.y;
        }
        if (sdlEvent.type == SDL_EVENT_KEY_DOWN) {
            gLastKeyPressed = sdlEvent.key.key;
        }
        if (sdlEvent.type == SDL_EVENT_TEXT_INPUT) {
            if (sdlEvent.text.text[0] != '\0') {
                gLastCharPressed = static_cast<unsigned char>(sdlEvent.text.text[0]);
            }
        }

        gRenderer.events.push_back(TranslateEvent(sdlEvent));
    }

    UpdateInputFromEvents();
    gRenderer.eventsReady = true;
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

    gRenderer.nativeEvent = sdlEvent;
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

    gRenderer.nativeEvent = sdlEvent;
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

SDL_Event GetNativeEvent() {
    EnsureInitialized();
    return gRenderer.nativeEvent;
}

}