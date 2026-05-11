<div align="center">

# QuarkCore

**An OpenGL rendering library built to make lighting easy for the Quark Engine — and ~1.5x faster than Raylib.**

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![OpenGL](https://img.shields.io/badge/OpenGL-3.3+-green.svg)](https://www.opengl.org/)
[![License](https://img.shields.io/badge/license-MIT-orange.svg)](#license)

</div>

---

## Quick Start

### Requirements

- C++17 or later
- OpenGL 3.3+
- CMake 3.16+

### Installation

```bash
git clone https://github.com/Quark-Engine/QuarkCore.git
cd QuarkCore
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Then link against `QuarkCore` in your `CMakeLists.txt`:

```cmake
target_link_libraries(your_target PRIVATE QuarkCore)
```

---

## Usage

```cpp
#include "QuarkCore/QuarkCore.hpp"

int main() {
    qc::InitWindow(1280, 720, "Hello World");
    qc::SetTargetFPS(60);

    while (!qc::WindowShouldClose()) {
        qc::BeginDrawing();
        qc::ClearBackground(qc::Color{20, 24, 32, 255});

        qc::DrawText("Hello, World!", 360, 340, 32, qc::WHITE);

        qc::EndDrawing();
    }

    qc::CloseWindow();
    return 0;
}
```

---

## API Overview

| Category | Functions |
|---|---|
| **Window** | `InitWindow`, `CloseWindow`, `WindowShouldClose`, `SetTargetFPS`, `SetWindowMinimumSize` |
| **Drawing** | `BeginDrawing`, `EndDrawing`, `ClearBackground` |
| **2D** | `BeginMode2D`, `EndMode2D`, `DrawRectangle`, `DrawCircle`, `DrawGrid`, `DrawTexture`, `DrawTexturePro` |
| **3D** | `BeginMode3D`, `EndMode3D`, `DrawPlane`, `DrawCube` |
| **Textures** | `LoadRenderTexture`, `UnloadRenderTexture`, `GenCheckerTexture`, `UnloadTexture` |
| **Shaders** | `LoadShaderFromMemory`, `GetShaderLocation`, `SetShaderValue`, `BeginShaderMode`, `EndShaderMode`, `UnloadShader` |
| **Text** | `DrawText`, `DrawTextEx`, `MeasureText`, `MeasureTextEx`, `GetDefaultFont`, `TextFormat` |
| **Input** | `IsKeyPressed`, `IsKeyDown`, `PollEvent` |
| **Logging** | `TraceLog`, `SetLogLevel` |
| **Screen** | `GetScreenWidth`, `GetScreenHeight`, `GetFPS`, `GetDeltaTime`, `GetCurrentMonitorRefreshRate` |

---

## Built-in Shader Constants

QuarkCore exposes its default vertex shader source so you can pair it with any custom fragment shader:

```cpp
qc::LoadShaderFromMemory(qc::kVertexShaderSource, myFragmentShader);
```

---

## Examples

The `examples/` directory contains demos for:

- `sandbox.cpp` — window setup, 2D/3D cameras, render textures, text rendering
- `shaders.cpp` — chromatic aberration, pixelation, vignette, scanlines with live parameter tuning

---

<div align="center">

Made for the **[Quark Engine](https://github.com/Quark-Engine/QuarkEngine)**

</div>
