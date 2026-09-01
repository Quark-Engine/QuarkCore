#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/Quark3D.hpp"
#include "Renderer/QuarkIRenderer.hpp"
#if defined(QC_ENABLE_OPENGL)
#include "Renderer/QuarkGL/QuarkGLRenderer.hpp"
#endif
#if defined(QC_ENABLE_VULKAN)
#include "Renderer/QuarkVulkan/QuarkVkRenderer.hpp"
#endif
#if defined(QC_ENABLE_D3D11)
#include "Renderer/QuarkDX11/QuarkD3D11Renderer.hpp"
#endif
#include "QuarkInternal.hpp"

#include <SDL3/SDL.h>
#if defined(QC_ENABLE_VULKAN)
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#endif
#if defined(QC_ENABLE_D3D11)
#include <d3d11.h>
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdarg>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <cstdint>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <AL/al.h>
#include <AL/alc.h>

#include <unordered_map>

namespace qc {

#if defined(QC_ENABLE_OPENGL)
QuarkGLRenderer gGLRenderer;
#endif
#if defined(QC_ENABLE_VULKAN)
QuarkVkRenderer gVkRenderer;
#endif
#if defined(QC_ENABLE_D3D11)
QuarkD3D11Renderer gD3D11Renderer;
#endif
IRenderer* gRendererPtr = nullptr;
RendererType gCurrentBackend = RendererType::Auto;
bool gVulkanLibraryLoaded = false;
int gRequestedMSAASamples = 1;
TextureFilterMode gTextureFilterMode = TextureFilterMode::Linear;
std::array<std::array<float, SDL_GAMEPAD_AXIS_COUNT>, 16> gGamepadDeadZones{};

#define gRenderer (*gRendererPtr)

int gTextLineSpacing = 0;

WindowState gWin;
int   gLastKeyPressed   = 0;
int   gLastCharPressed  = 0;
KeyboardKey gExitKey    = KeyboardKey::Escape;
Vec2  gMousePreviousPosition{};
Vec2  gMouseOffset{};
Vec2  gMouseScale{1.0f, 1.0f};
bool  gCursorHidden     = false;

struct rAudioProcessor {
    AudioCallback callback = nullptr;
    rAudioProcessor* next = nullptr;
};

struct rAudioBuffer {
    ALuint source = 0;
    ALuint buffer = 0;
    unsigned int frameCount = 0;
    unsigned int sampleRate = 0;
    unsigned int sampleSize = 0;
    unsigned int channels = 0;
    bool ownsBuffer = true;
    bool streaming = false;
    AudioCallback callback = nullptr;
    std::vector<AudioCallback> processors;
};

ALCdevice* gAudioDevice = nullptr;
ALCcontext* gAudioContext = nullptr;
float gMasterVolume = 1.0f;
int gAudioStreamBufferSizeDefault = 4096;
std::vector<AudioCallback> gAudioMixedProcessors;
LoadFileDataCallback gLoadFileDataCallback = nullptr;
SaveFileDataCallback gSaveFileDataCallback = nullptr;
LoadFileTextCallback gLoadFileTextCallback = nullptr;
SaveFileTextCallback gSaveFileTextCallback = nullptr;
AutomationEventList* gAutomationEventList = nullptr;
AutomationEventList gDefaultAutomationEventList{};
int gAutomationBaseFrame = 0;
bool gAutomationRecording = false;
bool gVrStereoEnabled = false;
VrStereoConfig gCurrentVrStereoConfig{};
Texture2D gShapesTexture{};
Rectangle gShapesTextureRect{};


const char* ToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::None:  return "NONE";
        default:              return "UNKNOWN";
    }
}

const char* RendererTypeToString(RendererType rendererType) {
    switch (rendererType) {
        case RendererType::Auto:   return "Auto";
        case RendererType::OpenGL: return "OpenGL";
        case RendererType::Vulkan: return "Vulkan";
        case RendererType::D3D11:  return "Direct3D 11";
        default:                   return "Unknown";
    }
}

std::string FormatTimeNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    std::ostringstream ss;
    ss << std::put_time(&tm, "%H:%M:%S");
    return ss.str();
}

#if defined(QC_ENABLE_D3D11)
static bool InitD3D11Backend(int width, int height, const char* title) {
    TraceLog(LogLevel::Info, "RENDERER", "Backend selected: Direct3D 11");

    gWin.window = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE);
    if (!gWin.window)
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

    gRendererPtr = &gD3D11Renderer;
    gRenderer.Init(gWin.window, width, height);
    gRenderer.SetTargetFPS(gWin.targetFps);
    if (gWin.vsyncSet) gRenderer.SetVSync(gWin.vsync);
    gCurrentBackend = RendererType::D3D11;
    return true;
}
#endif

void WriteLog(LogLevel level, const char* type, const std::string& message) {
    if (level < gWin.minimumLogLevel || level == LogLevel::None) return;
    std::cout
        << '[' << FormatTimeNow() << ']'
        << '[' << ToString(level) << ']'
        << '[' << type << "] "
        << message << '\n';
}

void CopyText(char* dst, size_t size, const char* src) {
    if (!dst || size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
#if defined(_MSC_VER)
    strncpy_s(dst, size, src, _TRUNCATE);
#else
    std::strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
#endif
}

void UpdateInputFromEvents() {
    float mx = 0.f, my = 0.f;
    const SDL_MouseButtonFlags ms = SDL_GetMouseState(&mx, &my);
    gWin.mousePosition = Vec2{
        (mx + gMouseOffset.x) * gMouseScale.x,
        (my + gMouseOffset.y) * gMouseScale.y
    };
    gWin.mouseButtons[static_cast<std::size_t>(MouseButton::Left)]   = (ms & SDL_BUTTON_LMASK) != 0;
    gWin.mouseButtons[static_cast<std::size_t>(MouseButton::Middle)] = (ms & SDL_BUTTON_MMASK) != 0;
    gWin.mouseButtons[static_cast<std::size_t>(MouseButton::Right)]  = (ms & SDL_BUTTON_RMASK) != 0;

    const bool* ks = SDL_GetKeyboardState(nullptr);
    for (int i = 0; i < static_cast<int>(SDL_SCANCODE_COUNT); ++i)
        gWin.currentKeys[static_cast<std::size_t>(i)] = ks[i];
}

void EnsureInitialized() {
    if (gWin.window == nullptr)
        throw std::runtime_error("QuarkCore is not initialized. Call InitWindow() first.");
}

static void CleanupBackendInitFailure() {
    if (gRendererPtr) {
        gRendererPtr = nullptr;
    }

    if (gWin.window) {
        SDL_DestroyWindow(gWin.window);
        gWin.window = nullptr;
    }

    if (gVulkanLibraryLoaded) {
#if defined(QC_ENABLE_VULKAN)
        SDL_Vulkan_UnloadLibrary();
#endif
        gVulkanLibraryLoaded = false;
    }

    gCurrentBackend = RendererType::Auto;
}

#if defined(QC_ENABLE_OPENGL)
static bool InitOpenGLBackend(int width, int height, const char* title) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    if (gRequestedMSAASamples > 1) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, gRequestedMSAASamples);
    }

    TraceLog(LogLevel::Info, "RENDERER", "Backend selected: OpenGL");

    gWin.window = SDL_CreateWindow(
        title,
        width,
        height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    if (!gWin.window)
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

    gRendererPtr = &gGLRenderer;
    gRenderer.Init(gWin.window, width, height);
    gRenderer.SetTargetFPS(gWin.targetFps);
    if (gWin.vsyncSet) {
        gRenderer.SetVSync(gWin.vsync);
    }
    gCurrentBackend = RendererType::OpenGL;
    return true;
}
#endif

static TextureFilterMode ConvertTextureFilterMode(int filter) {
    switch (filter) {
        case TEXTURE_FILTER_POINT: return TextureFilterMode::Nearest;
        case TEXTURE_FILTER_BILINEAR: return TextureFilterMode::Linear;
        case TEXTURE_FILTER_TRILINEAR:
        case TEXTURE_FILTER_ANISOTROPIC_4X:
        case TEXTURE_FILTER_ANISOTROPIC_8X:
        case TEXTURE_FILTER_ANISOTROPIC_16X:
        default: return TextureFilterMode::Linear;
    }
}

static ALenum GetOpenALFormat(unsigned int sampleSize, unsigned int channels) {
    if (channels == 1) {
        if (sampleSize == 8) return AL_FORMAT_MONO8;
        if (sampleSize == 16) return AL_FORMAT_MONO16;
    } else if (channels == 2) {
        if (sampleSize == 8) return AL_FORMAT_STEREO8;
        if (sampleSize == 16) return AL_FORMAT_STEREO16;
    }
    return 0;
}

static size_t GetAudioFrameSize(unsigned int sampleSize, unsigned int channels) {
    return static_cast<size_t>((sampleSize + 7u) / 8u) * static_cast<size_t>(channels);
}

static bool EnsureAudioDevice() {
    if (gAudioContext != nullptr) return true;
    InitAudioDevice();
    return gAudioContext != nullptr;
}

static uint16_t ReadLE16(const unsigned char* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

static uint32_t ReadLE32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static Wave LoadWaveFromWavData(const unsigned char* fileData, int dataSize) {
    Wave wave{};
    if (!fileData || dataSize < 44) return wave;
    if (std::memcmp(fileData, "RIFF", 4) != 0 || std::memcmp(fileData + 8, "WAVE", 4) != 0) return wave;

    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    unsigned int sampleSize = 0;
    const unsigned char* samples = nullptr;
    uint32_t sampleBytes = 0;

    int offset = 12;
    while (offset + 8 <= dataSize) {
        const unsigned char* chunk = fileData + offset;
        const uint32_t chunkSize = ReadLE32(chunk + 4);
        offset += 8;
        if (offset + static_cast<int>(chunkSize) > dataSize) break;

        if (std::memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
            const uint16_t audioFormat = ReadLE16(fileData + offset);
            channels = ReadLE16(fileData + offset + 2);
            sampleRate = ReadLE32(fileData + offset + 4);
            sampleSize = ReadLE16(fileData + offset + 14);
            if (audioFormat != 1 && audioFormat != 3) return {};
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            samples = fileData + offset;
            sampleBytes = chunkSize;
        }

        offset += static_cast<int>(chunkSize + (chunkSize & 1u));
    }

    const size_t frameSize = GetAudioFrameSize(sampleSize, channels);
    if (!samples || sampleBytes == 0 || frameSize == 0 || sampleRate == 0) return wave;

    void* data = MemAlloc(sampleBytes);
    if (!data) return wave;
    std::memcpy(data, samples, sampleBytes);
    wave.frameCount = static_cast<unsigned int>(sampleBytes / frameSize);
    wave.sampleRate = sampleRate;
    wave.sampleSize = sampleSize;
    wave.channels = channels;
    wave.data = data;
    return wave;
}

static void WriteLE16(std::ofstream& out, uint16_t value) {
    const unsigned char bytes[2] = {
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8) & 0xffu)
    };
    out.write(reinterpret_cast<const char*>(bytes), 2);
}

static void WriteLE32(std::ofstream& out, uint32_t value) {
    const unsigned char bytes[4] = {
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8) & 0xffu),
        static_cast<unsigned char>((value >> 16) & 0xffu),
        static_cast<unsigned char>((value >> 24) & 0xffu)
    };
    out.write(reinterpret_cast<const char*>(bytes), 4);
}

static Sound MakeSoundFromWave(Wave wave, bool copyData) {
    Sound sound{};
    if (!wave.data || wave.frameCount == 0 || !EnsureAudioDevice()) return sound;
    const ALenum format = GetOpenALFormat(wave.sampleSize, wave.channels);
    if (format == 0) return sound;

    ALuint buffer = 0;
    ALuint source = 0;
    alGenBuffers(1, &buffer);
    alGenSources(1, &source);
    const size_t byteCount = static_cast<size_t>(wave.frameCount) * GetAudioFrameSize(wave.sampleSize, wave.channels);
    const void* srcData = wave.data;
    std::vector<unsigned char> owned;
    if (copyData) {
        owned.resize(byteCount);
        std::memcpy(owned.data(), wave.data, byteCount);
        srcData = owned.data();
    }
    alBufferData(buffer, format, srcData, static_cast<ALsizei>(byteCount), static_cast<ALsizei>(wave.sampleRate));
    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcef(source, AL_GAIN, 1.0f);

    if (alGetError() != AL_NO_ERROR) {
        if (source) alDeleteSources(1, &source);
        if (buffer) alDeleteBuffers(1, &buffer);
        return sound;
    }

    auto* audioBuffer = new rAudioBuffer{};
    audioBuffer->source = source;
    audioBuffer->buffer = buffer;
    audioBuffer->frameCount = wave.frameCount;
    audioBuffer->sampleRate = wave.sampleRate;
    audioBuffer->sampleSize = wave.sampleSize;
    audioBuffer->channels = wave.channels;
    sound.stream.buffer = audioBuffer;
    sound.stream.sampleRate = wave.sampleRate;
    sound.stream.sampleSize = wave.sampleSize;
    sound.stream.channels = wave.channels;
    sound.frameCount = wave.frameCount;
    return sound;
}

static void DestroyAudioBuffer(rAudioBuffer* buffer, bool deleteOpenALBuffer) {
    if (!buffer) return;
    if (buffer->source) alDeleteSources(1, &buffer->source);
    if (deleteOpenALBuffer && buffer->buffer) alDeleteBuffers(1, &buffer->buffer);
    delete buffer;
}

void SetMSAASamples(int samples) {
    gRequestedMSAASamples = (samples == 2 || samples == 4 || samples == 8) ? samples : 1;
#if defined(QC_ENABLE_VULKAN)
    gVkRenderer.SetMSAASamples(gRequestedMSAASamples);
#endif
#if defined(QC_ENABLE_D3D11)
    gD3D11Renderer.SetMSAASamples(gRequestedMSAASamples);
#endif
}

void SetTextureFilterMode(TextureFilterMode mode) {
    gTextureFilterMode = mode;
    gWin.activeTextureFilter = (mode == TextureFilterMode::Nearest) ? TEXTURE_FILTER_POINT : TEXTURE_FILTER_BILINEAR;

    if (gRendererPtr) {
        gRenderer.SetTextureFilterMode(mode);
    }
}

#if defined(QC_ENABLE_VULKAN)
static bool InitVulkanBackend(int width, int height, const char* title) {
    TraceLog(LogLevel::Info, "RENDERER", "Backend selected: Vulkan");

    if (!SDL_Vulkan_LoadLibrary(nullptr))
        throw std::runtime_error(std::string("SDL_Vulkan_LoadLibrary failed: ") + SDL_GetError());
    gVulkanLibraryLoaded = true;

    gWin.window = SDL_CreateWindow(
        title,
        width,
        height,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );

    if (!gWin.window)
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());

    gRendererPtr = &gVkRenderer;
    gVkRenderer.SetMSAASamples(gRequestedMSAASamples);
    gRenderer.Init(gWin.window, width, height);
    gRenderer.SetTargetFPS(gWin.targetFps);
    if (gWin.vsyncSet) {
        gRenderer.SetVSync(gWin.vsync);
    }
    gCurrentBackend = RendererType::Vulkan;
    return true;
}
#endif

void InitWindow(int width, int height, const char* title, RendererType rendererType) {
    TraceLog(LogLevel::Info, "CORE", "Initializing QuarkCore...");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        TraceLog(LogLevel::Error, "SDL", TextFormat("SDL_Init failed: %s", SDL_GetError()));
        return;
    }
    TraceLog(LogLevel::Info, "SDL", "SDL subsystems initialized");

    int version = SDL_GetVersion();
    TraceLog(LogLevel::Info, "CORE",
        TextFormat("SDL Version: %d.%d.%d",
            SDL_VERSIONNUM_MAJOR(version),
            SDL_VERSIONNUM_MINOR(version),
            SDL_VERSIONNUM_MICRO(version)));

    TraceLog(LogLevel::Info, "WINDOW", TextFormat("Starting window creation: %s (%dx%d)", title ? title : "Quark", width, height));

    TraceLog(LogLevel::Info, "RENDERER", TextFormat("Selected backend: %s", RendererTypeToString(rendererType)));

    auto initVulkan = [&]() {
#if defined(QC_ENABLE_VULKAN)
        InitVulkanBackend(width, height, title);
#else
        throw std::runtime_error("QuarkCore was built without Vulkan support");
#endif
    };

    auto initOpenGL = [&]() {
#if defined(QC_ENABLE_OPENGL)
        InitOpenGLBackend(width, height, title);
#else
        throw std::runtime_error("QuarkCore was built without OpenGL support");
#endif
    };

        auto initD3D11 = [&]() {
    #if defined(QC_ENABLE_D3D11)
        InitD3D11Backend(width, height, title);
    #else
        throw std::runtime_error("QuarkCore was built without D3D11 support");
    #endif
        };

    try {
        if (rendererType == RendererType::Auto) {
            try {
                initVulkan();
            } catch (const std::exception& ex) {
                TraceLog(LogLevel::Warn, "VULKAN", (std::string("Vulkan initialization failed: ") + ex.what() + "; trying OpenGL.").c_str());
                CleanupBackendInitFailure();
                initOpenGL();
            } catch (...) {
                TraceLog(LogLevel::Warn, "VULKAN", "Vulkan initialization failed with an unknown exception; trying OpenGL.");
                CleanupBackendInitFailure();
                initOpenGL();
            }
        } else if (rendererType == RendererType::OpenGL) {
            initOpenGL();
        } else if (rendererType == RendererType::Vulkan) {
            initVulkan();
        } else if (rendererType == RendererType::D3D11) {
            initD3D11();
        }
    } catch (const std::exception& ex) {
        TraceLog(LogLevel::Error, "CORE", (std::string("Renderer initialization failed: ") + ex.what()).c_str());
        CleanupBackendInitFailure();
        SDL_Quit();
        return;
    } catch (...) {
        TraceLog(LogLevel::Error, "CORE", "Renderer initialization failed with an unknown exception.");
        CleanupBackendInitFailure();
        SDL_Quit();
        return;
    }

    if (!gWin.window) {
        TraceLog(LogLevel::Error, "WINDOW", "Window is null after renderer initialization.");
        SDL_Quit();
        return;
    }

    WriteLog(LogLevel::Info, "WINDOW", "Window created: " + std::string(title ? title : ""));
}

void CloseWindow() {
    if (gRendererPtr) {
        gRenderer.Shutdown();
        gRendererPtr = nullptr;
    }
    if (gVulkanLibraryLoaded) {
#if defined(QC_ENABLE_VULKAN)
        SDL_Vulkan_UnloadLibrary();
#endif
        gVulkanLibraryLoaded = false;
    }
    if (gWin.window) {
        SDL_DestroyWindow(gWin.window);
        gWin.window = nullptr;
    }
    gCurrentBackend = RendererType::Auto;
    SDL_Quit();
    WriteLog(LogLevel::Info, "WINDOW", "Window closed");
}

RendererType GetCurrentBackend() {
    return gCurrentBackend;
}

#if defined(QC_ENABLE_VULKAN)
static QuarkVkRenderer* GetVulkanRenderer() {
    if (!gRendererPtr || gRendererPtr->GetType() != RendererType::Vulkan) {
        return nullptr;
    }
    return &gVkRenderer;
}

VkInstance GetVulkanInstance() {
    if (QuarkVkRenderer* renderer = GetVulkanRenderer()) {
        return renderer->GetVulkanInstance();
    }
    return VK_NULL_HANDLE;
}

VkPhysicalDevice GetVulkanPhysicalDevice() {
    if (QuarkVkRenderer* renderer = GetVulkanRenderer()) {
        return renderer->GetVulkanPhysicalDevice();
    }
    return VK_NULL_HANDLE;
}

VkDevice GetVulkanDevice() {
    if (QuarkVkRenderer* renderer = GetVulkanRenderer()) {
        return renderer->GetVulkanDevice();
    }
    return VK_NULL_HANDLE;
}

uint32_t GetVulkanGraphicsQueueFamily() {
    if (QuarkVkRenderer* renderer = GetVulkanRenderer()) {
        return renderer->GetVulkanGraphicsQueueFamily();
    }
    return UINT32_MAX;
}

VkQueue GetVulkanGraphicsQueue() {
    if (QuarkVkRenderer* renderer = GetVulkanRenderer()) {
        return renderer->GetVulkanGraphicsQueue();
    }
    return VK_NULL_HANDLE;
}

VkDescriptorPool GetVulkanDescriptorPool() {
    if (QuarkVkRenderer* renderer = GetVulkanRenderer()) {
        return renderer->GetVulkanDescriptorPool();
    }
    return VK_NULL_HANDLE;
}

VkRenderPass GetVulkanMainRenderPass() {
    if (QuarkVkRenderer* renderer = GetVulkanRenderer()) {
        return renderer->GetVulkanMainRenderPass();
    }
    return VK_NULL_HANDLE;
}

uint32_t GetVulkanMinImageCount() {
    if (QuarkVkRenderer* renderer = GetVulkanRenderer()) {
        return renderer->GetVulkanMinImageCount();
    }
    return 0;
}

uint32_t GetVulkanImageCount() {
    if (QuarkVkRenderer* renderer = GetVulkanRenderer()) {
        return renderer->GetVulkanImageCount();
    }
    return 0;
}

VkSampleCountFlagBits GetVulkanMSAASamples() {
    if (QuarkVkRenderer* renderer = GetVulkanRenderer()) {
        return renderer->GetVulkanMSAASamples();
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

VkDescriptorSet GetVulkanTextureDescriptorSet(uint32_t textureId) {
    if (QuarkVkRenderer* renderer = GetVulkanRenderer()) {
        return renderer->GetTextureDescriptorSet(textureId);
    }
    return VK_NULL_HANDLE;
}

static VulkanRenderCallback gVulkanRenderCallback = nullptr;

void SetVulkanRenderCallback(VulkanRenderCallback callback) {
    gVulkanRenderCallback = callback;
}

VulkanRenderCallback GetVulkanRenderCallback() {
    return gVulkanRenderCallback;
}
#endif

#if defined(QC_ENABLE_D3D11)
static QuarkD3D11Renderer* GetD3D11Renderer() {
    if (!gRendererPtr || gRendererPtr->GetType() != RendererType::D3D11) {
        return nullptr;
    }
    return &gD3D11Renderer;
}

ID3D11Device* GetD3D11Device() {
    if (QuarkD3D11Renderer* renderer = GetD3D11Renderer()) {
        return renderer->GetD3D11Device();
    }
    return nullptr;
}

ID3D11DeviceContext* GetD3D11ImmediateContext() {
    if (QuarkD3D11Renderer* renderer = GetD3D11Renderer()) {
        return renderer->GetD3D11ImmediateContext();
    }
    return nullptr;
}

static D3D11RenderCallback gD3D11RenderCallback = nullptr;

void SetD3D11RenderCallback(D3D11RenderCallback callback) {
    gD3D11RenderCallback = callback;
}

D3D11RenderCallback GetD3D11RenderCallback() {
    return gD3D11RenderCallback;
}
#endif

bool WindowShouldClose() {
    if (!gWin.eventsReady) {
        PumpSystemEvents();
    }
    gWin.eventsReady = false;

    if (gRendererPtr && gRenderer.ShouldClose()) gWin.shouldClose = true;
    return gWin.shouldClose;
}

bool IsWindowReady() {
    return gWin.window != nullptr;
}

int GetScreenWidth()  { return gRendererPtr ? gRenderer.GetScreenWidth() : 0; }
int GetScreenHeight() { return gRendererPtr ? gRenderer.GetScreenHeight() : 0; }

void SetTargetFPS(int fps) {
    gWin.targetFps = fps;
    if (gRendererPtr) gRenderer.SetTargetFPS(fps);
}

bool SetVSync(bool enabled) {
    gWin.vsync = enabled;
    gWin.vsyncSet = true;
    if (gRendererPtr) return gRenderer.SetVSync(enabled);
    return true;
}

float GetFrameTime()  { return gRendererPtr ? gRenderer.GetFrameTime() : 0.0f; }
float GetDeltaTime()  { return GetFrameTime(); }

double GetTime() {
    if (!SDL_WasInit(SDL_INIT_VIDEO)) return 0.0;
    return static_cast<double>(SDL_GetTicks()) / 1000.0;
}

void SetLogLevel(LogLevel level) { gWin.minimumLogLevel = level; }

void TraceLog(LogLevel level, const char* type, const char* message) {
    WriteLog(level, type, message ? message : "");
}

const char* TextFormat(const char* fmt, ...) {
    thread_local char buf[4096];
    if (!fmt) { buf[0] = '\0'; return buf; }
    va_list args;
    va_start(args, fmt);
#if defined(_MSC_VER)
    vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
#else
    std::vsnprintf(buf, sizeof(buf), fmt, args);
#endif
    va_end(args);
    return buf;
}

int TextCopy(char* dst, const char* src) {
    if (dst == nullptr || src == nullptr) return 0;
    int i = 0;
    for (; src[i] != '\0'; ++i) dst[i] = src[i];
    dst[i] = '\0';
    return i;
}

bool TextIsEqual(const char* text1, const char* text2) {
    if (text1 == nullptr || text2 == nullptr) return text1 == text2;
    return std::strcmp(text1, text2) == 0;
}

unsigned int TextLength(const char* text) {
    return text ? static_cast<unsigned int>(std::strlen(text)) : 0u;
}

static thread_local char g_textBuffer[1024];

const char* TextSubtext(const char* text, int position, int length) {
    g_textBuffer[0] = '\0';
    if (text == nullptr) return g_textBuffer;

    const int textLength = static_cast<int>(std::strlen(text));
    if (position >= textLength) return g_textBuffer;
    if (length >= textLength) length = textLength;

    int index = 0;
    for (int i = position; (i < textLength) && (index < length); ++i) g_textBuffer[index++] = text[i];
    g_textBuffer[index] = '\0';
    return g_textBuffer;
}

const char* TextRemoveSpaces(const char* text) {
    g_textBuffer[0] = '\0';
    if (text == nullptr) return g_textBuffer;
    int index = 0;
    for (int i = 0; (i < 1024) && (text[i] != '\0'); ++i) {
        if (text[i] != ' ') g_textBuffer[index++] = text[i];
    }
    g_textBuffer[index] = '\0';
    return g_textBuffer;
}

char* GetTextBetween(const char* text, const char* begin, const char* end) {
    if (text == nullptr || begin == nullptr || end == nullptr) return nullptr;
    const char* start = std::strstr(text, begin);
    if (start == nullptr) return nullptr;
    start += std::strlen(begin);
    const char* stop = std::strstr(start, end);
    if (stop == nullptr) return nullptr;

    const int length = static_cast<int>(stop - start);
    char* result = static_cast<char*>(MemAlloc(static_cast<size_t>(length) + 1));
    if (result == nullptr) return nullptr;
    std::memcpy(result, start, static_cast<size_t>(length));
    result[length] = '\0';
    return result;
}

char* TextReplace(const char* text, const char* search, const char* replacement) {
    if (text == nullptr || search == nullptr || replacement == nullptr) return nullptr;

    const int textLen = static_cast<int>(std::strlen(text));
    const int searchLen = static_cast<int>(std::strlen(search));
    const int byLen = static_cast<int>(std::strlen(replacement));
    if (searchLen == 0) return nullptr;

    int count = 0;
    const char* ptr = std::strstr(text, search);
    while (ptr != nullptr) { ++count; ptr = std::strstr(ptr + searchLen, search); }
    if (count == 0) return nullptr;

    const int resultLen = textLen + (byLen - searchLen) * count + 1;
    char* result = static_cast<char*>(MemAlloc(static_cast<size_t>(resultLen)));
    if (result == nullptr) return nullptr;

    int pos = 0;
    const char* last = text;
    ptr = std::strstr(last, search);
    while (ptr != nullptr) {
        const int len = static_cast<int>(ptr - last);
        std::memcpy(result + pos, last, static_cast<size_t>(len));
        pos += len;
        std::memcpy(result + pos, replacement, static_cast<size_t>(byLen));
        pos += byLen;
        last = ptr + searchLen;
        ptr = std::strstr(last, search);
    }
    std::memcpy(result + pos, last, std::strlen(last) + 1);
    return result;
}

char* TextReplaceAlloc(const char* text, const char* search, const char* replacement) {
    if (text == nullptr) return nullptr;
    char* result = TextReplace(text, search, replacement);
    if (result != nullptr) return result;
    const size_t len = std::strlen(text);
    result = static_cast<char*>(MemAlloc(len + 1));
    if (result != nullptr) std::memcpy(result, text, len + 1);
    return result;
}

char* TextReplaceBetween(const char* text, const char* begin, const char* end, const char* replacement) {
    if (text == nullptr || begin == nullptr || end == nullptr || replacement == nullptr) return nullptr;

    const char* start = std::strstr(text, begin);
    if (start == nullptr) return nullptr;
    const char* stop = std::strstr(start + std::strlen(begin), end);
    if (stop == nullptr) return nullptr;
    stop += std::strlen(end);

    const int part1Len = static_cast<int>(start - text);
    const int part2Len = static_cast<int>((text + std::strlen(text)) - stop);
    const int replacementLen = static_cast<int>(std::strlen(replacement));
    const int resultLen = part1Len + replacementLen + part2Len + 1;

    char* result = static_cast<char*>(MemAlloc(static_cast<size_t>(resultLen)));
    if (result == nullptr) return nullptr;
    std::memcpy(result, text, static_cast<size_t>(part1Len));
    std::memcpy(result + part1Len, replacement, static_cast<size_t>(replacementLen));
    std::memcpy(result + part1Len + replacementLen, stop, static_cast<size_t>(part2Len));
    result[resultLen - 1] = '\0';
    return result;
}

char* TextReplaceBetweenAlloc(const char* text, const char* begin, const char* end, const char* replacement) {
    if (text == nullptr) return nullptr;
    char* result = TextReplaceBetween(text, begin, end, replacement);
    if (result != nullptr) return result;
    const size_t len = std::strlen(text);
    result = static_cast<char*>(MemAlloc(len + 1));
    if (result != nullptr) std::memcpy(result, text, len + 1);
    return result;
}

char* TextInsert(const char* text, const char* insert, int position) {
    if (text == nullptr || insert == nullptr) return nullptr;
    const int textLen = static_cast<int>(std::strlen(text));
    const int insertLen = static_cast<int>(std::strlen(insert));
    if (position < 0 || position > textLen) return nullptr;

    char* result = static_cast<char*>(MemAlloc(static_cast<size_t>(textLen + insertLen + 1)));
    if (result == nullptr) return nullptr;
    if (position > 0) std::memcpy(result, text, static_cast<size_t>(position));
    std::memcpy(result + position, insert, static_cast<size_t>(insertLen));
    std::memcpy(result + position + insertLen, text + position, static_cast<size_t>(textLen - position) + 1);
    return result;
}

char* TextInsertAlloc(const char* text, const char* insert, int position) {
    if (text == nullptr) return nullptr;
    char* result = TextInsert(text, insert, position);
    if (result != nullptr) return result;
    const size_t len = std::strlen(text);
    result = static_cast<char*>(MemAlloc(len + 1));
    if (result != nullptr) std::memcpy(result, text, len + 1);
    return result;
}

char* TextJoin(char** textList, int count, const char* delimiter) {
    if (textList == nullptr || count <= 0) return nullptr;

    int totalLength = 0;
    const int delimiterLength = delimiter ? static_cast<int>(std::strlen(delimiter)) : 0;
    for (int i = 0; i < count; ++i) {
        if (textList[i] != nullptr) totalLength += static_cast<int>(std::strlen(textList[i]));
        if (i < count - 1) totalLength += delimiterLength;
    }

    char* result = static_cast<char*>(MemAlloc(static_cast<size_t>(totalLength) + 1));
    if (result == nullptr) return nullptr;
    int pos = 0;
    for (int i = 0; i < count; ++i) {
        if (textList[i] != nullptr) {
            const int len = static_cast<int>(std::strlen(textList[i]));
            std::memcpy(result + pos, textList[i], static_cast<size_t>(len));
            pos += len;
        }
        if (i < count - 1 && delimiter != nullptr) {
            const int dlen = static_cast<int>(std::strlen(delimiter));
            std::memcpy(result + pos, delimiter, static_cast<size_t>(dlen));
            pos += dlen;
        }
    }
    result[pos] = '\0';
    return result;
}

char** TextSplit(const char* text, char delimiter, int* count) {
    if (count != nullptr) *count = 0;
    if (text == nullptr) return nullptr;

    int ncount = 1;
    const char* ptr = text;
    while (*ptr != '\0') { if (*ptr == delimiter) ++ncount; ++ptr; }

    char** result = static_cast<char**>(MemAlloc(static_cast<size_t>(ncount) * sizeof(char*)));
    if (result == nullptr) return nullptr;

    int index = 0;
    int length = 0;
    ptr = text;
    while (*ptr != '\0') {
        if (*ptr != delimiter) {
            ++length;
        } else {
            result[index] = static_cast<char*>(MemAlloc(static_cast<size_t>(length) + 1));
            if (length > 0) std::memcpy(result[index], ptr - length, static_cast<size_t>(length));
            result[index][length] = '\0';
            length = 0;
            ++index;
        }
        ++ptr;
    }
    result[index] = static_cast<char*>(MemAlloc(static_cast<size_t>(length) + 1));
    if (length > 0) std::memcpy(result[index], ptr - length, static_cast<size_t>(length));
    result[index][length] = '\0';
    ++index;

    if (count != nullptr) *count = index;
    return result;
}

void TextAppend(char* text, const char* append, int* position) {
    if (text == nullptr || append == nullptr || position == nullptr) return;
    int textLen = static_cast<int>(std::strlen(text));
    int pos = *position;
    if (pos > textLen) pos = textLen;
    if (pos < 0) pos = 0;
    const int appendLen = static_cast<int>(std::strlen(append));
    for (int i = 0; i < appendLen; ++i) text[pos + i] = append[i];
    text[pos + appendLen] = '\0';
    *position = pos + appendLen;
}

int TextFindIndex(const char* text, const char* search) {
    if (text == nullptr || search == nullptr) return -1;
    const char* found = std::strstr(text, search);
    if (found == nullptr) return -1;
    return static_cast<int>(found - text);
}

const char* TextToUpper(const char* text) {
    g_textBuffer[0] = '\0';
    if (text == nullptr) return g_textBuffer;
    int i = 0;
    for (; (i < 1023) && (text[i] != '\0'); ++i) g_textBuffer[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
    g_textBuffer[i] = '\0';
    return g_textBuffer;
}

const char* TextToLower(const char* text) {
    g_textBuffer[0] = '\0';
    if (text == nullptr) return g_textBuffer;
    int i = 0;
    for (; (i < 1023) && (text[i] != '\0'); ++i) g_textBuffer[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
    g_textBuffer[i] = '\0';
    return g_textBuffer;
}

const char* TextToPascal(const char* text) {
    g_textBuffer[0] = '\0';
    if (text == nullptr) return g_textBuffer;

    int j = 0;
    int i = 0;
    if (text[i] != '\0') {
        g_textBuffer[j++] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
        ++i;
    }
    for (; (i < 1023) && (text[i] != '\0'); ++i) {
        if (text[i] == ' ' || text[i] == '_' || text[i] == '-') {
            ++i;
            if (text[i] != '\0') g_textBuffer[j] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
        } else {
            g_textBuffer[j] = text[i];
        }
        ++j;
    }
    g_textBuffer[j] = '\0';
    return g_textBuffer;
}

const char* TextToSnake(const char* text) {
    g_textBuffer[0] = '\0';
    if (text == nullptr) return g_textBuffer;
    int j = 0;
    for (int i = 0; (i < 1023) && (text[i] != '\0'); ++i) {
        if (text[i] == ' ' || text[i] == '-' || ((text[i] >= 'A') && (text[i] <= 'Z'))) {
            if ((j > 0) && (g_textBuffer[j - 1] != '_')) g_textBuffer[j++] = '_';
            if ((text[i] >= 'A') && (text[i] <= 'Z')) g_textBuffer[j++] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        } else {
            g_textBuffer[j++] = text[i];
        }
    }
    g_textBuffer[j] = '\0';
    return g_textBuffer;
}

const char* TextToCamel(const char* text) {
    g_textBuffer[0] = '\0';
    if (text == nullptr) return g_textBuffer;

    int j = 0;
    int i = 0;
    if (text[i] != '\0') {
        g_textBuffer[j++] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        ++i;
    }
    for (; (i < 1023) && (text[i] != '\0'); ++i) {
        if (text[i] == ' ' || text[i] == '_' || text[i] == '-') {
            ++i;
            if (text[i] != '\0') g_textBuffer[j] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
        } else {
            g_textBuffer[j] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        }
        ++j;
    }
    g_textBuffer[j] = '\0';
    return g_textBuffer;
}

int TextToInteger(const char* text) {
    if (text == nullptr) return 0;
    int value = 0;
    int sign = 1;
    if ((text[0] == '-') || (text[0] == '+')) {
        if (text[0] == '-') sign = -1;
        ++text;
    }
    for (int i = 0; (text[i] >= '0') && (text[i] <= '9'); ++i) value = value * 10 + (text[i] - '0');
    return value * sign;
}

float TextToFloat(const char* text) {
    if (text == nullptr) return 0.0f;
    float value = 0.0f;
    float sign = 1.0f;
    if ((text[0] == '-') || (text[0] == '+')) {
        if (text[0] == '-') sign = -1.0f;
        ++text;
    }
    for (int i = 0; (text[i] >= '0') && (text[i] <= '9'); ++i) value = value * 10.0f + static_cast<float>(text[i] - '0');
    if (text[0] == '.') {
        float decimal = 0.1f;
        for (int i = 1; (text[i] >= '0') && (text[i] <= '9'); ++i) {
            value += static_cast<float>(text[i] - '0') * decimal;
            decimal *= 0.1f;
        }
    }
    return value * sign;
}

char** LoadTextLines(const char* text, int* count) {
    if (count != nullptr) *count = 0;
    if (text == nullptr) return nullptr;

    int ncount = 1;
    for (int i = 0; text[i] != '\0'; ++i) if (text[i] == '\n') ++ncount;

    char** lines = static_cast<char**>(MemAlloc(static_cast<size_t>(ncount) * sizeof(char*)));
    if (lines == nullptr) return nullptr;

    int index = 0;
    int length = 0;
    int start = 0;
    for (int i = 0; ; ++i) {
        const char c = text[i];
        if (c == '\0') {
            if (length > 0) {
                lines[index] = static_cast<char*>(MemAlloc(static_cast<size_t>(length) + 1));
                if (length > 0) std::memcpy(lines[index], text + start, static_cast<size_t>(length));
                lines[index][length] = '\0';
                ++index;
            }
            break;
        }
        if (c == '\n') {
            lines[index] = static_cast<char*>(MemAlloc(static_cast<size_t>(length) + 1));
            if (length > 0) std::memcpy(lines[index], text + start, static_cast<size_t>(length));
            lines[index][length] = '\0';
            ++index;
            ++i;
            length = 0;
            start = i;
            if (text[i] == '\0') {
                lines[index] = static_cast<char*>(MemAlloc(1));
                lines[index][0] = '\0';
                ++index;
                break;
            }
        } else {
            ++length;
        }
    }

    if (count != nullptr) *count = index;
    return lines;
}

void UnloadTextLines(char** text, int lineCount) {
    if (text == nullptr) return;
    for (int i = 0; i < lineCount; ++i) {
        if (text[i] != nullptr) MemFree(text[i]);
    }
    MemFree(text);
}

SDL_GLContext GetNativeContext() {
    EnsureInitialized();
    return SDL_GL_GetCurrentContext();
}

bool IsTextInputActive() {
    EnsureInitialized();
    return SDL_TextInputActive(gWin.window);
}

bool IsKeyPressedRepeat(int key) {
    return IsKeyPressed(static_cast<KeyboardKey>(key));
}

const char* GetKeyName(int key) {
    return SDL_GetKeyName(static_cast<SDL_Keycode>(key));
}

int GetTouchX(void) {
    return static_cast<int>(GetMousePosition().x);
}

int GetTouchY(void) {
    return static_cast<int>(GetMousePosition().y);
}

Vec2 GetTouchPosition(int index) {
    if (index == 0) {
        return GetMousePosition();
    }
    return Vec2{};
}

int GetTouchPointId(int index) {
    if (index == 0) {
        return 0;
    }
    return -1;
}

int GetTouchPointCount(void) {
    const bool hasTouch =
        IsMouseButtonDown(MouseButton::Left) ||
        IsMouseButtonDown(MouseButton::Right) ||
        IsMouseButtonDown(MouseButton::Middle);

    return hasTouch ? 1 : 0;
}

void SetGesturesEnabled(unsigned int flags) {
    gWin.enabledGestures = flags;
    if (flags == GESTURE_NONE) {
        gWin.currentGesture = GESTURE_NONE;
    }
}

bool IsGestureDetected(unsigned int gesture) {
    if ((gWin.enabledGestures & gesture) == 0u) {
        return false;
    }

    return gWin.currentGesture == static_cast<int>(gesture);
}

int GetGestureDetected(void) {
    return gWin.currentGesture;
}

float GetGestureHoldDuration(void) {
    return gWin.gestureHoldDuration;
}

Vec2 GetGestureDragVector(void) {
    return gWin.gestureDragVector;
}

float GetGestureDragAngle(void) {
    return gWin.gestureDragAngle;
}

Vec2 GetGesturePinchVector(void) {
    return gWin.gesturePinchVector;
}

float GetGesturePinchAngle(void) {
    return gWin.gesturePinchAngle;
}

void UpdateCamera(Camera3D* camera, int mode) {
    if (!camera) {
        return;
    }

    camera->projection = mode;
    if (mode == CAMERA_FREE || mode == CAMERA_ORBITAL || mode == CAMERA_FIRST_PERSON || mode == CAMERA_THIRD_PERSON) {
        return;
    }

    camera->target = camera->position + Vec3{0.0f, 0.0f, -1.0f};
}

void UpdateCameraPro(Camera3D* camera, Vec2 movement, Vec3 rotation, float zoom) {
    if (!camera) {
        return;
    }

    const Vec3 delta{movement.x, movement.y, 0.0f};
    camera->position += delta;
    camera->target += delta;

    if (rotation.x != 0.0f || rotation.y != 0.0f || rotation.z != 0.0f) {
        const Vec3 direction = (camera->target - camera->position).normalized();
        const Vec3 right = direction.cross(camera->up).normalized();
        const Vec3 up = right.cross(direction).normalized();

        const float yaw = rotation.y * DEG2RAD;
        const float pitch = rotation.x * DEG2RAD;
        const float roll = rotation.z * DEG2RAD;

        Vec3 rotatedDirection = direction;
        rotatedDirection = Vec3{
            rotatedDirection.x * std::cos(yaw) + rotatedDirection.z * std::sin(yaw),
            rotatedDirection.y,
            -rotatedDirection.x * std::sin(yaw) + rotatedDirection.z * std::cos(yaw)
        };

        rotatedDirection = Vec3{
            rotatedDirection.x,
            rotatedDirection.y * std::cos(pitch) - rotatedDirection.z * std::sin(pitch),
            rotatedDirection.y * std::sin(pitch) + rotatedDirection.z * std::cos(pitch)
        };

        const Vec3 rotatedUp = Vec3{
            up.x * std::cos(roll) - right.x * std::sin(roll),
            up.y * std::cos(roll) - right.y * std::sin(roll),
            up.z * std::cos(roll) - right.z * std::sin(roll)
        };

        camera->target = camera->position + rotatedDirection;
        camera->up = rotatedUp.normalized();
    }

    camera->fovy = std::clamp(camera->fovy + zoom, 1.0f, 179.0f);
}

Mat4 GetCameraMatrix(Camera3D camera) {
    return GetCameraMat4(camera);
}

Mat4 GetCameraMatrix2D(Camera2D camera) {
    const float angle = camera.rotation * DEG2RAD;
    const float cosA = std::cos(angle);
    const float sinA = std::sin(angle);
    const float scale = camera.zoom;

    const Mat4 translationToTarget = Mat4::translation(-camera.target.x, -camera.target.y, 0.0f);
    const Mat4 rotation = Mat4::identity();
    Mat4 rotationMatrix = rotation;
    rotationMatrix.m[0] = cosA;
    rotationMatrix.m[1] = sinA;
    rotationMatrix.m[4] = -sinA;
    rotationMatrix.m[5] = cosA;

    const Mat4 scaleMatrix = Mat4::scale(scale, scale, 1.0f);
    const Mat4 translationToScreen = Mat4::translation(camera.offset.x, camera.offset.y, 0.0f);

    return translationToScreen * scaleMatrix * rotationMatrix * translationToTarget;
}

void BeginBlendMode(int mode) {
    gWin.activeBlendMode = mode;

    if (gRendererPtr) {
        gRenderer.SetBlendMode(mode);
    }
}

void EndBlendMode(void) {
    gWin.activeBlendMode = BLEND_ALPHA;

    if (gRendererPtr) {
        gRenderer.SetBlendMode(BLEND_ALPHA);
    }
}

void BeginScissorMode(int x, int y, int width, int height) {
    gWin.scissorEnabled = true;
    gWin.scissorRect = Rectangle{
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(width),
        static_cast<float>(height)
    };

    if (gRendererPtr) {
        gRenderer.BeginScissorMode(x, y, width, height);
    }
}

void EndScissorMode(void) {
    gWin.scissorEnabled = false;
    gWin.scissorRect = Rectangle{};

    if (gRendererPtr) {
        gRenderer.EndScissorMode();
    }
}

void SetTextureFilter(Texture2D texture, int filter) {
    gWin.activeTextureFilter = filter;
    SetTextureFilterMode(ConvertTextureFilterMode(filter));

    if (gRendererPtr) {
        gRenderer.SetTextureFilter(filter);
    }
}

void SetTextureWrap(Texture2D texture, int wrap) {
    gWin.activeTextureWrap = wrap;

    if (gRendererPtr) {
        gRenderer.SetTextureWrap(wrap);
    }
}

void InitAudioDevice(void) {
    if (gAudioContext != nullptr) return;
    TraceLog(LogLevel::Info, "AUDIO", "Initializing OpenAL audio device...");
    gAudioDevice = alcOpenDevice(nullptr);
    if (!gAudioDevice) {
        TraceLog(LogLevel::Error, "AUDIO", "Failed to open OpenAL device");
        return;
    }
    gAudioContext = alcCreateContext(gAudioDevice, nullptr);
    if (!gAudioContext || !alcMakeContextCurrent(gAudioContext)) {
        TraceLog(LogLevel::Error, "AUDIO", "Failed to create OpenAL context");
        if (gAudioContext) alcDestroyContext(gAudioContext);
        alcCloseDevice(gAudioDevice);
        gAudioContext = nullptr;
        gAudioDevice = nullptr;
        return;
    }
    alListenerf(AL_GAIN, gMasterVolume);
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        TraceLog(LogLevel::Warn, "AUDIO", TextFormat("OpenAL context initialized, but reported error: 0x%04X", error));
    }
    TraceLog(LogLevel::Info, "AUDIO", "OpenAL audio device initialized successfully.");
}

void CloseAudioDevice(void) {
    if (gAudioContext) {
        TraceLog(LogLevel::Info, "AUDIO", "Closing OpenAL audio context...");
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(gAudioContext);
        gAudioContext = nullptr;
    }
    if (gAudioDevice) {
        alcCloseDevice(gAudioDevice);
        gAudioDevice = nullptr;
    }
}

bool IsAudioDeviceReady(void) {
    return gAudioDevice != nullptr && gAudioContext != nullptr;
}

void SetMasterVolume(float volume) {
    gMasterVolume = std::clamp(volume, 0.0f, 1.0f);
    if (IsAudioDeviceReady()) alListenerf(AL_GAIN, gMasterVolume);
}

float GetMasterVolume(void) {
    return gMasterVolume;
}

Wave LoadWave(const char* fileName) {
    int dataSize = 0;
    unsigned char* data = LoadFileData(fileName, &dataSize);
    Wave wave = LoadWaveFromMemory(GetFileExtension(fileName), data, dataSize);
    UnloadFileData(data);
    return wave;
}

Wave LoadWaveFromMemory(const char* fileType, const unsigned char* fileData, int dataSize) {
    if (!fileType || !fileData || dataSize <= 0) {
        TraceLog(LogLevel::Warn, "AUDIO", TextFormat("LoadWaveFromMemory: invalid input (fileType=%s, data=%p, size=%d)", fileType ? fileType : "<null>", static_cast<const void*>(fileData), dataSize));
        return {};
    }
    if (TextIsEqual(fileType, ".wav") || TextIsEqual(fileType, "wav") || TextIsEqual(fileType, ".WAV") || TextIsEqual(fileType, "WAV")) {
        TraceLog(LogLevel::Trace, "AUDIO", TextFormat("Loading WAV data (%d bytes)", dataSize));
        return LoadWaveFromWavData(fileData, dataSize);
    }
    TraceLog(LogLevel::Warn, "AUDIO", TextFormat("Unsupported wave format: %s", fileType));
    return {};
}

float* LoadWaveSamples(Wave wave) {
    if (!wave.data || wave.frameCount == 0 || wave.channels == 0) return nullptr;
    const size_t count = static_cast<size_t>(wave.frameCount) * wave.channels;
    float* samples = static_cast<float*>(MemAlloc(count * sizeof(float)));
    if (!samples) return nullptr;

    if (wave.sampleSize == 8) {
        const auto* src = static_cast<const unsigned char*>(wave.data);
        for (size_t i = 0; i < count; ++i) samples[i] = (static_cast<float>(src[i]) - 128.0f) / 128.0f;
    } else if (wave.sampleSize == 16) {
        const auto* src = static_cast<const int16_t*>(wave.data);
        for (size_t i = 0; i < count; ++i) samples[i] = static_cast<float>(src[i]) / 32768.0f;
    } else if (wave.sampleSize == 32) {
        std::memcpy(samples, wave.data, count * sizeof(float));
    } else {
        MemFree(samples);
        return nullptr;
    }
    return samples;
}

Sound LoadSound(const char* fileName) {
    TraceLog(LogLevel::Trace, "AUDIO", TextFormat("Loading sound file: %s", fileName ? fileName : "<null>"));
    Wave wave = LoadWave(fileName);
    Sound sound = LoadSoundFromWave(wave);
    UnloadWave(wave);
    if (sound.stream.buffer == nullptr) {
        TraceLog(LogLevel::Warn, "AUDIO", TextFormat("Failed to load sound file: %s", fileName ? fileName : "<null>"));
    } else {
        TraceLog(LogLevel::Info, "AUDIO", TextFormat("Sound loaded successfully: %s (%u frames)", fileName ? fileName : "<null>", sound.frameCount));
    }
    return sound;
}

Sound LoadSoundFromWave(Wave wave) {
    if (wave.data == nullptr || wave.frameCount == 0) {
        TraceLog(LogLevel::Warn, "AUDIO", "LoadSoundFromWave: empty wave data");
        return {};
    }
    TraceLog(LogLevel::Trace, "AUDIO", TextFormat("Creating sound buffer: %u frames, %uHz, %u-bit, %u channels", wave.frameCount, wave.sampleRate, wave.sampleSize, wave.channels));
    return MakeSoundFromWave(wave, false);
}

Sound LoadSoundAlias(Sound source) {
    Sound alias{};
    if (!source.stream.buffer || !EnsureAudioDevice()) return alias;
    ALuint src = 0;
    alGenSources(1, &src);
    alSourcei(src, AL_BUFFER, static_cast<ALint>(source.stream.buffer->buffer));
    auto* buffer = new rAudioBuffer{*source.stream.buffer};
    buffer->source = src;
    buffer->ownsBuffer = false;
    alias.stream = source.stream;
    alias.stream.buffer = buffer;
    alias.frameCount = source.frameCount;
    return alias;
}

void UpdateSound(Sound sound, const void* data, int frameCount) {
    if (!sound.stream.buffer || !data || frameCount <= 0) return;
    const ALenum format = GetOpenALFormat(sound.stream.sampleSize, sound.stream.channels);
    if (format == 0) return;
    const size_t byteCount = static_cast<size_t>(frameCount) * GetAudioFrameSize(sound.stream.sampleSize, sound.stream.channels);
    alSourceStop(sound.stream.buffer->source);
    alBufferData(sound.stream.buffer->buffer, format, data, static_cast<ALsizei>(byteCount), static_cast<ALsizei>(sound.stream.sampleRate));
    sound.stream.buffer->frameCount = static_cast<unsigned int>(frameCount);
}

void UnloadWave(Wave wave) {
    if (wave.data) MemFree(wave.data);
}

void UnloadSound(Sound sound) {
    TraceLog(LogLevel::Trace, "AUDIO", TextFormat("Unloading sound: buffer=%p", static_cast<void*>(sound.stream.buffer)));
    DestroyAudioBuffer(sound.stream.buffer, sound.stream.buffer ? sound.stream.buffer->ownsBuffer : false);
}

void UnloadSoundAlias(Sound alias) {
    DestroyAudioBuffer(alias.stream.buffer, false);
}

bool ExportWave(Wave wave, const char* fileName) {
    if (!wave.data || !fileName) return false;
    const size_t dataBytes = static_cast<size_t>(wave.frameCount) * GetAudioFrameSize(wave.sampleSize, wave.channels);
    if (dataBytes == 0 || dataBytes > UINT32_MAX) return false;
    std::ofstream out(fileName, std::ios::binary);
    if (!out) return false;
    out.write("RIFF", 4);
    WriteLE32(out, static_cast<uint32_t>(36 + dataBytes));
    out.write("WAVEfmt ", 8);
    WriteLE32(out, 16);
    WriteLE16(out, wave.sampleSize == 32 ? 3 : 1);
    WriteLE16(out, static_cast<uint16_t>(wave.channels));
    WriteLE32(out, wave.sampleRate);
    WriteLE32(out, static_cast<uint32_t>(wave.sampleRate * GetAudioFrameSize(wave.sampleSize, wave.channels)));
    WriteLE16(out, static_cast<uint16_t>(GetAudioFrameSize(wave.sampleSize, wave.channels)));
    WriteLE16(out, static_cast<uint16_t>(wave.sampleSize));
    out.write("data", 4);
    WriteLE32(out, static_cast<uint32_t>(dataBytes));
    out.write(static_cast<const char*>(wave.data), static_cast<std::streamsize>(dataBytes));
    return out.good();
}

bool ExportWaveAsCode(Wave wave, const char* fileName) {
    const size_t dataBytes = static_cast<size_t>(wave.frameCount) * GetAudioFrameSize(wave.sampleSize, wave.channels);
    return ExportDataAsCode(static_cast<const unsigned char*>(wave.data), static_cast<int>(dataBytes), fileName);
}

Wave WaveCopy(Wave wave) {
    Wave copy = wave;
    const size_t bytes = static_cast<size_t>(wave.frameCount) * GetAudioFrameSize(wave.sampleSize, wave.channels);
    copy.data = nullptr;
    if (wave.data && bytes > 0) {
        copy.data = MemAlloc(bytes);
        if (copy.data) std::memcpy(copy.data, wave.data, bytes);
    }
    return copy;
}

void WaveCrop(Wave* wave, int initFrame, int finalFrame) {
    if (!wave || !wave->data || wave->frameCount == 0) return;
    initFrame = std::clamp(initFrame, 0, static_cast<int>(wave->frameCount));
    finalFrame = std::clamp(finalFrame, initFrame, static_cast<int>(wave->frameCount));
    const size_t frameSize = GetAudioFrameSize(wave->sampleSize, wave->channels);
    const size_t frames = static_cast<size_t>(finalFrame - initFrame);
    void* data = MemAlloc(frames * frameSize);
    if (!data) return;
    std::memcpy(data, static_cast<unsigned char*>(wave->data) + static_cast<size_t>(initFrame) * frameSize, frames * frameSize);
    MemFree(wave->data);
    wave->data = data;
    wave->frameCount = static_cast<unsigned int>(frames);
}

void WaveFormat(Wave* wave, int sampleRate, int sampleSize, int channels) {
    if (!wave || !wave->data || sampleRate <= 0 || channels <= 0) return;
    if (sampleRate == static_cast<int>(wave->sampleRate) && sampleSize == static_cast<int>(wave->sampleSize) && channels == static_cast<int>(wave->channels)) return;
    float* samples = LoadWaveSamples(*wave);
    if (!samples) return;
    const size_t outCount = static_cast<size_t>(wave->frameCount) * static_cast<size_t>(channels);
    const size_t outBytes = outCount * static_cast<size_t>((sampleSize + 7) / 8);
    void* out = MemAlloc(outBytes);
    if (!out) { MemFree(samples); return; }
    for (unsigned int f = 0; f < wave->frameCount; ++f) {
        for (int ch = 0; ch < channels; ++ch) {
            const float value = samples[static_cast<size_t>(f) * wave->channels + static_cast<unsigned int>(std::min<int>(ch, wave->channels - 1))];
            const size_t idx = static_cast<size_t>(f) * channels + ch;
            if (sampleSize == 8) static_cast<unsigned char*>(out)[idx] = static_cast<unsigned char>(std::clamp(value * 127.0f + 128.0f, 0.0f, 255.0f));
            else if (sampleSize == 16) static_cast<int16_t*>(out)[idx] = static_cast<int16_t>(std::clamp(value, -1.0f, 1.0f) * 32767.0f);
            else if (sampleSize == 32) static_cast<float*>(out)[idx] = value;
        }
    }
    MemFree(samples);
    MemFree(wave->data);
    wave->data = out;
    wave->sampleRate = static_cast<unsigned int>(sampleRate);
    wave->sampleSize = static_cast<unsigned int>(sampleSize);
    wave->channels = static_cast<unsigned int>(channels);
}

Music LoadMusicStream(const char* fileName) {
    Music music{};
    music.stream = LoadSound(fileName).stream;
    if (music.stream.buffer) music.frameCount = music.stream.buffer->frameCount;
    return music;
}

Music LoadMusicStreamFromMemory(const char* fileType, const unsigned char* data, int dataSize) {
    Wave wave = LoadWaveFromMemory(fileType, data, dataSize);
    Music music{};
    Sound sound = LoadSoundFromWave(wave);
    music.stream = sound.stream;
    music.frameCount = sound.frameCount;
    UnloadWave(wave);
    return music;
}

void UnloadMusicStream(Music music) {
    UnloadAudioStream(music.stream);
}

bool IsMusicValid(Music music) {
    return IsAudioStreamValid(music.stream);
}

void PlayMusicStream(Music music) {
    PlayAudioStream(music.stream);
}

bool IsMusicStreamPlaying(Music music) {
    return IsAudioStreamPlaying(music.stream);
}

void UpdateMusicStream(Music music) {
    if (!music.stream.buffer) {
        return;
    }

    ALint state = 0;
    alGetSourcei(music.stream.buffer->source, AL_SOURCE_STATE, &state);
    if (state == AL_PLAYING || state == AL_PAUSED) {
        return;
    }
}

void StopMusicStream(Music music) {
    StopAudioStream(music.stream);
}

void PauseMusicStream(Music music) {
    PauseAudioStream(music.stream);
}

void ResumeMusicStream(Music music) {
    ResumeAudioStream(music.stream);
}

void SeekMusicStream(Music music, float position) {
    if (music.stream.buffer) {
        alSourcef(music.stream.buffer->source, AL_SEC_OFFSET, position);
    }
}

void SetMusicVolume(Music music, float volume) {
    SetAudioStreamVolume(music.stream, volume);
}

void SetMusicPitch(Music music, float pitch) {
    SetAudioStreamPitch(music.stream, pitch);
}

void SetMusicPan(Music music, float pan) {
    SetAudioStreamPan(music.stream, pan);
}

float GetMusicTimeLength(Music music) {
    return (music.stream.sampleRate == 0)
        ? 0.0f
        : static_cast<float>(music.frameCount) / static_cast<float>(music.stream.sampleRate);
}

float GetMusicTimePlayed(Music music) {
    float value = 0.0f;
    if (music.stream.buffer) {
        alGetSourcef(music.stream.buffer->source, AL_SEC_OFFSET, &value);
    }
    return value;
}

AudioStream LoadAudioStream(unsigned int sampleRate, unsigned int sampleSize, unsigned int channels) {
    AudioStream stream{};
    TraceLog(LogLevel::Trace, "AUDIO", TextFormat("Creating audio stream: %uHz, %u-bit, %u channels", sampleRate, sampleSize, channels));
    if (!EnsureAudioDevice() || GetOpenALFormat(sampleSize, channels) == 0) {
        TraceLog(LogLevel::Error, "AUDIO", TextFormat("Failed to create audio stream: invalid device or unsupported format (%uHz, %u-bit, %u channels)", sampleRate, sampleSize, channels));
        return stream;
    }

    auto* buffer = new rAudioBuffer{};
    alGenBuffers(1, &buffer->buffer);
    alGenSources(1, &buffer->source);
    buffer->sampleRate = sampleRate;
    buffer->sampleSize = sampleSize;
    buffer->channels = channels;
    buffer->streaming = true;
    stream.buffer = buffer;
    stream.sampleRate = sampleRate;
    stream.sampleSize = sampleSize;
    stream.channels = channels;
    TraceLog(LogLevel::Info, "AUDIO", TextFormat("Audio stream created successfully (%uHz, %u-bit, %u channels)", sampleRate, sampleSize, channels));
    return stream;
}

void UnloadAudioStream(AudioStream stream) {
    DestroyAudioBuffer(stream.buffer, stream.buffer ? stream.buffer->ownsBuffer : false);
}

bool IsAudioStreamValid(AudioStream stream) {
    return stream.buffer != nullptr && stream.buffer->source != 0;
}

bool IsAudioStreamPlaying(AudioStream stream) {
    ALint state = 0;
    if (stream.buffer) {
        alGetSourcei(stream.buffer->source, AL_SOURCE_STATE, &state);
    }
    return state == AL_PLAYING;
}

bool IsAudioStreamProcessed(AudioStream stream) {
    if (!stream.buffer) {
        return false;
    }

    ALint processed = 0;
    alGetSourcei(stream.buffer->source, AL_BUFFERS_QUEUED, &processed);
    return processed > 0;
}

void PlayAudioStream(AudioStream stream) {
    if (!stream.buffer) {
        TraceLog(LogLevel::Warn, "AUDIO", "PlayAudioStream: invalid stream");
        return;
    }
    TraceLog(LogLevel::Trace, "AUDIO", TextFormat("Playing audio stream (source=%u)", stream.buffer->source));
    alSourcePlay(stream.buffer->source);
}

void PauseAudioStream(AudioStream stream) {
    if (stream.buffer) {
        TraceLog(LogLevel::Trace, "AUDIO", TextFormat("Pausing audio stream (source=%u)", stream.buffer->source));
        alSourcePause(stream.buffer->source);
    }
}

void ResumeAudioStream(AudioStream stream) {
    PlayAudioStream(stream);
}

void StopAudioStream(AudioStream stream) {
    if (stream.buffer) {
        alSourceStop(stream.buffer->source);
    }
}

void UpdateAudioStream(AudioStream stream, const void* data, int frameCount) {
    if (!stream.buffer || !data || frameCount <= 0) {
        return;
    }

    if (stream.buffer->callback) {
        stream.buffer->callback(const_cast<void*>(data), static_cast<unsigned int>(frameCount));
    }

    for (AudioCallback processor : stream.buffer->processors) {
        if (processor) {
            processor(const_cast<void*>(data), static_cast<unsigned int>(frameCount));
        }
    }

    for (AudioCallback processor : gAudioMixedProcessors) {
        if (processor) {
            processor(const_cast<void*>(data), static_cast<unsigned int>(frameCount));
        }
    }

    const ALenum format = GetOpenALFormat(stream.sampleSize, stream.channels);
    const size_t bytes = static_cast<size_t>(frameCount) * GetAudioFrameSize(stream.sampleSize, stream.channels);
    alSourceStop(stream.buffer->source);
    alBufferData(stream.buffer->buffer, format, data, static_cast<ALsizei>(bytes), static_cast<ALsizei>(stream.sampleRate));
    alSourcei(stream.buffer->source, AL_BUFFER, static_cast<ALint>(stream.buffer->buffer));
}

void SetAudioStreamBufferSizeDefault(int size) {
    if (size > 0) {
        gAudioStreamBufferSizeDefault = size;
    }
}

void SetAudioStreamCallback(AudioStream stream, AudioCallback callback) {
    if (stream.buffer) {
        stream.buffer->callback = callback;
    }
}

void AttachAudioStreamProcessor(AudioStream stream, AudioCallback processor) {
    if (stream.buffer && processor) {
        stream.buffer->processors.push_back(processor);
    }
}

void DetachAudioStreamProcessor(AudioStream stream, AudioCallback processor) {
    if (!stream.buffer) {
        return;
    }

    auto& list = stream.buffer->processors;
    list.erase(std::remove(list.begin(), list.end(), processor), list.end());
}

void AttachAudioMixedProcessor(AudioCallback processor) {
    if (processor) {
        gAudioMixedProcessors.push_back(processor);
    }
}

void DetachAudioMixedProcessor(AudioCallback processor) {
    gAudioMixedProcessors.erase(
        std::remove(gAudioMixedProcessors.begin(), gAudioMixedProcessors.end(), processor),
        gAudioMixedProcessors.end()
    );
}

void SetAudioStreamVolume(AudioStream stream, float volume) {
    if (stream.buffer) {
        alSourcef(stream.buffer->source, AL_GAIN, std::max(0.0f, volume));
    }
}

void SetAudioStreamPitch(AudioStream stream, float pitch) {
    if (stream.buffer) {
        alSourcef(stream.buffer->source, AL_PITCH, std::max(0.01f, pitch));
    }
}

void SetAudioStreamPan(AudioStream stream, float pan) {
    if (stream.buffer) {
        alSource3f(stream.buffer->source, AL_POSITION, std::clamp(pan, -1.0f, 1.0f), 0.0f, 0.0f);
    }
}

void PlaySound(Sound sound) {
    PlayAudioStream(sound.stream);
}

void StopSound(Sound sound) {
    StopAudioStream(sound.stream);
}

void PauseSound(Sound sound) {
    PauseAudioStream(sound.stream);
}

void ResumeSound(Sound sound) {
    ResumeAudioStream(sound.stream);
}

bool IsSoundPlaying(Sound sound) {
    return IsAudioStreamPlaying(sound.stream);
}

void SetSoundVolume(Sound sound, float volume) {
    SetAudioStreamVolume(sound.stream, volume);
}

void SetSoundPitch(Sound sound, float pitch) {
    SetAudioStreamPitch(sound.stream, pitch);
}

void SetSoundPan(Sound sound, float pan) {
    SetAudioStreamPan(sound.stream, pan);
}

unsigned char* LoadFileData(const char* fileName, int* dataSize) {
    if (dataSize) *dataSize = 0;
    if (!fileName) return nullptr;
    if (gLoadFileDataCallback) return gLoadFileDataCallback(fileName, dataSize);
    std::ifstream in(fileName, std::ios::binary | std::ios::ate);
    if (!in) return nullptr;
    const std::streamsize size = in.tellg();
    if (size < 0 || size > std::numeric_limits<int>::max()) return nullptr;
    in.seekg(0, std::ios::beg);
    unsigned char* data = static_cast<unsigned char*>(MemAlloc(static_cast<size_t>(size) + 1));
    if (!data) return nullptr;
    if (size > 0 && !in.read(reinterpret_cast<char*>(data), size)) {
        MemFree(data);
        return nullptr;
    }
    data[size] = 0;
    if (dataSize) *dataSize = static_cast<int>(size);
    return data;
}

void UnloadFileData(unsigned char* data) {
    if (data) MemFree(data);
}

bool SaveFileData(const char* fileName, const void* data, int dataSize) {
    if (!fileName || (!data && dataSize > 0) || dataSize < 0) return false;
    if (gSaveFileDataCallback) return gSaveFileDataCallback(fileName, const_cast<void*>(data), dataSize);
    std::ofstream out(fileName, std::ios::binary);
    if (!out) return false;
    if (dataSize > 0) out.write(static_cast<const char*>(data), dataSize);
    return out.good();
}

bool ExportDataAsCode(const unsigned char* data, int dataSize, const char* fileName) {
    if (!data || dataSize < 0 || !fileName) return false;
    std::ofstream out(fileName);
    if (!out) return false;
    out << "static const unsigned char data[" << dataSize << "] = {";
    for (int i = 0; i < dataSize; ++i) {
        if ((i % 12) == 0) out << "\n    ";
        out << static_cast<unsigned int>(data[i]);
        if (i + 1 < dataSize) out << ", ";
    }
    out << "\n};\n";
    return out.good();
}

char* LoadFileText(const char* fileName) {
    if (!fileName) return nullptr;
    if (gLoadFileTextCallback) return gLoadFileTextCallback(fileName);
    int size = 0;
    unsigned char* data = LoadFileData(fileName, &size);
    if (!data) return nullptr;
    char* text = static_cast<char*>(MemAlloc(static_cast<size_t>(size) + 1));
    if (!text) {
        UnloadFileData(data);
        return nullptr;
    }
    std::memcpy(text, data, static_cast<size_t>(size));
    text[size] = '\0';
    UnloadFileData(data);
    return text;
}

void UnloadFileText(char* text) {
    if (text) MemFree(text);
}

bool SaveFileText(const char* fileName, const char* text) {
    if (!fileName || !text) return false;
    if (gSaveFileTextCallback) return gSaveFileTextCallback(fileName, const_cast<char*>(text));
    return SaveFileData(fileName, text, static_cast<int>(std::strlen(text)));
}

void SetLoadFileDataCallback(LoadFileDataCallback callback) {
    gLoadFileDataCallback = callback;
}

void SetSaveFileDataCallback(SaveFileDataCallback callback) {
    gSaveFileDataCallback = callback;
}

void SetLoadFileTextCallback(LoadFileTextCallback callback) {
    gLoadFileTextCallback = callback;
}
void SetSaveFileTextCallback(SaveFileTextCallback callback) {
    gSaveFileTextCallback = callback;
}

AutomationEventList LoadAutomationEventList(const char* fileName) {
    AutomationEventList list{};
    int dataSize = 0;
    unsigned char* data = LoadFileData(fileName, &dataSize);
    if (!data || dataSize < static_cast<int>(sizeof(unsigned int) * 2)) {
        UnloadFileData(data);
        return list;
    }
    const auto* words = reinterpret_cast<const unsigned int*>(data);
    list.capacity = words[0];
    list.count = words[1];
    const size_t bytes = static_cast<size_t>(list.count) * sizeof(AutomationEvent);
    if (list.count > 0 && static_cast<size_t>(dataSize) >= sizeof(unsigned int) * 2 + bytes) {
        list.events = static_cast<AutomationEvent*>(MemAlloc(bytes));
        if (list.events) std::memcpy(list.events, data + sizeof(unsigned int) * 2, bytes);
    }
    UnloadFileData(data);
    return list;
}

void UnloadAutomationEventList(AutomationEventList list) {
    if (list.events) MemFree(list.events);
}

bool ExportAutomationEventList(AutomationEventList list, const char* fileName) {
    if (!fileName) return false;
    std::ofstream out(fileName, std::ios::binary);
    if (!out) return false;
    WriteLE32(out, list.capacity);
    WriteLE32(out, list.count);
    if (list.events && list.count > 0) out.write(reinterpret_cast<const char*>(list.events), static_cast<std::streamsize>(list.count * sizeof(AutomationEvent)));
    return out.good();
}

void SetAutomationEventList(AutomationEventList* list) {
    gAutomationEventList = list;
}

void SetAutomationEventBaseFrame(int frame) {
    gAutomationBaseFrame = frame;
}

void StartAutomationEventRecording(void) {
    gAutomationRecording = true;

    if (gAutomationEventList == nullptr) {
        gAutomationEventList = &gDefaultAutomationEventList;
        gDefaultAutomationEventList = {};
        gDefaultAutomationEventList.capacity = 16;
        gDefaultAutomationEventList.events = static_cast<AutomationEvent*>(
            MemAlloc(sizeof(AutomationEvent) * gDefaultAutomationEventList.capacity)
        );
    }

    gAutomationBaseFrame = 0;
    gAutomationEventList->count = 0;
}

void StopAutomationEventRecording(void) {
    gAutomationRecording = false;

    if (gAutomationEventList == &gDefaultAutomationEventList && gAutomationEventList->events != nullptr) {
        gAutomationEventList->capacity = gAutomationEventList->count;
    }
}

void PlayAutomationEvent(AutomationEvent event) {
    if (!gAutomationRecording) {
        return;
    }

    if (gAutomationEventList == nullptr) {
        gAutomationEventList = &gDefaultAutomationEventList;
        gDefaultAutomationEventList = {};
        gDefaultAutomationEventList.capacity = 16;
        gDefaultAutomationEventList.events = static_cast<AutomationEvent*>(
            MemAlloc(sizeof(AutomationEvent) * gDefaultAutomationEventList.capacity)
        );
    }

    if (gAutomationEventList->events == nullptr) {
        gAutomationEventList->capacity = 16;
        gAutomationEventList->events = static_cast<AutomationEvent*>(
            MemAlloc(sizeof(AutomationEvent) * gAutomationEventList->capacity)
        );
    }

    if (gAutomationEventList->count >= gAutomationEventList->capacity) {
        const unsigned int newCapacity = gAutomationEventList->capacity == 0 ? 16u : gAutomationEventList->capacity * 2u;
        AutomationEvent* resized = static_cast<AutomationEvent*>(
            MemRealloc(gAutomationEventList->events, sizeof(AutomationEvent) * newCapacity)
        );

        if (!resized) {
            return;
        }

        gAutomationEventList->events = resized;
        gAutomationEventList->capacity = newCapacity;
    }

    event.frame = static_cast<unsigned int>(gAutomationBaseFrame + static_cast<int>(gAutomationEventList->count));
    gAutomationEventList->events[gAutomationEventList->count++] = event;
}

void BeginVrStereoMode(VrStereoConfig config) {
    gVrStereoEnabled = true;
    gCurrentVrStereoConfig = config;

    if (config.projection[0].m[0] == 0.0f &&
        config.projection[1].m[0] == 0.0f &&
        config.viewOffset[0].m[12] == 0.0f &&
        config.viewOffset[1].m[12] == 0.0f) {
        gCurrentVrStereoConfig = LoadVrStereoConfig({});
    }
}

void EndVrStereoMode(void) {
    gVrStereoEnabled = false;
    gCurrentVrStereoConfig = {};
}

VrStereoConfig LoadVrStereoConfig(VrDeviceInfo device) {
    VrStereoConfig config{};

    const float ipd = device.interpupillaryDistance > 0.0f
        ? device.interpupillaryDistance
        : 0.064f;
    const float eyeOffset = ipd * 0.5f;
    const float screenWidth = device.hScreenSize > 0.0f ? device.hScreenSize : 0.14f;
    const float screenHeight = device.vScreenSize > 0.0f ? device.vScreenSize : 0.08f;
    const float aspect = (device.hResolution > 0 && device.vResolution > 0)
        ? static_cast<float>(device.hResolution) / static_cast<float>(device.vResolution)
        : 1.7777778f;
    const float fov = 60.0f;
    const float nearPlane = 0.01f;
    const float farPlane = 1000.0f;

    config.projection[0] = MatrixPerspective(fov, aspect, nearPlane, farPlane);
    config.projection[1] = MatrixPerspective(fov, aspect, nearPlane, farPlane);
    config.viewOffset[0] = Mat4::translation(-eyeOffset, 0.0f, 0.0f);
    config.viewOffset[1] = Mat4::translation( eyeOffset, 0.0f, 0.0f);

    config.leftLensCenter[0] = -eyeOffset;
    config.leftLensCenter[1] = 0.0f;
    config.rightLensCenter[0] = eyeOffset;
    config.rightLensCenter[1] = 0.0f;

    config.leftScreenCenter[0] = -screenWidth * 0.5f;
    config.leftScreenCenter[1] = 0.0f;
    config.rightScreenCenter[0] = screenWidth * 0.5f;
    config.rightScreenCenter[1] = 0.0f;

    config.scale[0] = 1.0f;
    config.scale[1] = 1.0f;
    config.scaleIn[0] = 1.0f;
    config.scaleIn[1] = 1.0f;

    if (screenWidth > 0.0f) {
        config.scale[0] = screenWidth / std::max(0.0001f, screenWidth);
        config.scaleIn[0] = 1.0f / std::max(0.0001f, config.scale[0]);
    }
    if (screenHeight > 0.0f) {
        config.scale[1] = screenHeight / std::max(0.0001f, screenHeight);
        config.scaleIn[1] = 1.0f / std::max(0.0001f, config.scale[1]);
    }

    return config;
}

void UnloadVrStereoConfig(VrStereoConfig config) {
    const bool matchesActiveConfig =
        gCurrentVrStereoConfig.projection[0].m[0] == config.projection[0].m[0] &&
        gCurrentVrStereoConfig.projection[1].m[0] == config.projection[1].m[0] &&
        gCurrentVrStereoConfig.viewOffset[0].m[12] == config.viewOffset[0].m[12] &&
        gCurrentVrStereoConfig.viewOffset[1].m[12] == config.viewOffset[1].m[12];

    if (matchesActiveConfig || config.projection[0].m[0] == 0.0f && config.projection[1].m[0] == 0.0f) {
        gCurrentVrStereoConfig = {};
        gVrStereoEnabled = false;
    }
}

bool IsKeyDown(KeyboardKey key) {
    EnsureInitialized();
    const auto i = static_cast<std::size_t>(key);
    return i < gWin.currentKeys.size() ? gWin.currentKeys[i] : false;
}

bool IsKeyPressed(KeyboardKey key) {
    EnsureInitialized();
    const auto i = static_cast<std::size_t>(key);
    if (i >= gWin.currentKeys.size()) return false;
    return gWin.currentKeys[i] && !gWin.previousKeys[i];
}

bool IsKeyReleased(KeyboardKey key) {
    EnsureInitialized();
    const auto i = static_cast<std::size_t>(key);
    if (i >= gWin.currentKeys.size()) return false;
    return !gWin.currentKeys[i] && gWin.previousKeys[i];
}

bool IsKeyUp(KeyboardKey key) {
    return !IsKeyDown(key);
}

int GetKeyPressed() {
    int k = gLastKeyPressed;
    gLastKeyPressed = 0;
    return k;
}

int GetCharPressed() {
    int c = gLastCharPressed;
    gLastCharPressed = 0;
    return c;
}

void SetExitKey(KeyboardKey key) {
    gExitKey = key;
}

bool IsMouseButtonDown(MouseButton button) {
    EnsureInitialized();
    const auto i = static_cast<std::size_t>(button);
    return i < gWin.mouseButtons.size() ? gWin.mouseButtons[i] : false;
}

bool IsMouseButtonPressed(MouseButton button) {
    EnsureInitialized();
    const auto i = static_cast<std::size_t>(button);
    if (i >= gWin.mouseButtons.size()) {
        return false;
    }
    return gWin.mouseButtons[i] && !gWin.previousMouseButtons[i];
}

bool IsMouseButtonReleased(MouseButton button) {
    EnsureInitialized();
    const auto i = static_cast<std::size_t>(button);
    if (i >= gWin.mouseButtons.size()) {
        return false;
    }
    return !gWin.mouseButtons[i] && gWin.previousMouseButtons[i];
}

bool IsMouseButtonUp(MouseButton button) {
    return !IsMouseButtonDown(button);
}

Vec2 GetMousePosition() {
    EnsureInitialized();
    return gWin.mousePosition;
}

Vec2 GetMouseWheelMoveV() {
    EnsureInitialized();
    return gWin.mouseWheel;
}

float GetMouseWheelMove() {
    EnsureInitialized();
    return gWin.mouseWheel.y;
}

Vec2 GetMouseDelta() {
    Vec2 delta{ gWin.mousePosition.x - gMousePreviousPosition.x,
                gWin.mousePosition.y - gMousePreviousPosition.y };
    gMousePreviousPosition = gWin.mousePosition;
    return delta;
}

void SetMouseOffset(int offsetX, int offsetY) {
    gMouseOffset = Vec2{static_cast<float>(offsetX), static_cast<float>(offsetY)};
}

void SetMouseScale(float scaleX, float scaleY) {
    gMouseScale.x = (scaleX == 0.0f) ? 1.0f : scaleX;
    gMouseScale.y = (scaleY == 0.0f) ? 1.0f : scaleY;
}

void SetMousePosition(int x, int y) {
    if (gWin.window) {
        const float inverseScaleX = (gMouseScale.x == 0.0f) ? 1.0f : gMouseScale.x;
        const float inverseScaleY = (gMouseScale.y == 0.0f) ? 1.0f : gMouseScale.y;
        const float sdlX = (static_cast<float>(x) / inverseScaleX) - gMouseOffset.x;
        const float sdlY = (static_cast<float>(y) / inverseScaleY) - gMouseOffset.y;
        SDL_WarpMouseInWindow(gWin.window, sdlX, sdlY);
        gWin.mousePosition = Vec2{static_cast<float>(x), static_cast<float>(y)};
        gMousePreviousPosition = gWin.mousePosition;
    }
}

void DisableCursor() {
    if (gWin.window) {
        SDL_HideCursor();
        gCursorHidden = true;
    }
}

void EnableCursor() {
    if (gWin.window) {
        SDL_ShowCursor();
        gCursorHidden = false;
    }
}

bool IsCursorHidden() {
    return gCursorHidden;
}

void ShowCursor() {
    if (gWin.window) {
        SDL_ShowCursor();
        gCursorHidden = false;
    }
}

void HideCursor() {
    if (gWin.window) {
        SDL_HideCursor();
        gCursorHidden = true;
    }
}

bool IsCursorOnScreen() {
    if (!gWin.window) {
        return false;
    }

    float mx = 0.0f, my = 0.0f;
    SDL_GetGlobalMouseState(&mx, &my);
    int wx = 0, wy = 0, ww = 0, wh = 0;
    SDL_GetWindowPosition(gWin.window, &wx, &wy);
    SDL_GetWindowSize(gWin.window, &ww, &wh);
    return (mx >= wx && mx <= (wx + ww) && my >= wy && my <= (wy + wh));
}

void SetMouseCursor(MouseCursor cursor) {
    if (!gWin.window) {
        return;
    }

    SDL_SystemCursor sdl;
    switch (cursor) {
        case MouseCursor::Ibeam:
            sdl = SDL_SYSTEM_CURSOR_TEXT;
            break;
        case MouseCursor::Crosshair:
            sdl = SDL_SYSTEM_CURSOR_CROSSHAIR;
            break;
        case MouseCursor::PointingHand:
            sdl = SDL_SYSTEM_CURSOR_POINTER;
            break;
        case MouseCursor::ResizeEW:
            sdl = SDL_SYSTEM_CURSOR_EW_RESIZE;
            break;
        case MouseCursor::ResizeNS:
            sdl = SDL_SYSTEM_CURSOR_NS_RESIZE;
            break;
        case MouseCursor::ResizeNWSE:
            sdl = SDL_SYSTEM_CURSOR_NWSE_RESIZE;
            break;
        case MouseCursor::ResizeNESW:
            sdl = SDL_SYSTEM_CURSOR_NESW_RESIZE;
            break;
        case MouseCursor::ResizeAll:
            sdl = SDL_SYSTEM_CURSOR_MOVE;
            break;
        case MouseCursor::NotAllowed:
            sdl = SDL_SYSTEM_CURSOR_NOT_ALLOWED;
            break;
        default:
            sdl = SDL_SYSTEM_CURSOR_DEFAULT;
            break;
    }

    SDL_Cursor* c = SDL_CreateSystemCursor(sdl);
    if (c) {
        SDL_SetCursor(c);
        SDL_DestroyCursor(c);
    }
}

static SDL_Gamepad* OpenGamepadByIndex(int gamepad) {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids || gamepad < 0 || gamepad >= count) {
        SDL_free(ids);
        return nullptr;
    }

    SDL_Gamepad* result = SDL_OpenGamepad(ids[gamepad]);
    SDL_free(ids);
    return result;
}

static SDL_GamepadButton ToSDLGamepadButton(int button) {
    switch (button) {
        case GAMEPAD_BUTTON_LEFT_FACE_UP: return SDL_GAMEPAD_BUTTON_DPAD_UP;
        case GAMEPAD_BUTTON_LEFT_FACE_RIGHT: return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
        case GAMEPAD_BUTTON_LEFT_FACE_DOWN: return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
        case GAMEPAD_BUTTON_LEFT_FACE_LEFT: return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
        case GAMEPAD_BUTTON_RIGHT_FACE_UP: return SDL_GAMEPAD_BUTTON_NORTH;
        case GAMEPAD_BUTTON_RIGHT_FACE_RIGHT: return SDL_GAMEPAD_BUTTON_EAST;
        case GAMEPAD_BUTTON_RIGHT_FACE_DOWN: return SDL_GAMEPAD_BUTTON_SOUTH;
        case GAMEPAD_BUTTON_RIGHT_FACE_LEFT: return SDL_GAMEPAD_BUTTON_WEST;
        case GAMEPAD_BUTTON_LEFT_TRIGGER_1: return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
        case GAMEPAD_BUTTON_RIGHT_TRIGGER_1: return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
        case GAMEPAD_BUTTON_MIDDLE_LEFT: return SDL_GAMEPAD_BUTTON_BACK;
        case GAMEPAD_BUTTON_MIDDLE: return SDL_GAMEPAD_BUTTON_GUIDE;
        case GAMEPAD_BUTTON_MIDDLE_RIGHT: return SDL_GAMEPAD_BUTTON_START;
        case GAMEPAD_BUTTON_LEFT_THUMB: return SDL_GAMEPAD_BUTTON_LEFT_STICK;
        case GAMEPAD_BUTTON_RIGHT_THUMB: return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
        default: return SDL_GAMEPAD_BUTTON_INVALID;
    }
}

int GetGamepadCount() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    SDL_free(ids);
    return count;
}

bool IsGamepadAvailable(int gamepad) {
    SDL_Gamepad* controller = OpenGamepadByIndex(gamepad);
    if (!controller) return false;
    SDL_CloseGamepad(controller);
    return true;
}

const char* GetGamepadName(int gamepad) {
    SDL_Gamepad* controller = OpenGamepadByIndex(gamepad);
    if (!controller) return "UNKNOWN";
    const char* n = SDL_GetGamepadName(controller);
    SDL_CloseGamepad(controller);
    return n ? n : "UNKNOWN";
}

float GetGamepadAxisMovement(int gamepad, int axis) {
    SDL_Gamepad* controller = OpenGamepadByIndex(gamepad);
    if (!controller || axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT) {
        if (controller) SDL_CloseGamepad(controller);
        return 0.0f;
    }

    float value = static_cast<float>(SDL_GetGamepadAxis(
        controller, static_cast<SDL_GamepadAxis>(axis))) / 32767.0f;
    SDL_CloseGamepad(controller);

    const float deadZone = GetGamepadAxisDeadZone(gamepad, static_cast<GamepadAxis>(axis));
    if (std::fabs(value) <= deadZone) return 0.0f;
    const float sign = value < 0.0f ? -1.0f : 1.0f;
    return sign * ((std::fabs(value) - deadZone) / (1.0f - deadZone));
}

bool IsGamepadButtonPressed(int gamepad, int button) {
    if (gamepad < 0 || gamepad >= static_cast<int>(gWin.gamepadPressed.size()) ||
        button <= GAMEPAD_BUTTON_UNKNOWN || button >= GAMEPAD_BUTTON_COUNT) {
        return false;
    }
    return gWin.gamepadPressed[static_cast<std::size_t>(gamepad)][static_cast<std::size_t>(button)];
}

bool IsGamepadButtonDown(int gamepad, int button) {
    if (button <= GAMEPAD_BUTTON_UNKNOWN || button >= GAMEPAD_BUTTON_COUNT) return false;
    const SDL_GamepadButton sdlButton = ToSDLGamepadButton(button);
    if (sdlButton == SDL_GAMEPAD_BUTTON_INVALID) return false;
    SDL_Gamepad* controller = OpenGamepadByIndex(gamepad);
    if (!controller) return false;
    const bool pressed = SDL_GetGamepadButton(controller, sdlButton);
    SDL_CloseGamepad(controller);
    return pressed;
}

bool IsGamepadButtonUp(int gamepad, int button) {
    return !IsGamepadButtonDown(gamepad, button);
}

bool IsGamepadButtonReleased(int gamepad, int button) {
    if (gamepad < 0 || gamepad >= static_cast<int>(gWin.gamepadReleased.size()) ||
        button <= GAMEPAD_BUTTON_UNKNOWN || button >= GAMEPAD_BUTTON_COUNT) {
        return false;
    }
    return gWin.gamepadReleased[static_cast<std::size_t>(gamepad)][static_cast<std::size_t>(button)];
}

int GetGamepadButtonPressed() {
    const int result = gWin.lastGamepadButtonPressed;
    gWin.lastGamepadButtonPressed = -1;
    return result;
}

int GetGamepadAxisCount(int gamepad) {
    SDL_Gamepad* controller = OpenGamepadByIndex(gamepad);
    if (!controller) return 0;
    SDL_CloseGamepad(controller);
    return SDL_GAMEPAD_AXIS_COUNT;
}

bool SetGamepadAxisDeadZone(int gamepad, GamepadAxis axis, float deadZone) {
    if (gamepad < 0 || gamepad >= static_cast<int>(gGamepadDeadZones.size()) ||
        axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT ||
        !std::isfinite(deadZone) || deadZone < 0.0f || deadZone >= 1.0f) {
        return false;
    }
    gGamepadDeadZones[static_cast<std::size_t>(gamepad)][static_cast<std::size_t>(axis)] = deadZone;
    return true;
}

float GetGamepadAxisDeadZone(int gamepad, GamepadAxis axis) {
    if (gamepad < 0 || gamepad >= static_cast<int>(gGamepadDeadZones.size()) ||
        axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT) {
        return 0.0f;
    }
    return gGamepadDeadZones[static_cast<std::size_t>(gamepad)][static_cast<std::size_t>(axis)];
}

void SetGamepadVibration(int gamepad, float lowFrequency, float highFrequency, float durationSeconds) {
    SDL_Gamepad* controller = OpenGamepadByIndex(gamepad);
    if (!controller || !std::isfinite(lowFrequency) || !std::isfinite(highFrequency) ||
        !std::isfinite(durationSeconds) || durationSeconds < 0.0f) {
        if (controller) SDL_CloseGamepad(controller);
        return;
    }
    const auto toRumble = [](float value) {
        return static_cast<Uint16>(std::lround(Clamp(value, 0.0f, 1.0f) * 65535.0f));
    };
    const auto durationMs = static_cast<Uint32>(std::lround(durationSeconds * 1000.0f));
    SDL_RumbleGamepad(controller, toRumble(lowFrequency), toRumble(highFrequency), durationMs);
    SDL_CloseGamepad(controller);
}

int SetGamepadMappings(const char* mappings) {
    return mappings ? SDL_AddGamepadMapping(mappings) : -1;
}

bool AddGamepadMapping(const char* mapping) {
    return mapping && SDL_AddGamepadMapping(mapping) >= 0;
}

const char* GetGamepadMapping(int gamepad) {
    SDL_Gamepad* controller = OpenGamepadByIndex(gamepad);
    if (!controller) return "";

    char* mapping = SDL_GetGamepadMapping(controller);
    SDL_CloseGamepad(controller);
    if (!mapping) return "";

    thread_local std::string result;
    result = mapping;
    SDL_free(mapping);
    return result.c_str();
}

static Mat4 BuildTransform(const Vec3& position, const Vec3& axis, float angle, const Vec3& scale) {
    Mat4 translation = Mat4::translation(position.x, position.y, position.z);
    Mat4 scaleMat = Mat4::scale(scale.x, scale.y, scale.z);
    Mat4 rotation = Mat4::identity();
    if ((axis.x != 0.0f || axis.y != 0.0f || axis.z != 0.0f) && angle != 0.0f) {
        Vec3 n = axis.normalized();
        float c = std::cos(angle);
        float s = std::sin(angle);
        float t = 1.0f - c;

        rotation.m[0] = n.x * n.x * t + c;
        rotation.m[1] = n.x * n.y * t + n.z * s;
        rotation.m[2] = n.x * n.z * t - n.y * s;
        rotation.m[3] = 0.0f;

        rotation.m[4] = n.x * n.y * t - n.z * s;
        rotation.m[5] = n.y * n.y * t + c;
        rotation.m[6] = n.y * n.z * t + n.x * s;
        rotation.m[7] = 0.0f;

        rotation.m[8] = n.x * n.z * t + n.y * s;
        rotation.m[9] = n.y * n.z * t - n.x * s;
        rotation.m[10] = n.z * n.z * t + c;
        rotation.m[11] = 0.0f;

        rotation.m[12] = 0.0f;
        rotation.m[13] = 0.0f;
        rotation.m[14] = 0.0f;
        rotation.m[15] = 1.0f;
    }

    return translation * rotation * scaleMat;
}

static Vec3 TransformPoint(const Mat4& transform, const Vec3& point) {
    return Vec3{
        transform.m[0] * point.x + transform.m[4] * point.y + transform.m[8]  * point.z + transform.m[12],
        transform.m[1] * point.x + transform.m[5] * point.y + transform.m[9]  * point.z + transform.m[13],
        transform.m[2] * point.x + transform.m[6] * point.y + transform.m[10] * point.z + transform.m[14]
    };
}

static void DrawModelWireframe(const Model& model, const Mat4& transform, Color color) {
    if (!model.meshes) return;

    for (int i = 0; i < model.meshCount; ++i) {
        const Mesh& mesh = model.meshes[i];
        if (!mesh.vertices) continue;

        const bool hasIndices = mesh.indices != nullptr;
        const int triangleCount = mesh.triangleCount;

        for (int t = 0; t < triangleCount; ++t) {
            int idx0 = hasIndices ? mesh.indices[t * 3 + 0] : t * 3 + 0;
            int idx1 = hasIndices ? mesh.indices[t * 3 + 1] : t * 3 + 1;
            int idx2 = hasIndices ? mesh.indices[t * 3 + 2] : t * 3 + 2;

            Vec3 v0 = TransformPoint(transform, Vec3{
                mesh.vertices[idx0 * 3 + 0],
                mesh.vertices[idx0 * 3 + 1],
                mesh.vertices[idx0 * 3 + 2]
            });
            Vec3 v1 = TransformPoint(transform, Vec3{
                mesh.vertices[idx1 * 3 + 0],
                mesh.vertices[idx1 * 3 + 1],
                mesh.vertices[idx1 * 3 + 2]
            });
            Vec3 v2 = TransformPoint(transform, Vec3{
                mesh.vertices[idx2 * 3 + 0],
                mesh.vertices[idx2 * 3 + 1],
                mesh.vertices[idx2 * 3 + 2]
            });

            gRenderer.DrawLine3D(v0, v1, color);
            gRenderer.DrawLine3D(v1, v2, color);
            gRenderer.DrawLine3D(v2, v0, color);
        }
    }
}

void BeginDrawing()
{
    EnsureInitialized();
    gRenderer.BeginDrawing();
}

void EndDrawing()
{
    EnsureInitialized();
    gRenderer.EndDrawing();
}

void ClearBackground(Color color)
{
    EnsureInitialized();
    gRenderer.ClearBackground(color);
}

void DrawRectangle(float x, float y, float w, float h, Color c)
{
    gRenderer.DrawRectangle(x, y, w, h, c);
}

void DrawRectangle(const Rectangle& r, Color c)
{
    gRenderer.DrawRectangle(r, c);
}

void DrawRectangleV(Vec2 pos, Vec2 size, Color c)
{
    gRenderer.DrawRectangleV(pos, size, c);
}

void DrawRectangleLines(int x, int y, int w, int h, Color c)
{
    const Rectangle rect = Rectangle{ static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h) };
    gRenderer.DrawRectangleLines(rect, 1.0f, c);
}

void DrawRectangleRounded(Rectangle r, float rn, int seg, Color c)
{
    gRenderer.DrawRectangleRounded(r, rn, seg, c);
}

void DrawCircle(float cx, float cy, float radius, Color c)
{
    gRenderer.DrawCircle(cx, cy, radius, c);
}

void DrawCircleLines(float cx, float cy, float radius, Color c)
{
    gRenderer.DrawCircleLines(cx, cy, radius, c);
}

void DrawEllipse(float cx, float cy, float rh, float rv, Color c)
{
    gRenderer.DrawEllipse(cx, cy, rh, rv, c);
}

void DrawLine(float x1, float y1, float x2, float y2, Color c)
{
    gRenderer.DrawLine(x1, y1, x2, y2, c);
}

void DrawLineV(Vec2 start, Vec2 end, Color c)
{
    gRenderer.DrawLineV(start, end, c);
}

void DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color c)
{
    gRenderer.DrawTriangle(v1, v2, v3, c);
}

void DrawPoly(Vec2 center, int sides, float r, float rot, Color c)
{
    gRenderer.DrawPoly(center, sides, r, rot, c);
}

void DrawPixel(int posX, int posY, Color color)
{
    DrawRectangle(static_cast<float>(posX), static_cast<float>(posY), 1.0f, 1.0f, color);
}

void DrawPixelV(Vec2 position, Color color)
{
    DrawPixel(static_cast<int>(position.x), static_cast<int>(position.y), color);
}

void DrawLineEx(Vec2 startPos, Vec2 endPos, float thick, Color color)
{
    if (thick <= 1.0f)
    {
        DrawLineV(startPos, endPos, color);
        return;
    }

    const Vec2 dir = Vec2{ endPos.x - startPos.x, endPos.y - startPos.y };
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);

    if (len <= EPSILON)
    {
        DrawCircle(startPos.x, startPos.y, thick * 0.5f, color);
        return;
    }

    const Vec2 n = Vec2{
        -dir.y / len * thick * 0.5f,
        dir.x / len * thick * 0.5f
    };

    DrawTriangle(
        Vec2{ startPos.x + n.x, startPos.y + n.y },
        Vec2{ startPos.x - n.x, startPos.y - n.y },
        Vec2{ endPos.x + n.x, endPos.y + n.y },
        color
    );
    DrawTriangle(
        Vec2{ endPos.x + n.x, endPos.y + n.y },
        Vec2{ startPos.x - n.x, startPos.y - n.y },
        Vec2{ endPos.x - n.x, endPos.y - n.y },
        color
    );
}

void DrawLineStrip(const Vec2* points, int pointCount, Color color)
{
    if (!points || pointCount < 2)
    {
        return;
    }

    for (int i = 0; i + 1 < pointCount; ++i)
    {
        DrawLineV(points[i], points[i + 1], color);
    }
}

void DrawLineBezier(Vec2 startPos, Vec2 endPos, float thick, Color color)
{
    Vec2 last = startPos;

    for (int i = 1; i <= 24; ++i)
    {
        const float t = static_cast<float>(i) / 24.0f;
        const float u = 1.0f - t;
        const Vec2 control = Vec2{
            (startPos.x + endPos.x) * 0.5f,
            std::min(startPos.y, endPos.y) - std::fabs(endPos.x - startPos.x) * 0.25f
        };
        const Vec2 cur = Vec2{
            u * u * startPos.x + 2.0f * u * t * control.x + t * t * endPos.x,
            u * u * startPos.y + 2.0f * u * t * control.y + t * t * endPos.y
        };

        DrawLineEx(last, cur, thick, color);
        last = cur;
    }
}

void DrawLineDashed(Vec2 startPos, Vec2 endPos, int dashSize, int spaceSize, Color color)
{
    Vec2 dir = Vec2{ endPos.x - startPos.x, endPos.y - startPos.y };
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);

    if (len <= EPSILON || dashSize <= 0)
    {
        return;
    }

    dir.x /= len;
    dir.y /= len;

    const int step = std::max(1, dashSize + std::max(0, spaceSize));

    for (float d = 0.0f; d < len; d += static_cast<float>(step))
    {
        const float e = std::min(len, d + static_cast<float>(dashSize));
        const Vec2 a = Vec2{ startPos.x + dir.x * d, startPos.y + dir.y * d };
        const Vec2 b = Vec2{ startPos.x + dir.x * e, startPos.y + dir.y * e };
        DrawLineV(a, b, color);
    }
}

void DrawRectangleRec(Rectangle rec, Color color)
{
    DrawRectangle(rec, color);
}

void DrawRectanglePro(Rectangle rec, Vec2 origin, float rotation, Color color)
{
    const float rad = rotation * DEG2RAD;
    const float cs = std::cos(rad);
    const float sn = std::sin(rad);

    Vec2 corners[4] = {
        Vec2{ -origin.x, -origin.y },
        Vec2{ rec.width - origin.x, -origin.y },
        Vec2{ rec.width - origin.x, rec.height - origin.y },
        Vec2{ -origin.x, rec.height - origin.y }
    };

    for (auto& p : corners)
    {
        p = Vec2{
            rec.x + origin.x + p.x * cs - p.y * sn,
            rec.y + origin.y + p.x * sn + p.y * cs
        };
    }

    DrawTriangle(corners[0], corners[1], corners[2], color);
    DrawTriangle(corners[0], corners[2], corners[3], color);
}

void DrawRectangleGradientV(int posX, int posY, int width, int height, Color top, Color bottom)
{
    const int steps = std::max(1, height);

    for (int y = 0; y < steps; ++y)
    {
        const float t = static_cast<float>(y) / static_cast<float>(steps);
        const Color color = ColorLerp(top, bottom, t);
        DrawRectangle(static_cast<float>(posX), static_cast<float>(posY + y), static_cast<float>(width), 1.0f, color);
    }
}

void DrawRectangleGradientH(int posX, int posY, int width, int height, Color left, Color right)
{
    const int steps = std::max(1, width);

    for (int x = 0; x < steps; ++x)
    {
        const float t = static_cast<float>(x) / static_cast<float>(steps);
        const Color color = ColorLerp(left, right, t);
        DrawRectangle(static_cast<float>(posX + x), static_cast<float>(posY), 1.0f, static_cast<float>(height), color);
    }
}

void DrawRectangleGradientEx(Rectangle rec, Color col1, Color col2, Color col3, Color col4)
{
    const int stepsX = std::max(1, static_cast<int>(rec.width));
    const int stepsY = std::max(1, static_cast<int>(rec.height));

    for (int y = 0; y < stepsY; ++y)
    {
        const float ty = static_cast<float>(y) / static_cast<float>(stepsY);
        const Color topRow = ColorLerp(col1, col2, ty);
        const Color bottomRow = ColorLerp(col3, col4, ty);

        for (int x = 0; x < stepsX; ++x)
        {
            const float tx = static_cast<float>(x) / static_cast<float>(stepsX);
            const Color color = ColorLerp(topRow, bottomRow, tx);
            DrawRectangle(
                rec.x + static_cast<float>(x),
                rec.y + static_cast<float>(y),
                1.0f,
                1.0f,
                color
            );
        }
    }
}

void DrawRectangleLinesEx(Rectangle rec, float thick, Color color)
{
    DrawLineEx(Vec2{ rec.x, rec.y }, Vec2{ rec.x + rec.width, rec.y }, thick, color);
    DrawLineEx(Vec2{ rec.x + rec.width, rec.y }, Vec2{ rec.x + rec.width, rec.y + rec.height }, thick, color);
    DrawLineEx(Vec2{ rec.x + rec.width, rec.y + rec.height }, Vec2{ rec.x, rec.y + rec.height }, thick, color);
    DrawLineEx(Vec2{ rec.x, rec.y + rec.height }, Vec2{ rec.x, rec.y }, thick, color);
}

void DrawRectangleRoundedLines(Rectangle rec, float roundness, int segments, Color color)
{
    DrawRectangleRoundedLinesEx(rec, roundness, segments, 1.0f, color);
}

void DrawRectangleRoundedLinesEx(Rectangle rec, float roundness, int segments, float thick, Color color)
{
    const float radius = std::clamp(roundness, 0.0f, 1.0f) * std::min(rec.width, rec.height) * 0.5f;
    const int safeSegments = std::max(1, segments);

    if (radius <= 0.0f || rec.width <= 0.0f || rec.height <= 0.0f)
    {
        DrawRectangleLinesEx(rec, thick, color);
        return;
    }

    DrawLineEx(Vec2{ rec.x + radius, rec.y }, Vec2{ rec.x + rec.width - radius, rec.y }, thick, color);
    DrawLineEx(Vec2{ rec.x + rec.width, rec.y + radius }, Vec2{ rec.x + rec.width, rec.y + rec.height - radius }, thick, color);
    DrawLineEx(Vec2{ rec.x + rec.width - radius, rec.y + rec.height }, Vec2{ rec.x + radius, rec.y + rec.height }, thick, color);
    DrawLineEx(Vec2{ rec.x, rec.y + rec.height - radius }, Vec2{ rec.x, rec.y + radius }, thick, color);

    DrawCircleSectorLinesEx(Vec2{ rec.x + radius, rec.y + radius }, radius, 180.0f, 270.0f, safeSegments, thick, color);
    DrawCircleSectorLinesEx(Vec2{ rec.x + rec.width - radius, rec.y + radius }, radius, 270.0f, 360.0f, safeSegments, thick, color);
    DrawCircleSectorLinesEx(Vec2{ rec.x + rec.width - radius, rec.y + rec.height - radius }, radius, 0.0f, 90.0f, safeSegments, thick, color);
    DrawCircleSectorLinesEx(Vec2{ rec.x + radius, rec.y + rec.height - radius }, radius, 90.0f, 180.0f, safeSegments, thick, color);
}

void DrawCircleV(Vec2 center, float radius, Color color)
{
    DrawCircle(center.x, center.y, radius, color);
}

void DrawCircleGradient(Vec2 center, float radius, Color inner, Color outer)
{
    for (int i = static_cast<int>(radius); i > 0; --i)
    {
        const float t = static_cast<float>(i) / std::max(1.0f, radius);
        DrawCircleV(center, static_cast<float>(i), ColorLerp(inner, outer, t));
    }
}

void DrawCircleSector(Vec2 center, float radius, float startAngle, float endAngle, int segments, Color color)
{
    segments = std::max(3, segments);
    Vec2 last = Vec2{
        center.x + std::cos(startAngle * DEG2RAD) * radius,
        center.y + std::sin(startAngle * DEG2RAD) * radius
    };

    for (int i = 1; i <= segments; ++i)
    {
        const float a = (startAngle + (endAngle - startAngle) * static_cast<float>(i) / segments) * DEG2RAD;
        const Vec2 cur = Vec2{
            center.x + std::cos(a) * radius,
            center.y + std::sin(a) * radius
        };

        DrawTriangle(center, last, cur, color);
        last = cur;
    }
}

void DrawCircleSectorLines(Vec2 center, float radius, float startAngle, float endAngle, int segments, Color color)
{
    DrawCircleSectorLinesEx(center, radius, startAngle, endAngle, segments, 1.0f, color);
}

void DrawCircleSectorLinesEx(Vec2 center, float radius, float startAngle, float endAngle, int segments, float thick, Color color)
{
    segments = std::max(3, segments);
    Vec2 first = Vec2{
        center.x + std::cos(startAngle * DEG2RAD) * radius,
        center.y + std::sin(startAngle * DEG2RAD) * radius
    };
    Vec2 last = first;

    DrawLineEx(center, first, thick, color);

    for (int i = 1; i <= segments; ++i)
    {
        const float a = (startAngle + (endAngle - startAngle) * static_cast<float>(i) / segments) * DEG2RAD;
        const Vec2 cur = Vec2{
            center.x + std::cos(a) * radius,
            center.y + std::sin(a) * radius
        };

        DrawLineEx(last, cur, thick, color);
        last = cur;
    }

    DrawLineEx(center, last, thick, color);
}

void DrawCircleLinesV(Vec2 center, float radius, Color color)
{
    DrawCircleLines(center.x, center.y, radius, color);
}

void DrawCircleLinesEx(Vec2 center, float radius, float thick, Color color)
{
    const int loops = std::max(1, static_cast<int>(thick));

    for (int i = 0; i < loops; ++i)
    {
        DrawCircleLines(center.x, center.y, radius + static_cast<float>(i), color);
    }
}

void DrawEllipseV(Vec2 center, float radiusH, float radiusV, Color color)
{
    DrawEllipse(center.x, center.y, radiusH, radiusV, color);
}

void DrawEllipseLines(int centerX, int centerY, float radiusH, float radiusV, Color color)
{
    Vec2 last = Vec2{ static_cast<float>(centerX) + radiusH, static_cast<float>(centerY) };

    for (int i = 1; i <= 64; ++i)
    {
        const float a = 2.0f * PI * static_cast<float>(i) / 64.0f;
        const Vec2 cur = Vec2{
            static_cast<float>(centerX) + std::cos(a) * radiusH,
            static_cast<float>(centerY) + std::sin(a) * radiusV
        };

        DrawLineV(last, cur, color);
        last = cur;
    }
}

void DrawEllipseLinesV(Vec2 center, float radiusH, float radiusV, Color color)
{
    DrawEllipseLines(static_cast<int>(center.x), static_cast<int>(center.y), radiusH, radiusV, color);
}

void DrawEllipseLinesEx(Vec2 center, float radiusH, float radiusV, float thick, Color color)
{
    const int loops = std::max(1, static_cast<int>(thick));

    for (int i = 0; i < loops; ++i)
    {
        DrawEllipseLinesV(center, radiusH + static_cast<float>(i), radiusV + static_cast<float>(i), color);
    }
}

void DrawRing(Vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, int segments, Color color)
{
    DrawCircleSector(center, outerRadius, startAngle, endAngle, segments, color);
    DrawCircleSector(center, innerRadius, startAngle, endAngle, segments, BLANK);
}

void DrawRingLines(Vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, int segments, Color color)
{
    DrawRingLinesEx(center, innerRadius, outerRadius, startAngle, endAngle, segments, 1.0f, color);
}

void DrawRingLinesEx(Vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, int segments, float thick, Color color)
{
    DrawCircleSectorLinesEx(center, innerRadius, startAngle, endAngle, segments, thick, color);
    DrawCircleSectorLinesEx(center, outerRadius, startAngle, endAngle, segments, thick, color);
}

void DrawPolyLines(Vec2 center, int sides, float radius, float rotation, Color color)
{
    DrawPolyLinesEx(center, sides, radius, rotation, 1.0f, color);
}

void DrawPolyLinesEx(Vec2 center, int sides, float radius, float rotation, float thick, Color color)
{
    if (sides < 3)
    {
        return;
    }

    Vec2 prev{};
    Vec2 first{};

    for (int i = 0; i <= sides; ++i)
    {
        const float a = (rotation + 360.0f * static_cast<float>(i) / sides) * DEG2RAD;
        const Vec2 cur = Vec2{
            center.x + std::cos(a) * radius,
            center.y + std::sin(a) * radius
        };

        if (i == 0)
        {
            first = cur;
        }
        else
        {
            DrawLineEx(prev, cur, thick, color);
        }

        prev = cur;
    }

    DrawLineEx(prev, first, thick, color);
}

void DrawTriangleGradient(Vec2 v1, Vec2 v2, Vec2 v3, Color c1, Color c2, Color c3)
{
    const Vec2 center = (v1 + v2 + v3) * (1.0f / 3.0f);
    const Color top = c1;
    const Color left = c2;
    const Color right = c3;

    const int steps = 32;
    for (int i = 0; i < steps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float u = 1.0f - t;

        const Vec2 a = v1 * u + v2 * t;
        const Vec2 b = v1 * u + v3 * t;
        const Vec2 c = v2 * u + v3 * t;
        const Color ca = ColorLerp(top, left, t);
        const Color cb = ColorLerp(top, right, t);

        DrawLineEx(a, b, 1.0f, ColorLerp(ca, cb, 0.5f));
        DrawLineEx(c, b, 1.0f, ColorLerp(left, right, t));
        DrawLineEx(a, c, 1.0f, ColorLerp(top, left, 0.5f));
    }

    DrawTriangle(v1, v2, v3, ColorLerp(ColorLerp(c1, c2, 0.5f), c3, 0.5f));
}

void DrawTriangleLines(Vec2 v1, Vec2 v2, Vec2 v3, Color color)
{
    DrawTriangleLinesEx(v1, v2, v3, 1.0f, color);
}

void DrawTriangleLinesEx(Vec2 v1, Vec2 v2, Vec2 v3, float thick, Color color)
{
    DrawLineEx(v1, v2, thick, color);
    DrawLineEx(v2, v3, thick, color);
    DrawLineEx(v3, v1, thick, color);
}

void DrawTriangleFan(const Vec2* points, int pointCount, Color color)
{
    if (!points || pointCount < 3)
    {
        return;
    }

    for (int i = 1; i + 1 < pointCount; ++i)
    {
        DrawTriangle(points[0], points[i], points[i + 1], color);
    }
}

void DrawTriangleStrip(const Vec2* points, int pointCount, Color color)
{
    if (!points || pointCount < 3)
    {
        return;
    }

    for (int i = 0; i + 2 < pointCount; ++i)
    {
        if (i & 1)
        {
            DrawTriangle(points[i + 1], points[i], points[i + 2], color);
        }
        else
        {
            DrawTriangle(points[i], points[i + 1], points[i + 2], color);
        }
    }
}

void SetShapesTexture(Texture2D texture, Rectangle rec)
{
    gShapesTexture = texture;
    gShapesTextureRect = rec;
}

Texture2D GetShapesTexture(void)
{
    return gShapesTexture;
}

Rectangle GetShapesTextureRectangle(void)
{
    return gShapesTextureRect;
}

Vec2 GetSplinePointLinear(Vec2 startPos, Vec2 endPos, float t)
{
    return Lerp(startPos, endPos, t);
}

Vec2 GetSplinePointBezierQuadratic(Vec2 p1, Vec2 c2, Vec2 p3, float t)
{
    const float u = 1.0f - t;
    const float x = u * u * p1.x + 2.0f * u * t * c2.x + t * t * p3.x;
    const float y = u * u * p1.y + 2.0f * u * t * c2.y + t * t * p3.y;
    return {x, y};
}

Vec2 GetSplinePointBezierCubic(Vec2 p1, Vec2 c2, Vec2 c3, Vec2 p4, float t)
{
    const float u = 1.0f - t;
    const float x = u * u * u * p1.x + 3.0f * u * u * t * c2.x +
                   3.0f * u * t * t * c3.x + t * t * t * p4.x;
    const float y = u * u * u * p1.y + 3.0f * u * u * t * c2.y +
                   3.0f * u * t * t * c3.y + t * t * t * p4.y;
    return {x, y};
}

Vec2 GetSplinePointCatmullRom(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;

    const float x = 0.5f * ((2.0f * p2.x) +
        (-p1.x + p3.x) * t +
        (2.0f * p1.x - 5.0f * p2.x + 4.0f * p3.x - p4.x) * t2 +
        (-p1.x + 3.0f * p2.x - 3.0f * p3.x + p4.x) * t3);

    const float y = 0.5f * ((2.0f * p2.y) +
        (-p1.y + p3.y) * t +
        (2.0f * p1.y - 5.0f * p2.y + 4.0f * p3.y - p4.y) * t2 +
        (-p1.y + 3.0f * p2.y - 3.0f * p3.y + p4.y) * t3);

    return {x, y};
}

Vec2 GetSplinePointBasis(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;

    const float b1 = (-t3 + 3.0f * t2 - 3.0f * t + 1.0f) / 6.0f;
    const float b2 = (3.0f * t3 - 6.0f * t2 + 4.0f) / 6.0f;
    const float b3 = (-3.0f * t3 + 3.0f * t2 + 3.0f * t + 1.0f) / 6.0f;
    const float b4 = t3 / 6.0f;

    const float x = p1.x * b1 + p2.x * b2 + p3.x * b3 + p4.x * b4;
    const float y = p1.y * b1 + p2.y * b2 + p3.y * b3 + p4.y * b4;
    return {x, y};
}

void DrawSplineSegmentLinear(Vec2 p1, Vec2 p2, float thick, Color color)
{
    DrawLineEx(p1, p2, thick, color);
}

void DrawSplineSegmentBezierQuadratic(Vec2 p1, Vec2 c2, Vec2 p3, float thick, Color color)
{
    Vec2 last = p1;
    for (int i = 1; i <= 32; ++i) {
        const float t = static_cast<float>(i) / 32.0f;
        const Vec2 p = GetSplinePointBezierQuadratic(p1, c2, p3, t);
        DrawLineEx(last, p, thick, color);
        last = p;
    }
}

void DrawSplineSegmentBezierCubic(Vec2 p1, Vec2 c2, Vec2 c3, Vec2 p4, float thick, Color color)
{
    Vec2 last = p1;
    for (int i = 1; i <= 32; ++i) {
        const float t = static_cast<float>(i) / 32.0f;
        const Vec2 p = GetSplinePointBezierCubic(p1, c2, c3, p4, t);
        DrawLineEx(last, p, thick, color);
        last = p;
    }
}

void DrawSplineSegmentCatmullRom(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, float thick, Color color)
{
    Vec2 last = p2;
    for (int i = 1; i <= 32; ++i) {
        const float t = static_cast<float>(i) / 32.0f;
        const Vec2 p = GetSplinePointCatmullRom(p1, p2, p3, p4, t);
        DrawLineEx(last, p, thick, color);
        last = p;
    }
}

void DrawSplineSegmentBasis(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, float thick, Color color)
{
    Vec2 last = GetSplinePointBasis(p1, p2, p3, p4, 0.0f);
    for (int i = 1; i <= 32; ++i) {
        const float t = static_cast<float>(i) / 32.0f;
        const Vec2 p = GetSplinePointBasis(p1, p2, p3, p4, t);
        DrawLineEx(last, p, thick, color);
        last = p;
    }
}

void DrawSplineLinear(const Vec2* points, int pointCount, float thick, Color color)
{
    if (!points) {
        return;
    }

    for (int i = 0; i + 1 < pointCount; ++i) {
        DrawSplineSegmentLinear(points[i], points[i + 1], thick, color);
    }
}

void DrawSplineCatmullRom(const Vec2* points, int pointCount, float thick, Color color)
{
    if (!points) {
        return;
    }

    for (int i = 0; i + 3 < pointCount; ++i) {
        DrawSplineSegmentCatmullRom(points[i], points[i + 1], points[i + 2], points[i + 3], thick, color);
    }
}

void DrawSplineBasis(const Vec2* points, int pointCount, float thick, Color color)
{
    if (!points) {
        return;
    }

    for (int i = 0; i + 3 < pointCount; ++i) {
        DrawSplineSegmentBasis(points[i], points[i + 1], points[i + 2], points[i + 3], thick, color);
    }
}

void DrawSplineBezierQuadratic(const Vec2* points, int pointCount, float thick, Color color)
{
    if (!points) {
        return;
    }

    for (int i = 0; i + 2 < pointCount; i += 2) {
        DrawSplineSegmentBezierQuadratic(points[i], points[i + 1], points[i + 2], thick, color);
    }
}

void DrawSplineBezierCubic(const Vec2* points, int pointCount, float thick, Color color)
{
    if (!points) {
        return;
    }

    for (int i = 0; i + 3 < pointCount; i += 3) {
        DrawSplineSegmentBezierCubic(points[i], points[i + 1], points[i + 2], points[i + 3], thick, color);
    }
}

Texture2D LoadTexture(const char* filePath) {
    EnsureInitialized();
    ITexture it = gRenderer.LoadTexture(filePath);
    Texture2D t;
    t.id    = it.id;
    t.width = it.width;
    t.height= it.height;
    t.mipmaps = it.mipmaps;
    t.format  = it.format;
    t.valid = it.valid;
    return t;
}

Texture2D LoadTextureFromImage(Image image) {
    EnsureInitialized();
    ITexture it = gRenderer.LoadTextureFromImage(image);
    return Texture2D{ it.id, it.width, it.height, it.mipmaps, it.format, it.valid };
}

TextureCubemap LoadTextureCubemap(Image image, int layout) {
    const int cubemapLayout = layout;
    if (cubemapLayout < 0 || cubemapLayout > 3) {
        TraceLog(LogLevel::Warn, "TEXTURE",
                 TextFormat("LoadTextureCubemap: unsupported cubemap layout %d, falling back to default layout", cubemapLayout));
    }
    return LoadTextureFromImage(image);
}

void UpdateTexture(Texture2D texture, const void* pixels) {
    if (!pixels || texture.id == 0) return;
#if defined(QC_ENABLE_OPENGL)
    if (gRendererPtr && gRendererPtr->GetType() == RendererType::OpenGL) {
        glBindTexture(GL_TEXTURE_2D, texture.id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texture.width, texture.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
#else
    (void)texture;
#endif
}

void UpdateTextureRec(Texture2D texture, Rectangle rec, const void* pixels) {
    if (!pixels || texture.id == 0) return;
#if defined(QC_ENABLE_OPENGL)
    if (gRendererPtr && gRendererPtr->GetType() == RendererType::OpenGL) {
        glBindTexture(GL_TEXTURE_2D, texture.id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(rec.x), static_cast<GLint>(rec.y),
                        static_cast<GLsizei>(rec.width), static_cast<GLsizei>(rec.height),
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
#else
    (void)texture;
    (void)rec;
#endif
}

void GenTextureMipmaps(Texture2D* texture) {
    if (!texture || texture->id == 0) return;
#if defined(QC_ENABLE_OPENGL)
    if (gRendererPtr && gRendererPtr->GetType() == RendererType::OpenGL) {
        glBindTexture(GL_TEXTURE_2D, texture->id);
        glGenerateMipmap(GL_TEXTURE_2D);
        texture->mipmaps = 1 + static_cast<int>(std::floor(std::log2(static_cast<float>(std::max(texture->width, texture->height)))));
    }
#endif
}

void UnloadTexture(Texture2D texture) {
    ITexture it{ texture.id, texture.width, texture.height, texture.mipmaps, texture.format, texture.valid };
    gRenderer.UnloadTexture(it);
}

bool IsTextureValid(Texture2D texture) {
    ITexture it{ texture.id, texture.width, texture.height, texture.mipmaps, texture.format, texture.valid };
    return gRenderer.isTextureValid(it);
}

bool IsTextureReady(Texture2D texture) { return IsTextureValid(texture); }

void DrawTexture(const Texture2D& tex, float x, float y, Color tint) {
    ITexture it{ tex.id, tex.width, tex.height, tex.mipmaps, tex.format, tex.valid };
    gRenderer.DrawTexture(it, x, y, tint);
}

void DrawTextureV(Texture2D tex, Vec2 pos, Color tint) {
    ITexture it{ tex.id, tex.width, tex.height, tex.mipmaps, tex.format, tex.valid };
    gRenderer.DrawTextureV(it, pos, tint);
}

void DrawTextureEx(Texture2D tex, Vec2 pos, float rot, float scale, Color tint) {
    ITexture it{ tex.id, tex.width, tex.height, tex.mipmaps, tex.format, tex.valid };
    gRenderer.DrawTextureEx(it, pos, rot, scale, tint);
}

void DrawTextureRec(Texture2D tex, Rectangle source, Vec2 pos, Color tint) {
    ITexture it{ tex.id, tex.width, tex.height, tex.mipmaps, tex.format, tex.valid };
    gRenderer.DrawTextureRec(it, source, pos, tint);
}

void DrawTexturePro(Texture2D tex, Rectangle src, Rectangle dst, Vec2 origin, float rot, Color tint) {
    ITexture it{ tex.id, tex.width, tex.height, tex.mipmaps, tex.format, tex.valid };
    gRenderer.DrawTexturePro(it, src, dst, origin, rot, tint);
}

void DrawTextureTiled(Texture2D tex, float scale, Vec2 offset, Color tint) {
    ITexture it{ tex.id, tex.width, tex.height, tex.mipmaps, tex.format, tex.valid };
    gRenderer.DrawTextureTiled(it, scale, offset, tint);
}

void DrawTextureNPatch(Texture2D tex, NPatchInfo nPatchInfo, Rectangle dst, Vec2 origin, float rot, Color tint) {
    ITexture it{ tex.id, tex.width, tex.height, tex.mipmaps, tex.format, tex.valid };
    gRenderer.DrawTextureNPatch(it, nPatchInfo, dst, origin, rot, tint);
}

Texture2D GenCheckerTexture(int w, int h, int cellSize, Color a, Color b) {
    EnsureInitialized();
    ITexture it = gRenderer.GenCheckerTexture(w, h, cellSize, a, b);
    return Texture2D{ it.id, it.width, it.height, it.mipmaps, it.format, it.valid };
}

RenderTexture2D LoadRenderTexture(int w, int h) {
    EnsureInitialized();
    IRenderTexture ir = gRenderer.LoadRenderTexture(w, h);
    RenderTexture2D rt;
    rt.id       = ir.id;
    rt.depthId  = ir.depthId;
    rt.texture  = Texture2D{ ir.texture.id, ir.texture.width, ir.texture.height, ir.texture.mipmaps, ir.texture.format, ir.texture.valid };
    return rt;
}

void UnloadRenderTexture(RenderTexture2D target) {
    IRenderTexture ir;
    ir.id      = target.id;
    ir.depthId = target.depthId;
    ir.texture = ITexture{ target.texture.id, target.texture.width, target.texture.height, target.texture.mipmaps, target.texture.format, target.texture.valid };
    gRenderer.UnloadRenderTexture(ir);
}

bool IsRenderTextureValid(RenderTexture2D target) {
    IRenderTexture ir;
    ir.id      = target.id;
    ir.depthId = target.depthId;
    ir.texture = ITexture{ target.texture.id, target.texture.width, target.texture.height, target.texture.mipmaps, target.texture.format, target.texture.valid };
    return gRenderer.isRenderTextureValid(ir);
}

Texture2D GetRenderTextureTexture(RenderTexture2D target) {
    IRenderTexture ir;
    ir.id      = target.id;
    ir.depthId = target.depthId;
    ir.texture = ITexture{ target.texture.id, target.texture.width, target.texture.height, target.texture.mipmaps, target.texture.format, target.texture.valid };
    ITexture it = gRenderer.GetRenderTextureTexture(ir);
    return Texture2D{ it.id, it.width, it.height, it.mipmaps, it.format, it.valid };
}

Image LoadImageFromTexture(Texture2D texture) {
    EnsureInitialized();
    if (!IsTextureValid(texture)) return Image{};
    ITexture it{ texture.id, texture.width, texture.height, texture.mipmaps, texture.format, texture.valid };
    Image image = gRenderer.ReadTextureImage(it);
    if (!IsImageValid(image)) {
        TraceLog(LogLevel::Error, "IMAGE", "LoadImageFromTexture: failed to read texture pixels back to CPU");
    }
    return image;
}

Image LoadImageFromScreen(void) {
    EnsureInitialized();
    Image image = gRenderer.ReadScreenImage();
    if (!IsImageValid(image)) {
        TraceLog(LogLevel::Error, "IMAGE", "LoadImageFromScreen: failed to read backbuffer pixels to CPU");
    }
    return image;
}

void BeginTextureMode(RenderTexture2D target) {
    EnsureInitialized();
    IRenderTexture ir;
    ir.id      = target.id;
    ir.depthId = target.depthId;
    ir.texture = ITexture{ target.texture.id, target.texture.width, target.texture.height, target.texture.mipmaps, target.texture.format, target.texture.valid };
    gRenderer.BeginTextureMode(ir);
}

void EndTextureMode() {
    EnsureInitialized();
    gRenderer.EndTextureMode();
}

Font LoadFont(const char* fileName) {
    EnsureInitialized();
    const int defaultFontSize = 32;
    IFont iFont = gRenderer.LoadFont(fileName, defaultFontSize, nullptr, 0);
    Font f;
    gRenderer.FillFont(iFont, f);
    return f;
}

void UnloadFont(Font font) {
    if (font._rendererFontId != 0) {
        IFont iFont{ font._rendererFontId };
        gRenderer.UnloadFont(iFont);
    }
    delete[] font.glyphs;
    delete[] font.recs;
    font.glyphs = nullptr;
    font.recs   = nullptr;
}

Font GetDefaultFont() {
    EnsureInitialized();
    IFont iFont = gRenderer.LoadFont(nullptr, 32, nullptr, 0);
    Font f;
    gRenderer.FillFont(iFont, f);
    return f;
}

static std::vector<int> DefaultCodepointsAPI()
{
    std::vector<int> cps;
    cps.reserve(95);
    for (int c = 32; c <= 126; ++c) cps.push_back(c);
    return cps;
}

Font LoadFontEx(const char* fileName, int fontSize, const int* codepoints, int codepointCount)
{
    EnsureInitialized();
    IFont iFont = gRenderer.LoadFont(fileName, fontSize, codepoints, codepointCount);
    Font f;
    gRenderer.FillFont(iFont, f);
    return f;
}

Font LoadFontFromMemory(const char* fileType, const unsigned char* fileData, int dataSize,
                        int fontSize, const int* codepoints, int codepointCount)
{
    EnsureInitialized();
    IFont iFont = gRenderer.LoadFontFromMemory(fileType, fileData, dataSize, fontSize, codepoints, codepointCount);
    Font f;
    gRenderer.FillFont(iFont, f);
    return f;
}

bool IsFontValid(Font font)
{
    return font.valid && font.texture.id != 0 && font.glyphs != nullptr && font.recs != nullptr;
}

GlyphInfo* LoadFontData(const unsigned char* fileData, int dataSize, int fontSize,
                        const int* codepoints, int codepointCount, int type, int* glyphCount)
{
    if (glyphCount) *glyphCount = 0;
    const bool useUnicodeLayout = (type == 0 || type == 1 || type == 2);
    if (!useUnicodeLayout) {
        TraceLog(LogLevel::Warn, "FONT",
                 TextFormat("LoadFontData: unsupported font-load type %d, using unicode fallback", type));
    }
    if (!fileData || dataSize <= 0 || fontSize <= 0) return nullptr;

    std::vector<int> cps;
    const int* cpsPtr = codepoints;
    int cpsCount = codepointCount;
    if (cpsPtr == nullptr || cpsCount <= 0) {
        cps = DefaultCodepointsAPI();
        cpsPtr = cps.data();
        cpsCount = static_cast<int>(cps.size());
    }

    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) return nullptr;

    FT_Face face = nullptr;
    if (FT_New_Memory_Face(library, fileData, static_cast<FT_Long>(dataSize), 0, &face) != 0) {
        FT_Done_FreeType(library);
        return nullptr;
    }
    FT_Select_Charmap(face, FT_ENCODING_UNICODE);
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(fontSize));

    GlyphInfo* glyphs = static_cast<GlyphInfo*>(MemAlloc(sizeof(GlyphInfo) * static_cast<size_t>(cpsCount)));
    if (!glyphs) {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return nullptr;
    }

    int n = 0;
    for (int i = 0; i < cpsCount; ++i) {
        const int cp = cpsPtr[i];
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0) continue;

        FT_GlyphSlot slot = face->glyph;
        const int gw = static_cast<int>(slot->bitmap.width);
        const int gh = static_cast<int>(slot->bitmap.rows);

        GlyphInfo& g = glyphs[n];
        g.value = cp;
        g.offsetX = slot->bitmap_left;
        g.offsetY = slot->bitmap_top;
        g.advanceX = static_cast<int>(slot->advance.x / 64.0f);

        if (gw > 0 && gh > 0 && slot->bitmap.buffer != nullptr) {
            unsigned char* gdata = static_cast<unsigned char*>(MemAlloc(static_cast<size_t>(gw) * gh * 4));
            if (gdata) {
                for (int y = 0; y < gh; ++y) {
                    for (int x = 0; x < gw; ++x) {
                        const unsigned char a = slot->bitmap.buffer[y * slot->bitmap.pitch + x];
                        size_t o = (static_cast<size_t>(y) * gw + x) * 4;
                        gdata[o + 0] = 255;
                        gdata[o + 1] = 255;
                        gdata[o + 2] = 255;
                        gdata[o + 3] = a;
                    }
                }
                g.image = Image{ gdata, gw, gh, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
            }
        }
        ++n;
    }

    FT_Done_Face(face);
    FT_Done_FreeType(library);

    if (n == 0) {
        MemFree(glyphs);
        return nullptr;
    }
    if (glyphCount) *glyphCount = n;
    return glyphs;
}

void UnloadFontData(GlyphInfo* glyphs, int glyphCount)
{
    if (!glyphs) return;
    for (int i = 0; i < glyphCount; ++i) MemFree(glyphs[i].image.data);
    MemFree(glyphs);
}

Image GenImageFontAtlas(const GlyphInfo* glyphs, Rectangle** glyphRecs, int glyphCount,
                        int fontSize, int padding, int packMethod)
{
    if (glyphRecs) *glyphRecs = nullptr;
    const int effectiveFontSize = std::max(1, fontSize);
    const int effectivePadding = std::max(1, padding);
    const int effectivePackMethod = packMethod;
    if (effectivePackMethod != 0 && effectivePackMethod != 1) {
        TraceLog(LogLevel::Warn, "FONT",
                 TextFormat("GenImageFontAtlas: unsupported pack method %d, using default packing", effectivePackMethod));
    }
    if (!glyphs || glyphCount <= 0) return Image{};

    if (glyphRecs) *glyphRecs = new Rectangle[glyphCount]();

    int maxW = 1, maxH = 1;
    size_t totalArea = 0;
    for (int i = 0; i < glyphCount; ++i) {
        if (glyphs[i].image.data && glyphs[i].image.width > 0 && glyphs[i].image.height > 0) {
            const int w = glyphs[i].image.width, h = glyphs[i].image.height;
            maxW = std::max(maxW, w);
            maxH = std::max(maxH, h);
            totalArea += static_cast<size_t>(w + padding) * (h + padding);
        }
    }

    int atlasW = std::max(maxW + effectivePadding * 2, std::max(16, effectiveFontSize * 2));
    int atlasH = std::max(maxH + effectivePadding * 2, std::max(16, effectiveFontSize * 2));

    auto resize = [](std::vector<uint8_t>& px, int oldW, int newW, int oldH, int newH) {
        std::vector<uint8_t> np(static_cast<size_t>(newW) * newH * 4, 0);
        const int cw = std::min(oldW, newW), ch = std::min(oldH, newH);
        for (int y = 0; y < ch; ++y)
            std::memcpy(&np[static_cast<size_t>(y) * newW * 4],
                        &px[static_cast<size_t>(y) * oldW * 4],
                        static_cast<size_t>(cw) * 4);
        px.swap(np);
    };

    std::vector<uint8_t> px(static_cast<size_t>(atlasW) * atlasH * 4, 0);
    int penX = padding, penY = padding, rowHeight = 0;

    for (int i = 0; i < glyphCount; ++i) {
        const Image& gi = glyphs[i].image;
        if (!gi.data || gi.width <= 0 || gi.height <= 0) {
            if (glyphRecs) (*glyphRecs)[i] = Rectangle{};
            continue;
        }
        const int gw = gi.width, gh = gi.height;

        if (gw + padding * 2 > atlasW) {
            int nw = atlasW;
            while (nw < gw + padding * 2) nw *= 2;
            resize(px, atlasW, nw, atlasH, atlasH);
            atlasW = nw;
        }

        if (penX + gw + padding > atlasW) {
            penX = padding;
            penY += rowHeight + padding;
            rowHeight = 0;
        }

        if (penY + gh + padding > atlasH) {
            int nh = atlasH;
            while (nh < penY + gh + padding) nh *= 2;
            resize(px, atlasW, atlasW, atlasH, nh);
            atlasH = nh;
        }

        const unsigned char* src = static_cast<const unsigned char*>(gi.data);
        for (int y = 0; y < gh; ++y) {
            std::memcpy(&px[(static_cast<size_t>(penY + y) * atlasW + penX) * 4],
                        &src[static_cast<size_t>(y) * gw * 4],
                        static_cast<size_t>(gw) * 4);
        }

        if (glyphRecs) (*glyphRecs)[i] = Rectangle{ static_cast<float>(penX), static_cast<float>(penY),
                                                    static_cast<float>(gw), static_cast<float>(gh) };

        penX += gw + padding;
        rowHeight = std::max(rowHeight, gh);
    }

    Image atlas;
    atlas.data = MemAlloc(static_cast<size_t>(atlasW) * atlasH * 4);
    if (!atlas.data) {
        if (glyphRecs) {
            delete[] *glyphRecs;
            *glyphRecs = nullptr;
        }
        return Image{};
    }
    std::memcpy(atlas.data, px.data(), static_cast<size_t>(atlasW) * atlasH * 4);
    atlas.width = atlasW;
    atlas.height = atlasH;
    atlas.mipmaps = 1;
    atlas.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    return atlas;
}

Font LoadFontFromImage(Image image, Color key, int firstChar)
{
    EnsureInitialized();

    Font result{};
    if (!IsImageValid(image) || image.width <= 0 || image.height <= 0) return result;

    const int W = image.width, H = image.height;
    Color* colors = LoadImageColors(image);
    if (!colors) return result;

    std::vector<uint8_t> visited(static_cast<size_t>(W) * H, 0);
    struct Comp { int minX, minY, maxX, maxY; };
    std::vector<Comp> comps;
    std::vector<std::pair<int, int>> stack;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t idx = static_cast<size_t>(y) * W + x;
            if (visited[idx]) continue;
            visited[idx] = 1;
            const Color c = colors[idx];
            const bool isKey = (c.r == key.r && c.g == key.g && c.b == key.b);
            if (isKey) continue;

            Comp comp{ x, y, x, y };
            stack.clear();
            stack.emplace_back(x, y);
            while (!stack.empty()) {
                const auto [cx, cy] = stack.back();
                stack.pop_back();
                const size_t ci = static_cast<size_t>(cy) * W + cx;
                if (visited[ci]) continue;
                visited[ci] = 1;
                const Color cc = colors[ci];
                if (cc.r == key.r && cc.g == key.g && cc.b == key.b) continue;
                comp.minX = std::min(comp.minX, cx); comp.maxX = std::max(comp.maxX, cx);
                comp.minY = std::min(comp.minY, cy); comp.maxY = std::max(comp.maxY, cy);
                if (cx > 0)     stack.emplace_back(cx - 1, cy);
                if (cx < W - 1) stack.emplace_back(cx + 1, cy);
                if (cy > 0)     stack.emplace_back(cx, cy - 1);
                if (cy < H - 1) stack.emplace_back(cx, cy + 1);
            }
            comps.push_back(comp);
        }
    }

    UnloadImageColors(colors);
    if (comps.empty()) return result;

    std::sort(comps.begin(), comps.end(), [](const Comp& a, const Comp& b) {
        const int ay = (a.minY + a.maxY) / 2, by = (b.minY + b.maxY) / 2;
        if (ay != by) return ay < by;
        return (a.minX + a.maxX) < (b.minX + b.maxX);
    });

    const int glyphCount = static_cast<int>(comps.size());
    int nominalW = 0, nominalH = 0;
    for (const Comp& c : comps) {
        nominalW = std::max(nominalW, c.maxX - c.minX + 1);
        nominalH = std::max(nominalH, c.maxY - c.minY + 1);
    }

    Color* cols2 = LoadImageColors(image);
    unsigned char* rgba = static_cast<unsigned char*>(MemAlloc(static_cast<size_t>(W) * H * 4));
    if (!rgba) {
        UnloadImageColors(cols2);
        return result;
    }
    for (int yy = 0; yy < H; ++yy) {
        for (int xx = 0; xx < W; ++xx) {
            const size_t o = static_cast<size_t>(yy) * W + xx;
            const Color c = cols2[o];
            const bool kk = (c.r == key.r && c.g == key.g && c.b == key.b);
            rgba[o * 4 + 0] = c.r;
            rgba[o * 4 + 1] = c.g;
            rgba[o * 4 + 2] = c.b;
            rgba[o * 4 + 3] = kk ? 0 : 255;
        }
    }
    UnloadImageColors(cols2);

    GlyphInfo* glyphs = static_cast<GlyphInfo*>(MemAlloc(sizeof(GlyphInfo) * static_cast<size_t>(glyphCount)));
    Rectangle* recs = new Rectangle[glyphCount];
    for (int i = 0; i < glyphCount; ++i) {
        const Comp& c = comps[i];
        const int w = c.maxX - c.minX + 1, h = c.maxY - c.minY + 1;
        recs[i] = Rectangle{ static_cast<float>(c.minX), static_cast<float>(c.minY),
                             static_cast<float>(w), static_cast<float>(h) };
        glyphs[i].value = firstChar + i;
        glyphs[i].offsetX = 0;
        glyphs[i].offsetY = nominalH;
        glyphs[i].advanceX = nominalW;
        unsigned char* gd = static_cast<unsigned char*>(MemAlloc(static_cast<size_t>(w) * h * 4));
        if (gd) {
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const size_t src = (static_cast<size_t>(c.minY + y) * W + (c.minX + x)) * 4;
                    const size_t dst = (static_cast<size_t>(y) * w + x) * 4;
                    gd[dst + 0] = rgba[src + 0];
                    gd[dst + 1] = rgba[src + 1];
                    gd[dst + 2] = rgba[src + 2];
                    gd[dst + 3] = rgba[src + 3];
                }
            }
        }
        glyphs[i].image = Image{ gd, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    }

    Image atlas;
    atlas.data = rgba;
    atlas.width = W;
    atlas.height = H;
    atlas.mipmaps = 1;
    atlas.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;

    const ITexture it = gRenderer.LoadTextureFromImage(atlas);
    MemFree(atlas.data);

    if (!it.valid || it.id == 0) {
        for (int i = 0; i < glyphCount; ++i) MemFree(glyphs[i].image.data);
        MemFree(glyphs);
        delete[] recs;
        return result;
    }

    result.baseSize = (nominalH > 0) ? nominalH : image.height;
    result.glyphCount = glyphCount;
    result.glyphPadding = 0;
    result.valid = true;
    result._rendererFontId = 0;
    result.texture = Texture2D{ it.id, it.width, it.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, true };
    result.recs = recs;
    result.glyphs = glyphs;
    return result;
}

bool ExportFontAsCode(Font font, const char* fileName)
{
    if (!font.valid || !font.glyphs || !font.recs || !fileName) return false;

    Image atlas = LoadImageFromTexture(font.texture);

    std::ofstream out(fileName, std::ios::out | std::ios::trunc);
    if (!out) {
        if (IsImageValid(atlas)) UnloadImage(atlas);
        return false;
    }

    out << "// Font exported by Quark Core\n";
    out << "// baseSize: " << font.baseSize << "\n";
    out << "// glyphCount: " << font.glyphCount << "\n\n";
    out << "#include <stddef.h>\n\n";
    out << "typedef struct ExportGlyph {\n"
        << "    int value;\n"
        << "    int offsetX;\n"
        << "    int offsetY;\n"
        << "    int advanceX;\n"
        << "} ExportGlyph;\n\n";
    out << "typedef struct ExportRect {\n"
        << "    float x;\n"
        << "    float y;\n"
        << "    float width;\n"
        << "    float height;\n"
        << "} ExportRect;\n\n";

    out << "static const ExportGlyph exportedGlyphs[" << font.glyphCount << "] = {\n";
    for (int i = 0; i < font.glyphCount; ++i) {
        const GlyphInfo& g = font.glyphs[i];
        out << "    {" << g.value << ", " << g.offsetX << ", " << g.offsetY << ", " << g.advanceX << "}";
        if (i < font.glyphCount - 1) out << ",";
        out << "\n";
    }
    out << "};\n\n";

    out << "static const ExportRect exportedRecs[" << font.glyphCount << "] = {\n";
    for (int i = 0; i < font.glyphCount; ++i) {
        const Rectangle& r = font.recs[i];
        out << "    {" << r.x << "f, " << r.y << "f, " << r.width << "f, " << r.height << "f}";
        if (i < font.glyphCount - 1) out << ",";
        out << "\n";
    }
    out << "};\n\n";

    if (IsImageValid(atlas) && atlas.data && atlas.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
        out << "static const unsigned char exportedAtlasRGBA[" << (size_t)atlas.width * atlas.height * 4 << "] = {\n";
        const unsigned char* p = static_cast<const unsigned char*>(atlas.data);
        const size_t total = static_cast<size_t>(atlas.width) * atlas.height * 4;
        for (size_t i = 0; i < total; ++i) {
            out << static_cast<unsigned int>(p[i]);
            if (i < total - 1) out << ",";
            if ((i + 1) % 24 == 0) out << "\n";
            else out << " ";
        }
        out << "\n};\n";
        out << "static const int exportedAtlasWidth = " << atlas.width << ";\n";
        out << "static const int exportedAtlasHeight = " << atlas.height << ";\n";
    } else {
        out << "// (atlas pixels unavailable: texture could not be read back)\n";
    }

    out.close();
    if (IsImageValid(atlas)) UnloadImage(atlas);
    return true;
}

void DrawText(const char* text, int x, int y, int fontSize, Color color) {
    EnsureInitialized();
    gRenderer.DrawText(text, x, y, fontSize, color);
}

void DrawDebugText(const char* text, int x, int y, int fontSize, Color color) {
    EnsureInitialized();
    gRenderer.DrawDebugText(text, x, y, fontSize, color);
}

void DrawTextEx(Font font, const char* text, Vec2 position, float fontSize, float spacing, Color tint) {
    EnsureInitialized();
    IFont iFont{ font._rendererFontId };
    gRenderer.DrawTextEx(iFont, text, position, fontSize, spacing, tint);
}

Vec2 MeasureTextEx(Font font, const char* text, float fontSize, float spacing) {
    IFont iFont{ font._rendererFontId };
    return gRenderer.MeasureTextEx(iFont, text, fontSize, spacing);
}

int MeasureText(const char* text, int fontSize) {
    return gRenderer.MeasureText(text, fontSize);
}

Shader LoadShader(const char* vs, const char* fs) {
    EnsureInitialized();
    return gRenderer.LoadShader(vs, fs);
}

Shader LoadShaderFromMemory(const char* vs, const char* fs) {
    EnsureInitialized();
    return gRenderer.LoadShaderFromMemory(vs, fs);
}

void Set3DLightEnabled(int index, bool enabled) {
    EnsureInitialized();
    gRenderer.Set3DLightEnabled(index, enabled);
}

void UnloadShader(Shader shader)                                     { gRenderer.UnloadShader(shader); }
bool IsShaderValid(const Shader& shader)                             { return gRenderer.isShaderValid(const_cast<Shader&>(shader)); }
bool IsShaderReady(Shader shader)                                    { return IsShaderValid(shader); }

namespace {
    namespace fs = std::filesystem;

    static std::string ToLower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        return value;
    }

    static char* AllocatePathString(const std::string& path) {
        if (path.empty()) return nullptr;
        size_t size = path.size() + 1;
        char* data = static_cast<char*>(std::malloc(size));
        if (!data) return nullptr;
        std::memcpy(data, path.c_str(), size);
        return data;
    }

    static std::string NormalizeExtension(std::string ext) {
        if (!ext.empty() && ext[0] == '*') {
            ext.erase(0, 1);
        }
        if (!ext.empty() && ext[0] == '.') {
            return ToLower(ext);
        }
        if (!ext.empty()) {
            return ToLower(std::string(".") + ext);
        }
        return std::string();
    }

    static bool CompareExtension(const fs::path& path, const std::string& filter) {
        const std::string ext = ToLower(path.extension().string());
        return ext == filter;
    }

    static bool MatchesFilter(const fs::path& path, const char* filter) {
        if (!filter || filter[0] == '\0' || std::strcmp(filter, "*.*") == 0 || std::strcmp(filter, "*") == 0) {
            return true;
        }

        const std::string f = filter;
        if (f.rfind("FILES", 0) == 0) {
            return fs::is_regular_file(path);
        }
        if (f.rfind("DIRS", 0) == 0) {
            return fs::is_directory(path);
        }
        if (f.size() > 0 && f[0] == '*') {
            const std::string normalized = NormalizeExtension(f.substr(1));
            return CompareExtension(path, normalized);
        }
        if (f.size() > 0 && f[0] == '.') {
            return CompareExtension(path, ToLower(f));
        }
        return CompareExtension(path, NormalizeExtension(f));
    }

    static std::vector<std::string> LoadDirectoryFilesInternal(const char* basePath, const char* filter, bool scanSubdirs) {
        std::vector<std::string> result;
        if (!basePath) return result;

        try {
            fs::path root(basePath);
            if (!fs::exists(root)) return result;

            if (scanSubdirs) {
                for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
                    if (!entry.exists()) continue;
                    const fs::path& path = entry.path();
                    if (MatchesFilter(path, filter)) {
                        result.emplace_back(path.string());
                    }
                }
            } else {
                for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied)) {
                    if (!entry.exists()) continue;
                    const fs::path& path = entry.path();
                    if (MatchesFilter(path, filter)) {
                        result.emplace_back(path.string());
                    }
                }
            }
        } catch (const std::exception&) {
        }
        return result;
    }

    static FilePathList BuildFilePathList(const std::vector<std::string>& paths) {
        FilePathList list{};
        list.count = static_cast<unsigned int>(paths.size());
        if (list.count == 0) return list;

        list.paths = static_cast<char**>(std::malloc(sizeof(char*) * list.count));
        if (!list.paths) {
            list.count = 0;
            return list;
        }

        for (unsigned int i = 0; i < list.count; ++i) {
            list.paths[i] = AllocatePathString(paths[i]);
            if (!list.paths[i]) {
                for (unsigned int j = 0; j < i; ++j) {
                    std::free(list.paths[j]);
                }
                std::free(list.paths);
                list.count = 0;
                list.paths = nullptr;
                return list;
            }
        }
        return list;
    }

    static long FileTimeToTimeT(const fs::file_time_type& fileTime) {
        try {
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                fileTime - fs::file_time_type::clock::now()
                + std::chrono::system_clock::now()
            );

            return static_cast<long>(std::chrono::system_clock::to_time_t(sctp));
        } catch (...) {
            return 0;
        }
    }
}

int FileRename(const char* fileName, const char* fileRename) {
    if (!fileName || !fileRename) return -1;
    try {
        std::filesystem::rename(fileName, fileRename);
        return 0;
    } catch (...) {
        return -1;
    }
}

int FileRemove(const char* fileName) {
    if (!fileName) return -1;
    try {
        return std::filesystem::remove(fileName) ? 0 : -1;
    } catch (...) {
        return -1;
    }
}

int FileCopy(const char* srcPath, const char* dstPath) {
    if (!srcPath || !dstPath) return -1;
    try {
        std::filesystem::path target(dstPath);
        if (target.has_parent_path()) {
            std::filesystem::create_directories(target.parent_path());
        }
        std::filesystem::copy_file(srcPath, dstPath, std::filesystem::copy_options::overwrite_existing);
        return 0;
    } catch (...) {
        return -1;
    }
}

int FileMove(const char* srcPath, const char* dstPath) {
    if (!srcPath || !dstPath) return -1;
    try {
        std::filesystem::path target(dstPath);
        if (target.has_parent_path()) {
            std::filesystem::create_directories(target.parent_path());
        }
        std::filesystem::rename(srcPath, dstPath);
        return 0;
    } catch (...) {
        try {
            if (FileCopy(srcPath, dstPath) == 0) {
                return FileRemove(srcPath);
            }
        } catch (...) {
        }
        return -1;
    }
}

int FileTextReplace(const char* fileName, const char* search, const char* replacement) {
    if (!fileName || !search || !replacement) return -1;
    try {
        std::ifstream input(fileName, std::ios::binary);
        if (!input) return -1;
        std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        input.close();

        std::string needle = search;
        std::string repl = replacement;
        size_t pos = 0;
        bool replaced = false;
        while ((pos = content.find(needle, pos)) != std::string::npos) {
            content.replace(pos, needle.length(), repl);
            pos += repl.length();
            replaced = true;
        }
        if (!replaced) return -1;

        std::ofstream output(fileName, std::ios::binary | std::ios::trunc);
        if (!output) return -1;
        output << content;
        return 0;
    } catch (...) {
        return -1;
    }
}

int FileTextFindIndex(const char* fileName, const char* search) {
    if (!fileName || !search) return -1;
    try {
        std::ifstream input(fileName, std::ios::binary);
        if (!input) return -1;
        std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        const auto pos = content.find(search);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    } catch (...) {
        return -1;
    }
}

bool FileExists(const char* fileName) {
    if (!fileName) return false;
    try {
        return std::filesystem::exists(fileName) && std::filesystem::is_regular_file(fileName);
    } catch (...) {
        return false;
    }
}

bool DirectoryExists(const char* dirPath) {
    if (!dirPath) return false;
    try {
        return std::filesystem::exists(dirPath) && std::filesystem::is_directory(dirPath);
    } catch (...) {
        return false;
    }
}

bool IsFileExtension(const char* fileName, const char* ext) {
    if (!fileName || !ext) return false;
    try {
        const auto fileExt = ToLower(std::filesystem::path(fileName).extension().string());
        const std::string expected = ToLower(ext[0] == '.' ? std::string(ext) : std::string(".") + ext);
        return fileExt == expected;
    } catch (...) {
        return false;
    }
}

int GetFileLength(const char* fileName) {
    if (!fileName) return -1;
    try {
        auto size = std::filesystem::file_size(fileName);
        return size > static_cast<uintmax_t>(std::numeric_limits<int>::max()) ? -1 : static_cast<int>(size);
    } catch (...) {
        return -1;
    }
}

long GetFileModTime(const char* fileName) {
    if (!fileName) return 0;
    try {
        auto time = std::filesystem::last_write_time(fileName);
        return FileTimeToTimeT(time);
    } catch (...) {
        return 0;
    }
}

const char* GetFileExtension(const char* fileName) {
    thread_local std::string buffer;
    buffer.clear();
    if (!fileName) return buffer.c_str();
    try {
        buffer = std::filesystem::path(fileName).extension().string();
    } catch (...) {
        buffer.clear();
    }
    return buffer.c_str();
}

const char* GetFileName(const char* filePath) {
    thread_local std::string buffer;
    buffer.clear();
    if (!filePath) return buffer.c_str();
    try {
        buffer = std::filesystem::path(filePath).filename().string();
    } catch (...) {
        buffer.clear();
    }
    return buffer.c_str();
}

const char* GetFileNameWithoutExt(const char* filePath) {
    thread_local std::string buffer;
    buffer.clear();
    if (!filePath) return buffer.c_str();
    try {
        buffer = std::filesystem::path(filePath).stem().string();
    } catch (...) {
        buffer.clear();
    }
    return buffer.c_str();
}

const char* GetDirectoryPath(const char* filePath) {
    thread_local std::string buffer;
    buffer.clear();
    if (!filePath) return buffer.c_str();
    try {
        buffer = std::filesystem::path(filePath).parent_path().string();
    } catch (...) {
        buffer.clear();
    }
    return buffer.c_str();
}

const char* GetPrevDirectoryPath(const char* dirPath) {
    thread_local std::string buffer;
    buffer.clear();
    if (!dirPath) return buffer.c_str();
    try {
        const auto path = std::filesystem::path(dirPath).parent_path();
        buffer = path.parent_path().string();
    } catch (...) {
        buffer.clear();
    }
    return buffer.c_str();
}

const char* GetWorkingDirectory(void) {
    thread_local std::string buffer;
    buffer.clear();
    try {
        buffer = std::filesystem::current_path().string();
    } catch (...) {
        buffer.clear();
    }
    return buffer.c_str();
}

const char* GetApplicationDirectory(void) {
    thread_local std::string buffer;
    buffer.clear();
    const char* path = SDL_GetBasePath();
    if (path) {
        buffer = path;
    }
    return buffer.c_str();
}

int MakeDirectory(const char* dirPath) {
    if (!dirPath) return -1;
    try {
        std::filesystem::create_directories(dirPath);
        return 0;
    } catch (...) {
        return -1;
    }
}

int ChangeDirectory(const char* dirPath) {
    if (!dirPath) return -1;
    try {
        std::filesystem::current_path(dirPath);
        return 0;
    } catch (...) {
        return -1;
    }
}

bool IsPathFile(const char* path) {
    if (!path) return false;
    try {
        return std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
    } catch (...) {
        return false;
    }
}

bool IsPathDirectory(const char* path) {
    if (!path) return false;
    try {
        return std::filesystem::exists(path) && std::filesystem::is_directory(path);
    } catch (...) {
        return false;
    }
}

bool IsPathAbsolute(const char* path) {
    if (!path) return false;
    try {
        return std::filesystem::path(path).is_absolute();
    } catch (...) {
        return false;
    }
}

bool IsFileNameValid(const char* fileName) {
    if (!fileName) return false;
    const std::string name(fileName);
    if (name.empty() || name == "." || name == "..") return false;
    for (char c : name) {
        if (c == '\0') return false;
#if defined(_WIN32)
        const char invalid[] = R"(<>:\"/\|?*)";
        if (std::strchr(invalid, c)) return false;
#endif
    }
#if defined(_WIN32)
    const std::string upper = ToLower(name);
    static const char* reserved[] = {
        "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"
    };
    std::string stem = upper;
    auto dotPos = stem.find('.');
    if (dotPos != std::string::npos) stem.resize(dotPos);
    for (const char* r : reserved) {
        if (stem == r) return false;
    }
#endif
    return true;
}

FilePathList LoadDirectoryFiles(const char* dirPath) {
    return BuildFilePathList(LoadDirectoryFilesInternal(dirPath, "*.*", false));
}

FilePathList LoadDirectoryFilesEx(const char* basePath, const char* filter, bool scanSubdirs) {
    return BuildFilePathList(LoadDirectoryFilesInternal(basePath, filter, scanSubdirs));
}

void UnloadDirectoryFiles(FilePathList files) {
    if (!files.paths) return;

    for (unsigned int i = 0; i < files.count; ++i) {
        if (files.paths[i]) std::free(files.paths[i]);
    }

    std::free(files.paths);
}

bool IsFileDropped(void) {
    return !gWin.droppedFiles.empty();
}

FilePathList LoadDroppedFiles(void) {
    FilePathList result = BuildFilePathList(gWin.droppedFiles);
    
    gWin.droppedFiles.clear();
    return result;
}

void UnloadDroppedFiles(FilePathList files) {
    UnloadDirectoryFiles(files);
}

unsigned int GetDirectoryFileCount(const char* dirPath) {
    if (!dirPath) return 0;
    try {
        unsigned int count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(dirPath, std::filesystem::directory_options::skip_permission_denied)) {
            if (entry.exists()) ++count;
        }
        return count;
    } catch (...) {
        return 0;
    }
}

unsigned int GetDirectoryFileCountEx(const char* basePath, const char* filter, bool scanSubdirs) {
    return static_cast<unsigned int>(LoadDirectoryFilesInternal(basePath, filter, scanSubdirs).size());
}

int GetShaderLocation(const Shader& shader, const char* name) {
    return gRenderer.GetShaderLocation(shader, name);
}

int GetShaderLocation(const Shader& shader, ShaderLocationIndex locIndex) {
    return gRenderer.GetShaderLocation(shader, locIndex);
}

int GetShaderAttributeLocation(const Shader& s, const char* name) {
    return gRenderer.GetShaderAttributeLocation(s, name);
}

void SetShaderValue(const Shader& s, int loc, float value) {
    gRenderer.SetShaderValue(s, loc, value);
}

void SetShaderValue(const Shader& s, int loc, int value) {
    gRenderer.SetShaderValue(s, loc, value);
}

void SetShaderValue(const Shader& s, int loc, const Vec2& value) {
    gRenderer.SetShaderValue(s, loc, value);
}

void SetShaderValue(const Shader& s, int loc, const Vec3& value) {
    gRenderer.SetShaderValue(s, loc, value);
}

void SetShaderValue(const Shader& s, int loc, const Vec4& value) {
    gRenderer.SetShaderValue(s, loc, value);
}

void SetShaderValue(const Shader& s, int loc, const Color& value) {
    Color v = value;
    gRenderer.SetShaderValue(s, loc, v);
}

void SetShaderValue(const Shader& s, int loc, const void* value, int uniformType) {
    gRenderer.SetShaderValue(s, loc, value, uniformType);
}

void SetShaderValueV(const Shader& s, int loc, const void* value, int uniformType, int count) {
    gRenderer.SetShaderValueV(s, loc, value, uniformType, count);
}

void SetShaderValueMatrix(const Shader& s, int loc, const float* m) {
    gRenderer.SetShaderValueMatrix(s, loc, m);
}

void SetShaderValueMatrix(const Shader& s, int loc, const Matrix& mat) {
    gRenderer.SetShaderValueMatrix(s, loc, mat);
}

void SetShaderValueSampler(const Shader& s, int loc, int unit) {
    gRenderer.SetShaderValueSampler(s, loc, unit);
}

void SetShaderValueTexture(const Shader& s, int loc, const Texture2D& texture) {
    ITexture it{ texture.id, texture.width, texture.height, texture.mipmaps, texture.format, texture.valid };
    gRenderer.SetShaderValueTexture(s, loc, it);
}

void SetShaderValueTextureUnit(const Shader& s, int loc, const Texture2D& texture, int textureUnit) {
    ITexture it{ texture.id, texture.width, texture.height, texture.mipmaps, texture.format, texture.valid };
    gRenderer.SetShaderValueTextureUnit(s, loc, it, textureUnit);
}

void UnloadVertexArray(unsigned int vaoId) {
#if defined(QC_ENABLE_OPENGL)
    if (vaoId) glDeleteVertexArrays(1, &vaoId);
#else
    (void)vaoId;
#endif
}

void UnloadVertexBuffer(unsigned int vboId) {
#if defined(QC_ENABLE_OPENGL)
    if (vboId) glDeleteBuffers(1, &vboId);
#else
    (void)vboId;
#endif
}

Material LoadMaterialDefault() {
    Material material{};
    material.maps = new MaterialMap[MATERIAL_MAP_BRDF + 1]{};
    material.maps[MATERIAL_MAP_ALBEDO].color = WHITE;
    return material;
}

bool IsModelValid(Model model) {
    return model.meshCount > 0 && model.meshes != nullptr;
}

Material* LoadMaterials(const char* fileName, int* materialCount) {
    if (fileName != nullptr && fileName[0] != '\0') {
        TraceLog(LogLevel::Info, "ASSET",
                 TextFormat("LoadMaterials: using default material fallback for '%s'", fileName));
    }
    if (materialCount) *materialCount = 1;
    Material* materials = new Material[1];
    materials[0] = LoadMaterialDefault();
    return materials;
}

bool IsMaterialValid(Material material) {
    return material.maps != nullptr;
}

void UnloadMaterial(Material material) {
    delete[] material.maps;
    delete material.shader;
}

void SetMaterialTexture(Material* material, int mapType, Texture2D texture) {
    if (!material || !material->maps || mapType < 0 || mapType > MATERIAL_MAP_BRDF) return;
    material->maps[mapType].texture = texture;
}

void SetModelMeshMaterial(Model* model, int meshId, int materialId) {
    if (!model || !model->meshMaterial || meshId < 0 || meshId >= model->meshCount || materialId < 0 || materialId >= model->materialCount) return;
    model->meshMaterial[meshId] = materialId;
}

Model LoadModelFromMesh(Mesh mesh) {
    EnsureInitialized();
    gRenderer.UploadMesh(mesh, false);

    Model model{};
    model.meshCount = 1;
    model.materialCount = 1;
    model.meshes = new Mesh[1]{mesh};
    model.materials = new Material[1]{LoadMaterialDefault()};
    model.meshMaterial = new int[1]{0};
    return model;
}

Model LoadModelFromMesh(const char* name, Mesh mesh) {
    Model model = LoadModelFromMesh(mesh);
    if (name) model.directory = name;
    return model;
}

void BeginShaderMode(const Shader& shader) {
    gRenderer.BeginShaderMode(shader);
}

void EndShaderMode() {
    gRenderer.EndShaderMode();
}

Camera2D CreateCamera2D() {
    Camera2D c{};
    c.zoom = 1.f;
    return c;
}

void BeginMode2D(const Camera2D& camera) {
    EnsureInitialized();
    gRenderer.BeginMode2D(camera);
}

void EndMode2D() {
    gRenderer.EndMode2D();
}

Camera2D GetCamera2D() {
    return gRenderer.GetCamera2D();
}

void UpdateCamera2D(Camera2D& camera, float targetX, float targetY, float smoothness) {
    smoothness = std::clamp(smoothness, 0.f, 1.f);
    camera.target.x = camera.target.x * (1.f - smoothness) + targetX * smoothness;
    camera.target.y = camera.target.y * (1.f - smoothness) + targetY * smoothness;
}

Camera3D CreateCamera3D() {
    Camera3D c{};
    c.position = {0.f, 0.f, 10.f};
    c.target   = {0.f, 0.f,  0.f};
    c.up       = {0.f, 1.f,  0.f};
    return c;
}

void BeginMode3D(const Camera3D& camera) {
    EnsureInitialized();
    gRenderer.BeginMode3D(camera);
}

void EndMode3D() {
    gRenderer.EndMode3D();
}

void PushMatrix() {
    gRenderer.PushMatrix();
}

void PopMatrix() {
    gRenderer.PopMatrix();
}

void Translate(const Vec3& t) {
    gRenderer.Translate(t);
}

void Translate(float x, float y, float z) {
    gRenderer.Translate(Vec3{x, y, z});
}

void Rotate(float angle, const Vec3& ax) {
    gRenderer.Rotate(angle, ax);
}

void Rotate(float angle) {
    gRenderer.Rotate(angle, Vec3{0, 0, 1});
}

void Scale(const Vec3& s) {
    gRenderer.Scale(s);
}

void Scale(float s) {
    gRenderer.Scale(Vec3{s, s, s});
}

void MultMatrix(const Mat4& m) {
    gRenderer.MultMatrix(m);
}

void EnableBackfaceCulling() {
    gRenderer.EnableBackfaceCulling();
}

void DisableBackfaceCulling() {
    gRenderer.DisableBackfaceCulling();
}

const float* GetMatrixModelview() {
    return gRenderer.GetMatrixModelview();
}

const float* GetMatrixProjection() {
    return gRenderer.GetMatrixProjection();
}

void Set3DView(const Mat4& view, const Mat4& proj) {
    gRenderer.Set3DView(view, proj);
}

void DrawLine3D(Vec3 start, Vec3 end, Color c) {
    gRenderer.DrawLine3D(start, end, c);
}

void DrawPoint3D(Vec3 position, Color color) {
    constexpr float s = 0.01f;
    DrawLine3D({position.x - s, position.y, position.z}, {position.x + s, position.y, position.z}, color);
    DrawLine3D({position.x, position.y - s, position.z}, {position.x, position.y + s, position.z}, color);
    DrawLine3D({position.x, position.y, position.z - s}, {position.x, position.y, position.z + s}, color);
}

void DrawCircle3D(Vec3 center, float radius, Vec3 rotationAxis, float rotationAngle, Color color) {
    Vec3 axis = rotationAxis;
    const float axisLen = axis.length();
    if (axisLen <= EPSILON) {
        axis = Vec3{0.0f, 1.0f, 0.0f};
    } else {
        axis = axis * (1.0f / axisLen);
    }

    const Mat4 rotation = QuaternionToMatrix(QuaternionFromAxisAngle(axis, rotationAngle * DEG2RAD));
    Vec3 last = TransformPoint(rotation, Vec3{radius, 0.0f, 0.0f}) + center;

    for (int i = 1; i <= 64; ++i) {
        const float a = 2.0f * PI * static_cast<float>(i) / 64.0f;
        const Vec3 local{
            std::cos(a) * radius,
            std::sin(a) * radius,
            0.0f
        };
        const Vec3 cur = TransformPoint(rotation, local) + center;
        DrawLine3D(last, cur, color);
        last = cur;
    }
}

void DrawTriangle3D(Vec3 v1, Vec3 v2, Vec3 v3, Color color) {
    DrawLine3D(v1, v2, color);
    DrawLine3D(v2, v3, color);
    DrawLine3D(v3, v1, color);
}

void DrawTriangleStrip3D(const Vec3* points, int pointCount, Color color)
{
    if (!points || pointCount < 3) {
        return;
    }

    for (int i = 0; i + 2 < pointCount; ++i) {
        DrawTriangle3D(points[i], points[i + 1], points[i + 2], color);
    }
}

void DrawGrid(int slices, float spacing, Color color) {
    gRenderer.DrawGrid(slices, spacing, color);
}

void DrawPlane(Vec3 center, Vec2 size, Color c) {
    gRenderer.DrawPlane(center, size, c);
}

void DrawCube(Vec3 pos, float w, float h, float l, Color c) {
    gRenderer.DrawCube(pos, w, h, l, c);
}

void DrawCubeV(Vec3 pos, Vec3 size, Color c) {
    gRenderer.DrawCubeV(pos, size, c);
}

void DrawCubeWires(Vec3 pos, float w, float h, float l, Color c) {
    gRenderer.DrawCubeWires(pos, w, h, l, c);
}

void DrawCubeWiresV(Vec3 pos, Vec3 size, Color c) {
    gRenderer.DrawCubeWiresV(pos, size, c);
}

void DrawSphere(Vec3 center, float radius, Color c) {
    gRenderer.DrawSphere(center, radius, c);
}

void DrawSphereEx(Vec3 center, float radius, int rings, int slices, Color c) {
    gRenderer.DrawSphereEx(center, radius, rings, slices, c);
}

void DrawSphereWires(Vec3 center, float radius, int rings, int slices, Color c) {
    gRenderer.DrawSphereWires(center, radius, rings, slices, c);
}

void DrawCylinder(Vec3 pos, float rTop, float rBot, float h, int slices, Color c) {
    gRenderer.DrawCylinder(pos, rTop, rBot, h, slices, c);
}

void DrawCylinderEx(Vec3 s, Vec3 e, float rS, float rE, int sides, Color c) {
    gRenderer.DrawCylinderEx(s, e, rS, rE, sides, c);
}

void DrawCylinderWires(Vec3 pos, float rTop, float rBot, float h, int slices, Color c) {
    gRenderer.DrawCylinderWires(pos, rTop, rBot, h, slices, c);
}

void DrawCylinderWiresEx(Vec3 s, Vec3 e, float rS, float rE, int slices, Color c) {
    gRenderer.DrawCylinderWiresEx(s, e, rS, rE, slices, c);
}

void DrawCapsule(Vec3 startPos, Vec3 endPos, float radius, int rings, int slices, Color color) {
    DrawCylinderEx(startPos, endPos, radius, radius, slices, color);
    DrawSphereEx(startPos, radius, rings, slices, color);
    DrawSphereEx(endPos, radius, rings, slices, color);
}

void DrawCapsuleWires(Vec3 startPos, Vec3 endPos, float radius, int rings, int slices, Color color) {
    DrawCylinderWiresEx(startPos, endPos, radius, radius, slices, color);
    DrawSphereWires(startPos, radius, rings, slices, color);
    DrawSphereWires(endPos, radius, rings, slices, color);
}

void DrawRay(Ray ray, Color color) {
    DrawLine3D(ray.position, ray.position + ray.direction * 10000.0f, color);
}

Model LoadModel(const char* filePath) {
    EnsureInitialized();
    return gRenderer.LoadModel(filePath);
}

void UnloadModel(Model model) {
    gRenderer.UnloadModel(model);
}

void DrawModel(Model model, Vec3 position, float scale, Color tint) {
    Mat4 transform = BuildTransform(position, Vec3{0.0f, 1.0f, 0.0f}, 0.0f, Vec3{scale, scale, scale});
    gRenderer.DrawModelEx(model, transform, tint);
}

void DrawModelEx(Model model, Vec3 position, Vec3 rotationAxis,
                 float rotationAngle, Vec3 scale, Color tint) {
    Mat4 transform = BuildTransform(position, rotationAxis, rotationAngle, scale);
    gRenderer.DrawModelEx(model, transform, tint);
}

void DrawModelEx(Model model, const Mat4& transform) {
    gRenderer.DrawModelEx(model, transform);
}

void DrawModelWires(Model model, Vec3 position, float scale, Color tint) {
    Mat4 transform = BuildTransform(position, Vec3{0.0f, 0.0f, 1.0f}, 0.0f, Vec3{scale, scale, scale});
    DrawModelWireframe(model, transform, tint);
}

void DrawModelWiresEx(Model model, Vec3 position, Vec3 rotationAxis,
                      float rotationAngle, Vec3 scale, Color tint) {
    Mat4 transform = BuildTransform(position, rotationAxis, rotationAngle, scale);
    DrawModelWireframe(model, transform, tint);
}

namespace {

static Mesh CreateMesh(int vertexCount, int triangleCount) {
    Mesh mesh{};
    if (vertexCount <= 0) return mesh;
    mesh.vertexCount = vertexCount;
    mesh.triangleCount = triangleCount;
    mesh.vertices = new float[vertexCount * 3];
    mesh.normals = new float[vertexCount * 3];
    mesh.texcoords = new float[vertexCount * 2];
    mesh.indices = new unsigned short[triangleCount * 3];
    std::fill(mesh.vertices, mesh.vertices + vertexCount * 3, 0.0f);
    std::fill(mesh.normals, mesh.normals + vertexCount * 3, 0.0f);
    std::fill(mesh.texcoords, mesh.texcoords + vertexCount * 2, 0.0f);
    std::fill(mesh.indices, mesh.indices + triangleCount * 3, 0);
    return mesh;
}

static void FreeMeshCpuData(Mesh& mesh) {
    delete[] mesh.vertices; mesh.vertices = nullptr;
    delete[] mesh.texcoords; mesh.texcoords = nullptr;
    delete[] mesh.texcoords2; mesh.texcoords2 = nullptr;
    delete[] mesh.normals; mesh.normals = nullptr;
    delete[] mesh.tangents; mesh.tangents = nullptr;
    delete[] mesh.colors; mesh.colors = nullptr;
    delete[] mesh.indices; mesh.indices = nullptr;
    delete[] mesh.boneIndices; mesh.boneIndices = nullptr;
    delete[] mesh.boneWeights; mesh.boneWeights = nullptr;
    delete[] mesh.animVertices; mesh.animVertices = nullptr;
    delete[] mesh.animNormals; mesh.animNormals = nullptr;
}

static Vec3 CalculateTriangleTangent(const Vec3& p0, const Vec3& p1, const Vec3& p2,
                                     const Vec2& uv0, const Vec2& uv1, const Vec2& uv2) {
    Vec3 edge1 = p1 - p0;
    Vec3 edge2 = p2 - p0;
    Vec2 deltaUV1{uv1.x - uv0.x, uv1.y - uv0.y};
    Vec2 deltaUV2{uv2.x - uv0.x, uv2.y - uv0.y};
    float denom = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
    if (std::fabs(denom) < EPSILON) return Vec3{1.0f, 0.0f, 0.0f};
    float inv = 1.0f / denom;
    Vec3 tangent{
        inv * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x),
        inv * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y),
        inv * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z)
    };
    return tangent.normalized();
}

static Vec3 GetHeightSample(const Image& heightmap, int x, int y) {
    if (!heightmap.data || x < 0 || y < 0 || x >= heightmap.width || y >= heightmap.height) return Vec3{0,0,0};
    float value = GetImageColor(heightmap, x, y).r / 255.0f;
    return Vec3{value, value, value};
}

} // namespace

void UploadMesh(Mesh* mesh, bool dynamic) {
    if (!mesh) return;
    gRenderer.UploadMesh(*mesh, dynamic);
}

void UpdateMeshBuffer(Mesh mesh, int index, const void* data, int dataSize, int offset) {
    gRenderer.UpdateMeshBuffer(mesh, index, data, dataSize, offset);
}

void UnloadMesh(Mesh mesh) {
    gRenderer.UnloadMesh(mesh);
}

void DrawMesh(Mesh mesh, Material material, Matrix transform) {
    gRenderer.DrawMesh(mesh, material, transform);
}

void DrawMeshInstanced(Mesh mesh, Material material, const Matrix* transforms, int instances) {
    gRenderer.DrawMeshInstanced(mesh, material, transforms, instances);
}

BoundingBox GetMeshBoundingBox(Mesh mesh) {
    BoundingBox box{};
    if (!mesh.vertices || mesh.vertexCount <= 0) return box;

    box.min = Vec3{mesh.vertices[0], mesh.vertices[1], mesh.vertices[2]};
    box.max = box.min;
    for (int i = 1; i < mesh.vertexCount; ++i) {
        Vec3 p{mesh.vertices[i * 3 + 0], mesh.vertices[i * 3 + 1], mesh.vertices[i * 3 + 2]};
        box.min.x = std::min(box.min.x, p.x);
        box.min.y = std::min(box.min.y, p.y);
        box.min.z = std::min(box.min.z, p.z);
        box.max.x = std::max(box.max.x, p.x);
        box.max.y = std::max(box.max.y, p.y);
        box.max.z = std::max(box.max.z, p.z);
    }
    return box;
}

BoundingBox GetModelBoundingBox(Model model) {
    BoundingBox box{};
    if (!model.meshes || model.meshCount <= 0) return box;

    bool initialized = false;
    for (int i = 0; i < model.meshCount; ++i) {
        BoundingBox meshBox = GetMeshBoundingBox(model.meshes[i]);
        if (!initialized) {
            box = meshBox;
            initialized = true;
        } else {
            box.min.x = std::min(box.min.x, meshBox.min.x);
            box.min.y = std::min(box.min.y, meshBox.min.y);
            box.min.z = std::min(box.min.z, meshBox.min.z);
            box.max.x = std::max(box.max.x, meshBox.max.x);
            box.max.y = std::max(box.max.y, meshBox.max.y);
            box.max.z = std::max(box.max.z, meshBox.max.z);
        }
    }
    return box;
}

void GenMeshTangents(Mesh* mesh) {
    if (!mesh || !mesh->vertices || !mesh->normals || !mesh->texcoords || mesh->vertexCount <= 0) return;
    if (!mesh->tangents) mesh->tangents = new float[mesh->vertexCount * 3];
    std::fill(mesh->tangents, mesh->tangents + mesh->vertexCount * 3, 0.0f);

    if (!mesh->indices) return;

    for (int t = 0; t < mesh->triangleCount; ++t) {
        int i0 = mesh->indices[t * 3 + 0];
        int i1 = mesh->indices[t * 3 + 1];
        int i2 = mesh->indices[t * 3 + 2];
        if (i0 >= mesh->vertexCount || i1 >= mesh->vertexCount || i2 >= mesh->vertexCount) continue;

        Vec3 p0{mesh->vertices[i0 * 3 + 0], mesh->vertices[i0 * 3 + 1], mesh->vertices[i0 * 3 + 2]};
        Vec3 p1{mesh->vertices[i1 * 3 + 0], mesh->vertices[i1 * 3 + 1], mesh->vertices[i1 * 3 + 2]};
        Vec3 p2{mesh->vertices[i2 * 3 + 0], mesh->vertices[i2 * 3 + 1], mesh->vertices[i2 * 3 + 2]};
        Vec2 uv0{mesh->texcoords[i0 * 2 + 0], mesh->texcoords[i0 * 2 + 1]};
        Vec2 uv1{mesh->texcoords[i1 * 2 + 0], mesh->texcoords[i1 * 2 + 1]};
        Vec2 uv2{mesh->texcoords[i2 * 2 + 0], mesh->texcoords[i2 * 2 + 1]};

        Vec3 tangent = CalculateTriangleTangent(p0, p1, p2, uv0, uv1, uv2);
        for (int idx : {i0, i1, i2}) {
            mesh->tangents[idx * 3 + 0] += tangent.x;
            mesh->tangents[idx * 3 + 1] += tangent.y;
            mesh->tangents[idx * 3 + 2] += tangent.z;
        }
    }

    for (int i = 0; i < mesh->vertexCount; ++i) {
        Vec3 t{mesh->tangents[i * 3 + 0], mesh->tangents[i * 3 + 1], mesh->tangents[i * 3 + 2]};
        Vec3 n{mesh->normals[i * 3 + 0], mesh->normals[i * 3 + 1], mesh->normals[i * 3 + 2]};
        Vec3 tangent = t.normalized();
        if (tangent.length() > 0.0f) {
            mesh->tangents[i * 3 + 0] = tangent.x;
            mesh->tangents[i * 3 + 1] = tangent.y;
            mesh->tangents[i * 3 + 2] = tangent.z;
        } else {
            mesh->tangents[i * 3 + 0] = 1.0f;
            mesh->tangents[i * 3 + 1] = 0.0f;
            mesh->tangents[i * 3 + 2] = 0.0f;
        }
    }
}

bool ExportMesh(Mesh mesh, const char* fileName) {
    if (!fileName || !mesh.vertices || mesh.vertexCount <= 0) return false;
    std::ofstream out(fileName, std::ios::binary);
    if (!out) return false;

    bool hasUV = mesh.texcoords != nullptr;
    bool hasNormal = mesh.normals != nullptr;

    out << "# Generated by QuarkCore\n";
    for (int i = 0; i < mesh.vertexCount; ++i) {
        out << "v " << mesh.vertices[i * 3 + 0] << " "
            << mesh.vertices[i * 3 + 1] << " "
            << mesh.vertices[i * 3 + 2] << "\n";
    }
    if (hasUV) {
        for (int i = 0; i < mesh.vertexCount; ++i) {
            out << "vt " << mesh.texcoords[i * 2 + 0] << " "
                << mesh.texcoords[i * 2 + 1] << "\n";
        }
    }
    if (hasNormal) {
        for (int i = 0; i < mesh.vertexCount; ++i) {
            out << "vn " << mesh.normals[i * 3 + 0] << " "
                << mesh.normals[i * 3 + 1] << " "
                << mesh.normals[i * 3 + 2] << "\n";
        }
    }

    if (mesh.indices && mesh.triangleCount > 0) {
        for (int t = 0; t < mesh.triangleCount; ++t) {
            int a = mesh.indices[t * 3 + 0] + 1;
            int b = mesh.indices[t * 3 + 1] + 1;
            int c = mesh.indices[t * 3 + 2] + 1;
            if (hasUV && hasNormal) {
                out << "f " << a << "/" << a << "/" << a << " "
                    << b << "/" << b << "/" << b << " "
                    << c << "/" << c << "/" << c << "\n";
            } else if (hasUV) {
                out << "f " << a << "/" << a << " "
                    << b << "/" << b << " "
                    << c << "/" << c << "\n";
            } else if (hasNormal) {
                out << "f " << a << "//" << a << " "
                    << b << "//" << b << " "
                    << c << "//" << c << "\n";
            } else {
                out << "f " << a << " " << b << " " << c << "\n";
            }
        }
    } else {
        for (int i = 0; i + 2 < mesh.vertexCount; i += 3) {
            out << "f " << (i + 1) << " " << (i + 2) << " " << (i + 3) << "\n";
        }
    }

    return true;
}

bool ExportMeshAsCode(Mesh mesh, const char* fileName) {
    if (!fileName || !mesh.vertices || mesh.vertexCount <= 0) return false;
    std::ofstream out(fileName, std::ios::binary);
    if (!out) return false;

    out << "#include \"QuarkCore/Quark3D.hpp\"\n\n";
    out << "static float meshVertices[] = {\n";
    for (int i = 0; i < mesh.vertexCount; ++i) {
        out << "    " << mesh.vertices[i * 3 + 0] << "f, "
            << mesh.vertices[i * 3 + 1] << "f, "
            << mesh.vertices[i * 3 + 2] << "f,\n";
    }
    out << "};\n\n";

    if (mesh.normals) {
        out << "static float meshNormals[] = {\n";
        for (int i = 0; i < mesh.vertexCount; ++i) {
            out << "    " << mesh.normals[i * 3 + 0] << "f, "
                << mesh.normals[i * 3 + 1] << "f, "
                << mesh.normals[i * 3 + 2] << "f,\n";
        }
        out << "};\n\n";
    }

    if (mesh.texcoords) {
        out << "static float meshTexcoords[] = {\n";
        for (int i = 0; i < mesh.vertexCount; ++i) {
            out << "    " << mesh.texcoords[i * 2 + 0] << "f, "
                << mesh.texcoords[i * 2 + 1] << "f,\n";
        }
        out << "};\n\n";
    }

    if (mesh.indices && mesh.triangleCount > 0) {
        out << "static unsigned short meshIndices[] = {\n";
        for (int i = 0; i < mesh.triangleCount * 3; ++i) {
            out << "    " << mesh.indices[i] << ",\n";
        }
        out << "};\n\n";
    }

    out << "Mesh mesh = {};\n";
    out << "mesh.vertexCount = " << mesh.vertexCount << ";\n";
    out << "mesh.triangleCount = " << mesh.triangleCount << ";\n";
    out << "mesh.vertices = meshVertices;\n";
    if (mesh.normals) out << "mesh.normals = meshNormals;\n";
    if (mesh.texcoords) out << "mesh.texcoords = meshTexcoords;\n";
    if (mesh.indices && mesh.triangleCount > 0) out << "mesh.indices = meshIndices;\n";

    return true;
}

Mesh GenMeshPoly(int sides, float radius) {
    if (sides < 3) return Mesh{};
    int vertexCount = sides + 1;
    int triangleCount = sides - 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);
    float angleStep = 2.0f * PI / sides;

    mesh.vertices[0] = 0.0f;
    mesh.vertices[1] = 0.0f;
    mesh.vertices[2] = 0.0f;
    mesh.normals[0] = 0.0f; mesh.normals[1] = 0.0f; mesh.normals[2] = 1.0f;
    mesh.texcoords[0] = 0.5f; mesh.texcoords[1] = 0.5f;

    for (int i = 0; i < sides; ++i) {
        float angle = i * angleStep;
        float x = std::cos(angle) * radius;
        float y = std::sin(angle) * radius;
        mesh.vertices[(i + 1) * 3 + 0] = x;
        mesh.vertices[(i + 1) * 3 + 1] = y;
        mesh.vertices[(i + 1) * 3 + 2] = 0.0f;
        mesh.normals[(i + 1) * 3 + 0] = 0.0f;
        mesh.normals[(i + 1) * 3 + 1] = 0.0f;
        mesh.normals[(i + 1) * 3 + 2] = 1.0f;
        mesh.texcoords[(i + 1) * 2 + 0] = x / (radius * 2.0f) + 0.5f;
        mesh.texcoords[(i + 1) * 2 + 1] = y / (radius * 2.0f) + 0.5f;
    }

    for (int i = 0; i < triangleCount; ++i) {
        mesh.indices[i * 3 + 0] = (unsigned short)0;
        mesh.indices[i * 3 + 1] = (unsigned short)(i + 1);
        mesh.indices[i * 3 + 2] = (unsigned short)(i + 2);
    }

    return mesh;
}

Mesh GenMeshPlane(float width, float length, int resX, int resZ) {
    if (width <= 0.0f || length <= 0.0f || resX <= 0 || resZ <= 0) return Mesh{};
    int vertsX = resX + 1;
    int vertsZ = resZ + 1;
    int vertexCount = vertsX * vertsZ;
    int triangleCount = resX * resZ * 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    for (int z = 0; z < vertsZ; ++z) {
        for (int x = 0; x < vertsX; ++x) {
            int index = z * vertsX + x;
            float fx = ((float)x / resX - 0.5f) * width;
            float fz = ((float)z / resZ - 0.5f) * length;
            mesh.vertices[index * 3 + 0] = fx;
            mesh.vertices[index * 3 + 1] = 0.0f;
            mesh.vertices[index * 3 + 2] = fz;
            mesh.normals[index * 3 + 0] = 0.0f;
            mesh.normals[index * 3 + 1] = 1.0f;
            mesh.normals[index * 3 + 2] = 0.0f;
            mesh.texcoords[index * 2 + 0] = (float)x / resX;
            mesh.texcoords[index * 2 + 1] = (float)z / resZ;
        }
    }

    int idx = 0;
    for (int z = 0; z < resZ; ++z) {
        for (int x = 0; x < resX; ++x) {
            int a = z * vertsX + x;
            int b = a + 1;
            int c = a + vertsX;
            int d = c + 1;
            mesh.indices[idx++] = (unsigned short)a;
            mesh.indices[idx++] = (unsigned short)c;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)c;
            mesh.indices[idx++] = (unsigned short)d;
        }
    }

    return mesh;
}

Mesh GenMeshCube(float width, float height, float length) {
    float hw = width * 0.5f;
    float hh = height * 0.5f;
    float hl = length * 0.5f;
    Mesh mesh = CreateMesh(24, 12);
    const Vec3 positions[24] = {
        {-hw, -hh,  hl}, { hw, -hh,  hl}, { hw,  hh,  hl}, {-hw,  hh,  hl},
        {-hw, -hh, -hl}, {-hw,  hh, -hl}, { hw,  hh, -hl}, { hw, -hh, -hl},
        {-hw,  hh, -hl}, {-hw,  hh,  hl}, { hw,  hh,  hl}, { hw,  hh, -hl},
        {-hw, -hh, -hl}, { hw, -hh, -hl}, { hw, -hh,  hl}, {-hw, -hh,  hl},
        { hw, -hh, -hl}, { hw,  hh, -hl}, { hw,  hh,  hl}, { hw, -hh,  hl},
        {-hw, -hh, -hl}, {-hw, -hh,  hl}, {-hw,  hh,  hl}, {-hw,  hh, -hl}
    };
    const Vec3 normals[24] = {
        {0,0,1},{0,0,1},{0,0,1},{0,0,1},
        {0,0,-1},{0,0,-1},{0,0,-1},{0,0,-1},
        {0,1,0},{0,1,0},{0,1,0},{0,1,0},
        {0,-1,0},{0,-1,0},{0,-1,0},{0,-1,0},
        {1,0,0},{1,0,0},{1,0,0},{1,0,0},
        {-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0}
    };
    const Vec2 uvs[24] = {
        {0,0},{1,0},{1,1},{0,1}, {0,0},{1,0},{1,1},{0,1},
        {0,0},{1,0},{1,1},{0,1}, {0,0},{1,0},{1,1},{0,1},
        {0,0},{1,0},{1,1},{0,1}, {0,0},{1,0},{1,1},{0,1}
    };
    for (int i = 0; i < 24; ++i) {
        mesh.vertices[i * 3 + 0] = positions[i].x;
        mesh.vertices[i * 3 + 1] = positions[i].y;
        mesh.vertices[i * 3 + 2] = positions[i].z;
        mesh.normals[i * 3 + 0] = normals[i].x;
        mesh.normals[i * 3 + 1] = normals[i].y;
        mesh.normals[i * 3 + 2] = normals[i].z;
        mesh.texcoords[i * 2 + 0] = uvs[i].x;
        mesh.texcoords[i * 2 + 1] = uvs[i].y;
    }
    const unsigned short indices[] = {
         0,  1,  2,  0,  2,  3,
         4,  5,  6,  4,  6,  7,
         8,  9, 10,  8, 10, 11,
        12, 13, 14, 12, 14, 15,
        16, 17, 18, 16, 18, 19,
        20, 21, 22, 20, 22, 23
    };
    std::copy(std::begin(indices), std::end(indices), mesh.indices);
    return mesh;
}

Mesh GenMeshSphere(float radius, int rings, int slices) {
    if (radius <= 0.0f || rings < 2 || slices < 3) return Mesh{};
    int vertexCount = (rings + 1) * (slices + 1);
    int triangleCount = rings * slices * 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    int v = 0;
    for (int r = 0; r <= rings; ++r) {
        float phi = PI * r / rings;
        for (int s = 0; s <= slices; ++s) {
            float theta = 2.0f * PI * s / slices;
            float x = std::sin(phi) * std::cos(theta);
            float y = std::cos(phi);
            float z = std::sin(phi) * std::sin(theta);
            mesh.vertices[v * 3 + 0] = x * radius;
            mesh.vertices[v * 3 + 1] = y * radius;
            mesh.vertices[v * 3 + 2] = z * radius;
            mesh.normals[v * 3 + 0] = x;
            mesh.normals[v * 3 + 1] = y;
            mesh.normals[v * 3 + 2] = z;
            mesh.texcoords[v * 2 + 0] = (float)s / slices;
            mesh.texcoords[v * 2 + 1] = (float)r / rings;
            ++v;
        }
    }

    int idx = 0;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < slices; ++s) {
            int a = r * (slices + 1) + s;
            int b = a + slices + 1;
            mesh.indices[idx++] = (unsigned short)a;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)(a + 1);
            mesh.indices[idx++] = (unsigned short)(a + 1);
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)(b + 1);
        }
    }
    return mesh;
}

Mesh GenMeshHemiSphere(float radius, int rings, int slices) {
    if (radius <= 0.0f || rings < 1 || slices < 3) return Mesh{};
    int vertexCount = (rings + 1) * (slices + 1);
    int triangleCount = rings * slices;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    int v = 0;
    for (int r = 0; r <= rings; ++r) {
        float phi = 0.5f * PI * r / rings;
        for (int s = 0; s <= slices; ++s) {
            float theta = 2.0f * PI * s / slices;
            float x = std::sin(phi) * std::cos(theta);
            float y = std::cos(phi);
            float z = std::sin(phi) * std::sin(theta);
            mesh.vertices[v * 3 + 0] = x * radius;
            mesh.vertices[v * 3 + 1] = y * radius;
            mesh.vertices[v * 3 + 2] = z * radius;
            mesh.normals[v * 3 + 0] = x;
            mesh.normals[v * 3 + 1] = y;
            mesh.normals[v * 3 + 2] = z;
            mesh.texcoords[v * 2 + 0] = (float)s / slices;
            mesh.texcoords[v * 2 + 1] = (float)r / rings;
            ++v;
        }
    }

    int idx = 0;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < slices; ++s) {
            int a = r * (slices + 1) + s;
            int b = a + slices + 1;
            mesh.indices[idx++] = (unsigned short)a;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)(a + 1);
            mesh.indices[idx++] = (unsigned short)(a + 1);
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)(b + 1);
        }
    }
    return mesh;
}

Mesh GenMeshCylinder(float radius, float height, int slices) {
    if (radius <= 0.0f || height <= 0.0f || slices < 3) return Mesh{};
    int vertexCount = (slices + 1) * 2 + 2;
    int triangleCount = slices * 4;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    int v = 0;
    for (int i = 0; i <= slices; ++i) {
        float theta = 2.0f * PI * i / slices;
        float x = std::cos(theta) * radius;
        float z = std::sin(theta) * radius;
        mesh.vertices[v * 3 + 0] = x;
        mesh.vertices[v * 3 + 1] = -height * 0.5f;
        mesh.vertices[v * 3 + 2] = z;
        mesh.normals[v * 3 + 0] = x;
        mesh.normals[v * 3 + 1] = 0.0f;
        mesh.normals[v * 3 + 2] = z;
        mesh.texcoords[v * 2 + 0] = (float)i / slices;
        mesh.texcoords[v * 2 + 1] = 0.0f;
        ++v;
        mesh.vertices[v * 3 + 0] = x;
        mesh.vertices[v * 3 + 1] = height * 0.5f;
        mesh.vertices[v * 3 + 2] = z;
        mesh.normals[v * 3 + 0] = x;
        mesh.normals[v * 3 + 1] = 0.0f;
        mesh.normals[v * 3 + 2] = z;
        mesh.texcoords[v * 2 + 0] = (float)i / slices;
        mesh.texcoords[v * 2 + 1] = 1.0f;
        ++v;
    }

    int topCenter = v++;
    int bottomCenter = v++;
    mesh.vertices[topCenter * 3 + 0] = 0.0f;
    mesh.vertices[topCenter * 3 + 1] = height * 0.5f;
    mesh.vertices[topCenter * 3 + 2] = 0.0f;
    mesh.normals[topCenter * 3 + 0] = 0.0f;
    mesh.normals[topCenter * 3 + 1] = 1.0f;
    mesh.normals[topCenter * 3 + 2] = 0.0f;
    mesh.texcoords[topCenter * 2 + 0] = 0.5f;
    mesh.texcoords[topCenter * 2 + 1] = 0.5f;

    mesh.vertices[bottomCenter * 3 + 0] = 0.0f;
    mesh.vertices[bottomCenter * 3 + 1] = -height * 0.5f;
    mesh.vertices[bottomCenter * 3 + 2] = 0.0f;
    mesh.normals[bottomCenter * 3 + 0] = 0.0f;
    mesh.normals[bottomCenter * 3 + 1] = -1.0f;
    mesh.normals[bottomCenter * 3 + 2] = 0.0f;
    mesh.texcoords[bottomCenter * 2 + 0] = 0.5f;
    mesh.texcoords[bottomCenter * 2 + 1] = 0.5f;

    int idx = 0;
    for (int i = 0; i < slices; ++i) {
        int lower0 = i * 2;
        int upper0 = lower0 + 1;
        int lower1 = ((i + 1) % (slices + 1)) * 2;
        int upper1 = lower1 + 1;

        mesh.indices[idx++] = (unsigned short)lower0;
        mesh.indices[idx++] = (unsigned short)upper0;
        mesh.indices[idx++] = (unsigned short)lower1;
        mesh.indices[idx++] = (unsigned short)upper0;
        mesh.indices[idx++] = (unsigned short)upper1;
        mesh.indices[idx++] = (unsigned short)lower1;

        mesh.indices[idx++] = (unsigned short)topCenter;
        mesh.indices[idx++] = (unsigned short)upper1;
        mesh.indices[idx++] = (unsigned short)upper0;

        mesh.indices[idx++] = (unsigned short)bottomCenter;
        mesh.indices[idx++] = (unsigned short)lower0;
        mesh.indices[idx++] = (unsigned short)lower1;
    }
    return mesh;
}

Mesh GenMeshCone(float radius, float height, int slices) {
    if (radius <= 0.0f || height <= 0.0f || slices < 3) return Mesh{};
    int vertexCount = slices + 2;
    int triangleCount = slices * 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    int apex = 0;
    mesh.vertices[0] = 0.0f;
    mesh.vertices[1] = height * 0.5f;
    mesh.vertices[2] = 0.0f;
    mesh.normals[0] = 0.0f;
    mesh.normals[1] = 1.0f;
    mesh.normals[2] = 0.0f;
    mesh.texcoords[0] = 0.5f;
    mesh.texcoords[1] = 0.5f;

    int baseCenter = 1;
    mesh.vertices[3] = 0.0f;
    mesh.vertices[4] = -height * 0.5f;
    mesh.vertices[5] = 0.0f;
    mesh.normals[3] = 0.0f;
    mesh.normals[4] = -1.0f;
    mesh.normals[5] = 0.0f;
    mesh.texcoords[2] = 0.5f;
    mesh.texcoords[3] = 0.5f;

    for (int i = 0; i < slices; ++i) {
        float theta = 2.0f * PI * i / slices;
        float x = std::cos(theta) * radius;
        float z = std::sin(theta) * radius;
        int v = 2 + i;
        mesh.vertices[v * 3 + 0] = x;
        mesh.vertices[v * 3 + 1] = -height * 0.5f;
        mesh.vertices[v * 3 + 2] = z;
        mesh.normals[v * 3 + 0] = x;
        mesh.normals[v * 3 + 1] = radius;
        mesh.normals[v * 3 + 2] = z;
        Vec3 n = Vec3{mesh.normals[v * 3 + 0], mesh.normals[v * 3 + 1], mesh.normals[v * 3 + 2]}.normalized();
        mesh.normals[v * 3 + 0] = n.x;
        mesh.normals[v * 3 + 1] = n.y;
        mesh.normals[v * 3 + 2] = n.z;
        mesh.texcoords[v * 2 + 0] = (std::cos(theta) + 1.0f) * 0.5f;
        mesh.texcoords[v * 2 + 1] = (std::sin(theta) + 1.0f) * 0.5f;
    }

    int idx = 0;
    for (int i = 0; i < slices; ++i) {
        int next = 2 + ((i + 1) % slices);
        mesh.indices[idx++] = (unsigned short)apex;
        mesh.indices[idx++] = (unsigned short)(2 + i);
        mesh.indices[idx++] = (unsigned short)next;
        mesh.indices[idx++] = (unsigned short)baseCenter;
        mesh.indices[idx++] = (unsigned short)next;
        mesh.indices[idx++] = (unsigned short)(2 + i);
    }
    return mesh;
}

Mesh GenMeshTorus(float radius, float size, int radSeg, int sides) {
    if (radius <= 0.0f || size <= 0.0f || radSeg < 3 || sides < 3) return Mesh{};
    int vertexCount = radSeg * sides;
    int triangleCount = radSeg * sides * 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    int v = 0;
    for (int ring = 0; ring < radSeg; ++ring) {
        float u = 2.0f * PI * ring / radSeg;
        Vec3 center{std::cos(u) * radius, 0.0f, std::sin(u) * radius};
        Vec3 ringDir{-std::sin(u), 0.0f, std::cos(u)};
        Vec3 ringUp{0.0f, 1.0f, 0.0f};
        for (int side = 0; side < sides; ++side) {
            float vAngle = 2.0f * PI * side / sides;
            float cx = std::cos(vAngle) * size;
            float cy = std::sin(vAngle) * size;
            Vec3 position = center + ringDir * cx + ringUp * cy;
            Vec3 normal = Vec3{ringDir.x * cx + ringUp.x * cy,
                               ringDir.y * cx + ringUp.y * cy,
                               ringDir.z * cx + ringUp.z * cy}.normalized();
            mesh.vertices[v * 3 + 0] = position.x;
            mesh.vertices[v * 3 + 1] = position.y;
            mesh.vertices[v * 3 + 2] = position.z;
            mesh.normals[v * 3 + 0] = normal.x;
            mesh.normals[v * 3 + 1] = normal.y;
            mesh.normals[v * 3 + 2] = normal.z;
            mesh.texcoords[v * 2 + 0] = (float)ring / radSeg;
            mesh.texcoords[v * 2 + 1] = (float)side / sides;
            ++v;
        }
    }

    int idx = 0;
    for (int ring = 0; ring < radSeg; ++ring) {
        for (int side = 0; side < sides; ++side) {
            int nextRing = (ring + 1) % radSeg;
            int nextSide = (side + 1) % sides;
            int a = ring * sides + side;
            int b = nextRing * sides + side;
            int c = nextRing * sides + nextSide;
            int d = ring * sides + nextSide;
            mesh.indices[idx++] = (unsigned short)a;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)d;
            mesh.indices[idx++] = (unsigned short)d;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)c;
        }
    }
    return mesh;
}

Mesh GenMeshKnot(float radius, float size, int radSeg, int sides) {
    if (radius <= 0.0f || size <= 0.0f || radSeg < 3 || sides < 3) return Mesh{};
    int vertexCount = radSeg * sides;
    int triangleCount = radSeg * sides * 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    auto knotPos = [&](float t) {
        float x = (2.0f + std::cos(3.0f * t)) * std::cos(2.0f * t);
        float y = (2.0f + std::cos(3.0f * t)) * std::sin(2.0f * t);
        float z = std::sin(3.0f * t);
        return Vec3{x, y, z} * radius;
    };

    auto knotTangent = [&](float t) {
        float dx = -2.0f * std::sin(2.0f * t) - 3.0f * std::sin(3.0f * t) * std::cos(2.0f * t) - 2.0f * std::sin(2.0f * t) * std::cos(3.0f * t);
        float dy =  2.0f * std::cos(2.0f * t) + 3.0f * std::sin(3.0f * t) * std::sin(2.0f * t) + 2.0f * std::cos(2.0f * t) * std::cos(3.0f * t);
        float dz =  3.0f * std::cos(3.0f * t);
        return Vec3{dx, dy, dz}.normalized();
    };

    for (int i = 0; i < radSeg; ++i) {
        float u = 2.0f * PI * i / radSeg;
        Vec3 center = knotPos(u);
        Vec3 tangent = knotTangent(u);
        Vec3 normal = Vec3{-tangent.y, tangent.x, 0.0f}.normalized();
        Vec3 binormal = tangent.cross(normal).normalized();
        for (int j = 0; j < sides; ++j) {
            float v = 2.0f * PI * j / sides;
            float cx = std::cos(v) * size;
            float cy = std::sin(v) * size;
            Vec3 position = center + normal * cx + binormal * cy;
            Vec3 n = (normal * cx + binormal * cy).normalized();
            int index = i * sides + j;
            mesh.vertices[index * 3 + 0] = position.x;
            mesh.vertices[index * 3 + 1] = position.y;
            mesh.vertices[index * 3 + 2] = position.z;
            mesh.normals[index * 3 + 0] = n.x;
            mesh.normals[index * 3 + 1] = n.y;
            mesh.normals[index * 3 + 2] = n.z;
            mesh.texcoords[index * 2 + 0] = (float)i / radSeg;
            mesh.texcoords[index * 2 + 1] = (float)j / sides;
        }
    }

    int idx = 0;
    for (int i = 0; i < radSeg; ++i) {
        int nextRing = (i + 1) % radSeg;
        for (int j = 0; j < sides; ++j) {
            int nextSide = (j + 1) % sides;
            int a = i * sides + j;
            int b = nextRing * sides + j;
            int c = nextRing * sides + nextSide;
            int d = i * sides + nextSide;
            mesh.indices[idx++] = (unsigned short)a;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)d;
            mesh.indices[idx++] = (unsigned short)d;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)c;
        }
    }
    return mesh;
}

Mesh GenMeshHeightmap(Image heightmap, Vec3 size) {
    if (!heightmap.data || heightmap.width <= 0 || heightmap.height <= 0) return Mesh{};
    int width = heightmap.width;
    int height = heightmap.height;
    int vertexCount = width * height;
    int triangleCount = (width - 1) * (height - 1) * 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = y * width + x;
            float fx = ((float)x / (width - 1) - 0.5f) * size.x;
            float fz = ((float)y / (height - 1) - 0.5f) * size.z;
            Vec3 sample = GetHeightSample(heightmap, x, y);
            float fy = (sample.x - 0.5f) * size.y;
            mesh.vertices[idx * 3 + 0] = fx;
            mesh.vertices[idx * 3 + 1] = fy;
            mesh.vertices[idx * 3 + 2] = fz;
            mesh.texcoords[idx * 2 + 0] = (float)x / (width - 1);
            mesh.texcoords[idx * 2 + 1] = (float)y / (height - 1);
        }
    }

    int idx = 0;
    for (int y = 0; y < height - 1; ++y) {
        for (int x = 0; x < width - 1; ++x) {
            int a = y * width + x;
            int b = a + 1;
            int c = a + width;
            int d = c + 1;
            mesh.indices[idx++] = (unsigned short)a;
            mesh.indices[idx++] = (unsigned short)c;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)b;
            mesh.indices[idx++] = (unsigned short)c;
            mesh.indices[idx++] = (unsigned short)d;
        }
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int index = y * width + x;
            Vec3 center{mesh.vertices[index * 3 + 0], mesh.vertices[index * 3 + 1], mesh.vertices[index * 3 + 2]};
            Vec3 left = GetHeightSample(heightmap, x - 1, y);
            Vec3 right = GetHeightSample(heightmap, x + 1, y);
            Vec3 down = GetHeightSample(heightmap, x, y - 1);
            Vec3 up = GetHeightSample(heightmap, x, y + 1);
            float dx = (right.x - left.x) * size.y;
            float dz = (up.x - down.x) * size.y;
            Vec3 normal = Vec3{-dx, 2.0f, -dz}.normalized();
            mesh.normals[index * 3 + 0] = normal.x;
            mesh.normals[index * 3 + 1] = normal.y;
            mesh.normals[index * 3 + 2] = normal.z;
        }
    }
    return mesh;
}

Mesh GenMeshCubicmap(Image cubicmap, Vec3 cubeSize) {
    if (!cubicmap.data || cubicmap.width <= 0 || cubicmap.height <= 0) return Mesh{};
    int width = cubicmap.width;
    int height = cubicmap.height;
    std::vector<Mesh> cubes;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Color pixel = GetImageColor(cubicmap, x, y);
            if (pixel.a == 0) continue;
            Mesh cube = GenMeshCube(cubeSize.x, cubeSize.y, cubeSize.z);
            float px = ((float)x - width * 0.5f + 0.5f) * cubeSize.x;
            float pz = ((float)y - height * 0.5f + 0.5f) * cubeSize.z;
            for (int v = 0; v < cube.vertexCount; ++v) {
                cube.vertices[v * 3 + 0] += px;
                cube.vertices[v * 3 + 1] += cubeSize.y * 0.5f;
                cube.vertices[v * 3 + 2] += pz;
            }
            cubes.push_back(std::move(cube));
        }
    }

    if (cubes.empty()) return Mesh{};
    int totalVerts = 0;
    int totalTris = 0;
    for (auto& cube : cubes) {
        totalVerts += cube.vertexCount;
        totalTris += cube.triangleCount;
    }

    Mesh mesh = CreateMesh(totalVerts, totalTris);
    int vOffset = 0;
    int iOffset = 0;
    for (auto& cube : cubes) {
        for (int v = 0; v < cube.vertexCount; ++v) {
            mesh.vertices[(vOffset + v) * 3 + 0] = cube.vertices[v * 3 + 0];
            mesh.vertices[(vOffset + v) * 3 + 1] = cube.vertices[v * 3 + 1];
            mesh.vertices[(vOffset + v) * 3 + 2] = cube.vertices[v * 3 + 2];
            mesh.normals[(vOffset + v) * 3 + 0] = cube.normals[v * 3 + 0];
            mesh.normals[(vOffset + v) * 3 + 1] = cube.normals[v * 3 + 1];
            mesh.normals[(vOffset + v) * 3 + 2] = cube.normals[v * 3 + 2];
            mesh.texcoords[(vOffset + v) * 2 + 0] = cube.texcoords[v * 2 + 0];
            mesh.texcoords[(vOffset + v) * 2 + 1] = cube.texcoords[v * 2 + 1];
        }
        for (int t = 0; t < cube.triangleCount * 3; ++t) {
            mesh.indices[iOffset + t] = (unsigned short)(cube.indices[t] + vOffset);
        }
        vOffset += cube.vertexCount;
        iOffset += cube.triangleCount * 3;
        FreeMeshCpuData(cube);
    }
    return mesh;
}

void DrawBoundingBox(BoundingBox box, Color color) {
    Vec3 vertices[8] = {
        {box.min.x, box.min.y, box.min.z},
        {box.max.x, box.min.y, box.min.z},
        {box.max.x, box.max.y, box.min.z},
        {box.min.x, box.max.y, box.min.z},
        {box.min.x, box.min.y, box.max.z},
        {box.max.x, box.min.y, box.max.z},
        {box.max.x, box.max.y, box.max.z},
        {box.min.x, box.max.y, box.max.z}
    };

    gRenderer.DrawLine3D(vertices[0], vertices[1], color);
    gRenderer.DrawLine3D(vertices[1], vertices[2], color);
    gRenderer.DrawLine3D(vertices[2], vertices[3], color);
    gRenderer.DrawLine3D(vertices[3], vertices[0], color);

    gRenderer.DrawLine3D(vertices[4], vertices[5], color);
    gRenderer.DrawLine3D(vertices[5], vertices[6], color);
    gRenderer.DrawLine3D(vertices[6], vertices[7], color);
    gRenderer.DrawLine3D(vertices[7], vertices[4], color);

    gRenderer.DrawLine3D(vertices[0], vertices[4], color);
    gRenderer.DrawLine3D(vertices[1], vertices[5], color);
    gRenderer.DrawLine3D(vertices[2], vertices[6], color);
    gRenderer.DrawLine3D(vertices[3], vertices[7], color);
}

void DrawBillboard(const Camera3D& camera, Texture2D texture, Vec3 position, float scale, Color tint) {
    Rectangle source{0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)};
    Vec2 size{static_cast<float>(texture.width) * scale, static_cast<float>(texture.height) * scale};
    DrawBillboardRec(camera, texture, source, position, size, tint);
}

void DrawBillboardRec(const Camera3D& camera, Texture2D texture, Rectangle source,
                      Vec3 position, Vec2 size, Color tint) {
    DrawBillboardPro(camera, texture, source, position, camera.up, size, Vec2{0.5f, 0.5f}, 0.0f, tint);
}

void DrawBillboardPro(const Camera3D& camera, Texture2D texture, Rectangle source,
                      Vec3 position, Vec3 up, Vec2 size, Vec2 origin, float rotation, Color tint) {
    Vec3 screenPos = GetWorldToScreen(position, camera);
    Vec3 screenUp = GetWorldToScreen(position + up, camera);
    float angle = std::atan2(screenUp.y - screenPos.y, screenUp.x - screenPos.x) * (180.0f / 3.14159265359f);
    angle += rotation;

    Rectangle dest{
        screenPos.x - size.x * origin.x,
        screenPos.y - size.y * origin.y,
        size.x,
        size.y
    };
    Vec2 originPixels{size.x * origin.x, size.y * origin.y};
    DrawTexturePro(texture, source, dest, originPixels, angle, tint);
}

Vec2 GetWorldToScreen2D(Vec2 position, Camera2D camera) {
    float dx = position.x - camera.target.x;
    float dy = position.y - camera.target.y;
    float cosA = std::cos(camera.rotation * 3.14159265359f / 180.f);
    float sinA = std::sin(camera.rotation * 3.14159265359f / 180.f);
    return Vec2{
        camera.offset.x + (dx * cosA - dy * sinA) * camera.zoom,
        camera.offset.y + (dx * sinA + dy * cosA) * camera.zoom
    };
}

Vec2 GetScreenToWorld2D(Vec2 position, Camera2D camera) {
    float cosA  = std::cos(camera.rotation * 3.14159265359f / 180.f);
    float sinA  = std::sin(camera.rotation * 3.14159265359f / 180.f);
    float scale = camera.zoom;
    float tx    = camera.offset.x - camera.target.x * scale * cosA - camera.target.y * scale * sinA;
    float ty    = camera.offset.y + camera.target.x * scale * sinA - camera.target.y * scale * cosA;
    float x     = (position.x - tx) * scale * cosA - (position.y - ty) * scale * sinA;
    float y     = (position.x - tx) * scale * sinA + (position.y - ty) * scale * cosA;
    return Vec2{ camera.target.x + x / (scale * scale),
                 camera.target.y + y / (scale * scale) };
}

Vec3 GetWorldToScreen(Vec3 position, Camera3D camera) {
    Vec3 forward = (camera.target - camera.position).normalized();
    Vec3 right   = forward.cross(camera.up).normalized();
    Vec3 up      = right.cross(forward);
    Vec3 rel     = position - camera.position;
    float cX = rel.dot(right), cY = rel.dot(up), cZ = rel.dot(forward);

    float sw = static_cast<float>(GetScreenWidth());
    float sh = static_cast<float>(GetScreenHeight());
    if (sw <= 0.f || sh <= 0.f) return Vec3{0.f, 0.f, cZ};

    float aspect    = sw / sh;
    float fovRad    = camera.fovy * 3.14159265359f / 180.f;
    float halfH     = std::tan(fovRad * .5f);
    float halfW     = halfH * aspect;

    float ndcX, ndcY;
    if (camera.projection == CAMERA_ORTHOGRAPHIC) {
        float os = camera.fovy > 0.f ? camera.fovy : 1.f;
        ndcX = cX / (os * aspect);
        ndcY = cY / os;
    } else {
        if (cZ == 0.f) cZ = 1e-6f;
        ndcX = cX / (cZ * halfW);
        ndcY = cY / (cZ * halfH);
    }
    return Vec3{ (ndcX * .5f + .5f) * sw, (.5f - ndcY * .5f) * sh, cZ };
}

Ray GetScreenToWorldRay(Vec2 mouse, Camera3D camera) {
    return GetScreenToWorldRayEx(mouse, camera, GetScreenWidth(), GetScreenHeight());
}

Ray GetScreenToWorldRayEx(Vec2 mouse, Camera3D camera, int width, int height) {
    Ray ray;
    ray.position = camera.position;
    Vec3 forward = (camera.target - camera.position).normalized();
    Vec3 right   = forward.cross(camera.up).normalized();
    Vec3 up      = right.cross(forward);

    float sw = static_cast<float>(width);
    float sh = static_cast<float>(height);
    if (sw <= 0.f || sh <= 0.f) return ray;

    float aspect  = sw / sh;
    float fovRad  = camera.fovy * 3.14159265359f / 180.f;
    float fovH    = 2.f * std::tan(fovRad / 2.f);
    float fovW    = fovH * aspect;
    float x       = (mouse.x / sw - .5f) * fovW;
    float y       = (.5f - mouse.y / sh) * fovH;

    ray.direction = Vec3{
        forward.x + right.x * x + up.x * y,
        forward.y + right.y * x + up.y * y,
        forward.z + right.z * x + up.z * y
    }.normalized();
    return ray;
}

Vec2 GetWorldToScreenEx(Vec3 position, Camera3D camera, int width, int height) {
    Vec3 forward = (camera.target - camera.position).normalized();
    Vec3 right   = forward.cross(camera.up).normalized();
    Vec3 up      = right.cross(forward);
    Vec3 rel     = position - camera.position;
    float cX = rel.dot(right), cY = rel.dot(up), cZ = rel.dot(forward);

    float sw = static_cast<float>(width);
    float sh = static_cast<float>(height);
    if (sw <= 0.f || sh <= 0.f) return Vec2{};

    float aspect    = sw / sh;
    float fovRad    = camera.fovy * 3.14159265359f / 180.f;
    float halfH     = std::tan(fovRad * .5f);
    float halfW     = halfH * aspect;

    float ndcX, ndcY;
    if (camera.projection == CAMERA_ORTHOGRAPHIC) {
        float os = camera.fovy > 0.f ? camera.fovy : 1.f;
        ndcX = cX / (os * aspect);
        ndcY = cY / os;
    } else {
        if (cZ == 0.f) cZ = 1e-6f;
        ndcX = cX / (cZ * halfW);
        ndcY = cY / (cZ * halfH);
    }
    return Vec2{ (ndcX * .5f + .5f) * sw, (.5f - ndcY * .5f) * sh };
}

bool CheckCollisionRecs(Rectangle a, Rectangle b) {
    return !(a.x + a.width < b.x || b.x + b.width < a.x ||
             a.y + a.height < b.y || b.y + b.height < a.y);
}

bool CheckCollisionCircles(Vec2 c1, float r1, Vec2 c2, float r2) {
    float dx = c2.x - c1.x, dy = c2.y - c1.y;
    return std::sqrt(dx*dx + dy*dy) < (r1 + r2);
}

bool CheckCollisionPointRec(Vec2 point, Rectangle rect) {
    return point.x >= rect.x && point.x <= rect.x + rect.width &&
           point.y >= rect.y && point.y <= rect.y + rect.height;
}

bool CheckCollisionPointCircle(Vec2 point, Vec2 center, float radius) {
    float dx = point.x - center.x, dy = point.y - center.y;
    return std::sqrt(dx*dx + dy*dy) <= radius;
}

bool CheckCollisionCircleRec(Vec2 center, float radius, Rectangle rec) {
    const float closestX = std::clamp(center.x, rec.x, rec.x + rec.width);
    const float closestY = std::clamp(center.y, rec.y, rec.y + rec.height);
    const float dx = center.x - closestX, dy = center.y - closestY;
    return (dx * dx + dy * dy) <= (radius * radius);
}

bool CheckCollisionCircleLine(Vec2 center, float radius, Vec2 p1, Vec2 p2) {
    const float ldx = p2.x - p1.x, ldy = p2.y - p1.y;
    const float len2 = ldx * ldx + ldy * ldy;
    if (len2 <= 1e-9f) {
        const float ddx = center.x - p1.x, ddy = center.y - p1.y;
        return (ddx * ddx + ddy * ddy) <= (radius * radius);
    }
    float t = ((center.x - p1.x) * ldx + (center.y - p1.y) * ldy) / len2;
    t = std::clamp(t, 0.0f, 1.0f);
    const float px = p1.x + t * ldx, py = p1.y + t * ldy;
    const float distX = center.x - px, distY = center.y - py;
    return (distX * distX + distY * distY) <= (radius * radius);
}

bool CheckCollisionPointTriangle(Vec2 point, Vec2 p1, Vec2 p2, Vec2 p3) {
    auto sign = [](Vec2 a, Vec2 b, Vec2 c) {
        return (a.x - c.x) * (b.y - c.y) - (b.x - c.x) * (a.y - c.y);
    };
    const float d1 = sign(point, p1, p2);
    const float d2 = sign(point, p2, p3);
    const float d3 = sign(point, p3, p1);
    const bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    const bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
    return !(hasNeg && hasPos);
}

bool CheckCollisionPointLine(Vec2 point, Vec2 p1, Vec2 p2, int threshold) {
    const float ldx = p2.x - p1.x, ldy = p2.y - p1.y;
    const float len2 = ldx * ldx + ldy * ldy;
    if (len2 <= 1e-9f) {
        const float ddx = p1.x - point.x, ddy = p1.y - point.y;
        return std::sqrt(ddx * ddx + ddy * ddy) <= static_cast<float>(threshold);
    }
    float t = ((point.x - p1.x) * ldx + (point.y - p1.y) * ldy) / len2;
    t = std::clamp(t, 0.0f, 1.0f);
    const float px = p1.x + t * ldx, py = p1.y + t * ldy;
    const float ddx = px - point.x, ddy = py - point.y;
    return std::sqrt(ddx * ddx + ddy * ddy) <= static_cast<float>(threshold);
}

bool CheckCollisionPointPoly(Vec2 point, const Vec2* points, int pointCount) {
    if (!points || pointCount < 3) return false;
    bool inside = false;
    int j = pointCount - 1;
    for (int i = 0; i < pointCount; ++i) {
        const Vec2 vi = points[i];
        const Vec2 vj = points[j];
        if (((vi.y > point.y) != (vj.y > point.y)) &&
            (point.x < (vj.x - vi.x) * (point.y - vi.y) / (vj.y - vi.y) + vi.x)) {
            inside = !inside;
        }
        j = i;
    }
    return inside;
}

bool CheckCollisionLines(Vec2 startPos1, Vec2 endPos1, Vec2 startPos2, Vec2 endPos2, Vec2* collisionPoint) {
    const float d1x = endPos1.x - startPos1.x, d1y = endPos1.y - startPos1.y;
    const float d2x = endPos2.x - startPos2.x, d2y = endPos2.y - startPos2.y;
    const float denom = d1x * d2y - d1y * d2x;
    if (std::fabs(denom) <= 1e-9f) return false;

    const float rx = startPos2.x - startPos1.x, ry = startPos2.y - startPos1.y;
    const float t = (rx * d2y - ry * d2x) / denom;
    const float u = (rx * d1y - ry * d1x) / denom;
    if (t < 0.0f || t > 1.0f || u < 0.0f || u > 1.0f) return false;

    if (collisionPoint) *collisionPoint = Vec2{ startPos1.x + d1x * t, startPos1.y + d1y * t };
    return true;
}

Rectangle GetCollisionRec(Rectangle rec1, Rectangle rec2) {
    Rectangle out{};
    const float x1 = std::max(rec1.x, rec2.x);
    const float y1 = std::max(rec1.y, rec2.y);
    const float x2 = std::min(rec1.x + rec1.width, rec2.x + rec2.width);
    const float y2 = std::min(rec1.y + rec1.height, rec2.y + rec2.height);
    if (x2 < x1 || y2 < y1) return out;
    out.x = x1;
    out.y = y1;
    out.width = x2 - x1;
    out.height = y2 - y1;
    return out;
}

bool CheckCollisionSpheres(Vec3 center1, float radius1, Vec3 center2, float radius2) {
    const float dx = center2.x - center1.x;
    const float dy = center2.y - center1.y;
    const float dz = center2.z - center1.z;
    const float dist2 = dx * dx + dy * dy + dz * dz;
    const float r = radius1 + radius2;
    return dist2 <= (r * r);
}

bool CheckCollisionBoxes(BoundingBox box1, BoundingBox box2) {
    return (box1.max.x >= box2.min.x && box2.max.x >= box1.min.x) &&
           (box1.max.y >= box2.min.y && box2.max.y >= box1.min.y) &&
           (box1.max.z >= box2.min.z && box2.max.z >= box1.min.z);
}

bool CheckCollisionBoxSphere(BoundingBox box, Vec3 center, float radius) {
    const float closestX = std::clamp(center.x, box.min.x, box.max.x);
    const float closestY = std::clamp(center.y, box.min.y, box.max.y);
    const float closestZ = std::clamp(center.z, box.min.z, box.max.z);
    const float dx = center.x - closestX;
    const float dy = center.y - closestY;
    const float dz = center.z - closestZ;
    return (dx * dx + dy * dy + dz * dz) <= (radius * radius);
}

RayCollision GetRayCollisionSphere(Ray ray, Vec3 center, float radius) {
    RayCollision result{};
    const Vec3 dir = ray.direction;
    const float a = dir.dot(dir);
    if (a <= 0.0f) return result;

    const Vec3 oc = ray.position - center;
    const float b = 2.0f * dir.dot(oc);
    const float c = oc.dot(oc) - radius * radius;
    const float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f) return result;

    const float t = (-b - std::sqrt(discriminant)) / (2.0f * a);
    if (t < 0.0f) return result;

    result.hit = true;
    result.distance = t;
    result.point = ray.position + dir * t;
    result.normal = (result.point - center).normalized();
    return result;
}

RayCollision GetRayCollisionMesh(Ray ray, Mesh mesh, Matrix transform) {
    RayCollision result{};
    if (!mesh.vertices || mesh.vertexCount <= 0) return result;

    bool hitAny = false;
    float bestDistance = INFINITY;
    const bool indexed = (mesh.indices != nullptr) && (mesh.triangleCount > 0);

    const int triangleCount = mesh.triangleCount > 0 ? mesh.triangleCount : mesh.vertexCount / 3;
    for (int i = 0; i < triangleCount; ++i) {
        int v0, v1, v2;
        if (indexed) {
            v0 = mesh.indices[i * 3 + 0];
            v1 = mesh.indices[i * 3 + 1];
            v2 = mesh.indices[i * 3 + 2];
        } else {
            v0 = i * 3 + 0;
            v1 = i * 3 + 1;
            v2 = i * 3 + 2;
        }
        if (v0 < 0 || v1 < 0 || v2 < 0 || v0 >= mesh.vertexCount ||
            v1 >= mesh.vertexCount || v2 >= mesh.vertexCount) continue;

        const Vec3 a = transform * Vec3{ mesh.vertices[v0 * 3 + 0], mesh.vertices[v0 * 3 + 1], mesh.vertices[v0 * 3 + 2] };
        const Vec3 b = transform * Vec3{ mesh.vertices[v1 * 3 + 0], mesh.vertices[v1 * 3 + 1], mesh.vertices[v1 * 3 + 2] };
        const Vec3 c = transform * Vec3{ mesh.vertices[v2 * 3 + 0], mesh.vertices[v2 * 3 + 1], mesh.vertices[v2 * 3 + 2] };

        const RayCollision tri = GetRayCollisionTriangle(ray, a, b, c);
        if (tri.hit && tri.distance < bestDistance) {
            bestDistance = tri.distance;
            result = tri;
            hitAny = true;
        }
    }

    return hitAny ? result : RayCollision{};
}

RayCollision GetRayCollisionQuad(Ray ray, Vec3 p1, Vec3 p2, Vec3 p3, Vec3 p4) {
    RayCollision result{};
    const RayCollision tri1 = GetRayCollisionTriangle(ray, p1, p2, p3);
    const RayCollision tri2 = GetRayCollisionTriangle(ray, p1, p3, p4);
    if (tri1.hit && tri2.hit) return (tri1.distance < tri2.distance) ? tri1 : tri2;
    return tri1.hit ? tri1 : tri2;
}

Color Fade(Color color, float alpha)           { color.a = static_cast<unsigned char>(color.a * alpha); return color; }
Color ColorAlpha(Color color, float alpha)     { color.a = static_cast<unsigned char>(255.f * alpha);   return color; }

Color ColorTint(Color color, Color tint) {
    return Color{
        static_cast<unsigned char>((color.r / 255.f) * (tint.r / 255.f) * 255.f),
        static_cast<unsigned char>((color.g / 255.f) * (tint.g / 255.f) * 255.f),
        static_cast<unsigned char>((color.b / 255.f) * (tint.b / 255.f) * 255.f),
        color.a
    };
}

Color ColorBrightness(Color color, float factor) {
    return Color{
        static_cast<unsigned char>(std::clamp(color.r * factor, 0.f, 255.f)),
        static_cast<unsigned char>(std::clamp(color.g * factor, 0.f, 255.f)),
        static_cast<unsigned char>(std::clamp(color.b * factor, 0.f, 255.f)),
        color.a
    };
}

Color ColorContrast(Color color, float contrast) {
    auto channel = [&](unsigned char ch) -> unsigned char {
        float v = (ch / 255.f - .5f) * contrast + .5f;
        return static_cast<unsigned char>(std::clamp(v * 255.f, 0.f, 255.f));
    };
    return Color{ channel(color.r), channel(color.g), channel(color.b), color.a };
}

Color GetColor(unsigned int hex) {
    return Color{
        static_cast<unsigned char>((hex >> 16) & 0xFF),
        static_cast<unsigned char>((hex >>  8) & 0xFF),
        static_cast<unsigned char>( hex        & 0xFF),
        255
    };
}

Color ColorFromNormalized(float r, float g, float b, float a) {
    return Color{
        static_cast<unsigned char>(std::clamp(r, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(g, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(b, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(a, 0.0f, 1.0f) * 255.0f)
    };
}

bool ColorIsEqual(Color col1, Color col2) {
    return (col1.r == col2.r) && (col1.g == col2.g) &&
           (col1.b == col2.b) && (col1.a == col2.a);
}

int ColorToInt(Color color) {
    return (static_cast<int>(color.a) << 24) |
           (static_cast<int>(color.r) << 16) |
           (static_cast<int>(color.g) << 8)  |
           static_cast<int>(color.b);
}

Vec4 ColorNormalize(Color color) {
    return Vec4{
        color.r / 255.0f,
        color.g / 255.0f,
        color.b / 255.0f,
        color.a / 255.0f
    };
}

Vec3 ColorToHSV(Color color) {
    float r = color.r / 255.0f;
    float g = color.g / 255.0f;
    float b = color.b / 255.0f;

    float max = std::max(std::max(r, g), b);
    float min = std::min(std::min(r, g), b);
    float delta = max - min;

    float hue = 0.0f;
    float saturation = (max > 0.0001f) ? (delta / max) : 0.0f;
    float value = max;

    if (delta > 0.0001f) {
        if (max == r) hue = (g - b) / delta + ((g < b) ? 6.0f : 0.0f);
        else if (max == g) hue = (b - r) / delta + 2.0f;
        else hue = (r - g) / delta + 4.0f;
        hue *= 60.0f;
    }
    return Vec3{hue, saturation, value};
}

Color ColorFromHSV(float hue, float saturation, float value) {
    float h = hue;
    while (h < 0.0f) h += 360.0f;
    h = std::fmod(h, 360.0f) / 60.0f;
    int sector = static_cast<int>(h);
    float f = h - sector;
    float p = value * (1.0f - saturation);
    float q = value * (1.0f - saturation * f);
    float t = value * (1.0f - saturation * (1.0f - f));

    float r = 0.0f, g = 0.0f, b = 0.0f;
    switch (sector) {
        case 0: r = value; g = t; b = p; break;
        case 1: r = q; g = value; b = p; break;
        case 2: r = p; g = value; b = t; break;
        case 3: r = p; g = q; b = value; break;
        case 4: r = t; g = p; b = value; break;
        default: r = value; g = p; b = q; break;
    }
    return Color{
        static_cast<unsigned char>(std::clamp(r, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(g, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(b, 0.0f, 1.0f) * 255.0f),
        255
    };
}

Color ColorAlphaBlend(Color dst, Color src, Color tint) {
    Color srcTint{
        static_cast<unsigned char>(static_cast<int>(src.r) * static_cast<int>(tint.r) / 255),
        static_cast<unsigned char>(static_cast<int>(src.g) * static_cast<int>(tint.g) / 255),
        static_cast<unsigned char>(static_cast<int>(src.b) * static_cast<int>(tint.b) / 255),
        static_cast<unsigned char>(static_cast<int>(src.a) * static_cast<int>(tint.a) / 255)
    };
    float alpha = static_cast<float>(
        static_cast<int>(srcTint.a) + static_cast<int>(dst.a) * (255 - static_cast<int>(srcTint.a)) / 255) / 255.0f;
    float multiplier = (alpha <= 0.0f) ? 0.0f : (static_cast<float>(srcTint.a) / 255.0f / alpha);
    auto chan = [&](unsigned char d, unsigned char s) -> unsigned char {
        return static_cast<unsigned char>(std::clamp(
            static_cast<float>(d) * (1.0f - multiplier) + static_cast<float>(s) * multiplier, 0.0f, 255.0f));
    };
    return Color{
        chan(dst.r, srcTint.r),
        chan(dst.g, srcTint.g),
        chan(dst.b, srcTint.b),
        static_cast<unsigned char>(std::clamp(alpha * 255.0f, 0.0f, 255.0f))
    };
}

Color ColorLerp(Color color1, Color color2, float factor) {
    return Color{
        static_cast<unsigned char>(std::clamp(color1.r + (color2.r - color1.r) * factor, 0.0f, 255.0f)),
        static_cast<unsigned char>(std::clamp(color1.g + (color2.g - color1.g) * factor, 0.0f, 255.0f)),
        static_cast<unsigned char>(std::clamp(color1.b + (color2.b - color1.b) * factor, 0.0f, 255.0f)),
        static_cast<unsigned char>(std::clamp(color1.a + (color2.a - color1.a) * factor, 0.0f, 255.0f))
    };
}

void WaitTime(double seconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(seconds * 1000.0)));
}

int  GetRandomValue(int min, int max) {
    if (min > max) std::swap(min, max);
    return min + (std::rand() % (max - min + 1));
}

int* LoadRandomSequence(unsigned int count, int min, int max) {
    if (count == 0) return nullptr;
    if (min > max) std::swap(min, max);

    int* sequence = static_cast<int*>(std::malloc(static_cast<size_t>(count) * sizeof(int)));
    if (!sequence) return nullptr;

    for (unsigned int i = 0; i < count; ++i) {
        sequence[i] = GetRandomValue(min, max);
    }

    return sequence;
}

void UnloadRandomSequence(int* sequence) {
    std::free(sequence);
}

void SetRandomSeed(unsigned int seed) { std::srand(seed); }

int GetGlyphIndex(Font font, int codepoint) {
    if (!font.valid || !font.glyphs || font.glyphCount <= 0) return 0;
    for (int i = 0; i < font.glyphCount; ++i) {
        if (font.glyphs[i].value == codepoint) return i;
    }
    for (int i = 0; i < font.glyphCount; ++i) {
        if (font.glyphs[i].value == 63) return i;
    }
    return 0;
}

GlyphInfo GetGlyphInfo(Font font, int codepoint) {
    if (!font.valid || !font.glyphs) return GlyphInfo{};
    return font.glyphs[GetGlyphIndex(font, codepoint)];
}

Rectangle GetGlyphAtlasRec(Font font, int codepoint) {
    if (!font.valid || !font.recs) return Rectangle{};
    return font.recs[GetGlyphIndex(font, codepoint)];
}

Vec2 MeasureTextCodepoints(Font font, const int* codepoints, int length, float fontSize, float spacing) {
    if (!codepoints || !font.valid || !font.glyphs || font.baseSize <= 0) return {};
    const float scale = fontSize / static_cast<float>(font.baseSize);
    const float lineHeight = static_cast<float>(std::max(gTextLineSpacing > 0 ? gTextLineSpacing : font.baseSize, 1)) * scale;
    float maxW = 0.0f, x = 0.0f;
    int lines = 1;
    for (int i = 0; i < length; ++i) {
        const int cp = codepoints[i];
        if (cp == '\n') {
            maxW = std::max(maxW, x);
            x = 0.0f;
            ++lines;
            continue;
        }
        const int index = GetGlyphIndex(font, cp);
        x += font.glyphs[index].advanceX * scale + spacing;
    }
    maxW = std::max(maxW, x);
    return Vec2{ maxW, lineHeight * static_cast<float>(lines) };
}

void DrawTextCodepoint(Font font, int codepoint, Vec2 position, float fontSize, Color tint) {
    if (!font.valid || !font.glyphs || !font.recs || font.texture.id == 0 || font.baseSize <= 0) return;
    const int index = GetGlyphIndex(font, codepoint);
    if (font.glyphs[index].image.data == nullptr) return;
    const float scale = fontSize / static_cast<float>(font.baseSize);
    const float w = static_cast<float>(font.glyphs[index].image.width) * scale;
    const float h = static_cast<float>(font.glyphs[index].image.height) * scale;
    ITexture it{ font.texture.id, font.texture.width, font.texture.height, font.texture.mipmaps, font.texture.format, font.texture.valid };
    gRenderer.DrawTexturePro(it, font.recs[index], Rectangle{ position.x, position.y, w, h }, Vec2{}, 0.0f, tint);
}

void DrawTextCodepoints(Font font, const int* codepoints, int codepointCount, Vec2 position, float fontSize, float spacing, Color tint) {
    if (!codepoints || !font.valid || !font.glyphs || !font.recs || font.texture.id == 0 || font.baseSize <= 0) return;
    const float scale = fontSize / static_cast<float>(font.baseSize);
    const float lineHeight = static_cast<float>(std::max(gTextLineSpacing > 0 ? gTextLineSpacing : font.baseSize, 1)) * scale;
    ITexture it{ font.texture.id, font.texture.width, font.texture.height, font.texture.mipmaps, font.texture.format, font.texture.valid };
    float x = 0.0f, y = 0.0f;
    for (int i = 0; i < codepointCount; ++i) {
        const int cp = codepoints[i];
        if (cp == '\n') {
            x = 0.0f;
            y += lineHeight;
            continue;
        }
        const int index = GetGlyphIndex(font, cp);
        if (font.glyphs[index].image.data != nullptr) {
            const float w = static_cast<float>(font.glyphs[index].image.width) * scale;
            const float h = static_cast<float>(font.glyphs[index].image.height) * scale;
            gRenderer.DrawTexturePro(it, font.recs[index], Rectangle{ position.x + x, position.y + y, w, h }, Vec2{}, 0.0f, tint);
        }
        x += font.glyphs[index].advanceX * scale + spacing;
    }
}

void DrawTextPro(Font font, const char* text, Vec2 position, Vec2 origin, float rotation, float fontSize, float spacing, Color tint) {
    if (!text || !font.valid || !font.glyphs || !font.recs || font.texture.id == 0 || font.baseSize <= 0) return;
    const float scale = fontSize / static_cast<float>(font.baseSize);
    const float cosR = std::cos(rotation * DEG2RAD);
    const float sinR = std::sin(rotation * DEG2RAD);
    ITexture it{ font.texture.id, font.texture.width, font.texture.height, font.texture.mipmaps, font.texture.format, font.texture.valid };
    Vec2 textOffset{0, 0};

    const char* p = text;
    while (*p != '\0') {
        const unsigned char lead = static_cast<unsigned char>(*p);
        int cp = 0, seq = 0;
        if ((lead & 0x80) == 0) { cp = lead; seq = 1; }
        else if ((lead & 0xE0) == 0xC0) { cp = lead & 0x1F; seq = 2; }
        else if ((lead & 0xF0) == 0xE0) { cp = lead & 0x0F; seq = 3; }
        else if ((lead & 0xF8) == 0xF0) { cp = lead & 0x07; seq = 4; }
        else { cp = lead; seq = 1; }
        for (int k = 1; k < seq && p[k] != '\0'; ++k) cp = (cp << 6) | (static_cast<unsigned char>(p[k]) & 0x3F);

        const int index = GetGlyphIndex(font, cp);
        if (font.glyphs[index].image.data != nullptr) {
            Vec2 charPos{ position.x + textOffset.x, position.y + textOffset.y };
            charPos.x -= origin.x * cosR - origin.y * sinR;
            charPos.y -= origin.x * sinR + origin.y * cosR;
            const float w = static_cast<float>(font.glyphs[index].image.width) * scale;
            const float h = static_cast<float>(font.glyphs[index].image.height) * scale;
            gRenderer.DrawTexturePro(it, font.recs[index],
                                     Rectangle{ charPos.x - w / 2.0f, charPos.y - h / 2.0f, w, h },
                                     Vec2{ w / 2.0f, h / 2.0f }, rotation, tint);
        }
        textOffset.x += font.glyphs[index].advanceX * scale + spacing;
        p += seq;
    }
}

void SetTextLineSpacing(int spacing) {
    gTextLineSpacing = spacing;
}

void DrawFPS(int posX, int posY) {
    const int fps = GetFPS();
    Color color = LIME;
    if (fps < 30)      color = Color{230, 41, 55, 255};   // RED
    else if (fps < 50) color = Color{190, 33, 55, 255};   // MAROON
    else               color = LIME;
    DrawText(TextFormat("%d FPS", fps), posX, posY, 20, color);
}

int GetCodepoint(const char* text, int* codepointSize) {
    if (text == nullptr) return 0x3f;

    int cp = 0x3f;
    int size = 1;
    const unsigned char firstByte = static_cast<unsigned char>(*text);

    int extra = 0;
    if ((firstByte & 0x80) == 0) {
        cp = firstByte;
    } else if ((firstByte & 0xE0) == 0xC0) {
        cp = firstByte & 0x1F;
        extra = 1;
    } else if ((firstByte & 0xF0) == 0xE0) {
        cp = firstByte & 0x0F;
        extra = 2;
    } else if ((firstByte & 0xF8) == 0xF0) {
        cp = firstByte & 0x07;
        extra = 3;
    }

    for (int i = 0; i < extra; ++i) {
        const unsigned char nextByte = static_cast<unsigned char>(text[1 + i]);
        if ((nextByte & 0xC0) == 0x80) {
            cp = (cp << 6) + (nextByte & 0x3F);
            ++size;
        } else {
            cp = 0x3f;
            break;
        }
    }

    if (codepointSize != nullptr) *codepointSize = size;
    return cp;
}

int GetCodepointNext(const char* text, int* codepointSize) {
    if (text == nullptr) return 0x3f;

    const unsigned char b0 = static_cast<unsigned char>(text[0]);
    if ((b0 == 0xEF) && (static_cast<unsigned char>(text[1]) == 0xBB) &&
        (static_cast<unsigned char>(text[2]) == 0xBF)) text += 3;

    const unsigned char firstByte = static_cast<unsigned char>(*text);
    int cp = 0x3f;
    int size = 0;
    int extra = 0;

    if ((firstByte & 0x80) == 0) {
        cp = firstByte;
        size = 1;
    } else if ((firstByte & 0xE0) == 0xC0) {
        cp = firstByte & 0x1F;
        size = 2;
        extra = 1;
    } else if ((firstByte & 0xF0) == 0xE0) {
        cp = firstByte & 0x0F;
        size = 3;
        extra = 2;
    } else if ((firstByte & 0xF8) == 0xF0) {
        cp = firstByte & 0x07;
        size = 4;
        extra = 3;
    } else {
        size = 1;
    }

    for (int i = 0; i < extra; ++i) {
        const unsigned char nextByte = static_cast<unsigned char>(text[1 + i]);
        if ((nextByte & 0xC0) == 0x80) {
            cp = (cp << 6) + (nextByte & 0x3F);
        } else {
            cp = 0x3f;
            size = 1;
            break;
        }
    }

    if (codepointSize != nullptr) *codepointSize = size;
    return cp;
}

int GetCodepointPrevious(const char* text, int* codepointSize) {
    if (text == nullptr) return 0x3f;

    int count = 1;
    while ((static_cast<unsigned char>(text[-count]) & 0xC0) == 0x80) ++count;

    const char* start = text - count;
    const unsigned char firstByte = static_cast<unsigned char>(*start);

    int charBytes = 0;
    if ((firstByte & 0x80) == 0)           charBytes = 1;
    else if ((firstByte & 0xE0) == 0xC0)   charBytes = 2;
    else if ((firstByte & 0xF0) == 0xE0)   charBytes = 3;
    else if ((firstByte & 0xF8) == 0xF0)   charBytes = 4;

    if (charBytes == count) {
        int cp = firstByte & (0xFF >> (charBytes + 1));
        bool valid = true;
        for (int i = 1; i < charBytes; ++i) {
            const unsigned char nextByte = static_cast<unsigned char>(start[i]);
            if ((nextByte & 0xC0) == 0x80) {
                cp = (cp << 6) + (nextByte & 0x3F);
            } else {
                valid = false;
                cp = 0x3f;
                break;
            }
        }
        if (codepointSize != nullptr) *codepointSize = valid ? charBytes : 1;
        return cp;
    }

    if (codepointSize != nullptr) *codepointSize = 1;
    return 0x3f;
}

const char* CodepointToUTF8(int codepoint, int* utf8Size) {
    static thread_local char utf8[5] = {0};
    int size = 0;

    if (codepoint <= 0x7F) {
        utf8[0] = static_cast<char>(codepoint);
        size = 1;
    } else if (codepoint <= 0x7FF) {
        utf8[0] = static_cast<char>(0xC0 | (codepoint >> 6));
        utf8[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 2;
    } else if (codepoint <= 0xFFFF) {
        utf8[0] = static_cast<char>(0xE0 | (codepoint >> 12));
        utf8[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 3;
    } else if (codepoint <= 0x10FFFF) {
        utf8[0] = static_cast<char>(0xF0 | (codepoint >> 18));
        utf8[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        utf8[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 4;
    } else {
        utf8[0] = '?';
        size = 1;
    }

    if (utf8Size != nullptr) *utf8Size = size;
    utf8[size] = 0;
    return utf8;
}

int GetCodepointCount(const char* text) {
    if (text == nullptr) return 0;
    int length = 0;
    const char* ptr = text;
    while (*ptr != '\0') {
        int size = 0;
        GetCodepoint(ptr, &size);
        ptr += size;
        ++length;
    }
    return length;
}

int* LoadCodepoints(const char* text, int* count) {
    if (count != nullptr) *count = 0;
    if (text == nullptr) return nullptr;

    int length = 0;
    const char* ptr = text;
    while (*ptr != '\0') {
        int size = 0;
        GetCodepoint(ptr, &size);
        ptr += size;
        ++length;
    }

    int* codepoints = static_cast<int*>(MemAlloc(static_cast<size_t>(length + 1) * sizeof(int)));
    if (codepoints == nullptr) return nullptr;

    int index = 0;
    ptr = text;
    while (*ptr != '\0') {
        int size = 0;
        codepoints[index] = GetCodepoint(ptr, &size);
        ptr += size;
        ++index;
    }
    codepoints[length] = 0;

    if (count != nullptr) *count = length;
    return codepoints;
}

void UnloadCodepoints(int* codepoints) {
    if (codepoints != nullptr) MemFree(codepoints);
}

char* LoadUTF8(const int* codepoints, int length) {
    if (codepoints == nullptr || length <= 0) return nullptr;

    int total = 0;
    for (int i = 0; i < length; ++i) {
        int utf8Size = 0;
        CodepointToUTF8(codepoints[i], &utf8Size);
        total += utf8Size;
    }

    char* text = static_cast<char*>(MemAlloc(static_cast<size_t>(total + 1)));
    if (text == nullptr) return nullptr;

    int index = 0;
    for (int i = 0; i < length; ++i) {
        int utf8Size = 0;
        const char* utf8 = CodepointToUTF8(codepoints[i], &utf8Size);
        for (int j = 0; j < utf8Size; ++j) text[index++] = utf8[j];
    }
    text[index] = '\0';
    return text;
}

void UnloadUTF8(char* text) {
    if (text != nullptr) MemFree(text);
}

namespace {

Transform AiMatrixToTransform(const aiMatrix4x4& m) {
    Transform result;
    aiVector3D t, s;
    aiQuaternion r;
    m.Decompose(s, r, t);
    result.translation = Vec3{t.x, t.y, t.z};
    result.rotation = Quaternion{r.x, r.y, r.z, r.w};
    result.scale = Vec3{s.x, s.y, s.z};
    return result;
}

Transform TransformLerp(const Transform& a, const Transform& b, float amount) {
    Transform result;
    result.translation.x = Lerp(a.translation.x, b.translation.x, amount);
    result.translation.y = Lerp(a.translation.y, b.translation.y, amount);
    result.translation.z = Lerp(a.translation.z, b.translation.z, amount);
    result.rotation = QuaternionSlerp(a.rotation, b.rotation, amount);
    result.scale.x = Lerp(a.scale.x, b.scale.x, amount);
    result.scale.y = Lerp(a.scale.y, b.scale.y, amount);
    result.scale.z = Lerp(a.scale.z, b.scale.z, amount);
    return result;
}

unsigned int CountSceneNodes(aiNode* node) {
    if (node == nullptr) return 0;
    unsigned int count = 1;
    for (unsigned int c = 0; c < node->mNumChildren; ++c) {
        count += CountSceneNodes(node->mChildren[c]);
    }
    return count;
}

void BuildSkeletonFromHierarchy(aiNode* node, ModelSkeleton& skeleton, int parent) {
    if (node == nullptr) return;

    const unsigned int id = skeleton.boneCount;
    const std::string nodeName = node->mName.length > 0 ? node->mName.C_Str() : "node";
    skeleton.bones[id].parent = parent;
    std::strncpy(skeleton.bones[id].name, nodeName.c_str(), sizeof(skeleton.bones[id].name) - 1);
    skeleton.bones[id].name[sizeof(skeleton.bones[id].name) - 1] = '\0';
    skeleton.bindPose[id] = AiMatrixToTransform(node->mTransformation);
    skeleton.boneCount++;

    for (unsigned int c = 0; c < node->mNumChildren; ++c) {
        BuildSkeletonFromHierarchy(node->mChildren[c], skeleton, static_cast<int>(id));
    }
}

std::vector<Transform> EvaluateAnimationPose(const ModelAnimation& anim, float frame) {
    std::vector<Transform> locals(anim.boneCount);
    if (anim.boneCount == 0 || anim.keyframeCount <= 0 || anim.keyframePoses == nullptr) {
        return locals;
    }
    const int frameCount = anim.keyframeCount;
    int f0 = static_cast<int>(frame) % frameCount;
    if (f0 < 0) f0 += frameCount;
    const int f1 = (f0 + 1) % frameCount;
    const float amount = Clamp(frame - std::floor(frame), 0.0f, 1.0f);
    for (unsigned int b = 0; b < anim.boneCount; ++b) {
        locals[b] = TransformLerp(anim.keyframePoses[f0][b], anim.keyframePoses[f1][b], amount);
    }
    return locals;
}

std::vector<Mat4> GlobalBindTransforms(const ModelSkeleton& skel) {
    std::vector<Mat4> globals(skel.boneCount, Mat4::identity());
    for (unsigned int b = 0; b < skel.boneCount; ++b) {
        const Transform& bp = skel.bindPose[b];
        Mat4 local = TransformToMatrix(bp.translation, bp.rotation, bp.scale);
        if (skel.bones[b].parent >= 0) {
            globals[b] = globals[static_cast<size_t>(skel.bones[b].parent)] * local;
        } else {
            globals[b] = local;
        }
    }
    return globals;
}

void ApplySkinningToMesh(Model& model, Mesh& mesh) {
    if (mesh.vertices == nullptr || mesh.vertexCount <= 0) return;
    if (mesh.boneIndices == nullptr || mesh.boneWeights == nullptr || model.boneMatrices == nullptr) return;
    if (model.skeleton.bones == nullptr || model.skeleton.boneCount == 0) return;

    if (mesh.animVertices == nullptr) {
        mesh.animVertices = new float[static_cast<size_t>(mesh.vertexCount) * 3u];
    }
    if (mesh.animNormals == nullptr) {
        mesh.animNormals = new float[static_cast<size_t>(mesh.vertexCount) * 3u];
    }

    for (int v = 0; v < mesh.vertexCount; ++v) {
        const int base = v * 3;
        Vec3 pos{0.0f, 0.0f, 0.0f};
        Vec3 normal{0.0f, 0.0f, 0.0f};
        float totalWeight = 0.0f;

        for (int slot = 0; slot < 4; ++slot) {
            const int boneIndex = static_cast<int>(mesh.boneIndices[static_cast<size_t>(v) * 4u + static_cast<size_t>(slot)]);
            const float weight = mesh.boneWeights[static_cast<size_t>(v) * 4u + static_cast<size_t>(slot)];
            if (boneIndex < 0 || boneIndex >= static_cast<int>(model.skeleton.boneCount) || weight <= 0.0f) {
                continue;
            }

            const Mat4 boneMatrix = model.boneMatrices[static_cast<size_t>(boneIndex)];
            const Vec4 localPos{
                mesh.vertices[base + 0],
                mesh.vertices[base + 1],
                mesh.vertices[base + 2],
                1.0f
            };
            const Vec4 worldPos = boneMatrix * localPos;
            pos += Vec3{worldPos.x, worldPos.y, worldPos.z} * weight;

            Vec3 localNormal{0.0f, 0.0f, 0.0f};
            if (mesh.normals != nullptr) {
                localNormal = Vec3{
                    mesh.normals[base + 0],
                    mesh.normals[base + 1],
                    mesh.normals[base + 2]
                };
            } else {
                localNormal = Vec3{0.0f, 1.0f, 0.0f};
            }

            const Vec4 worldNormal = boneMatrix * Vec4{localNormal.x, localNormal.y, localNormal.z, 0.0f};
            normal += Vec3{worldNormal.x, worldNormal.y, worldNormal.z} * weight;
            totalWeight += weight;
        }

        if (totalWeight > 0.0f) {
            const float invWeight = 1.0f / totalWeight;
            pos = pos * invWeight;
            normal = normal * invWeight;
            const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
            if (length > 0.0001f) {
                normal = normal * (1.0f / length);
            }
        } else {
            pos = Vec3{mesh.vertices[base + 0], mesh.vertices[base + 1], mesh.vertices[base + 2]};
            if (mesh.normals != nullptr) {
                normal = Vec3{mesh.normals[base + 0], mesh.normals[base + 1], mesh.normals[base + 2]};
            } else {
                normal = Vec3{0.0f, 1.0f, 0.0f};
            }
        }

        mesh.animVertices[base + 0] = pos.x;
        mesh.animVertices[base + 1] = pos.y;
        mesh.animVertices[base + 2] = pos.z;
        mesh.animNormals[base + 0] = normal.x;
        mesh.animNormals[base + 1] = normal.y;
        mesh.animNormals[base + 2] = normal.z;

        mesh.vertices[base + 0] = pos.x;
        mesh.vertices[base + 1] = pos.y;
        mesh.vertices[base + 2] = pos.z;
        if (mesh.normals != nullptr) {
            mesh.normals[base + 0] = normal.x;
            mesh.normals[base + 1] = normal.y;
            mesh.normals[base + 2] = normal.z;
        }
    }
}

void ApplyPoseToModel(Model model, const ModelSkeleton& skel,
                      const std::vector<Transform>& locals,
                      const ModelAnimation& anim, int f0) {
    const unsigned int boneCount = static_cast<unsigned int>(locals.size());
    if (boneCount == 0) return;

    std::vector<Mat4> globals(boneCount, Mat4::identity());
    for (unsigned int b = 0; b < boneCount; ++b) {
        Mat4 local = TransformToMatrix(locals[b].translation, locals[b].rotation, locals[b].scale);
        if (skel.bones != nullptr && skel.bones[b].parent >= 0) {
            globals[b] = globals[static_cast<size_t>(skel.bones[b].parent)] * local;
        } else {
            globals[b] = local;
        }
    }

    const bool haveSkeleton = (skel.bones != nullptr && skel.bindPose != nullptr);
    std::vector<Mat4> bindInv(boneCount, Mat4::identity());
    if (haveSkeleton) {
        std::vector<Mat4> bindGlobal = GlobalBindTransforms(skel);
        for (unsigned int b = 0; b < boneCount; ++b) bindInv[b] = bindGlobal[b].inverted();
    }

    if (model.boneMatrices != nullptr) {
        for (unsigned int b = 0; b < boneCount; ++b) {
            model.boneMatrices[b] = globals[b] * bindInv[b];
        }
    }

    model.currentPose = (anim.keyframePoses != nullptr) ? anim.keyframePoses[f0] : nullptr;
}

} // namespace

void qcPopulateModelSkeleton(const aiScene* scene, Model& model) {
    if (scene == nullptr || scene->mRootNode == nullptr) return;
    const unsigned int nodeCount = CountSceneNodes(scene->mRootNode);
    if (nodeCount == 0) return;

    ModelSkeleton skel{};
    skel.boneCount = 0;
    skel.bones = new BoneInfo[nodeCount];
    skel.bindPose = new Transform[nodeCount];
    BuildSkeletonFromHierarchy(scene->mRootNode, skel, -1);

    model.skeleton = skel;
    delete[] model.boneMatrices;
    model.boneMatrices = (model.skeleton.boneCount > 0) ? new Matrix[model.skeleton.boneCount] : nullptr;
}

void qcFreeModelSkeleton(Model& model) {
    delete[] model.skeleton.bones;
    model.skeleton.bones = nullptr;
    delete[] model.skeleton.bindPose;
    model.skeleton.bindPose = nullptr;
    model.skeleton.boneCount = 0;
    model.currentPose = nullptr;
    delete[] model.boneMatrices;
    model.boneMatrices = nullptr;
}

static double AnimSampleTime(double startTime, double endTime, int frame, int totalFrames) {
    const double span = (endTime > startTime) ? (endTime - startTime) : 1.0;
    const double t = static_cast<double>(frame) / static_cast<double>(std::max(1, totalFrames));
    return startTime + span * t;
}

static aiVector3D InterpolateVectorKeys(const aiVectorKey* keys, unsigned int keyCount, double time) {
    if (keyCount == 1) return keys[0].mValue;

    for (unsigned int k = 0; k + 1 < keyCount; ++k) {
        const double t0 = keys[k].mTime;
        const double t1 = keys[k + 1].mTime;
        const bool isLastSegment = (k + 1 == keyCount - 1);

        if (time >= t0 && (time <= t1 || isLastSegment)) {
            const double weight = (t1 > t0) ? ((time - t0) / (t1 - t0)) : 0.0;
            const aiVector3D& a = keys[k].mValue;
            const aiVector3D& b = keys[k + 1].mValue;
            return a + (b - a) * static_cast<float>(weight);
        }
    }
    return keys[0].mValue;
}

static aiQuaternion InterpolateRotationKeys(const aiQuatKey* keys, unsigned int keyCount, double time) {
    if (keyCount == 1) return keys[0].mValue;

    for (unsigned int k = 0; k + 1 < keyCount; ++k) {
        const double t0 = keys[k].mTime;
        const double t1 = keys[k + 1].mTime;
        const bool isLastSegment = (k + 1 == keyCount - 1);

        if (time >= t0 && (time <= t1 || isLastSegment)) {
            const double weight = (t1 > t0) ? ((time - t0) / (t1 - t0)) : 0.0;
            aiQuaternion result;
            aiQuaternion::Interpolate(result, keys[k].mValue, keys[k + 1].mValue, static_cast<float>(weight));
            result.Normalize();
            return result;
        }
    }
    return keys[0].mValue;
}

static void SampleBoneChannel(const aiNodeAnim* channel, int frame, int totalFrames, Transform& outPose) {
    if (channel->mNumPositionKeys > 0) {
        const double time = AnimSampleTime(
            channel->mPositionKeys[0].mTime,
            channel->mPositionKeys[channel->mNumPositionKeys - 1].mTime,
            frame, totalFrames);
        const aiVector3D pos = InterpolateVectorKeys(channel->mPositionKeys, channel->mNumPositionKeys, time);
        outPose.translation = Vec3{pos.x, pos.y, pos.z};
    }

    if (channel->mNumRotationKeys > 0) {
        const double time = AnimSampleTime(
            channel->mRotationKeys[0].mTime,
            channel->mRotationKeys[channel->mNumRotationKeys - 1].mTime,
            frame, totalFrames);
        const aiQuaternion rot = InterpolateRotationKeys(channel->mRotationKeys, channel->mNumRotationKeys, time);
        outPose.rotation = Quaternion{rot.x, rot.y, rot.z, rot.w};
    }

    if (channel->mNumScalingKeys > 0) {
        const double time = AnimSampleTime(
            channel->mScalingKeys[0].mTime,
            channel->mScalingKeys[channel->mNumScalingKeys - 1].mTime,
            frame, totalFrames);
        const aiVector3D scl = InterpolateVectorKeys(channel->mScalingKeys, channel->mNumScalingKeys, time);
        outPose.scale = Vec3{scl.x, scl.y, scl.z};
    }
}

static std::vector<const aiNodeAnim*> MapChannelsToBones(const aiAnimation* anim, const ModelSkeleton& skel) {
    std::vector<const aiNodeAnim*> channels(skel.boneCount, nullptr);

    for (unsigned int c = 0; c < anim->mNumChannels; ++c) {
        const aiNodeAnim* nodeAnim = anim->mChannels[c];
        const std::string boneName = nodeAnim->mNodeName.C_Str();

        for (unsigned int b = 0; b < skel.boneCount; ++b) {
            if (boneName == std::string(skel.bones[b].name)) {
                channels[b] = nodeAnim;
                break;
            }
        }
    }
    return channels;
}

static unsigned int ComputeKeyframeCount(const std::vector<const aiNodeAnim*>& channels) {
    unsigned int frames = 0;
    for (const aiNodeAnim* channel : channels) {
        if (channel == nullptr) continue;
        frames = std::max(frames, channel->mNumPositionKeys);
        frames = std::max(frames, channel->mNumRotationKeys);
        frames = std::max(frames, channel->mNumScalingKeys);
    }
    return frames;
}

static void BuildKeyframePoses(const ModelSkeleton& skel, const std::vector<const aiNodeAnim*>& channels,
                                ModelAnimation& dst) {
    dst.keyframePoses = new ModelAnimPose[dst.keyframeCount];
    for (int f = 0; f < dst.keyframeCount; ++f) {
        dst.keyframePoses[f] = new Transform[dst.boneCount];
        for (unsigned int b = 0; b < dst.boneCount; ++b) {
            dst.keyframePoses[f][b] = skel.bindPose[b];
        }
    }

    for (unsigned int b = 0; b < skel.boneCount; ++b) {
        const aiNodeAnim* channel = channels[b];
        if (channel == nullptr) continue;

        for (int f = 0; f < dst.keyframeCount; ++f) {
            SampleBoneChannel(channel, f, dst.keyframeCount, dst.keyframePoses[f][b]);
        }
    }
}

ModelAnimation* LoadModelAnimations(const char* fileName, int* animCount) {
    if (animCount != nullptr) *animCount = 0;
    if (fileName == nullptr || *fileName == '\0') return nullptr;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(fileName, aiProcess_Triangulate | aiProcess_GenNormals);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || !scene->mRootNode) {
        TraceLog(LogLevel::Error, "MODEL", TextFormat("[Anim] Failed to load animations from %s: %s",
            fileName, importer.GetErrorString()));
        return nullptr;
    }

    if (scene->mNumAnimations == 0) {
        TraceLog(LogLevel::Warn, "MODEL", TextFormat("[Anim] No animations found in %s", fileName));
        return nullptr;
    }

    ModelSkeleton skel{};
    const unsigned int nodeCount = CountSceneNodes(scene->mRootNode);
    skel.bones = new BoneInfo[nodeCount];
    skel.bindPose = new Transform[nodeCount];
    BuildSkeletonFromHierarchy(scene->mRootNode, skel, -1);

    const int count = static_cast<int>(scene->mNumAnimations);
    ModelAnimation* result = new ModelAnimation[count];

    for (unsigned int a = 0; a < scene->mNumAnimations; ++a) {
        const aiAnimation* anim = scene->mAnimations[a];
        ModelAnimation& dst = result[a];
        dst.boneCount = skel.boneCount;
        dst.keyframeCount = 0;

        if (anim->mName.length > 0) {
            std::strncpy(dst.name, anim->mName.C_Str(), sizeof(dst.name) - 1);
            dst.name[sizeof(dst.name) - 1] = '\0';
        }

        const std::vector<const aiNodeAnim*> channels = MapChannelsToBones(anim, skel);
        const unsigned int frames = ComputeKeyframeCount(channels);
        if (frames == 0) continue;

        dst.keyframeCount = static_cast<int>(frames);
        BuildKeyframePoses(skel, channels, dst);
    }

    delete[] skel.bones;
    delete[] skel.bindPose;

    if (animCount != nullptr) *animCount = count;
    return result;
}

void UnloadModelAnimations(ModelAnimation* animations, int animCount) {
    if (animations == nullptr) return;
    for (int a = 0; a < animCount; ++a) {
        ModelAnimation& anim = animations[a];
        if (anim.keyframePoses) {
            for (int f = 0; f < anim.keyframeCount; ++f) {
                delete[] anim.keyframePoses[f];
            }
            delete[] anim.keyframePoses;
            anim.keyframePoses = nullptr;
        }
        anim.keyframeCount = 0;
    }
    delete[] animations;
}

bool IsModelAnimationValid(Model model, ModelAnimation anim) {
    if (model.skeleton.bones == nullptr || anim.boneCount != model.skeleton.boneCount) return false;
    if (anim.keyframePoses == nullptr || anim.keyframeCount <= 0) return false;
    return true;
}

static int WrapFrameIndex(float frame, int keyframeCount) {
    int wrapped = static_cast<int>(frame) % keyframeCount;
    if (wrapped < 0) wrapped += keyframeCount;
    return wrapped;
}

static void ApplySkinningToAllMeshes(Model& model) {
    if (model.meshes == nullptr) return;
    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex) {
        ApplySkinningToMesh(model, model.meshes[meshIndex]);
    }
}

void UpdateModelAnimation(Model model, ModelAnimation anim, float frame) {
    if (anim.boneCount == 0 || anim.keyframeCount <= 0 || anim.keyframePoses == nullptr) return;

    std::vector<Transform> locals = EvaluateAnimationPose(anim, frame);
    const int f0 = WrapFrameIndex(frame, anim.keyframeCount);
    ApplyPoseToModel(model, model.skeleton, locals, anim, f0);
    ApplySkinningToAllMeshes(model);
}

void UpdateModelAnimationEx(Model model, ModelAnimation animA, float frameA,
                            ModelAnimation animB, float frameB, float blend) {
    if (animA.boneCount == 0 || animA.keyframeCount <= 0 || animA.keyframePoses == nullptr) return;

    const float b = Clamp(blend, 0.0f, 1.0f);
    std::vector<Transform> locals = EvaluateAnimationPose(animA, frameA);

    const bool canBlendWithB = (animB.boneCount == animA.boneCount) && animB.keyframeCount > 0
        && animB.keyframePoses != nullptr && b > 0.0f;
    if (canBlendWithB) {
        std::vector<Transform> localsB = EvaluateAnimationPose(animB, frameB);
        for (unsigned int i = 0; i < locals.size(); ++i) {
            locals[i] = TransformLerp(locals[i], localsB[i], b);
        }
    }

    const int f0 = WrapFrameIndex(frameA, animA.keyframeCount);
    ApplyPoseToModel(model, model.skeleton, locals, animA, f0);
    ApplySkinningToAllMeshes(model);
}

} // namespace qc
