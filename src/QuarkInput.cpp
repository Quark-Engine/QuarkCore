#include "QuarkCore/QuarkCore.hpp"
#include "QuarkInternal.hpp"
#include "Renderer/QuarkIRenderer.hpp"
static int ToQuarkGamepadButton(SDL_GamepadButton button) {
    switch (button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return GAMEPAD_BUTTON_LEFT_FACE_UP;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return GAMEPAD_BUTTON_LEFT_FACE_RIGHT;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return GAMEPAD_BUTTON_LEFT_FACE_DOWN;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return GAMEPAD_BUTTON_LEFT_FACE_LEFT;
        case SDL_GAMEPAD_BUTTON_NORTH: return GAMEPAD_BUTTON_RIGHT_FACE_UP;
        case SDL_GAMEPAD_BUTTON_EAST: return GAMEPAD_BUTTON_RIGHT_FACE_RIGHT;
        case SDL_GAMEPAD_BUTTON_SOUTH: return GAMEPAD_BUTTON_RIGHT_FACE_DOWN;
        case SDL_GAMEPAD_BUTTON_WEST: return GAMEPAD_BUTTON_RIGHT_FACE_LEFT;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return GAMEPAD_BUTTON_LEFT_TRIGGER_1;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return GAMEPAD_BUTTON_RIGHT_TRIGGER_1;
        case SDL_GAMEPAD_BUTTON_BACK: return GAMEPAD_BUTTON_MIDDLE_LEFT;
        case SDL_GAMEPAD_BUTTON_GUIDE: return GAMEPAD_BUTTON_MIDDLE;
        case SDL_GAMEPAD_BUTTON_START: return GAMEPAD_BUTTON_MIDDLE_RIGHT;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: return GAMEPAD_BUTTON_LEFT_THUMB;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return GAMEPAD_BUTTON_RIGHT_THUMB;
        default: return GAMEPAD_BUTTON_UNKNOWN;
    }
}

extern qci::IRenderer* gRendererPtr;
extern int gLastKeyPressed;
extern int gLastCharPressed;
extern KeyboardKey gExitKey;

namespace {

NativeEventCallback gNativeEventCallback = nullptr;

} // namespace

void PumpInput() {
    gWin.previousKeys = gWin.currentKeys;
    gWin.previousMouseButtons = gWin.mouseButtons;
    gWin.mouseWheel = { 0.0f, 0.0f };
    gWin.windowResized = false;
    gLastKeyPressed = 0;
    gLastCharPressed = 0;
    for (auto& buttons : gWin.gamepadPressed) buttons.fill(false);
    for (auto& buttons : gWin.gamepadReleased) buttons.fill(false);
    gWin.lastGamepadButtonPressed = -1;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (gNativeEventCallback != nullptr) {
            gNativeEventCallback(&event);
        }

        if (event.type == SDL_EVENT_DROP_FILE || event.type == SDL_EVENT_DROP_TEXT) {
            if (event.drop.data != nullptr) {
                constexpr size_t DropPathBufferSize = 4096;
                char dropPath[DropPathBufferSize]{};
                CopyText(dropPath, DropPathBufferSize, event.drop.data);
                gWin.droppedFiles.emplace_back(dropPath);
            }
        }

        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            gWin.shouldClose = true;
        }
        if (event.type == SDL_EVENT_WINDOW_RESIZED ||
            event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            gWin.windowResized = true;
        }
        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && gRendererPtr != nullptr) {
            gRendererPtr->RefreshViewport();
        }
        if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            gWin.mouseWheel.x += event.wheel.x;
            gWin.mouseWheel.y += event.wheel.y;
        }
        if (event.type == SDL_EVENT_KEY_DOWN) {
            gLastKeyPressed = event.key.key;
            if (gExitKey != KEY_NULL && ToSDLScancode(gExitKey) == event.key.scancode) {
                gWin.shouldClose = true;
            }
        }
        if (event.type == SDL_EVENT_TEXT_INPUT && event.text.text[0] != '\0') {
            gLastCharPressed = static_cast<unsigned char>(event.text.text[0]);
        }

        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
            event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
            int gamepadCount = 0;
            SDL_JoystickID* gamepadIds = SDL_GetGamepads(&gamepadCount);
            int gamepadIndex = -1;
            if (gamepadIds != nullptr) {
                for (int i = 0; i < gamepadCount; ++i) {
                    if (gamepadIds[i] == event.gbutton.which) {
                        gamepadIndex = i;
                        break;
                    }
                }
                SDL_free(gamepadIds);
            }

            const int button = ToQuarkGamepadButton(
                static_cast<SDL_GamepadButton>(event.gbutton.button));
            if (gamepadIndex >= 0 &&
                gamepadIndex < static_cast<int>(gWin.gamepadPressed.size()) &&
                button > GAMEPAD_BUTTON_UNKNOWN && button < GAMEPAD_BUTTON_COUNT) {
                if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                    gWin.gamepadPressed[static_cast<std::size_t>(gamepadIndex)]
                        [static_cast<std::size_t>(button)] = true;
                    gWin.lastGamepadButtonPressed = button;
                } else {
                    gWin.gamepadReleased[static_cast<std::size_t>(gamepadIndex)]
                        [static_cast<std::size_t>(button)] = true;
                }
            }
        }
    }

    UpdateInputState();
    gWin.inputPolled = true;
}

void SetNativeEventCallback(NativeEventCallback callback) {
    gNativeEventCallback = callback;
}
