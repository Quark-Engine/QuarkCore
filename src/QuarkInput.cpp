#include "QuarkCore/QuarkCore.hpp"
#include "QuarkInternal.hpp"
#include "Renderer/QuarkIRenderer.hpp"

static int ToQuarkGamepadButton(SDL_GamepadButton button)
{
    switch (button)
    {
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return GAMEPAD_BUTTON_LEFT_FACE_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return GAMEPAD_BUTTON_LEFT_FACE_RIGHT;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return GAMEPAD_BUTTON_LEFT_FACE_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return GAMEPAD_BUTTON_LEFT_FACE_LEFT;
    case SDL_GAMEPAD_BUTTON_NORTH:
        return GAMEPAD_BUTTON_RIGHT_FACE_UP;
    case SDL_GAMEPAD_BUTTON_EAST:
        return GAMEPAD_BUTTON_RIGHT_FACE_RIGHT;
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return GAMEPAD_BUTTON_RIGHT_FACE_DOWN;
    case SDL_GAMEPAD_BUTTON_WEST:
        return GAMEPAD_BUTTON_RIGHT_FACE_LEFT;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return GAMEPAD_BUTTON_LEFT_TRIGGER_1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return GAMEPAD_BUTTON_RIGHT_TRIGGER_1;
    case SDL_GAMEPAD_BUTTON_BACK:
        return GAMEPAD_BUTTON_MIDDLE_LEFT;
    case SDL_GAMEPAD_BUTTON_GUIDE:
        return GAMEPAD_BUTTON_MIDDLE;
    case SDL_GAMEPAD_BUTTON_START:
        return GAMEPAD_BUTTON_MIDDLE_RIGHT;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return GAMEPAD_BUTTON_LEFT_THUMB;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return GAMEPAD_BUTTON_RIGHT_THUMB;
    default:
        return GAMEPAD_BUTTON_UNKNOWN;
    }
}

extern qci::IRenderer* g_pRenderer;
extern int g_LastKeyPressed;
extern int g_LastCharPressed;
extern KeyboardKey g_ExitKey;

namespace
{
    NativeEventCallback s_pfnNativeEventCallback = nullptr;
} // anonymous

void PumpInput()
{
    g_Win.m_aPreviousKeys = g_Win.m_aCurrentKeys;
    g_Win.m_aPreviousMouseButtons = g_Win.m_aMouseButtons;
    g_Win.m_MouseWheel = {0.0f, 0.0f};
    g_Win.m_WindowResized = false;
    g_LastKeyPressed = 0;
    g_LastCharPressed = 0;
    for (auto& aButtons : g_Win.m_aGamepadPressed)
    {
        aButtons.fill(false);
    }
    for (auto& aButtons : g_Win.m_aGamepadReleased)
    {
        aButtons.fill(false);
    }
    g_Win.m_LastGamepadButtonPressed = -1;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (s_pfnNativeEventCallback != nullptr)
        {
            s_pfnNativeEventCallback(&event);
        }

        if (event.type == SDL_EVENT_DROP_FILE || event.type == SDL_EVENT_DROP_TEXT)
        {
            if (event.drop.data != nullptr)
            {
                constexpr size_t DROP_PATH_BUFFER_SIZE = 4096;
                char aDropPath[DROP_PATH_BUFFER_SIZE]{};
                CopyText(aDropPath, DROP_PATH_BUFFER_SIZE, event.drop.data);
                g_Win.m_vDroppedFiles.emplace_back(aDropPath);
            }
        }

        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        {
            g_Win.m_ShouldClose = true;
        }
        if (event.type == SDL_EVENT_WINDOW_RESIZED ||
            event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
        {
            g_Win.m_WindowResized = true;
        }
        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && g_pRenderer != nullptr)
        {
            g_pRenderer->RefreshViewport();
        }
        if (event.type == SDL_EVENT_MOUSE_WHEEL)
        {
            g_Win.m_MouseWheel.x += event.wheel.x;
            g_Win.m_MouseWheel.y += event.wheel.y;
        }
        if (event.type == SDL_EVENT_KEY_DOWN)
        {
            g_LastKeyPressed = event.key.key;
            if (g_ExitKey != KEY_NULL && ToSDLScancode(g_ExitKey) == event.key.scancode)
            {
                g_Win.m_ShouldClose = true;
            }
        }
        if (event.type == SDL_EVENT_TEXT_INPUT && event.text.text[0] != '\0')
        {
            g_LastCharPressed = static_cast<unsigned char>(event.text.text[0]);
        }

        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
            event.type == SDL_EVENT_GAMEPAD_BUTTON_UP)
        {
            int gamepadCount = 0;
            SDL_JoystickID* pGamepadIds = SDL_GetGamepads(&gamepadCount);
            int gamepadIndex = -1;
            if (pGamepadIds != nullptr)
            {
                for (int i = 0; i < gamepadCount; ++i)
                {
                    if (pGamepadIds[i] == event.gbutton.which)
                    {
                        gamepadIndex = i;
                        break;
                    }
                }
                SDL_free(pGamepadIds);
            }

            const int button = ToQuarkGamepadButton(
                static_cast<SDL_GamepadButton>(event.gbutton.button));
            if (gamepadIndex >= 0 &&
                gamepadIndex < static_cast<int>(g_Win.m_aGamepadPressed.size()) &&
                button > GAMEPAD_BUTTON_UNKNOWN && button < GAMEPAD_BUTTON_COUNT)
            {
                if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
                {
                    g_Win.m_aGamepadPressed[static_cast<std::size_t>(gamepadIndex)]
                                        [static_cast<std::size_t>(button)] = true;
                    g_Win.m_LastGamepadButtonPressed = button;
                }
                else
                {
                    g_Win.m_aGamepadReleased[static_cast<std::size_t>(gamepadIndex)]
                                         [static_cast<std::size_t>(button)] = true;
                }
            }
        }
    }

    UpdateInputState();
    g_Win.m_InputPolled = true;
}

void SetNativeEventCallback(NativeEventCallback pfnCallback)
{
    s_pfnNativeEventCallback = pfnCallback;
}
