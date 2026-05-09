<div align="center">

# ⚛️ QuarkCore

**A fast, lightweight OpenGL rendering library — the backbone of the Quark Engine.**

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![OpenGL](https://img.shields.io/badge/OpenGL-3.3+-green.svg)](https://www.opengl.org/)
[![License](https://img.shields.io/badge/license-MIT-orange.svg)](#license)

*Inspired by [Raylib](https://www.raylib.com/) — but engineered for speed.*

</div>

---

## What is QuarkCore?

QuarkCore is a low-level OpenGL rendering library designed to make graphics programming fast and ergonomic in C++. It powers the **Quark Engine** and is available as a standalone library for projects that need a clean, high-performance foundation without the overhead of larger frameworks.

It takes inspiration from Raylib's friendly API surface, but is built from the ground up with a focus on raw performance — currently benchmarking **~1.5× faster** than Raylib in typical rendering workloads.

---

## Features

- **Window & context management** — create windows, set FPS targets, handle events
- **2D & 3D rendering** — cameras, draw calls, grids, planes, cubes, and more
- **Texture system** — load, generate, and unload textures; render-to-texture support
- **Shader pipeline** — load shaders from files or memory, set uniforms at runtime
- **Text rendering** — built-in font, custom fonts, text measurement utilities
- **Input handling** — keyboard, mouse, event polling
- **Logging & formatting** — structured trace logging with `TextFormat`
- **Lightweight by design** — no hidden allocations, minimal dependencies

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

## Comparison with Raylib

QuarkCore takes Raylib's API philosophy — simple function calls, no OOP ceremony — and optimises the internals for the Quark Engine's specific rendering pipeline.

| | QuarkCore | Raylib |
|---|---|---|
| API style | `qc::` namespace | Global C functions |
| Performance | ~1.5× faster | Baseline |
| Integration | Quark Engine native | Standalone |
| OpenGL target | 3.3 Core | 3.3 / ES2 / ES3 |
| Custom shaders | First-class | Supported |

---

## License

MIT License. See [LICENSE](LICENSE) for details.

---