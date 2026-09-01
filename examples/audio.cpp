#include "QuarkCore/QuarkCore.hpp"

#include <cmath>
#include <cstdint>
#include <algorithm>

int main() {
    qc::InitWindow(1280, 720, "QuarkCore Audio Demo", qc::RendererType::OpenGL);
    qc::SetTargetFPS(60);

    qc::InitAudioDevice();
    qc::SetMasterVolume(0.8f);

    qc::Sound beep = qc::LoadSound("resources/test.wav");
    if (beep.stream.buffer == nullptr) {
        qc::TraceLog(qc::LogLevel::Warn, "AUDIO", "resources/test.wav not found; generating a simple tone instead");

        qc::Wave wave{};
        wave.frameCount = 22050;
        wave.sampleRate = 44100;
        wave.sampleSize = 16;
        wave.channels = 1;
        wave.data = qc::MemAlloc(static_cast<size_t>(wave.frameCount) * 2u);

        auto* samples = static_cast<std::int16_t*>(wave.data);
        for (unsigned int i = 0; i < wave.frameCount; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(wave.sampleRate);
            const float tone = std::sin(2.0f * 3.14159265358979323846f * 440.0f * t) * 0.5f;
            samples[i] = static_cast<std::int16_t>(tone * 32767.0f);
        }

        beep = qc::LoadSoundFromWave(wave);
        qc::UnloadWave(wave);
    }

    qc::Font font = qc::GetDefaultFont();
    bool playing = false;
    float volume = 0.8f;

    const char* infoLine1 = "OpenAL is active.";
    const char* infoLine2 = "Press space to hear the generated tone.";
    const float infoFontSize1 = 24.0f;
    const float infoFontSize2 = 22.0f;
    const float infoSpacing = 2.0f;
    const float infoPadding = 20.0f;

    while (!qc::WindowShouldClose()) {
        if (qc::IsKeyPressed(qc::KeyboardKey::Space)) {
            if (playing) {
                qc::StopSound(beep);
                playing = false;
            } else {
                qc::PlaySound(beep);
                playing = true;
            }
        }

        if (qc::IsKeyPressed(qc::KeyboardKey::Right)) {
            volume = std::clamp(volume + 0.1f, 0.0f, 1.0f);
            qc::SetSoundVolume(beep, volume);
        }
        if (qc::IsKeyPressed(qc::KeyboardKey::Left)) {
            volume = std::clamp(volume - 0.1f, 0.0f, 1.0f);
            qc::SetSoundVolume(beep, volume);
        }

        qc::Vec2 infoSize1 = qc::MeasureTextEx(font, infoLine1, infoFontSize1, infoSpacing);
        qc::Vec2 infoSize2 = qc::MeasureTextEx(font, infoLine2, infoFontSize2, infoSpacing);
        float infoBoxWidth = std::max(infoSize1.x, infoSize2.x) + infoPadding * 2.0f;

        qc::BeginDrawing();
        qc::ClearBackground(qc::Color{18, 22, 35, 255});

        qc::DrawText("QuarkCore Audio Demo", 40, 32, 32, qc::WHITE);
        qc::DrawText("Space = play/stop", 40, 88, 24, qc::LIGHTGRAY);
        qc::DrawText("Left/Right = volume", 40, 122, 24, qc::LIGHTGRAY);

        qc::DrawRectangle(40, 180, 420, 28, qc::Color{60, 60, 80, 255});
        qc::DrawRectangle(40, 180, static_cast<int>(volume * 420.0f), 28, qc::GREEN);
        qc::DrawText(qc::TextFormat("Volume: %.2f", volume), 40, 220, 22, qc::WHITE);

        qc::DrawRectangle(40, 320, static_cast<int>(infoBoxWidth), 180, qc::Color{35, 42, 60, 220});
        qc::DrawTextEx(font, infoLine1, qc::Vec2{40.0f + infoPadding, 350.0f}, infoFontSize1, infoSpacing, qc::WHITE);
        qc::DrawTextEx(font, infoLine2, qc::Vec2{40.0f + infoPadding, 390.0f}, infoFontSize2, infoSpacing, qc::LIGHTGRAY);

        qc::EndDrawing();
    }

    qc::StopSound(beep);
    qc::UnloadSound(beep);
    qc::CloseAudioDevice();
    qc::CloseWindow();
    return 0;
}