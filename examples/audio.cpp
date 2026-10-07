#include "QuarkCore/QuarkCore.hpp"

#include <cmath>
#include <cstdint>
#include <algorithm>

int main() {
    InitWindow(1280, 720, "QuarkCore Audio Demo", RendererType::OpenGL);
    SetTargetFPS(60);

    InitAudioDevice();
    SetMasterVolume(0.8f);

    Sound beep = LoadSound("resources/test.wav");
    if (beep.stream.buffer == nullptr) {
        TraceLog(LogLevel::Warn, "AUDIO", "resources/test.wav not found; generating a simple tone instead");

        Wave wave{};
        wave.frameCount = 22050;
        wave.sampleRate = 44100;
        wave.sampleSize = 16;
        wave.channels = 1;
        wave.data = MemAlloc(static_cast<size_t>(wave.frameCount) * 2u);

        auto* samples = static_cast<std::int16_t*>(wave.data);
        for (unsigned int i = 0; i < wave.frameCount; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(wave.sampleRate);
            const float tone = std::sin(2.0f * 3.14159265358979323846f * 440.0f * t) * 0.5f;
            samples[i] = static_cast<std::int16_t>(tone * 32767.0f);
        }

        beep = LoadSoundFromWave(wave);
        UnloadWave(wave);
    }

    Font font = GetDefaultFont();
    bool playing = false;
    float volume = 0.8f;

    const char* infoLine1 = "OpenAL is active.";
    const char* infoLine2 = "Press space to hear the generated tone.";
    const float infoFontSize1 = 24.0f;
    const float infoFontSize2 = 22.0f;
    const float infoSpacing = 2.0f;
    const float infoPadding = 20.0f;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_SPACE)) {
            if (playing) {
                StopSound(beep);
                playing = false;
            } else {
                PlaySound(beep);
                playing = true;
            }
        }

        if (IsKeyPressed(KEY_RIGHT)) {
            volume = std::clamp(volume + 0.1f, 0.0f, 1.0f);
            SetSoundVolume(beep, volume);
        }
        if (IsKeyPressed(KEY_LEFT)) {
            volume = std::clamp(volume - 0.1f, 0.0f, 1.0f);
            SetSoundVolume(beep, volume);
        }

        Vec2 infoSize1 = MeasureTextEx(font, infoLine1, infoFontSize1, infoSpacing);
        Vec2 infoSize2 = MeasureTextEx(font, infoLine2, infoFontSize2, infoSpacing);
        float infoBoxWidth = std::max(infoSize1.x, infoSize2.x) + infoPadding * 2.0f;

        BeginDrawing();
        ClearBackground(Color{18, 22, 35, 255});

        DrawText("QuarkCore Audio Demo", 40, 32, 32, WHITE);
        DrawText("Space = play/stop", 40, 88, 24, LIGHTGRAY);
        DrawText("Left/Right = volume", 40, 122, 24, LIGHTGRAY);

        DrawRectangle(40, 180, 420, 28, Color{60, 60, 80, 255});
        DrawRectangle(40, 180, static_cast<int>(volume * 420.0f), 28, GREEN);
        DrawText(TextFormat("Volume: %.2f", volume), 40, 220, 22, WHITE);

        DrawRectangle(40, 320, static_cast<int>(infoBoxWidth), 180, Color{35, 42, 60, 220});
        DrawTextEx(font, infoLine1, Vec2{40.0f + infoPadding, 350.0f}, infoFontSize1, infoSpacing, WHITE);
        DrawTextEx(font, infoLine2, Vec2{40.0f + infoPadding, 390.0f}, infoFontSize2, infoSpacing, LIGHTGRAY);

        EndDrawing();
    }

    StopSound(beep);
    UnloadSound(beep);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}