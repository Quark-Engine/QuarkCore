#include "QuarkCore/qcImGui.h"
#include "imgui.h"
#include <GL/glew.h>
#include <cmath>
#include <limits>
#include <cstdint>
#include <cstring>

static ImGuiMouseCursor CurrentMouseCursor = ImGuiMouseCursor_COUNT;
static qc::MouseCursor MouseCursorMap[ImGuiMouseCursor_COUNT];
static ImGuiKey KeyMap[512];
static bool KeyMapInitialized = false;
static bool LastFrameFocused = false;
static bool LastControlPressed = false;
static bool LastShiftPressed = false;
static bool LastAltPressed = false;
static bool LastSuperPressed = false;
static ImGuiContext* GlobalContext = nullptr;

static GLuint g_QcImGuiProgram = 0;
static GLuint g_QcImGuiVAO = 0;
static GLuint g_QcImGuiVBO = 0;
static GLuint g_QcImGuiEBO = 0;
static GLint g_QcImGuiAttribPosition = -1;
static GLint g_QcImGuiAttribUV = -1;
static GLint g_QcImGuiAttribColor = -1;
static GLint g_QcImGuiUniformTexture = -1;
static GLint g_QcImGuiUniformProjection = -1;

static inline ImTextureID QcImGuiTextureId(unsigned int id) {
    return (ImTextureID)(intptr_t)id;
}

static inline unsigned int QcImGuiTextureIdToUint(ImTextureID id) {
    return static_cast<unsigned int>((intptr_t)id);
}

static const char* QcImGuiVertexShader = R"(
#version 330 core
layout (location = 0) in vec2 aPosition;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 aColor;

uniform mat4 uProjection;

out vec2 vUV;
out vec4 vColor;

void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
}
)";

static const char* QcImGuiFragmentShader = R"(
#version 330 core
in vec2 vUV;
in vec4 vColor;

uniform sampler2D uTexture;

out vec4 fragColor;

void main() {
    fragColor = vColor * texture(uTexture, vUV);
}
)";

static bool QcImGuiCompileShader(GLuint shader, const char* source) {
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        return false;
    }
    return true;
}

static void QcImGuiCreateDeviceObjects() {
    if (g_QcImGuiProgram != 0)
        return;

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    if (!QcImGuiCompileShader(vertexShader, QcImGuiVertexShader)) {
        glDeleteShader(vertexShader);
        return;
    }

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    if (!QcImGuiCompileShader(fragmentShader, QcImGuiFragmentShader)) {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return;
    }

    g_QcImGuiProgram = glCreateProgram();
    glAttachShader(g_QcImGuiProgram, vertexShader);
    glAttachShader(g_QcImGuiProgram, fragmentShader);
    glLinkProgram(g_QcImGuiProgram);

    GLint linked = 0;
    glGetProgramiv(g_QcImGuiProgram, GL_LINK_STATUS, &linked);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (!linked) {
        char log[512];
        glGetProgramInfoLog(g_QcImGuiProgram, 512, nullptr, log);
        glDeleteProgram(g_QcImGuiProgram);
        g_QcImGuiProgram = 0;
        return;
    }

    g_QcImGuiAttribPosition = 0;
    g_QcImGuiAttribUV = 1;
    g_QcImGuiAttribColor = 2;
    g_QcImGuiUniformTexture = glGetUniformLocation(g_QcImGuiProgram, "uTexture");
    g_QcImGuiUniformProjection = glGetUniformLocation(g_QcImGuiProgram, "uProjection");

    glGenVertexArrays(1, &g_QcImGuiVAO);
    glGenBuffers(1, &g_QcImGuiVBO);
    glGenBuffers(1, &g_QcImGuiEBO);

    glBindVertexArray(g_QcImGuiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_QcImGuiVBO);
    glEnableVertexAttribArray(g_QcImGuiAttribPosition);
    glVertexAttribPointer(g_QcImGuiAttribPosition, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, pos)));
    glEnableVertexAttribArray(g_QcImGuiAttribUV);
    glVertexAttribPointer(g_QcImGuiAttribUV, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, uv)));
    glEnableVertexAttribArray(g_QcImGuiAttribColor);
    glVertexAttribPointer(g_QcImGuiAttribColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, col)));
    glBindVertexArray(0);
}

static void QcImGuiDestroyDeviceObjects() {
    if (g_QcImGuiVAO) {
        glDeleteVertexArrays(1, &g_QcImGuiVAO);
        g_QcImGuiVAO = 0;
    }
    if (g_QcImGuiVBO) {
        glDeleteBuffers(1, &g_QcImGuiVBO);
        g_QcImGuiVBO = 0;
    }
    if (g_QcImGuiEBO) {
        glDeleteBuffers(1, &g_QcImGuiEBO);
        g_QcImGuiEBO = 0;
    }
    if (g_QcImGuiProgram) {
        glDeleteProgram(g_QcImGuiProgram);
        g_QcImGuiProgram = 0;
    }
}

static void SetupMouseCursors() {
    MouseCursorMap[ImGuiMouseCursor_Arrow] = qc::MouseCursor::Arrow;
    MouseCursorMap[ImGuiMouseCursor_TextInput] = qc::MouseCursor::Ibeam;
    MouseCursorMap[ImGuiMouseCursor_Hand] = qc::MouseCursor::PointingHand;
    MouseCursorMap[ImGuiMouseCursor_ResizeAll] = qc::MouseCursor::ResizeAll;
    MouseCursorMap[ImGuiMouseCursor_ResizeEW] = qc::MouseCursor::ResizeEW;
    MouseCursorMap[ImGuiMouseCursor_ResizeNESW] = qc::MouseCursor::ResizeNESW;
    MouseCursorMap[ImGuiMouseCursor_ResizeNS] = qc::MouseCursor::ResizeNS;
    MouseCursorMap[ImGuiMouseCursor_ResizeNWSE] = qc::MouseCursor::ResizeNWSE;
    MouseCursorMap[ImGuiMouseCursor_NotAllowed] = qc::MouseCursor::NotAllowed;
}

static void SetupKeymap() {
    if (KeyMapInitialized)
        return;

    KeyMapInitialized = true;
    std::memset(KeyMap, 0, sizeof(KeyMap));

    KeyMap[static_cast<int>(qc::KeyboardKey::A)] = ImGuiKey_A;
    KeyMap[static_cast<int>(qc::KeyboardKey::B)] = ImGuiKey_B;
    KeyMap[static_cast<int>(qc::KeyboardKey::C)] = ImGuiKey_C;
    KeyMap[static_cast<int>(qc::KeyboardKey::D)] = ImGuiKey_D;
    KeyMap[static_cast<int>(qc::KeyboardKey::E)] = ImGuiKey_E;
    KeyMap[static_cast<int>(qc::KeyboardKey::F)] = ImGuiKey_F;
    KeyMap[static_cast<int>(qc::KeyboardKey::G)] = ImGuiKey_G;
    KeyMap[static_cast<int>(qc::KeyboardKey::H)] = ImGuiKey_H;
    KeyMap[static_cast<int>(qc::KeyboardKey::I)] = ImGuiKey_I;
    KeyMap[static_cast<int>(qc::KeyboardKey::J)] = ImGuiKey_J;
    KeyMap[static_cast<int>(qc::KeyboardKey::K)] = ImGuiKey_K;
    KeyMap[static_cast<int>(qc::KeyboardKey::L)] = ImGuiKey_L;
    KeyMap[static_cast<int>(qc::KeyboardKey::M)] = ImGuiKey_M;
    KeyMap[static_cast<int>(qc::KeyboardKey::N)] = ImGuiKey_N;
    KeyMap[static_cast<int>(qc::KeyboardKey::O)] = ImGuiKey_O;
    KeyMap[static_cast<int>(qc::KeyboardKey::P)] = ImGuiKey_P;
    KeyMap[static_cast<int>(qc::KeyboardKey::Q)] = ImGuiKey_Q;
    KeyMap[static_cast<int>(qc::KeyboardKey::R)] = ImGuiKey_R;
    KeyMap[static_cast<int>(qc::KeyboardKey::S)] = ImGuiKey_S;
    KeyMap[static_cast<int>(qc::KeyboardKey::T)] = ImGuiKey_T;
    KeyMap[static_cast<int>(qc::KeyboardKey::U)] = ImGuiKey_U;
    KeyMap[static_cast<int>(qc::KeyboardKey::V)] = ImGuiKey_V;
    KeyMap[static_cast<int>(qc::KeyboardKey::W)] = ImGuiKey_W;
    KeyMap[static_cast<int>(qc::KeyboardKey::X)] = ImGuiKey_X;
    KeyMap[static_cast<int>(qc::KeyboardKey::Y)] = ImGuiKey_Y;
    KeyMap[static_cast<int>(qc::KeyboardKey::Z)] = ImGuiKey_Z;
    KeyMap[static_cast<int>(qc::KeyboardKey::Num0)] = ImGuiKey_0;
    KeyMap[static_cast<int>(qc::KeyboardKey::Num1)] = ImGuiKey_1;
    KeyMap[static_cast<int>(qc::KeyboardKey::Num2)] = ImGuiKey_2;
    KeyMap[static_cast<int>(qc::KeyboardKey::Num3)] = ImGuiKey_3;
    KeyMap[static_cast<int>(qc::KeyboardKey::Num4)] = ImGuiKey_4;
    KeyMap[static_cast<int>(qc::KeyboardKey::Num5)] = ImGuiKey_5;
    KeyMap[static_cast<int>(qc::KeyboardKey::Num6)] = ImGuiKey_6;
    KeyMap[static_cast<int>(qc::KeyboardKey::Num7)] = ImGuiKey_7;
    KeyMap[static_cast<int>(qc::KeyboardKey::Num8)] = ImGuiKey_8;
    KeyMap[static_cast<int>(qc::KeyboardKey::Num9)] = ImGuiKey_9;
    KeyMap[static_cast<int>(qc::KeyboardKey::Space)] = ImGuiKey_Space;
    KeyMap[static_cast<int>(qc::KeyboardKey::Escape)] = ImGuiKey_Escape;
    KeyMap[static_cast<int>(qc::KeyboardKey::Enter)] = ImGuiKey_Enter;
    KeyMap[static_cast<int>(qc::KeyboardKey::Tab)] = ImGuiKey_Tab;
    KeyMap[static_cast<int>(qc::KeyboardKey::Backspace)] = ImGuiKey_Backspace;
    KeyMap[static_cast<int>(qc::KeyboardKey::Insert)] = ImGuiKey_Insert;
    KeyMap[static_cast<int>(qc::KeyboardKey::Delete)] = ImGuiKey_Delete;
    KeyMap[static_cast<int>(qc::KeyboardKey::Right)] = ImGuiKey_RightArrow;
    KeyMap[static_cast<int>(qc::KeyboardKey::Left)] = ImGuiKey_LeftArrow;
    KeyMap[static_cast<int>(qc::KeyboardKey::Down)] = ImGuiKey_DownArrow;
    KeyMap[static_cast<int>(qc::KeyboardKey::Up)] = ImGuiKey_UpArrow;
    KeyMap[static_cast<int>(qc::KeyboardKey::PageUp)] = ImGuiKey_PageUp;
    KeyMap[static_cast<int>(qc::KeyboardKey::PageDown)] = ImGuiKey_PageDown;
    KeyMap[static_cast<int>(qc::KeyboardKey::Home)] = ImGuiKey_Home;
    KeyMap[static_cast<int>(qc::KeyboardKey::End)] = ImGuiKey_End;
    KeyMap[static_cast<int>(qc::KeyboardKey::CapsLock)] = ImGuiKey_CapsLock;
    KeyMap[static_cast<int>(qc::KeyboardKey::ScrollLock)] = ImGuiKey_ScrollLock;
    KeyMap[static_cast<int>(qc::KeyboardKey::NumLock)] = ImGuiKey_NumLock;
    KeyMap[static_cast<int>(qc::KeyboardKey::PrintScreen)] = ImGuiKey_PrintScreen;
    KeyMap[static_cast<int>(qc::KeyboardKey::Pause)] = ImGuiKey_Pause;
    KeyMap[static_cast<int>(qc::KeyboardKey::F1)] = ImGuiKey_F1;
    KeyMap[static_cast<int>(qc::KeyboardKey::F2)] = ImGuiKey_F2;
    KeyMap[static_cast<int>(qc::KeyboardKey::F3)] = ImGuiKey_F3;
    KeyMap[static_cast<int>(qc::KeyboardKey::F4)] = ImGuiKey_F4;
    KeyMap[static_cast<int>(qc::KeyboardKey::F5)] = ImGuiKey_F5;
    KeyMap[static_cast<int>(qc::KeyboardKey::F6)] = ImGuiKey_F6;
    KeyMap[static_cast<int>(qc::KeyboardKey::F7)] = ImGuiKey_F7;
    KeyMap[static_cast<int>(qc::KeyboardKey::F8)] = ImGuiKey_F8;
    KeyMap[static_cast<int>(qc::KeyboardKey::F9)] = ImGuiKey_F9;
    KeyMap[static_cast<int>(qc::KeyboardKey::F10)] = ImGuiKey_F10;
    KeyMap[static_cast<int>(qc::KeyboardKey::F11)] = ImGuiKey_F11;
    KeyMap[static_cast<int>(qc::KeyboardKey::F12)] = ImGuiKey_F12;
    KeyMap[static_cast<int>(qc::KeyboardKey::LeftShift)] = ImGuiKey_LeftShift;
    KeyMap[static_cast<int>(qc::KeyboardKey::LeftControl)] = ImGuiKey_LeftCtrl;
    KeyMap[static_cast<int>(qc::KeyboardKey::LeftAlt)] = ImGuiKey_LeftAlt;
    KeyMap[static_cast<int>(qc::KeyboardKey::LeftSuper)] = ImGuiKey_LeftSuper;
    KeyMap[static_cast<int>(qc::KeyboardKey::RightShift)] = ImGuiKey_RightShift;
    KeyMap[static_cast<int>(qc::KeyboardKey::RightControl)] = ImGuiKey_RightCtrl;
    KeyMap[static_cast<int>(qc::KeyboardKey::RightAlt)] = ImGuiKey_RightAlt;
    KeyMap[static_cast<int>(qc::KeyboardKey::RightSuper)] = ImGuiKey_RightSuper;
    KeyMap[static_cast<int>(qc::KeyboardKey::Menu)] = ImGuiKey_Menu;
    KeyMap[static_cast<int>(qc::KeyboardKey::LeftBracket)] = ImGuiKey_LeftBracket;
    KeyMap[static_cast<int>(qc::KeyboardKey::Backslash)] = ImGuiKey_Backslash;
    KeyMap[static_cast<int>(qc::KeyboardKey::RightBracket)] = ImGuiKey_RightBracket;
    KeyMap[static_cast<int>(qc::KeyboardKey::Grave)] = ImGuiKey_GraveAccent;
    KeyMap[static_cast<int>(qc::KeyboardKey::Keypad0)] = ImGuiKey_Keypad0;
    KeyMap[static_cast<int>(qc::KeyboardKey::Keypad1)] = ImGuiKey_Keypad1;
    KeyMap[static_cast<int>(qc::KeyboardKey::Keypad2)] = ImGuiKey_Keypad2;
    KeyMap[static_cast<int>(qc::KeyboardKey::Keypad3)] = ImGuiKey_Keypad3;
    KeyMap[static_cast<int>(qc::KeyboardKey::Keypad4)] = ImGuiKey_Keypad4;
    KeyMap[static_cast<int>(qc::KeyboardKey::Keypad5)] = ImGuiKey_Keypad5;
    KeyMap[static_cast<int>(qc::KeyboardKey::Keypad6)] = ImGuiKey_Keypad6;
    KeyMap[static_cast<int>(qc::KeyboardKey::Keypad7)] = ImGuiKey_Keypad7;
    KeyMap[static_cast<int>(qc::KeyboardKey::Keypad8)] = ImGuiKey_Keypad8;
    KeyMap[static_cast<int>(qc::KeyboardKey::Keypad9)] = ImGuiKey_Keypad9;
    KeyMap[static_cast<int>(qc::KeyboardKey::KeypadPeriod)] = ImGuiKey_KeypadDecimal;
    KeyMap[static_cast<int>(qc::KeyboardKey::KeypadDivide)] = ImGuiKey_KeypadDivide;
    KeyMap[static_cast<int>(qc::KeyboardKey::KeypadMultiply)] = ImGuiKey_KeypadMultiply;
    KeyMap[static_cast<int>(qc::KeyboardKey::KeypadMinus)] = ImGuiKey_KeypadSubtract;
    KeyMap[static_cast<int>(qc::KeyboardKey::KeypadPlus)] = ImGuiKey_KeypadAdd;
    KeyMap[static_cast<int>(qc::KeyboardKey::KeypadEnter)] = ImGuiKey_KeypadEnter;
    KeyMap[static_cast<int>(qc::KeyboardKey::KeypadEquals)] = ImGuiKey_KeypadEqual;
}

static bool qcImGuiIsControlDown() {
    return qc::IsKeyDown(qc::KeyboardKey::RightControl) || qc::IsKeyDown(qc::KeyboardKey::LeftControl);
}

static bool qcImGuiIsShiftDown() {
    return qc::IsKeyDown(qc::KeyboardKey::RightShift) || qc::IsKeyDown(qc::KeyboardKey::LeftShift);
}

static bool qcImGuiIsAltDown() {
    return qc::IsKeyDown(qc::KeyboardKey::RightAlt) || qc::IsKeyDown(qc::KeyboardKey::LeftAlt);
}

static bool qcImGuiIsSuperDown() {
    return qc::IsKeyDown(qc::KeyboardKey::RightSuper) || qc::IsKeyDown(qc::KeyboardKey::LeftSuper);
}

static void ImGuiNewFrame(float deltaTime) {
    ImGuiIO& io = ImGui::GetIO();
    QcImGuiCreateDeviceObjects();
    if (!g_QcImGuiProgram)
        return;

    io.DisplaySize = ImVec2((float)qc::GetScreenWidth(), (float)qc::GetScreenHeight());
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    if (deltaTime <= 0.0f)
        deltaTime = 1.0f / 60.0f;
    io.DeltaTime = deltaTime;

    if (io.BackendFlags & ImGuiBackendFlags_HasMouseCursors) {
        if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) == 0) {
            ImGuiMouseCursor imguiCursor = ImGui::GetMouseCursor();
            if (imguiCursor != CurrentMouseCursor || io.MouseDrawCursor) {
                CurrentMouseCursor = imguiCursor;
                if (io.MouseDrawCursor || imguiCursor == ImGuiMouseCursor_None) {
                    qc::DisableCursor();
                } else {
                    qc::EnableCursor();
                    qc::SetMouseCursor(MouseCursorMap[imguiCursor]);
                }
            }
        }
    }
}

static void SetupGlobals() {
    LastFrameFocused = qc::IsWindowFocused();
    LastControlPressed = false;
    LastShiftPressed = false;
    LastAltPressed = false;
    LastSuperPressed = false;
}

static void HandleGamepadButtonEvent(ImGuiIO& io, int button, ImGuiKey key) {
    if (qc::IsGamepadButtonPressed(0, button))
        io.AddKeyEvent(key, true);
    else
        io.AddKeyEvent(key, false);
}

static void HandleGamepadStickEvent(ImGuiIO& io, int axis, ImGuiKey negKey, ImGuiKey posKey) {
    constexpr float deadZone = 0.20f;
    float axisValue = qc::GetGamepadAxisMovement(0, axis);
    io.AddKeyAnalogEvent(negKey, axisValue < -deadZone, axisValue < -deadZone ? -axisValue : 0.0f);
    io.AddKeyAnalogEvent(posKey, axisValue > deadZone, axisValue > deadZone ? axisValue : 0.0f);
}

static void EnableScissor(float x, float y, float width, float height) {
    glEnable(GL_SCISSOR_TEST);
    int fbHeight = qc::GetScreenHeight();
    int sx = static_cast<int>(x);
    int sy = static_cast<int>(fbHeight - (y + height));
    int sw = static_cast<int>(width);
    int sh = static_cast<int>(height);
    glScissor(sx, sy, sw, sh);
}

static void ImGuiRenderDrawData(ImDrawData* draw_data) {
    if (!draw_data || draw_data->TotalVtxCount == 0)
        return;

    QcImGuiCreateDeviceObjects();
    if (!g_QcImGuiProgram)
        return;

    GLint lastViewport[4]; glGetIntegerv(GL_VIEWPORT, lastViewport);
    GLint lastProgram = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &lastProgram);
    GLint lastTexture = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture);
    GLint lastArrayBuffer = 0; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &lastArrayBuffer);
    GLint lastElementArrayBuffer = 0; glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &lastElementArrayBuffer);
    GLint lastVertexArray = 0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &lastVertexArray);
    GLboolean lastBlend = glIsEnabled(GL_BLEND);
    GLboolean lastCullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean lastDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean lastScissorTest = glIsEnabled(GL_SCISSOR_TEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);

    glUseProgram(g_QcImGuiProgram);
    glUniform1i(g_QcImGuiUniformTexture, 0);

    const float L = draw_data->DisplayPos.x;
    const float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    const float T = draw_data->DisplayPos.y;
    const float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
    const float ortho_projection[4][4] = {
        { 2.0f / (R - L), 0.0f, 0.0f, 0.0f },
        { 0.0f, 2.0f / (T - B), 0.0f, 0.0f },
        { 0.0f, 0.0f, -1.0f, 0.0f },
        { (R + L) / (L - R), (T + B) / (B - T), 0.0f, 1.0f },
    };
    glUniformMatrix4fv(g_QcImGuiUniformProjection, 1, GL_FALSE, &ortho_projection[0][0]);

    glBindVertexArray(g_QcImGuiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_QcImGuiVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_QcImGuiEBO);

    GLenum indexType = sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;

    for (int n = 0; n < draw_data->CmdListsCount; ++n) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        glBufferData(GL_ARRAY_BUFFER, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), cmd_list->VtxBuffer.Data, GL_STREAM_DRAW);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), cmd_list->IdxBuffer.Data, GL_STREAM_DRAW);

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; ++cmd_i) {
            const ImDrawCmd& pcmd = cmd_list->CmdBuffer[cmd_i];
            if (pcmd.UserCallback) {
                pcmd.UserCallback(cmd_list, &pcmd);
            } else {
                const float clipX = pcmd.ClipRect.x - draw_data->DisplayPos.x;
                const float clipY = pcmd.ClipRect.y - draw_data->DisplayPos.y;
                const float clipW = pcmd.ClipRect.z - pcmd.ClipRect.x;
                const float clipH = pcmd.ClipRect.w - pcmd.ClipRect.y;
                if (clipW <= 0.0f || clipH <= 0.0f)
                    continue;

                EnableScissor(clipX, clipY, clipW, clipH);
                unsigned int textureId = QcImGuiTextureIdToUint(pcmd.GetTexID());
                glBindTexture(GL_TEXTURE_2D, textureId);
                glDrawElements(indexType, pcmd.ElemCount, indexType, reinterpret_cast<void*>(static_cast<intptr_t>(pcmd.IdxOffset * sizeof(ImDrawIdx))));
            }
        }
    }

    glBindVertexArray(lastVertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, lastArrayBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lastElementArrayBuffer);
    glUseProgram(lastProgram);
    glBindTexture(GL_TEXTURE_2D, lastTexture);
    glViewport(lastViewport[0], lastViewport[1], (GLsizei)lastViewport[2], (GLsizei)lastViewport[3]);
    if (!lastBlend) glDisable(GL_BLEND);
    if (lastDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (lastCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (lastScissorTest) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
}

bool ImGui_ImplQc_ProcessEvents() {
    ImGuiIO& io = ImGui::GetIO();

    bool focused = qc::IsWindowFocused();
    if (focused != LastFrameFocused)
        io.AddFocusEvent(focused);
    LastFrameFocused = focused;

    bool ctrlDown = qcImGuiIsControlDown();
    if (ctrlDown != LastControlPressed)
        io.AddKeyEvent(ImGuiMod_Ctrl, ctrlDown);
    LastControlPressed = ctrlDown;

    bool shiftDown = qcImGuiIsShiftDown();
    if (shiftDown != LastShiftPressed)
        io.AddKeyEvent(ImGuiMod_Shift, shiftDown);
    LastShiftPressed = shiftDown;

    bool altDown = qcImGuiIsAltDown();
    if (altDown != LastAltPressed)
        io.AddKeyEvent(ImGuiMod_Alt, altDown);
    LastAltPressed = altDown;

    bool superDown = qcImGuiIsSuperDown();
    if (superDown != LastSuperPressed)
        io.AddKeyEvent(ImGuiMod_Super, superDown);
    LastSuperPressed = superDown;

    for (int keyItr = 0; keyItr < static_cast<int>(qc::KeyboardKey::RightSuper) + 1; ++keyItr) {
        ImGuiKey key = KeyMap[keyItr];
        if (key == ImGuiKey_None)
            continue;

        if (qc::IsKeyReleased(static_cast<qc::KeyboardKey>(keyItr)))
            io.AddKeyEvent(key, false);
        else if (qc::IsKeyPressed(static_cast<qc::KeyboardKey>(keyItr)))
            io.AddKeyEvent(key, true);
    }

    if (io.WantCaptureKeyboard) {
        int ch = qc::GetCharPressed();
        while (ch != 0) {
            io.AddInputCharacter(ch);
            ch = qc::GetCharPressed();
        }
    }

    bool processMouse = focused;
    if (processMouse) {
        if (!io.WantSetMousePos) {
            qc::Vec2 pos = qc::GetMousePosition();
            io.AddMousePosEvent(pos.x, pos.y);
        }

        auto setMouseEvent = [&io](qc::MouseButton button, ImGuiMouseButton imguiButton) {
            if (qc::IsMouseButtonPressed(button))
                io.AddMouseButtonEvent(imguiButton, true);
            else if (qc::IsMouseButtonReleased(button))
                io.AddMouseButtonEvent(imguiButton, false);
        };

        setMouseEvent(qc::MouseButton::Left, ImGuiMouseButton_Left);
        setMouseEvent(qc::MouseButton::Right, ImGuiMouseButton_Right);
        setMouseEvent(qc::MouseButton::Middle, ImGuiMouseButton_Middle);
    } else {
        io.AddMousePosEvent(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());
    }

    if ((io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) && qc::IsGamepadAvailable(0)) {
        HandleGamepadButtonEvent(io, 0, ImGuiKey_GamepadDpadUp);
        HandleGamepadButtonEvent(io, 1, ImGuiKey_GamepadDpadRight);
        HandleGamepadButtonEvent(io, 2, ImGuiKey_GamepadDpadDown);
        HandleGamepadButtonEvent(io, 3, ImGuiKey_GamepadDpadLeft);
        HandleGamepadButtonEvent(io, 4, ImGuiKey_GamepadFaceUp);
        HandleGamepadButtonEvent(io, 5, ImGuiKey_GamepadFaceLeft);
        HandleGamepadButtonEvent(io, 6, ImGuiKey_GamepadFaceDown);
        HandleGamepadButtonEvent(io, 7, ImGuiKey_GamepadFaceRight);
        HandleGamepadButtonEvent(io, 8, ImGuiKey_GamepadL1);
        HandleGamepadButtonEvent(io, 9, ImGuiKey_GamepadL2);
        HandleGamepadButtonEvent(io, 10, ImGuiKey_GamepadR1);
        HandleGamepadButtonEvent(io, 11, ImGuiKey_GamepadR2);
        HandleGamepadButtonEvent(io, 12, ImGuiKey_GamepadL3);
        HandleGamepadButtonEvent(io, 13, ImGuiKey_GamepadR3);
        HandleGamepadButtonEvent(io, 14, ImGuiKey_GamepadStart);
        HandleGamepadButtonEvent(io, 15, ImGuiKey_GamepadBack);

        HandleGamepadStickEvent(io, 0, ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight);
        HandleGamepadStickEvent(io, 1, ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown);
        HandleGamepadStickEvent(io, 2, ImGuiKey_GamepadRStickLeft, ImGuiKey_GamepadRStickRight);
        HandleGamepadStickEvent(io, 3, ImGuiKey_GamepadRStickUp, ImGuiKey_GamepadRStickDown);
    }

    return true;
}

bool ImGui_ImplQc_Init() {
    SetupGlobals();
    SetupKeymap();
    SetupMouseCursors();

    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "imgui_impl_quarkcore";
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad | ImGuiBackendFlags_HasSetMousePos;
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

    QcImGuiCreateDeviceObjects();
    return g_QcImGuiProgram != 0;
}

void ImGui_ImplQc_Shutdown() {
    QcImGuiDestroyDeviceObjects();
}

void ImGui_ImplQc_NewFrame() {
    ImGuiNewFrame(qc::GetFrameTime());
}

void ImGui_ImplQc_UpdateTexture(ImTextureData* tex) {
    if (!tex)
        return;

    switch (tex->Status) {
        case ImTextureStatus_WantCreate: {
            GLuint* textureId = new GLuint(0);
            glGenTextures(1, textureId);
            glBindTexture(GL_TEXTURE_2D, *textureId);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            GLenum format = tex->Format == ImTextureFormat_Alpha8 ? GL_RED : GL_RGBA;
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, format, tex->Width, tex->Height, 0, format, GL_UNSIGNED_BYTE, tex->GetPixels());
            glBindTexture(GL_TEXTURE_2D, 0);

            tex->BackendUserData = textureId;
            tex->SetTexID(QcImGuiTextureId(*textureId));
            tex->Status = ImTextureStatus_OK;
        } break;

        case ImTextureStatus_WantUpdates: {
            GLuint* textureId = static_cast<GLuint*>(tex->BackendUserData);
            if (!textureId)
                break;
            glBindTexture(GL_TEXTURE_2D, *textureId);
            GLenum format = tex->Format == ImTextureFormat_Alpha8 ? GL_RED : GL_RGBA;
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex->Width, tex->Height, format, GL_UNSIGNED_BYTE, tex->GetPixels());
            glBindTexture(GL_TEXTURE_2D, 0);
            tex->Status = ImTextureStatus_OK;
        } break;

        case ImTextureStatus_WantDestroy: {
            GLuint* textureId = static_cast<GLuint*>(tex->BackendUserData);
            if (textureId) {
                glDeleteTextures(1, textureId);
                delete textureId;
                tex->BackendUserData = nullptr;
            }
            tex->SetTexID(ImTextureID());
            tex->Status = ImTextureStatus_Destroyed;
        } break;

        default:
            break;
    }
}

void ImGui_ImplQc_RenderDrawData(ImDrawData* draw_data) {
    ImGuiRenderDrawData(draw_data);
}

void qcImGuiBeginInitImGui() {
    SetupGlobals();
    if (GlobalContext == nullptr)
        GlobalContext = ImGui::CreateContext(nullptr);
    SetupKeymap();

    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig defaultConfig;
    defaultConfig.SizePixels = 13;
    defaultConfig.PixelSnapH = true;
    io.Fonts->AddFontDefault(&defaultConfig);
}

void qcImGuiEndInitImGui() {
    ImGui::SetCurrentContext(GlobalContext);
    SetupMouseCursors();
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "imgui_impl_quarkcore";
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad | ImGuiBackendFlags_HasSetMousePos | ImGuiBackendFlags_HasMouseCursors;
    QcImGuiCreateDeviceObjects();
}

void qcImGuiSetup(bool dark) {
    qcImGuiBeginInitImGui();
    if (dark)
        ImGui::StyleColorsDark();
    else
        ImGui::StyleColorsLight();
    qcImGuiEndInitImGui();
}

void qcImGuiBegin() {
    ImGui::SetCurrentContext(GlobalContext);
    qcImGuiBeginDelta(qc::GetFrameTime());
}

void qcImGuiBeginDelta(float deltaTime) {
    ImGui::SetCurrentContext(GlobalContext);
    ImGuiNewFrame(deltaTime);
    ImGui_ImplQc_ProcessEvents();
    ImGui::NewFrame();
}

void qcImGuiEnd() {
    ImGui::SetCurrentContext(GlobalContext);
    ImGui::Render();
    ImGui_ImplQc_RenderDrawData(ImGui::GetDrawData());
}

void qcImGuiShutdown() {
    if (GlobalContext == nullptr)
        return;
    ImGui::SetCurrentContext(GlobalContext);
    ImGui_ImplQc_Shutdown();
    ImGui::DestroyContext(GlobalContext);
    GlobalContext = nullptr;
}

void qcImGuiImage(const qc::Texture2D* image) {
    if (!image)
        return;
    if (GlobalContext)
        ImGui::SetCurrentContext(GlobalContext);
    ImGui::Image(QcImGuiTextureId(image->id), ImVec2((float)image->width, (float)image->height));
}

bool qcImGuiImageButton(const char* name, const qc::Texture2D* image) {
    if (!image)
        return false;
    if (GlobalContext)
        ImGui::SetCurrentContext(GlobalContext);
    return ImGui::ImageButton(name, QcImGuiTextureId(image->id), ImVec2((float)image->width, (float)image->height));
}

bool qcImGuiImageButtonSize(const char* name, const qc::Texture2D* image, qc::Vec2 size) {
    if (!image)
        return false;
    if (GlobalContext)
        ImGui::SetCurrentContext(GlobalContext);
    return ImGui::ImageButton(name, QcImGuiTextureId(image->id), ImVec2(size.x, size.y));
}

void qcImGuiImageSize(const qc::Texture2D* image, int width, int height) {
    if (!image)
        return;
    if (GlobalContext)
        ImGui::SetCurrentContext(GlobalContext);
    ImGui::Image(QcImGuiTextureId(image->id), ImVec2((float)width, (float)height));
}

void qcImGuiImageSizeV(const qc::Texture2D* image, qc::Vec2 size) {
    if (!image)
        return;
    if (GlobalContext)
        ImGui::SetCurrentContext(GlobalContext);
    ImGui::Image(QcImGuiTextureId(image->id), ImVec2(size.x, size.y));
}

void qcImGuiImageRect(const qc::Texture2D* image, int destWidth, int destHeight, qc::Rectangle sourceRect) {
    if (!image)
        return;
    if (GlobalContext)
        ImGui::SetCurrentContext(GlobalContext);

    ImVec2 uv0;
    ImVec2 uv1;
    if (sourceRect.width < 0) {
        uv0.x = -sourceRect.x / image->width;
        uv1.x = uv0.x - std::fabs(sourceRect.width) / image->width;
    } else {
        uv0.x = sourceRect.x / image->width;
        uv1.x = uv0.x + sourceRect.width / image->width;
    }
    if (sourceRect.height < 0) {
        uv0.y = -sourceRect.y / image->height;
        uv1.y = uv0.y - std::fabs(sourceRect.height) / image->height;
    } else {
        uv0.y = sourceRect.y / image->height;
        uv1.y = uv0.y + sourceRect.height / image->height;
    }
    ImGui::Image(QcImGuiTextureId(image->id), ImVec2((float)destWidth, (float)destHeight), uv0, uv1);
}

void qcImGuiImageRenderTexture(const qc::RenderTexture2D* target) {
    if (!target)
        return;
    if (GlobalContext)
        ImGui::SetCurrentContext(GlobalContext);
    qcImGuiImageRect(&target->texture, target->texture.width, target->texture.height, qc::Rectangle{0.0f, 0.0f, (float)target->texture.width, -(float)target->texture.height});
}

void qcImGuiImageRenderTextureFit(const qc::RenderTexture2D* target, bool center) {
    if (!target)
        return;
    if (GlobalContext)
        ImGui::SetCurrentContext(GlobalContext);

    ImVec2 area = ImGui::GetContentRegionAvail();
    float scale = area.x / target->texture.width;
    float y = target->texture.height * scale;
    if (y > area.y)
        scale = area.y / target->texture.height;

    int sizeX = static_cast<int>(target->texture.width * scale);
    int sizeY = static_cast<int>(target->texture.height * scale);

    if (center) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (area.x - sizeX) * 0.5f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (area.y - sizeY) * 0.5f);
    }

    qcImGuiImageRect(&target->texture, sizeX, sizeY, qc::Rectangle{0.0f, 0.0f, (float)target->texture.width, -(float)target->texture.height});
}
