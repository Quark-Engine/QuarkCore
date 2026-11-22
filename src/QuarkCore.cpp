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
#include <atomic>
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
using namespace qci;

static void CopyFixedString(char* pDst, size_t dstSize, const char* pSrc)
{
    if (pDst == nullptr || dstSize == 0)
        return;
    if (pSrc == nullptr)
    {
        pDst[0] = '\0';
        return;
    }

    std::snprintf(pDst, dstSize, "%s", pSrc);
}

#if defined(QC_ENABLE_OPENGL)
QuarkGLRenderer gGLRenderer;
#endif
#if defined(QC_ENABLE_VULKAN)
QuarkVkRenderer gVkRenderer;
#endif
#if defined(QC_ENABLE_D3D11)
QuarkD3D11Renderer gD3D11Renderer;
#endif
qci::IRenderer* g_pRenderer = nullptr;
RendererType gCurrentBackend = RendererType::Auto;
bool gVulkanLibraryLoaded = false;
int gRequestedMSAASamples = 1;
TextureFilter g_TextureFilter = TEXTURE_FILTER_BILINEAR;
std::array<std::array<float, SDL_GAMEPAD_AXIS_COUNT>, 16> gGamepadDeadZones{};

#define gRenderer (*g_pRenderer)

int gTextLineSpacing = 0;
static TraceLogCallback s_pfnTraceLogCallback = nullptr;

WindowState g_Win;
int g_LastKeyPressed = 0;
int g_LastCharPressed = 0;
KeyboardKey g_ExitKey = KEY_ESCAPE;
Vec2 gMousePreviousPosition{};
Vec2 gMouseOffset{};
Vec2 gMouseScale{1.0f, 1.0f};
bool gCursorHidden = false;

struct DebugRenderStats
{
    std::uint64_t m_Vertices = 0;
    std::uint64_t m_Calls = 0;
};

DebugRenderStats gCurrentDebugRenderStats;
DebugRenderStats gPreviousDebugRenderStats;
bool gDebugInterfaceEnabled = false;

namespace qci
{
    void RecordDebugRenderDraw(std::uint64_t vertexCount)
    {
        std::atomic_ref<std::uint64_t>(gCurrentDebugRenderStats.m_Vertices).fetch_add(vertexCount, std::memory_order_relaxed);
        std::atomic_ref<std::uint64_t>(gCurrentDebugRenderStats.m_Calls).fetch_add(1, std::memory_order_relaxed);
    }
} // qci

struct rAudioProcessor
{
    AudioCallback m_Callback = nullptr;
    rAudioProcessor* m_pNext = nullptr;
};

struct rAudioBuffer
{
    ALuint m_Source = 0;
    ALuint m_Buffer = 0;
    unsigned int m_FrameCount = 0;
    unsigned int m_SampleRate = 0;
    unsigned int m_SampleSize = 0;
    unsigned int m_Channels = 0;
    bool m_OwnsBuffer = true;
    bool m_Streaming = false;
    AudioCallback m_Callback = nullptr;
    std::vector<AudioCallback> m_vProcessors;
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

const char* ToString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::None:
        return "NONE";
    default:
        return "UNKNOWN";
    }
}

const char* RendererTypeToString(RendererType rendererType)
{
    switch (rendererType)
    {
    case RendererType::Auto:
        return "Auto";
    case RendererType::OpenGL:
        return "OpenGL";
    case RendererType::Vulkan:
        return "Vulkan";
    case RendererType::D3D11:
        return "Direct3D 11";
    default:
        return "Unknown";
    }
}

std::string FormatTimeNow()
{
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

static SDL_WindowFlags BuildWindowFlags(SDL_WindowFlags baseFlags)
{
    SDL_WindowFlags flags = baseFlags;
    if ((g_Win.m_PendingConfigFlags & FLAG_WINDOW_MAXIMIZED) != 0u)
    {
        flags |= SDL_WINDOW_MAXIMIZED;
    }
    if ((g_Win.m_PendingConfigFlags & FLAG_WINDOW_HIDDEN) != 0u)
    {
        flags |= SDL_WINDOW_HIDDEN;
    }
    if ((g_Win.m_PendingConfigFlags & FLAG_FULLSCREEN_MODE) != 0u)
    {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    if ((g_Win.m_PendingConfigFlags & (FLAG_BORDERLESS_WINDOWED_MODE | FLAG_WINDOW_UNDECORATED)) != 0u)
    {
        flags |= SDL_WINDOW_BORDERLESS;
    }
    return flags;
}

#if defined(QC_ENABLE_D3D11)
static bool InitD3D11Backend(int width, int height, const char* pTitle)
{
    TraceLog(LogLevel::Info, "RENDERER", "Backend selected: Direct3D 11");

    g_Win.m_pWindow = SDL_CreateWindow(pTitle, width, height, BuildWindowFlags(SDL_WINDOW_RESIZABLE));
    if (!g_Win.m_pWindow)
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    ApplyPlatformDarkWindowBackground(g_Win.m_pWindow);

    int pixelW = width;
    int pixelH = height;
    SDL_GetWindowSizeInPixels(g_Win.m_pWindow, &pixelW, &pixelH);
    if (pixelW <= 0 || pixelH <= 0)
    {
        pixelW = width;
        pixelH = height;
    }

    g_pRenderer = &gD3D11Renderer;
    gRenderer.Init(g_Win.m_pWindow, pixelW, pixelH);
    gRenderer.SetTargetFPS(g_Win.m_TargetFps);
    if (g_Win.m_VsyncSet)
        gRenderer.SetVSync(g_Win.m_Vsync);
    gCurrentBackend = RendererType::D3D11;
    g_Win.m_PendingConfigFlags = 0u;
    return true;
}
#endif

void WriteLog(LogLevel level, const char* pType, const std::string& message)
{
    if (level < g_Win.m_MinimumLogLevel || level == LogLevel::None)
        return;
    std::ostringstream logMessage;
    logMessage
        << '[' << FormatTimeNow() << ']'
        << '[' << ToString(level) << ']'
        << '[' << pType << "] "
        << message;
    const std::string formattedMessage = logMessage.str();
    std::cout << formattedMessage << '\n';
    if (s_pfnTraceLogCallback != nullptr)
    {
        s_pfnTraceLogCallback(level, formattedMessage.c_str());
    }
}

void CopyText(char* pDst, size_t size, const char* pSrc)
{
    if (!pDst || size == 0)
        return;
    if (!pSrc)
    {
        pDst[0] = '\0';
        return;
    }
#if defined(_MSC_VER)
    strncpy_s(pDst, size, pSrc, _TRUNCATE);
#else
    std::snprintf(pDst, size, "%s", pSrc);
#endif
}

void UpdateInputState()
{
    float mx = 0.f, my = 0.f;
    const SDL_MouseButtonFlags ms = SDL_GetMouseState(&mx, &my);
    g_Win.m_MousePosition = Vec2{
        (mx + gMouseOffset.x) * gMouseScale.x,
        (my + gMouseOffset.y) * gMouseScale.y};
    g_Win.m_aMouseButtons[MOUSE_BUTTON_LEFT] = (ms & SDL_BUTTON_LMASK) != 0;
    g_Win.m_aMouseButtons[MOUSE_BUTTON_RIGHT] = (ms & SDL_BUTTON_RMASK) != 0;
    g_Win.m_aMouseButtons[MOUSE_BUTTON_MIDDLE] = (ms & SDL_BUTTON_MMASK) != 0;
    g_Win.m_aMouseButtons[MOUSE_BUTTON_SIDE] = (ms & SDL_BUTTON_X1MASK) != 0;
    g_Win.m_aMouseButtons[MOUSE_BUTTON_EXTRA] = (ms & SDL_BUTTON_X2MASK) != 0;
    g_Win.m_aMouseButtons[MOUSE_BUTTON_FORWARD] = false;
    g_Win.m_aMouseButtons[MOUSE_BUTTON_BACK] = false;

    const bool* ks = SDL_GetKeyboardState(nullptr);
    for (int i = 0; i < static_cast<int>(SDL_SCANCODE_COUNT); ++i)
        g_Win.m_aCurrentKeys[static_cast<std::size_t>(i)] = ks[i];
}

void EnsureInitialized()
{
    if (g_Win.m_pWindow == nullptr)
        throw std::runtime_error("QuarkCore is not initialized. Call InitWindow() first.");
}

static void CleanupBackendInitFailure()
{
    if (g_pRenderer)
    {
        g_pRenderer = nullptr;
    }

    if (g_Win.m_pWindow)
    {
        SDL_DestroyWindow(g_Win.m_pWindow);
        g_Win.m_pWindow = nullptr;
    }

    if (gVulkanLibraryLoaded)
    {
#if defined(QC_ENABLE_VULKAN)
        SDL_Vulkan_UnloadLibrary();
#endif
        gVulkanLibraryLoaded = false;
    }

    gCurrentBackend = RendererType::Auto;
}

#if defined(QC_ENABLE_OPENGL)
static bool InitOpenGLBackend(int width, int height, const char* pTitle)
{
#if defined(__ANDROID__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, gRequestedMSAASamples > 1 ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, gRequestedMSAASamples > 1 ? gRequestedMSAASamples : 0);

    TraceLog(LogLevel::Info, "RENDERER", "Backend selected: OpenGL");

    g_Win.m_pWindow = SDL_CreateWindow(
        pTitle,
        width,
        height,
        BuildWindowFlags(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE));

    if (!g_Win.m_pWindow)
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    ApplyPlatformDarkWindowBackground(g_Win.m_pWindow);

    int pixelW = width;
    int pixelH = height;
    SDL_GetWindowSizeInPixels(g_Win.m_pWindow, &pixelW, &pixelH);
    if (pixelW <= 0 || pixelH <= 0)
    {
        pixelW = width;
        pixelH = height;
    }

    g_pRenderer = &gGLRenderer;
    gRenderer.Init(g_Win.m_pWindow, pixelW, pixelH);
    g_Win.m_PendingConfigFlags = 0u;
    int sampleBuffers = 0;
    int actualSamples = 0;
    if (SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &sampleBuffers) &&
        SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &actualSamples))
    {
        const int effectiveSamples = sampleBuffers > 0 ? actualSamples : 1;
        if (effectiveSamples != gRequestedMSAASamples)
        {
            TraceLog(LogLevel::Warn, "RENDERER",
                     TextFormat("OpenGL requested %d MSAA samples, but the selected pixel format provides %d.",
                                gRequestedMSAASamples, effectiveSamples));
        }
    }
    gRenderer.SetTargetFPS(g_Win.m_TargetFps);
    if (g_Win.m_VsyncSet)
    {
        gRenderer.SetVSync(g_Win.m_Vsync);
    }
    gCurrentBackend = RendererType::OpenGL;
    return true;
}
#endif

static ALenum GetOpenALFormat(unsigned int sampleSize, unsigned int channels)
{
    if (channels == 1)
    {
        if (sampleSize == 8)
            return AL_FORMAT_MONO8;
        if (sampleSize == 16)
            return AL_FORMAT_MONO16;
    }
    else if (channels == 2)
    {
        if (sampleSize == 8)
            return AL_FORMAT_STEREO8;
        if (sampleSize == 16)
            return AL_FORMAT_STEREO16;
    }
    return 0;
}

static size_t GetAudioFrameSize(unsigned int sampleSize, unsigned int channels)
{
    return static_cast<size_t>((sampleSize + 7u) / 8u) * static_cast<size_t>(channels);
}

static bool EnsureAudioDevice()
{
    if (gAudioContext != nullptr)
        return true;
    InitAudioDevice();
    return gAudioContext != nullptr;
}

static uint16_t ReadLE16(const unsigned char* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

static uint32_t ReadLE32(const unsigned char* p)
{
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static Wave LoadWaveFromWavData(const unsigned char* pFileData, int dataSize)
{
    Wave wave{};
    if (!pFileData || dataSize < 44)
        return wave;
    if (std::memcmp(pFileData, "RIFF", 4) != 0 || std::memcmp(pFileData + 8, "WAVE", 4) != 0)
        return wave;

    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    unsigned int sampleSize = 0;
    const unsigned char* pSamples = nullptr;
    uint32_t sampleBytes = 0;

    int offset = 12;
    while (offset + 8 <= dataSize)
    {
        const unsigned char* pChunk = pFileData + offset;
        const uint32_t chunkSize = ReadLE32(pChunk + 4);
        offset += 8;
        if (offset + static_cast<int>(chunkSize) > dataSize)
            break;

        if (std::memcmp(pChunk, "fmt ", 4) == 0 && chunkSize >= 16)
        {
            const uint16_t audioFormat = ReadLE16(pFileData + offset);
            channels = ReadLE16(pFileData + offset + 2);
            sampleRate = ReadLE32(pFileData + offset + 4);
            sampleSize = ReadLE16(pFileData + offset + 14);
            if (audioFormat != 1 && audioFormat != 3)
                return {};
        }
        else if (std::memcmp(pChunk, "data", 4) == 0)
        {
            pSamples = pFileData + offset;
            sampleBytes = chunkSize;
        }

        offset += static_cast<int>(chunkSize + (chunkSize & 1u));
    }

    const size_t frameSize = GetAudioFrameSize(sampleSize, channels);
    if (!pSamples || sampleBytes == 0 || frameSize == 0 || sampleRate == 0)
        return wave;

    void* pData = MemAlloc(sampleBytes);
    if (!pData)
        return wave;
    std::memcpy(pData, pSamples, sampleBytes);
    wave.frameCount = static_cast<unsigned int>(sampleBytes / frameSize);
    wave.sampleRate = sampleRate;
    wave.sampleSize = sampleSize;
    wave.channels = channels;
    wave.data = pData;
    return wave;
}

static void WriteLE16(std::ofstream& out, uint16_t value)
{
    const unsigned char bytes[2] = {
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8) & 0xffu)};
    out.write(reinterpret_cast<const char*>(bytes), 2);
}

static void WriteLE32(std::ofstream& out, uint32_t value)
{
    const unsigned char bytes[4] = {
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8) & 0xffu),
        static_cast<unsigned char>((value >> 16) & 0xffu),
        static_cast<unsigned char>((value >> 24) & 0xffu)};
    out.write(reinterpret_cast<const char*>(bytes), 4);
}

static Sound MakeSoundFromWave(Wave wave, bool copyData)
{
    Sound sound{};
    if (!wave.data || wave.frameCount == 0 || !EnsureAudioDevice())
        return sound;
    const ALenum format = GetOpenALFormat(wave.sampleSize, wave.channels);
    if (format == 0)
        return sound;

    ALuint buffer = 0;
    ALuint source = 0;
    alGenBuffers(1, &buffer);
    alGenSources(1, &source);
    const size_t byteCount = static_cast<size_t>(wave.frameCount) * GetAudioFrameSize(wave.sampleSize, wave.channels);
    const void* pSrcData = wave.data;
    std::vector<unsigned char> vOwned;
    if (copyData)
    {
        vOwned.resize(byteCount);
        std::memcpy(vOwned.data(), wave.data, byteCount);
        pSrcData = vOwned.data();
    }
    alBufferData(buffer, format, pSrcData, static_cast<ALsizei>(byteCount), static_cast<ALsizei>(wave.sampleRate));
    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcef(source, AL_GAIN, 1.0f);

    if (alGetError() != AL_NO_ERROR)
    {
        if (source)
            alDeleteSources(1, &source);
        if (buffer)
            alDeleteBuffers(1, &buffer);
        return sound;
    }

    auto* pAudioBuffer = new rAudioBuffer{};
    pAudioBuffer->m_Source = source;
    pAudioBuffer->m_Buffer = buffer;
    pAudioBuffer->m_FrameCount = wave.frameCount;
    pAudioBuffer->m_SampleRate = wave.sampleRate;
    pAudioBuffer->m_SampleSize = wave.sampleSize;
    pAudioBuffer->m_Channels = wave.channels;
    sound.stream.buffer = pAudioBuffer;
    sound.stream.sampleRate = wave.sampleRate;
    sound.stream.sampleSize = wave.sampleSize;
    sound.stream.channels = wave.channels;
    sound.frameCount = wave.frameCount;
    return sound;
}

static void DestroyAudioBuffer(rAudioBuffer* pBuffer, bool deleteOpenALBuffer)
{
    if (!pBuffer)
        return;
    if (pBuffer->m_Source)
        alDeleteSources(1, &pBuffer->m_Source);
    if (deleteOpenALBuffer && pBuffer->m_Buffer)
        alDeleteBuffers(1, &pBuffer->m_Buffer);
    delete pBuffer;
}

void SetMSAASamples(int samples)
{
    gRequestedMSAASamples = (samples == 2 || samples == 4 || samples == 8) ? samples : 1;
#if defined(QC_ENABLE_OPENGL)
    if (gCurrentBackend == RendererType::OpenGL)
    {
        int sampleBuffers = 0;
        int actualSamples = 0;
        if (SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &sampleBuffers) &&
            SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &actualSamples) &&
            (sampleBuffers > 0 ? actualSamples : 1) != gRequestedMSAASamples)
        {
            TraceLog(LogLevel::Warn, "RENDERER",
                     "OpenGL MSAA is configured when the window is created; call SetMSAASamples before InitWindow to change it.");
        }
    }
#endif
#if defined(QC_ENABLE_VULKAN)
    gVkRenderer.SetMSAASamples(gRequestedMSAASamples);
#endif
#if defined(QC_ENABLE_D3D11)
    gD3D11Renderer.SetMSAASamples(gRequestedMSAASamples);
#endif
}

void SetTextureFilterMode(TextureFilter mode)
{
    g_TextureFilter = mode;
    g_Win.m_ActiveTextureFilter = mode;

    if (g_pRenderer)
    {
        gRenderer.SetTextureFilterMode(mode);
    }
}

#if defined(QC_ENABLE_VULKAN)
static bool InitVulkanBackend(int width, int height, const char* pTitle)
{
    TraceLog(LogLevel::Info, "RENDERER", "Backend selected: Vulkan");

    if (!SDL_Vulkan_LoadLibrary(nullptr))
        throw std::runtime_error(std::string("SDL_Vulkan_LoadLibrary failed: ") + SDL_GetError());
    gVulkanLibraryLoaded = true;

    g_Win.m_pWindow = SDL_CreateWindow(
        pTitle,
        width,
        height,
        BuildWindowFlags(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE));

    if (!g_Win.m_pWindow)
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    ApplyPlatformDarkWindowBackground(g_Win.m_pWindow);

    int pixelW = width;
    int pixelH = height;
    SDL_GetWindowSizeInPixels(g_Win.m_pWindow, &pixelW, &pixelH);
    if (pixelW <= 0 || pixelH <= 0)
    {
        pixelW = width;
        pixelH = height;
    }

    g_pRenderer = &gVkRenderer;
    gVkRenderer.SetMSAASamples(gRequestedMSAASamples);
    gRenderer.Init(g_Win.m_pWindow, pixelW, pixelH);
    g_Win.m_PendingConfigFlags = 0u;
    gRenderer.SetTargetFPS(g_Win.m_TargetFps);
    if (g_Win.m_VsyncSet)
    {
        gRenderer.SetVSync(g_Win.m_Vsync);
    }
    gCurrentBackend = RendererType::Vulkan;
    return true;
}
#endif

void InitWindow(int width, int height, const char* pTitle, RendererType rendererType)
{
    TraceLog(LogLevel::Info, "CORE", "Initializing QuarkCore...");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD))
    {
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

    TraceLog(LogLevel::Info, "WINDOW", TextFormat("Starting window creation: %s (%dx%d)", pTitle ? pTitle : "Quark", width, height));

    TraceLog(LogLevel::Info, "RENDERER", TextFormat("Selected backend: %s", RendererTypeToString(rendererType)));

    auto initVulkan = [&]()
    {
#if defined(QC_ENABLE_VULKAN)
        InitVulkanBackend(width, height, pTitle);
#else
        throw std::runtime_error("QuarkCore was built without Vulkan support");
#endif
    };

    auto initOpenGL = [&]()
    {
#if defined(QC_ENABLE_OPENGL)
        InitOpenGLBackend(width, height, pTitle);
#else
        throw std::runtime_error("QuarkCore was built without OpenGL support");
#endif
    };

    auto initD3D11 = [&]()
    {
#if defined(QC_ENABLE_D3D11)
        InitD3D11Backend(width, height, pTitle);
#else
        throw std::runtime_error("QuarkCore was built without D3D11 support");
#endif
    };

    try
    {
        if (rendererType == RendererType::Auto)
        {
            try
            {
                initVulkan();
            }
            catch (const std::exception& ex)
            {
                TraceLog(LogLevel::Warn, "VULKAN", (std::string("Vulkan initialization failed: ") + ex.what() + "; trying OpenGL.").c_str());
                CleanupBackendInitFailure();
                initOpenGL();
            }
            catch (...)
            {
                TraceLog(LogLevel::Warn, "VULKAN", "Vulkan initialization failed with an unknown exception; trying OpenGL.");
                CleanupBackendInitFailure();
                initOpenGL();
            }
        }
        else if (rendererType == RendererType::OpenGL)
        {
            initOpenGL();
        }
        else if (rendererType == RendererType::Vulkan)
        {
            initVulkan();
        }
        else if (rendererType == RendererType::D3D11)
        {
            initD3D11();
        }
    }
    catch (const std::exception& ex)
    {
        TraceLog(LogLevel::Error, "CORE", (std::string("Renderer initialization failed: ") + ex.what()).c_str());
        CleanupBackendInitFailure();
        SDL_Quit();
        return;
    }
    catch (...)
    {
        TraceLog(LogLevel::Error, "CORE", "Renderer initialization failed with an unknown exception.");
        CleanupBackendInitFailure();
        SDL_Quit();
        return;
    }

    if (!g_Win.m_pWindow)
    {
        TraceLog(LogLevel::Error, "WINDOW", "Window is null after renderer initialization.");
        SDL_Quit();
        return;
    }

    WriteLog(LogLevel::Info, "WINDOW", "Window created: " + std::string(pTitle ? pTitle : ""));
}

void CloseWindow()
{
    if (g_pRenderer)
    {
        gRenderer.Shutdown();
        g_pRenderer = nullptr;
    }
    if (gVulkanLibraryLoaded)
    {
#if defined(QC_ENABLE_VULKAN)
        SDL_Vulkan_UnloadLibrary();
#endif
        gVulkanLibraryLoaded = false;
    }
    if (g_Win.m_pWindow)
    {
        SDL_DestroyWindow(g_Win.m_pWindow);
        g_Win.m_pWindow = nullptr;
    }
    gCurrentBackend = RendererType::Auto;
    SDL_Quit();
    WriteLog(LogLevel::Info, "WINDOW", "Window closed");
}

RendererType GetCurrentBackend()
{
    return gCurrentBackend;
}

#if defined(QC_ENABLE_VULKAN)
static QuarkVkRenderer* GetVulkanRenderer()
{
    if (!g_pRenderer || g_pRenderer->GetType() != RendererType::Vulkan)
    {
        return nullptr;
    }
    return &gVkRenderer;
}

VkInstance GetVulkanInstance()
{
    if (QuarkVkRenderer* renderer = GetVulkanRenderer())
    {
        return renderer->GetVulkanInstance();
    }
    return VK_NULL_HANDLE;
}

VkPhysicalDevice GetVulkanPhysicalDevice()
{
    if (QuarkVkRenderer* renderer = GetVulkanRenderer())
    {
        return renderer->GetVulkanPhysicalDevice();
    }
    return VK_NULL_HANDLE;
}

VkDevice GetVulkanDevice()
{
    if (QuarkVkRenderer* renderer = GetVulkanRenderer())
    {
        return renderer->GetVulkanDevice();
    }
    return VK_NULL_HANDLE;
}

uint32_t GetVulkanGraphicsQueueFamily()
{
    if (QuarkVkRenderer* renderer = GetVulkanRenderer())
    {
        return renderer->GetVulkanGraphicsQueueFamily();
    }
    return UINT32_MAX;
}

VkQueue GetVulkanGraphicsQueue()
{
    if (QuarkVkRenderer* renderer = GetVulkanRenderer())
    {
        return renderer->GetVulkanGraphicsQueue();
    }
    return VK_NULL_HANDLE;
}

VkDescriptorPool GetVulkanDescriptorPool()
{
    if (QuarkVkRenderer* renderer = GetVulkanRenderer())
    {
        return renderer->GetVulkanDescriptorPool();
    }
    return VK_NULL_HANDLE;
}

VkRenderPass GetVulkanMainRenderPass()
{
    if (QuarkVkRenderer* renderer = GetVulkanRenderer())
    {
        return renderer->GetVulkanMainRenderPass();
    }
    return VK_NULL_HANDLE;
}

uint32_t GetVulkanMinImageCount()
{
    if (QuarkVkRenderer* renderer = GetVulkanRenderer())
    {
        return renderer->GetVulkanMinImageCount();
    }
    return 0;
}

uint32_t GetVulkanImageCount()
{
    if (QuarkVkRenderer* renderer = GetVulkanRenderer())
    {
        return renderer->GetVulkanImageCount();
    }
    return 0;
}

VkSampleCountFlagBits GetVulkanMSAASamples()
{
    if (QuarkVkRenderer* renderer = GetVulkanRenderer())
    {
        return renderer->GetVulkanMSAASamples();
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

VkDescriptorSet GetVulkanTextureDescriptorSet(uint32_t textureId)
{
    if (QuarkVkRenderer* renderer = GetVulkanRenderer())
    {
        return renderer->GetTextureDescriptorSet(textureId);
    }
    return VK_NULL_HANDLE;
}

static VulkanRenderCallback s_pfnVulkanRenderCallback = nullptr;

void SetVulkanRenderCallback(VulkanRenderCallback callback)
{
    s_pfnVulkanRenderCallback = callback;
}

VulkanRenderCallback GetVulkanRenderCallback()
{
    return s_pfnVulkanRenderCallback;
}
#endif

#if defined(QC_ENABLE_D3D11)
static QuarkD3D11Renderer* GetD3D11Renderer()
{
    if (!g_pRenderer || g_pRenderer->GetType() != RendererType::D3D11)
    {
        return nullptr;
    }
    return &gD3D11Renderer;
}

ID3D11Device* GetD3D11Device()
{
    if (QuarkD3D11Renderer* renderer = GetD3D11Renderer())
    {
        return renderer->GetD3D11Device();
    }
    return nullptr;
}

ID3D11DeviceContext* GetD3D11ImmediateContext()
{
    if (QuarkD3D11Renderer* renderer = GetD3D11Renderer())
    {
        return renderer->GetD3D11ImmediateContext();
    }
    return nullptr;
}

ID3D11ShaderResourceView* GetD3D11TextureShaderResourceView(uint32_t textureId)
{
    if (QuarkD3D11Renderer* renderer = GetD3D11Renderer())
    {
        return renderer->GetD3D11TextureShaderResourceView(textureId);
    }
    return nullptr;
}

static D3D11RenderCallback s_pfnD3D11RenderCallback = nullptr;

void SetD3D11RenderCallback(D3D11RenderCallback callback)
{
    s_pfnD3D11RenderCallback = callback;
}

D3D11RenderCallback GetD3D11RenderCallback()
{
    return s_pfnD3D11RenderCallback;
}
#endif

bool WindowShouldClose()
{
    if (!g_Win.m_InputPolled)
    {
        PumpInput();
    }
    g_Win.m_InputPolled = false;

    if (g_pRenderer && gRenderer.ShouldClose())
        g_Win.m_ShouldClose = true;
    return g_Win.m_ShouldClose;
}

void RequestWindowClose()
{
    g_Win.m_ShouldClose = true;
}

void CancelWindowClose()
{
    g_Win.m_ShouldClose = false;
}

bool IsWindowReady()
{
    return g_Win.m_pWindow != nullptr;
}

int GetScreenWidth()
{
    return g_pRenderer ? gRenderer.GetScreenWidth() : 0;
}
int GetScreenHeight()
{
    return g_pRenderer ? gRenderer.GetScreenHeight() : 0;
}

void SetTargetFPS(int fps)
{
    g_Win.m_TargetFps = fps;
    if (g_pRenderer)
        gRenderer.SetTargetFPS(fps);
}

bool SetVSync(bool enabled)
{
    g_Win.m_Vsync = enabled;
    g_Win.m_VsyncSet = true;
    if (g_pRenderer)
        return gRenderer.SetVSync(enabled);
    return true;
}

float GetFrameTime()
{
    return g_pRenderer ? gRenderer.GetFrameTime() : 0.0f;
}
float GetDeltaTime()
{
    return GetFrameTime();
}

double GetTime()
{
    if (!SDL_WasInit(SDL_INIT_VIDEO))
        return 0.0;
    return static_cast<double>(SDL_GetTicks()) / 1000.0;
}

void SetLogLevel(LogLevel level)
{
    g_Win.m_MinimumLogLevel = level;
}

void SetTraceLogCallback(TraceLogCallback callback)
{
    s_pfnTraceLogCallback = callback;
    if (callback != nullptr)
    {
        TraceLog(LogLevel::Info, "WINDOW", "Trace log callback registered");
    }
}

void TraceLog(LogLevel level, const char* pType, const char* pMessage)
{
    WriteLog(level, pType, pMessage ? pMessage : "");
}

const char* TextFormat(const char* pFmt, ...)
{
    static constexpr int TEXTFORMAT_MAX_BUFFERS = 4;
    static constexpr size_t TEXTFORMAT_BUFFER_SIZE = 4096;
    thread_local char aBuffers[TEXTFORMAT_MAX_BUFFERS][TEXTFORMAT_BUFFER_SIZE] = {};
    thread_local int bufferIndex = 0;

    char* pCurrentBuffer = aBuffers[bufferIndex];
    bufferIndex = (bufferIndex + 1) % TEXTFORMAT_MAX_BUFFERS;

    if (!pFmt)
    {
        pCurrentBuffer[0] = '\0';
        return pCurrentBuffer;
    }

    va_list args;
    va_start(args, pFmt);
#if defined(_MSC_VER)
    vsnprintf_s(pCurrentBuffer, TEXTFORMAT_BUFFER_SIZE, _TRUNCATE, pFmt, args);
#else
    std::vsnprintf(pCurrentBuffer, TEXTFORMAT_BUFFER_SIZE, pFmt, args);
#endif
    va_end(args);
    return pCurrentBuffer;
}

int TextCopy(char* pDst, const char* pSrc)
{
    if (pDst == nullptr || pSrc == nullptr)
        return 0;
    int i = 0;
    for (; pSrc[i] != '\0'; ++i)
        pDst[i] = pSrc[i];
    pDst[i] = '\0';
    return i;
}

bool TextIsEqual(const char* pText1, const char* pText2)
{
    if (pText1 == nullptr || pText2 == nullptr)
        return pText1 == pText2;
    return std::strcmp(pText1, pText2) == 0;
}

unsigned int TextLength(const char* pText)
{
    return pText ? static_cast<unsigned int>(std::strlen(pText)) : 0u;
}

static thread_local char s_aTextBuffer[1024];

const char* TextSubtext(const char* pText, int position, int length)
{
    s_aTextBuffer[0] = '\0';
    if (pText == nullptr)
        return s_aTextBuffer;

    const int textLength = static_cast<int>(std::strlen(pText));
    if (position >= textLength)
        return s_aTextBuffer;
    if (length >= textLength)
        length = textLength;

    int index = 0;
    for (int i = position; (i < textLength) && (index < length); ++i)
        s_aTextBuffer[index++] = pText[i];
    s_aTextBuffer[index] = '\0';
    return s_aTextBuffer;
}

const char* TextRemoveSpaces(const char* pText)
{
    s_aTextBuffer[0] = '\0';
    if (pText == nullptr)
        return s_aTextBuffer;
    int index = 0;
    for (int i = 0; (i < 1024) && (pText[i] != '\0'); ++i)
    {
        if (pText[i] != ' ')
            s_aTextBuffer[index++] = pText[i];
    }
    s_aTextBuffer[index] = '\0';
    return s_aTextBuffer;
}

char* GetTextBetween(const char* pText, const char* pBegin, const char* pEnd)
{
    if (pText == nullptr || pBegin == nullptr || pEnd == nullptr)
        return nullptr;
    const char* pStart = std::strstr(pText, pBegin);
    if (pStart == nullptr)
        return nullptr;
    pStart += std::strlen(pBegin);
    const char* pStop = std::strstr(pStart, pEnd);
    if (pStop == nullptr)
        return nullptr;

    const int length = static_cast<int>(pStop - pStart);
    char* pResult = static_cast<char*>(MemAlloc(static_cast<size_t>(length) + 1));
    if (pResult == nullptr)
        return nullptr;
    std::memcpy(pResult, pStart, static_cast<size_t>(length));
    pResult[length] = '\0';
    return pResult;
}

char* TextReplace(const char* pText, const char* pSearch, const char* pReplacement)
{
    if (pText == nullptr || pSearch == nullptr || pReplacement == nullptr)
        return nullptr;

    const int textLen = static_cast<int>(std::strlen(pText));
    const int searchLen = static_cast<int>(std::strlen(pSearch));
    const int byLen = static_cast<int>(std::strlen(pReplacement));
    if (searchLen == 0)
        return nullptr;

    int count = 0;
    const char* pPtr = std::strstr(pText, pSearch);
    while (pPtr != nullptr)
    {
        ++count;
        pPtr = std::strstr(pPtr + searchLen, pSearch);
    }
    if (count == 0)
        return nullptr;

    const int resultLen = textLen + (byLen - searchLen) * count + 1;
    char* pResult = static_cast<char*>(MemAlloc(static_cast<size_t>(resultLen)));
    if (pResult == nullptr)
        return nullptr;

    int pos = 0;
    const char* pLast = pText;
    pPtr = std::strstr(pLast, pSearch);
    while (pPtr != nullptr)
    {
        const int len = static_cast<int>(pPtr - pLast);
        std::memcpy(pResult + pos, pLast, static_cast<size_t>(len));
        pos += len;
        std::memcpy(pResult + pos, pReplacement, static_cast<size_t>(byLen));
        pos += byLen;
        pLast = pPtr + searchLen;
        pPtr = std::strstr(pLast, pSearch);
    }
    std::memcpy(pResult + pos, pLast, std::strlen(pLast) + 1);
    return pResult;
}

char* TextReplaceAlloc(const char* pText, const char* pSearch, const char* pReplacement)
{
    if (pText == nullptr)
        return nullptr;
    char* pResult = TextReplace(pText, pSearch, pReplacement);
    if (pResult != nullptr)
        return pResult;
    const size_t len = std::strlen(pText);
    pResult = static_cast<char*>(MemAlloc(len + 1));
    if (pResult != nullptr)
        std::memcpy(pResult, pText, len + 1);
    return pResult;
}

char* TextReplaceBetween(const char* pText, const char* pBegin, const char* pEnd, const char* pReplacement)
{
    if (pText == nullptr || pBegin == nullptr || pEnd == nullptr || pReplacement == nullptr)
        return nullptr;

    const char* pStart = std::strstr(pText, pBegin);
    if (pStart == nullptr)
        return nullptr;
    const char* pStop = std::strstr(pStart + std::strlen(pBegin), pEnd);
    if (pStop == nullptr)
        return nullptr;
    pStop += std::strlen(pEnd);

    const int part1Len = static_cast<int>(pStart - pText);
    const int part2Len = static_cast<int>((pText + std::strlen(pText)) - pStop);
    const int replacementLen = static_cast<int>(std::strlen(pReplacement));
    const int resultLen = part1Len + replacementLen + part2Len + 1;

    char* pResult = static_cast<char*>(MemAlloc(static_cast<size_t>(resultLen)));
    if (pResult == nullptr)
        return nullptr;
    std::memcpy(pResult, pText, static_cast<size_t>(part1Len));
    std::memcpy(pResult + part1Len, pReplacement, static_cast<size_t>(replacementLen));
    std::memcpy(pResult + part1Len + replacementLen, pStop, static_cast<size_t>(part2Len));
    pResult[resultLen - 1] = '\0';
    return pResult;
}

char* TextReplaceBetweenAlloc(const char* pText, const char* pBegin, const char* pEnd, const char* pReplacement)
{
    if (pText == nullptr)
        return nullptr;
    char* pResult = TextReplaceBetween(pText, pBegin, pEnd, pReplacement);
    if (pResult != nullptr)
        return pResult;
    const size_t len = std::strlen(pText);
    pResult = static_cast<char*>(MemAlloc(len + 1));
    if (pResult != nullptr)
        std::memcpy(pResult, pText, len + 1);
    return pResult;
}

char* TextInsert(const char* pText, const char* pInsert, int position)
{
    if (pText == nullptr || pInsert == nullptr)
        return nullptr;
    const int textLen = static_cast<int>(std::strlen(pText));
    const int insertLen = static_cast<int>(std::strlen(pInsert));
    if (position < 0 || position > textLen)
        return nullptr;

    char* pResult = static_cast<char*>(MemAlloc(static_cast<size_t>(textLen + insertLen + 1)));
    if (pResult == nullptr)
        return nullptr;
    if (position > 0)
        std::memcpy(pResult, pText, static_cast<size_t>(position));
    std::memcpy(pResult + position, pInsert, static_cast<size_t>(insertLen));
    std::memcpy(pResult + position + insertLen, pText + position, static_cast<size_t>(textLen - position) + 1);
    return pResult;
}

char* TextInsertAlloc(const char* pText, const char* pInsert, int position)
{
    if (pText == nullptr)
        return nullptr;
    char* pResult = TextInsert(pText, pInsert, position);
    if (pResult != nullptr)
        return pResult;
    const size_t len = std::strlen(pText);
    pResult = static_cast<char*>(MemAlloc(len + 1));
    if (pResult != nullptr)
        std::memcpy(pResult, pText, len + 1);
    return pResult;
}

char* TextJoin(char** ppTextList, int count, const char* pDelimiter)
{
    if (ppTextList == nullptr || count <= 0)
        return nullptr;

    int totalLength = 0;
    const int delimiterLength = pDelimiter ? static_cast<int>(std::strlen(pDelimiter)) : 0;
    for (int i = 0; i < count; ++i)
    {
        if (ppTextList[i] != nullptr)
            totalLength += static_cast<int>(std::strlen(ppTextList[i]));
        if (i < count - 1)
            totalLength += delimiterLength;
    }

    char* pResult = static_cast<char*>(MemAlloc(static_cast<size_t>(totalLength) + 1));
    if (pResult == nullptr)
        return nullptr;
    int pos = 0;
    for (int i = 0; i < count; ++i)
    {
        if (ppTextList[i] != nullptr)
        {
            const int len = static_cast<int>(std::strlen(ppTextList[i]));
            std::memcpy(pResult + pos, ppTextList[i], static_cast<size_t>(len));
            pos += len;
        }
        if (i < count - 1 && pDelimiter != nullptr)
        {
            const int dlen = static_cast<int>(std::strlen(pDelimiter));
            std::memcpy(pResult + pos, pDelimiter, static_cast<size_t>(dlen));
            pos += dlen;
        }
    }
    pResult[pos] = '\0';
    return pResult;
}

char** TextSplit(const char* pText, char delimiter, int* pCount)
{
    if (pCount != nullptr)
        *pCount = 0;
    if (pText == nullptr)
        return nullptr;

    int ncount = 1;
    const char* pPtr = pText;
    while (*pPtr != '\0')
    {
        if (*pPtr == delimiter)
            ++ncount;
        ++pPtr;
    }

    char** ppResult = static_cast<char**>(MemAlloc(static_cast<size_t>(ncount) * sizeof(char*)));
    if (ppResult == nullptr)
        return nullptr;

    int index = 0;
    int length = 0;
    pPtr = pText;
    while (*pPtr != '\0')
    {
        if (*pPtr != delimiter)
        {
            ++length;
        }
        else
        {
            ppResult[index] = static_cast<char*>(MemAlloc(static_cast<size_t>(length) + 1));
            if (length > 0)
                std::memcpy(ppResult[index], pPtr - length, static_cast<size_t>(length));
            ppResult[index][length] = '\0';
            length = 0;
            ++index;
        }
        ++pPtr;
    }
    ppResult[index] = static_cast<char*>(MemAlloc(static_cast<size_t>(length) + 1));
    if (length > 0)
        std::memcpy(ppResult[index], pPtr - length, static_cast<size_t>(length));
    ppResult[index][length] = '\0';
    ++index;

    if (pCount != nullptr)
        *pCount = index;
    return ppResult;
}

void TextAppend(char* pText, const char* pAppend, int* pPosition)
{
    if (pText == nullptr || pAppend == nullptr || pPosition == nullptr)
        return;
    int textLen = static_cast<int>(std::strlen(pText));
    int pos = *pPosition;
    if (pos > textLen)
        pos = textLen;
    if (pos < 0)
        pos = 0;
    const int appendLen = static_cast<int>(std::strlen(pAppend));
    for (int i = 0; i < appendLen; ++i)
        pText[pos + i] = pAppend[i];
    pText[pos + appendLen] = '\0';
    *pPosition = pos + appendLen;
}

int TextFindIndex(const char* pText, const char* pSearch)
{
    if (pText == nullptr || pSearch == nullptr)
        return -1;
    const char* pFound = std::strstr(pText, pSearch);
    if (pFound == nullptr)
        return -1;
    return static_cast<int>(pFound - pText);
}

const char* TextToUpper(const char* pText)
{
    s_aTextBuffer[0] = '\0';
    if (pText == nullptr)
        return s_aTextBuffer;
    int i = 0;
    for (; (i < 1023) && (pText[i] != '\0'); ++i)
        s_aTextBuffer[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(pText[i])));
    s_aTextBuffer[i] = '\0';
    return s_aTextBuffer;
}

const char* TextToLower(const char* pText)
{
    s_aTextBuffer[0] = '\0';
    if (pText == nullptr)
        return s_aTextBuffer;
    int i = 0;
    for (; (i < 1023) && (pText[i] != '\0'); ++i)
        s_aTextBuffer[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(pText[i])));
    s_aTextBuffer[i] = '\0';
    return s_aTextBuffer;
}

const char* TextToPascal(const char* pText)
{
    s_aTextBuffer[0] = '\0';
    if (pText == nullptr)
        return s_aTextBuffer;

    int j = 0;
    int i = 0;
    if (pText[i] != '\0')
    {
        s_aTextBuffer[j++] = static_cast<char>(std::toupper(static_cast<unsigned char>(pText[i])));
        ++i;
    }
    for (; (i < 1023) && (pText[i] != '\0'); ++i)
    {
        if (pText[i] == ' ' || pText[i] == '_' || pText[i] == '-')
        {
            ++i;
            if (pText[i] != '\0')
                s_aTextBuffer[j] = static_cast<char>(std::toupper(static_cast<unsigned char>(pText[i])));
        }
        else
        {
            s_aTextBuffer[j] = pText[i];
        }
        ++j;
    }
    s_aTextBuffer[j] = '\0';
    return s_aTextBuffer;
}

const char* TextToSnake(const char* pText)
{
    s_aTextBuffer[0] = '\0';
    if (pText == nullptr)
        return s_aTextBuffer;
    int j = 0;
    for (int i = 0; (i < 1023) && (pText[i] != '\0'); ++i)
    {
        if (pText[i] == ' ' || pText[i] == '-' || ((pText[i] >= 'A') && (pText[i] <= 'Z')))
        {
            if ((j > 0) && (s_aTextBuffer[j - 1] != '_'))
                s_aTextBuffer[j++] = '_';
            if ((pText[i] >= 'A') && (pText[i] <= 'Z'))
                s_aTextBuffer[j++] = static_cast<char>(std::tolower(static_cast<unsigned char>(pText[i])));
        }
        else
        {
            s_aTextBuffer[j++] = pText[i];
        }
    }
    s_aTextBuffer[j] = '\0';
    return s_aTextBuffer;
}

const char* TextToCamel(const char* pText)
{
    s_aTextBuffer[0] = '\0';
    if (pText == nullptr)
        return s_aTextBuffer;

    int j = 0;
    int i = 0;
    if (pText[i] != '\0')
    {
        s_aTextBuffer[j++] = static_cast<char>(std::tolower(static_cast<unsigned char>(pText[i])));
        ++i;
    }
    for (; (i < 1023) && (pText[i] != '\0'); ++i)
    {
        if (pText[i] == ' ' || pText[i] == '_' || pText[i] == '-')
        {
            ++i;
            if (pText[i] != '\0')
                s_aTextBuffer[j] = static_cast<char>(std::toupper(static_cast<unsigned char>(pText[i])));
        }
        else
        {
            s_aTextBuffer[j] = static_cast<char>(std::tolower(static_cast<unsigned char>(pText[i])));
        }
        ++j;
    }
    s_aTextBuffer[j] = '\0';
    return s_aTextBuffer;
}

int TextToInteger(const char* pText)
{
    if (pText == nullptr)
        return 0;
    int value = 0;
    int sign = 1;
    if ((pText[0] == '-') || (pText[0] == '+'))
    {
        if (pText[0] == '-')
            sign = -1;
        ++pText;
    }
    for (int i = 0; (pText[i] >= '0') && (pText[i] <= '9'); ++i)
        value = value * 10 + (pText[i] - '0');
    return value * sign;
}

float TextToFloat(const char* pText)
{
    if (pText == nullptr)
        return 0.0f;
    float value = 0.0f;
    float sign = 1.0f;
    if ((pText[0] == '-') || (pText[0] == '+'))
    {
        if (pText[0] == '-')
            sign = -1.0f;
        ++pText;
    }
    for (int i = 0; (pText[i] >= '0') && (pText[i] <= '9'); ++i)
        value = value * 10.0f + static_cast<float>(pText[i] - '0');
    if (pText[0] == '.')
    {
        float decimal = 0.1f;
        for (int i = 1; (pText[i] >= '0') && (pText[i] <= '9'); ++i)
        {
            value += static_cast<float>(pText[i] - '0') * decimal;
            decimal *= 0.1f;
        }
    }
    return value * sign;
}

char** LoadTextLines(const char* pText, int* pCount)
{
    if (pCount != nullptr)
        *pCount = 0;
    if (pText == nullptr)
        return nullptr;

    int ncount = 1;
    for (int i = 0; pText[i] != '\0'; ++i)
        if (pText[i] == '\n')
            ++ncount;

    char** ppLines = static_cast<char**>(MemAlloc(static_cast<size_t>(ncount) * sizeof(char*)));
    if (ppLines == nullptr)
        return nullptr;

    int index = 0;
    int length = 0;
    int start = 0;
    for (int i = 0;; ++i)
    {
        const char c = pText[i];
        if (c == '\0')
        {
            if (length > 0)
            {
                ppLines[index] = static_cast<char*>(MemAlloc(static_cast<size_t>(length) + 1));
                if (length > 0)
                    std::memcpy(ppLines[index], pText + start, static_cast<size_t>(length));
                ppLines[index][length] = '\0';
                ++index;
            }
            break;
        }
        if (c == '\n')
        {
            ppLines[index] = static_cast<char*>(MemAlloc(static_cast<size_t>(length) + 1));
            if (length > 0)
                std::memcpy(ppLines[index], pText + start, static_cast<size_t>(length));
            ppLines[index][length] = '\0';
            ++index;
            ++i;
            length = 0;
            start = i;
            if (pText[i] == '\0')
            {
                ppLines[index] = static_cast<char*>(MemAlloc(1));
                ppLines[index][0] = '\0';
                ++index;
                break;
            }
        }
        else
        {
            ++length;
        }
    }

    if (pCount != nullptr)
        *pCount = index;
    return ppLines;
}

void UnloadTextLines(char** ppText, int lineCount)
{
    if (ppText == nullptr)
        return;
    for (int i = 0; i < lineCount; ++i)
    {
        if (ppText[i] != nullptr)
            MemFree(ppText[i]);
    }
    MemFree(ppText);
}

SDL_GLContext GetNativeContext()
{
    EnsureInitialized();
    return SDL_GL_GetCurrentContext();
}

bool IsTextInputActive()
{
    EnsureInitialized();
    return SDL_TextInputActive(g_Win.m_pWindow);
}

bool IsKeyPressedRepeat(int key)
{
    return IsKeyPressed(static_cast<KeyboardKey>(key));
}

const char* GetKeyName(int key)
{
    return SDL_GetKeyName(static_cast<SDL_Keycode>(key));
}

int GetTouchX(void)
{
    return static_cast<int>(GetMousePosition().x);
}

int GetTouchY(void)
{
    return static_cast<int>(GetMousePosition().y);
}

Vec2 GetTouchPosition(int index)
{
    if (index == 0)
    {
        return GetMousePosition();
    }
    return Vec2{};
}

int GetTouchPointId(int index)
{
    if (index == 0)
    {
        return 0;
    }
    return -1;
}

int GetTouchPointCount(void)
{
    const bool hasTouch =
        IsMouseButtonDown(MOUSE_BUTTON_LEFT) ||
        IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ||
        IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);

    return hasTouch ? 1 : 0;
}

void SetGesturesEnabled(unsigned int flags)
{
    g_Win.m_EnabledGestures = flags;
    if (flags == GESTURE_NONE)
    {
        g_Win.m_CurrentGesture = GESTURE_NONE;
    }
}

bool IsGestureDetected(unsigned int gesture)
{
    if ((g_Win.m_EnabledGestures & gesture) == 0u)
    {
        return false;
    }

    return g_Win.m_CurrentGesture == static_cast<int>(gesture);
}

int GetGestureDetected(void)
{
    return g_Win.m_CurrentGesture;
}

float GetGestureHoldDuration(void)
{
    return g_Win.m_GestureHoldDuration;
}

Vec2 GetGestureDragVector(void)
{
    return g_Win.m_GestureDragVector;
}

float GetGestureDragAngle(void)
{
    return g_Win.m_GestureDragAngle;
}

Vec2 GetGesturePinchVector(void)
{
    return g_Win.m_GesturePinchVector;
}

float GetGesturePinchAngle(void)
{
    return g_Win.m_GesturePinchAngle;
}

void UpdateCamera(Camera3D* pCamera, int mode)
{
    if (!pCamera)
    {
        return;
    }

    pCamera->projection = mode;
    if (mode == CAMERA_FREE || mode == CAMERA_ORBITAL || mode == CAMERA_FIRST_PERSON || mode == CAMERA_THIRD_PERSON)
    {
        return;
    }

    pCamera->target = pCamera->position + Vec3{0.0f, 0.0f, -1.0f};
}

void UpdateCameraPro(Camera3D* pCamera, Vec2 movement, Vec3 rotation, float zoom)
{
    if (!pCamera)
    {
        return;
    }

    const Vec3 delta{movement.x, movement.y, 0.0f};
    pCamera->position += delta;
    pCamera->target += delta;

    if (rotation.x != 0.0f || rotation.y != 0.0f || rotation.z != 0.0f)
    {
        const Vec3 direction = (pCamera->target - pCamera->position).normalized();
        const Vec3 right = direction.cross(pCamera->up).normalized();
        const Vec3 up = right.cross(direction).normalized();

        const float yaw = rotation.y * DEG2RAD;
        const float pitch = rotation.x * DEG2RAD;
        const float roll = rotation.z * DEG2RAD;

        Vec3 rotatedDirection = direction;
        rotatedDirection = Vec3{
            rotatedDirection.x * std::cos(yaw) + rotatedDirection.z * std::sin(yaw),
            rotatedDirection.y,
            -rotatedDirection.x * std::sin(yaw) + rotatedDirection.z * std::cos(yaw)};

        rotatedDirection = Vec3{
            rotatedDirection.x,
            rotatedDirection.y * std::cos(pitch) - rotatedDirection.z * std::sin(pitch),
            rotatedDirection.y * std::sin(pitch) + rotatedDirection.z * std::cos(pitch)};

        const Vec3 rotatedUp = Vec3{
            up.x * std::cos(roll) - right.x * std::sin(roll),
            up.y * std::cos(roll) - right.y * std::sin(roll),
            up.z * std::cos(roll) - right.z * std::sin(roll)};

        pCamera->target = pCamera->position + rotatedDirection;
        pCamera->up = rotatedUp.normalized();
    }

    pCamera->fovy = std::clamp(pCamera->fovy + zoom, 1.0f, 179.0f);
}

Mat4 GetCameraMatrix(Camera3D camera)
{
    return GetCameraMat4(camera);
}

Mat4 GetCameraMatrix2D(Camera2D camera)
{
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

void BeginBlendMode(int mode)
{
    g_Win.m_ActiveBlendMode = mode;

    if (g_pRenderer)
    {
        gRenderer.SetBlendMode(mode);
    }
}

void EndBlendMode(void)
{
    g_Win.m_ActiveBlendMode = BLEND_ALPHA;

    if (g_pRenderer)
    {
        gRenderer.SetBlendMode(BLEND_ALPHA);
    }
}

void BeginScissorMode(int x, int y, int width, int height)
{
    g_Win.m_ScissorEnabled = true;
    g_Win.m_ScissorRect = Rectangle{
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(width),
        static_cast<float>(height)};

    if (g_pRenderer)
    {
        gRenderer.BeginScissorMode(x, y, width, height);
    }
}

void EndScissorMode(void)
{
    g_Win.m_ScissorEnabled = false;
    g_Win.m_ScissorRect = Rectangle{};

    if (g_pRenderer)
    {
        gRenderer.EndScissorMode();
    }
}

void SetTextureFilter(Texture2D texture, TextureFilter filter)
{
    (void)texture;
    g_Win.m_ActiveTextureFilter = filter;
    SetTextureFilterMode(filter);

    if (g_pRenderer)
    {
        gRenderer.SetTextureFilter(filter);
    }
}

void SetTextureWrap(Texture2D texture, int wrap)
{
    g_Win.m_ActiveTextureWrap = wrap;

    if (g_pRenderer)
    {
        gRenderer.SetTextureWrap(texture.id, wrap);
    }
}

void InitAudioDevice(void)
{
    if (gAudioContext != nullptr)
        return;
    TraceLog(LogLevel::Info, "AUDIO", "Initializing OpenAL audio device...");
    gAudioDevice = alcOpenDevice(nullptr);
    if (!gAudioDevice)
    {
        TraceLog(LogLevel::Error, "AUDIO", "Failed to open OpenAL device");
        return;
    }
    gAudioContext = alcCreateContext(gAudioDevice, nullptr);
    if (!gAudioContext || !alcMakeContextCurrent(gAudioContext))
    {
        TraceLog(LogLevel::Error, "AUDIO", "Failed to create OpenAL context");
        if (gAudioContext)
            alcDestroyContext(gAudioContext);
        alcCloseDevice(gAudioDevice);
        gAudioContext = nullptr;
        gAudioDevice = nullptr;
        return;
    }
    alListenerf(AL_GAIN, gMasterVolume);
    ALenum error = alGetError();
    if (error != AL_NO_ERROR)
    {
        TraceLog(LogLevel::Warn, "AUDIO", TextFormat("OpenAL context initialized, but reported error: 0x%04X", error));
    }
    TraceLog(LogLevel::Info, "AUDIO", "OpenAL audio device initialized successfully.");
}

void CloseAudioDevice(void)
{
    if (gAudioContext)
    {
        TraceLog(LogLevel::Info, "AUDIO", "Closing OpenAL audio context...");
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(gAudioContext);
        gAudioContext = nullptr;
    }
    if (gAudioDevice)
    {
        alcCloseDevice(gAudioDevice);
        gAudioDevice = nullptr;
    }
}

bool IsAudioDeviceReady(void)
{
    return gAudioDevice != nullptr && gAudioContext != nullptr;
}

void SetMasterVolume(float volume)
{
    gMasterVolume = std::clamp(volume, 0.0f, 1.0f);
    if (IsAudioDeviceReady())
        alListenerf(AL_GAIN, gMasterVolume);
}

float GetMasterVolume(void)
{
    return gMasterVolume;
}

Wave LoadWave(const char* pFileName)
{
    int dataSize = 0;
    unsigned char* pData = LoadFileData(pFileName, &dataSize);
    Wave wave = LoadWaveFromMemory(GetFileExtension(pFileName), pData, dataSize);
    UnloadFileData(pData);
    return wave;
}

Wave LoadWaveFromMemory(const char* pFileType, const unsigned char* pFileData, int dataSize)
{
    if (!pFileType || !pFileData || dataSize <= 0)
    {
        TraceLog(LogLevel::Warn, "AUDIO", TextFormat("LoadWaveFromMemory: invalid input (fileType=%s, data=%p, size=%d)", pFileType ? pFileType : "<null>", static_cast<const void*>(pFileData), dataSize));
        return {};
    }
    if (TextIsEqual(pFileType, ".wav") || TextIsEqual(pFileType, "wav") || TextIsEqual(pFileType, ".WAV") || TextIsEqual(pFileType, "WAV"))
    {
        TraceLog(LogLevel::Trace, "AUDIO", TextFormat("Loading WAV data (%d bytes)", dataSize));
        return LoadWaveFromWavData(pFileData, dataSize);
    }
    TraceLog(LogLevel::Warn, "AUDIO", TextFormat("Unsupported wave format: %s", pFileType));
    return {};
}

float* LoadWaveSamples(Wave wave)
{
    if (!wave.data || wave.frameCount == 0 || wave.channels == 0)
        return nullptr;
    const size_t count = static_cast<size_t>(wave.frameCount) * wave.channels;
    float* pSamples = static_cast<float*>(MemAlloc(count * sizeof(float)));
    if (!pSamples)
        return nullptr;

    if (wave.sampleSize == 8)
    {
        const auto* pSrc = static_cast<const unsigned char*>(wave.data);
        for (size_t i = 0; i < count; ++i)
            pSamples[i] = (static_cast<float>(pSrc[i]) - 128.0f) / 128.0f;
    }
    else if (wave.sampleSize == 16)
    {
        const auto* pSrc = static_cast<const int16_t*>(wave.data);
        for (size_t i = 0; i < count; ++i)
            pSamples[i] = static_cast<float>(pSrc[i]) / 32768.0f;
    }
    else if (wave.sampleSize == 32)
    {
        std::memcpy(pSamples, wave.data, count * sizeof(float));
    }
    else
    {
        MemFree(pSamples);
        return nullptr;
    }
    return pSamples;
}

Sound LoadSound(const char* pFileName)
{
    TraceLog(LogLevel::Trace, "AUDIO", TextFormat("Loading sound file: %s", pFileName ? pFileName : "<null>"));
    Wave wave = LoadWave(pFileName);
    Sound sound = LoadSoundFromWave(wave);
    UnloadWave(wave);
    if (sound.stream.buffer == nullptr)
    {
        TraceLog(LogLevel::Warn, "AUDIO", TextFormat("Failed to load sound file: %s", pFileName ? pFileName : "<null>"));
    }
    else
    {
        TraceLog(LogLevel::Info, "AUDIO", TextFormat("Sound loaded successfully: %s (%u frames)", pFileName ? pFileName : "<null>", sound.frameCount));
    }
    return sound;
}

Sound LoadSoundFromWave(Wave wave)
{
    if (wave.data == nullptr || wave.frameCount == 0)
    {
        TraceLog(LogLevel::Warn, "AUDIO", "LoadSoundFromWave: empty wave data");
        return {};
    }
    TraceLog(LogLevel::Trace, "AUDIO", TextFormat("Creating sound buffer: %u frames, %uHz, %u-bit, %u channels", wave.frameCount, wave.sampleRate, wave.sampleSize, wave.channels));
    return MakeSoundFromWave(wave, false);
}

Sound LoadSoundAlias(Sound source)
{
    Sound alias{};
    if (!source.stream.buffer || !EnsureAudioDevice())
        return alias;
    ALuint src = 0;
    alGenSources(1, &src);
    alSourcei(src, AL_BUFFER, static_cast<ALint>(source.stream.buffer->m_Buffer));
    auto* pBuffer = new rAudioBuffer{*source.stream.buffer};
    pBuffer->m_Source = src;
    pBuffer->m_OwnsBuffer = false;
    alias.stream = source.stream;
    alias.stream.buffer = pBuffer;
    alias.frameCount = source.frameCount;
    return alias;
}

void UpdateSound(Sound sound, const void* pData, int frameCount)
{
    if (!sound.stream.buffer || !pData || frameCount <= 0)
        return;
    const ALenum format = GetOpenALFormat(sound.stream.sampleSize, sound.stream.channels);
    if (format == 0)
        return;
    const size_t byteCount = static_cast<size_t>(frameCount) * GetAudioFrameSize(sound.stream.sampleSize, sound.stream.channels);
    alSourceStop(sound.stream.buffer->m_Source);
    alBufferData(sound.stream.buffer->m_Buffer, format, pData, static_cast<ALsizei>(byteCount), static_cast<ALsizei>(sound.stream.sampleRate));
    sound.stream.buffer->m_FrameCount = static_cast<unsigned int>(frameCount);
}

void UnloadWave(Wave wave)
{
    if (wave.data)
        MemFree(wave.data);
}

void UnloadSound(Sound sound)
{
    TraceLog(LogLevel::Trace, "AUDIO", TextFormat("Unloading sound: buffer=%p", static_cast<void*>(sound.stream.buffer)));
    DestroyAudioBuffer(sound.stream.buffer, sound.stream.buffer ? sound.stream.buffer->m_OwnsBuffer : false);
}

void UnloadSoundAlias(Sound alias)
{
    DestroyAudioBuffer(alias.stream.buffer, false);
}

bool ExportWave(Wave wave, const char* pFileName)
{
    if (!wave.data || !pFileName)
        return false;
    const size_t dataBytes = static_cast<size_t>(wave.frameCount) * GetAudioFrameSize(wave.sampleSize, wave.channels);
    if (dataBytes == 0 || dataBytes > UINT32_MAX)
        return false;
    std::ofstream out(pFileName, std::ios::binary);
    if (!out)
        return false;
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

bool ExportWaveAsCode(Wave wave, const char* pFileName)
{
    const size_t dataBytes = static_cast<size_t>(wave.frameCount) * GetAudioFrameSize(wave.sampleSize, wave.channels);
    return ExportDataAsCode(static_cast<const unsigned char*>(wave.data), static_cast<int>(dataBytes), pFileName);
}

Wave WaveCopy(Wave wave)
{
    Wave copy = wave;
    const size_t bytes = static_cast<size_t>(wave.frameCount) * GetAudioFrameSize(wave.sampleSize, wave.channels);
    copy.data = nullptr;
    if (wave.data && bytes > 0)
    {
        copy.data = MemAlloc(bytes);
        if (copy.data)
            std::memcpy(copy.data, wave.data, bytes);
    }
    return copy;
}

void WaveCrop(Wave* pWave, int initFrame, int finalFrame)
{
    if (!pWave || !pWave->data || pWave->frameCount == 0)
        return;
    initFrame = std::clamp(initFrame, 0, static_cast<int>(pWave->frameCount));
    finalFrame = std::clamp(finalFrame, initFrame, static_cast<int>(pWave->frameCount));
    const size_t frameSize = GetAudioFrameSize(pWave->sampleSize, pWave->channels);
    const size_t frames = static_cast<size_t>(finalFrame - initFrame);
    void* pData = MemAlloc(frames * frameSize);
    if (!pData)
        return;
    std::memcpy(pData, static_cast<unsigned char*>(pWave->data) + static_cast<size_t>(initFrame) * frameSize, frames * frameSize);
    MemFree(pWave->data);
    pWave->data = pData;
    pWave->frameCount = static_cast<unsigned int>(frames);
}

void WaveFormat(Wave* pWave, int sampleRate, int sampleSize, int channels)
{
    if (!pWave || !pWave->data || sampleRate <= 0 || channels <= 0)
        return;
    if (sampleRate == static_cast<int>(pWave->sampleRate) && sampleSize == static_cast<int>(pWave->sampleSize) && channels == static_cast<int>(pWave->channels))
        return;
    float* pSamples = LoadWaveSamples(*pWave);
    if (!pSamples)
        return;
    const size_t outCount = static_cast<size_t>(pWave->frameCount) * static_cast<size_t>(channels);
    const size_t outBytes = outCount * static_cast<size_t>((sampleSize + 7) / 8);
    void* pOut = MemAlloc(outBytes);
    if (!pOut)
    {
        MemFree(pSamples);
        return;
    }
    for (unsigned int f = 0; f < pWave->frameCount; ++f)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            const float value = pSamples[static_cast<size_t>(f) * pWave->channels + static_cast<unsigned int>(std::min<int>(ch, pWave->channels - 1))];
            const size_t idx = static_cast<size_t>(f) * channels + ch;
            if (sampleSize == 8)
                static_cast<unsigned char*>(pOut)[idx] = static_cast<unsigned char>(std::clamp(value * 127.0f + 128.0f, 0.0f, 255.0f));
            else if (sampleSize == 16)
                static_cast<int16_t*>(pOut)[idx] = static_cast<int16_t>(std::clamp(value, -1.0f, 1.0f) * 32767.0f);
            else if (sampleSize == 32)
                static_cast<float*>(pOut)[idx] = value;
        }
    }
    MemFree(pSamples);
    MemFree(pWave->data);
    pWave->data = pOut;
    pWave->sampleRate = static_cast<unsigned int>(sampleRate);
    pWave->sampleSize = static_cast<unsigned int>(sampleSize);
    pWave->channels = static_cast<unsigned int>(channels);
}

Music LoadMusicStream(const char* pFileName)
{
    Music music{};
    music.stream = LoadSound(pFileName).stream;
    if (music.stream.buffer)
        music.frameCount = music.stream.buffer->m_FrameCount;
    return music;
}

Music LoadMusicStreamFromMemory(const char* pFileType, const unsigned char* pData, int dataSize)
{
    Wave wave = LoadWaveFromMemory(pFileType, pData, dataSize);
    Music music{};
    Sound sound = LoadSoundFromWave(wave);
    music.stream = sound.stream;
    music.frameCount = sound.frameCount;
    UnloadWave(wave);
    return music;
}

void UnloadMusicStream(Music music)
{
    UnloadAudioStream(music.stream);
}

bool IsMusicValid(Music music)
{
    return IsAudioStreamValid(music.stream);
}

void PlayMusicStream(Music music)
{
    PlayAudioStream(music.stream);
}

bool IsMusicStreamPlaying(Music music)
{
    return IsAudioStreamPlaying(music.stream);
}

void UpdateMusicStream(Music music)
{
    if (!music.stream.buffer)
    {
        return;
    }

    ALint state = 0;
    alGetSourcei(music.stream.buffer->m_Source, AL_SOURCE_STATE, &state);
    if (state == AL_PLAYING || state == AL_PAUSED)
    {
        return;
    }
}

void StopMusicStream(Music music)
{
    StopAudioStream(music.stream);
}

void PauseMusicStream(Music music)
{
    PauseAudioStream(music.stream);
}

void ResumeMusicStream(Music music)
{
    ResumeAudioStream(music.stream);
}

void SeekMusicStream(Music music, float position)
{
    if (music.stream.buffer)
    {
        alSourcef(music.stream.buffer->m_Source, AL_SEC_OFFSET, position);
    }
}

void SetMusicVolume(Music music, float volume)
{
    SetAudioStreamVolume(music.stream, volume);
}

void SetMusicPitch(Music music, float pitch)
{
    SetAudioStreamPitch(music.stream, pitch);
}

void SetMusicPan(Music music, float pan)
{
    SetAudioStreamPan(music.stream, pan);
}

float GetMusicTimeLength(Music music)
{
    return (music.stream.sampleRate == 0)
               ? 0.0f
               : static_cast<float>(music.frameCount) / static_cast<float>(music.stream.sampleRate);
}

float GetMusicTimePlayed(Music music)
{
    float value = 0.0f;
    if (music.stream.buffer)
    {
        alGetSourcef(music.stream.buffer->m_Source, AL_SEC_OFFSET, &value);
    }
    return value;
}

AudioStream LoadAudioStream(unsigned int sampleRate, unsigned int sampleSize, unsigned int channels)
{
    AudioStream stream{};
    TraceLog(LogLevel::Trace, "AUDIO", TextFormat("Creating audio stream: %uHz, %u-bit, %u channels", sampleRate, sampleSize, channels));
    if (!EnsureAudioDevice() || GetOpenALFormat(sampleSize, channels) == 0)
    {
        TraceLog(LogLevel::Error, "AUDIO", TextFormat("Failed to create audio stream: invalid device or unsupported format (%uHz, %u-bit, %u channels)", sampleRate, sampleSize, channels));
        return stream;
    }

    auto* pBuffer = new rAudioBuffer{};
    alGenBuffers(1, &pBuffer->m_Buffer);
    alGenSources(1, &pBuffer->m_Source);
    pBuffer->m_SampleRate = sampleRate;
    pBuffer->m_SampleSize = sampleSize;
    pBuffer->m_Channels = channels;
    pBuffer->m_Streaming = true;
    stream.buffer = pBuffer;
    stream.sampleRate = sampleRate;
    stream.sampleSize = sampleSize;
    stream.channels = channels;
    TraceLog(LogLevel::Info, "AUDIO", TextFormat("Audio stream created successfully (%uHz, %u-bit, %u channels)", sampleRate, sampleSize, channels));
    return stream;
}

void UnloadAudioStream(AudioStream stream)
{
    DestroyAudioBuffer(stream.buffer, stream.buffer ? stream.buffer->m_OwnsBuffer : false);
}

bool IsAudioStreamValid(AudioStream stream)
{
    return stream.buffer != nullptr && stream.buffer->m_Source != 0;
}

bool IsAudioStreamPlaying(AudioStream stream)
{
    ALint state = 0;
    if (stream.buffer)
    {
        alGetSourcei(stream.buffer->m_Source, AL_SOURCE_STATE, &state);
    }
    return state == AL_PLAYING;
}

bool IsAudioStreamProcessed(AudioStream stream)
{
    if (!stream.buffer)
    {
        return false;
    }

    ALint processed = 0;
    alGetSourcei(stream.buffer->m_Source, AL_BUFFERS_QUEUED, &processed);
    return processed > 0;
}

void PlayAudioStream(AudioStream stream)
{
    if (!stream.buffer)
    {
        TraceLog(LogLevel::Warn, "AUDIO", "PlayAudioStream: invalid stream");
        return;
    }
    TraceLog(LogLevel::Trace, "AUDIO", TextFormat("Playing audio stream (source=%u)", stream.buffer->m_Source));
    alSourcePlay(stream.buffer->m_Source);
}

void PauseAudioStream(AudioStream stream)
{
    if (stream.buffer)
    {
        TraceLog(LogLevel::Trace, "AUDIO", TextFormat("Pausing audio stream (source=%u)", stream.buffer->m_Source));
        alSourcePause(stream.buffer->m_Source);
    }
}

void ResumeAudioStream(AudioStream stream)
{
    PlayAudioStream(stream);
}

void StopAudioStream(AudioStream stream)
{
    if (stream.buffer)
    {
        alSourceStop(stream.buffer->m_Source);
    }
}

void UpdateAudioStream(AudioStream stream, const void* pData, int frameCount)
{
    if (!stream.buffer || !pData || frameCount <= 0)
    {
        return;
    }

    if (stream.buffer->m_Callback)
    {
        stream.buffer->m_Callback(const_cast<void*>(pData), static_cast<unsigned int>(frameCount));
    }

    for (AudioCallback processor : stream.buffer->m_vProcessors)
    {
        if (processor)
        {
            processor(const_cast<void*>(pData), static_cast<unsigned int>(frameCount));
        }
    }

    for (AudioCallback processor : gAudioMixedProcessors)
    {
        if (processor)
        {
            processor(const_cast<void*>(pData), static_cast<unsigned int>(frameCount));
        }
    }

    const ALenum format = GetOpenALFormat(stream.sampleSize, stream.channels);
    const size_t bytes = static_cast<size_t>(frameCount) * GetAudioFrameSize(stream.sampleSize, stream.channels);
    alSourceStop(stream.buffer->m_Source);
    alBufferData(stream.buffer->m_Buffer, format, pData, static_cast<ALsizei>(bytes), static_cast<ALsizei>(stream.sampleRate));
    alSourcei(stream.buffer->m_Source, AL_BUFFER, static_cast<ALint>(stream.buffer->m_Buffer));
}

void SetAudioStreamBufferSizeDefault(int size)
{
    if (size > 0)
    {
        gAudioStreamBufferSizeDefault = size;
    }
}

void SetAudioStreamCallback(AudioStream stream, AudioCallback callback)
{
    if (stream.buffer)
    {
        stream.buffer->m_Callback = callback;
    }
}

void AttachAudioStreamProcessor(AudioStream stream, AudioCallback processor)
{
    if (stream.buffer && processor)
    {
        stream.buffer->m_vProcessors.push_back(processor);
    }
}

void DetachAudioStreamProcessor(AudioStream stream, AudioCallback processor)
{
    if (!stream.buffer)
    {
        return;
    }

    auto& list = stream.buffer->m_vProcessors;
    list.erase(std::remove(list.begin(), list.end(), processor), list.end());
}

void AttachAudioMixedProcessor(AudioCallback processor)
{
    if (processor)
    {
        gAudioMixedProcessors.push_back(processor);
    }
}

void DetachAudioMixedProcessor(AudioCallback processor)
{
    gAudioMixedProcessors.erase(
        std::remove(gAudioMixedProcessors.begin(), gAudioMixedProcessors.end(), processor),
        gAudioMixedProcessors.end());
}

void SetAudioStreamVolume(AudioStream stream, float volume)
{
    if (stream.buffer)
    {
        alSourcef(stream.buffer->m_Source, AL_GAIN, std::max(0.0f, volume));
    }
}

void SetAudioStreamPitch(AudioStream stream, float pitch)
{
    if (stream.buffer)
    {
        alSourcef(stream.buffer->m_Source, AL_PITCH, std::max(0.01f, pitch));
    }
}

void SetAudioStreamPan(AudioStream stream, float pan)
{
    if (stream.buffer)
    {
        alSource3f(stream.buffer->m_Source, AL_POSITION, std::clamp(pan, -1.0f, 1.0f), 0.0f, 0.0f);
    }
}

void PlaySound(Sound sound)
{
    PlayAudioStream(sound.stream);
}

void StopSound(Sound sound)
{
    StopAudioStream(sound.stream);
}

void PauseSound(Sound sound)
{
    PauseAudioStream(sound.stream);
}

void ResumeSound(Sound sound)
{
    ResumeAudioStream(sound.stream);
}

bool IsSoundPlaying(Sound sound)
{
    return IsAudioStreamPlaying(sound.stream);
}

void SetSoundVolume(Sound sound, float volume)
{
    SetAudioStreamVolume(sound.stream, volume);
}

void SetSoundPitch(Sound sound, float pitch)
{
    SetAudioStreamPitch(sound.stream, pitch);
}

void SetSoundPan(Sound sound, float pan)
{
    SetAudioStreamPan(sound.stream, pan);
}

unsigned char* LoadFileData(const char* pFileName, int* pDataSize)
{
    if (pDataSize)
        *pDataSize = 0;
    if (!pFileName)
        return nullptr;
    if (gLoadFileDataCallback)
        return gLoadFileDataCallback(pFileName, pDataSize);
    std::ifstream in(pFileName, std::ios::binary | std::ios::ate);
    if (!in)
        return nullptr;
    const std::streamsize size = in.tellg();
    if (size < 0 || size > std::numeric_limits<int>::max())
        return nullptr;
    in.seekg(0, std::ios::beg);
    unsigned char* pData = static_cast<unsigned char*>(MemAlloc(static_cast<size_t>(size) + 1));
    if (!pData)
        return nullptr;
    if (size > 0 && !in.read(reinterpret_cast<char*>(pData), size))
    {
        MemFree(pData);
        return nullptr;
    }
    pData[size] = 0;
    if (pDataSize)
        *pDataSize = static_cast<int>(size);
    return pData;
}

void UnloadFileData(unsigned char* pData)
{
    if (pData)
        MemFree(pData);
}

bool SaveFileData(const char* pFileName, const void* pData, int dataSize)
{
    if (!pFileName || (!pData && dataSize > 0) || dataSize < 0)
        return false;
    if (gSaveFileDataCallback)
        return gSaveFileDataCallback(pFileName, const_cast<void*>(pData), dataSize);
    std::ofstream out(pFileName, std::ios::binary);
    if (!out)
        return false;
    if (dataSize > 0)
        out.write(static_cast<const char*>(pData), dataSize);
    return out.good();
}

bool ExportDataAsCode(const unsigned char* pData, int dataSize, const char* pFileName)
{
    if (!pData || dataSize < 0 || !pFileName)
        return false;
    std::ofstream out(pFileName);
    if (!out)
        return false;
    out << "static const unsigned char data[" << dataSize << "] = {";
    for (int i = 0; i < dataSize; ++i)
    {
        if ((i % 12) == 0)
            out << "\n    ";
        out << static_cast<unsigned int>(pData[i]);
        if (i + 1 < dataSize)
            out << ", ";
    }
    out << "\n};\n";
    return out.good();
}

char* LoadFileText(const char* pFileName)
{
    if (!pFileName)
        return nullptr;
    if (gLoadFileTextCallback)
        return gLoadFileTextCallback(pFileName);
    int size = 0;
    unsigned char* pData = LoadFileData(pFileName, &size);
    if (!pData)
        return nullptr;
    char* pText = static_cast<char*>(MemAlloc(static_cast<size_t>(size) + 1));
    if (!pText)
    {
        UnloadFileData(pData);
        return nullptr;
    }
    std::memcpy(pText, pData, static_cast<size_t>(size));
    pText[size] = '\0';
    UnloadFileData(pData);
    return pText;
}

void UnloadFileText(char* pText)
{
    if (pText)
        MemFree(pText);
}

bool SaveFileText(const char* pFileName, const char* pText)
{
    if (!pFileName || !pText)
        return false;
    if (gSaveFileTextCallback)
        return gSaveFileTextCallback(pFileName, const_cast<char*>(pText));
    return SaveFileData(pFileName, pText, static_cast<int>(std::strlen(pText)));
}

void SetLoadFileDataCallback(LoadFileDataCallback callback)
{
    gLoadFileDataCallback = callback;
}

void SetSaveFileDataCallback(SaveFileDataCallback callback)
{
    gSaveFileDataCallback = callback;
}

void SetLoadFileTextCallback(LoadFileTextCallback callback)
{
    gLoadFileTextCallback = callback;
}
void SetSaveFileTextCallback(SaveFileTextCallback callback)
{
    gSaveFileTextCallback = callback;
}

AutomationEventList LoadAutomationEventList(const char* pFileName)
{
    AutomationEventList list{};
    int dataSize = 0;
    unsigned char* pData = LoadFileData(pFileName, &dataSize);
    if (!pData || dataSize < static_cast<int>(sizeof(unsigned int) * 2))
    {
        UnloadFileData(pData);
        return list;
    }
    const auto* pWords = reinterpret_cast<const unsigned int*>(pData);
    list.capacity = pWords[0];
    list.count = pWords[1];
    const size_t bytes = static_cast<size_t>(list.count) * sizeof(AutomationEvent);
    if (list.count > 0 && static_cast<size_t>(dataSize) >= sizeof(unsigned int) * 2 + bytes)
    {
        list.events = static_cast<AutomationEvent*>(MemAlloc(bytes));
        if (list.events)
            std::memcpy(list.events, pData + sizeof(unsigned int) * 2, bytes);
    }
    UnloadFileData(pData);
    return list;
}

void UnloadAutomationEventList(AutomationEventList list)
{
    if (list.events)
        MemFree(list.events);
}

bool ExportAutomationEventList(AutomationEventList list, const char* pFileName)
{
    if (!pFileName)
        return false;
    std::ofstream out(pFileName, std::ios::binary);
    if (!out)
        return false;
    WriteLE32(out, list.capacity);
    WriteLE32(out, list.count);
    if (list.events && list.count > 0)
        out.write(reinterpret_cast<const char*>(list.events), static_cast<std::streamsize>(list.count * sizeof(AutomationEvent)));
    return out.good();
}

void SetAutomationEventList(AutomationEventList* pList)
{
    gAutomationEventList = pList;
}

void SetAutomationEventBaseFrame(int frame)
{
    gAutomationBaseFrame = frame;
}

void StartAutomationEventRecording(void)
{
    gAutomationRecording = true;

    if (gAutomationEventList == nullptr)
    {
        gAutomationEventList = &gDefaultAutomationEventList;
        gDefaultAutomationEventList = {};
        gDefaultAutomationEventList.capacity = 16;
        gDefaultAutomationEventList.events = static_cast<AutomationEvent*>(
            MemAlloc(sizeof(AutomationEvent) * gDefaultAutomationEventList.capacity));
    }

    gAutomationBaseFrame = 0;
    gAutomationEventList->count = 0;
}

void StopAutomationEventRecording(void)
{
    gAutomationRecording = false;

    if (gAutomationEventList == &gDefaultAutomationEventList && gAutomationEventList->events != nullptr)
    {
        gAutomationEventList->capacity = gAutomationEventList->count;
    }
}

void PlayAutomationEvent(AutomationEvent event)
{
    if (!gAutomationRecording)
    {
        return;
    }

    if (gAutomationEventList == nullptr)
    {
        gAutomationEventList = &gDefaultAutomationEventList;
        gDefaultAutomationEventList = {};
        gDefaultAutomationEventList.capacity = 16;
        gDefaultAutomationEventList.events = static_cast<AutomationEvent*>(
            MemAlloc(sizeof(AutomationEvent) * gDefaultAutomationEventList.capacity));
    }

    if (gAutomationEventList->events == nullptr)
    {
        gAutomationEventList->capacity = 16;
        gAutomationEventList->events = static_cast<AutomationEvent*>(
            MemAlloc(sizeof(AutomationEvent) * gAutomationEventList->capacity));
    }

    if (gAutomationEventList->count >= gAutomationEventList->capacity)
    {
        const unsigned int newCapacity = gAutomationEventList->capacity == 0 ? 16u : gAutomationEventList->capacity * 2u;
        AutomationEvent* pResized = static_cast<AutomationEvent*>(
            MemRealloc(gAutomationEventList->events, sizeof(AutomationEvent) * newCapacity));

        if (!pResized)
        {
            return;
        }

        gAutomationEventList->events = pResized;
        gAutomationEventList->capacity = newCapacity;
    }

    event.frame = static_cast<unsigned int>(gAutomationBaseFrame + static_cast<int>(gAutomationEventList->count));
    gAutomationEventList->events[gAutomationEventList->count++] = event;
}

void BeginVrStereoMode(VrStereoConfig config)
{
    gVrStereoEnabled = true;
    gCurrentVrStereoConfig = config;

    if (config.projection[0].m[0] == 0.0f &&
        config.projection[1].m[0] == 0.0f &&
        config.viewOffset[0].m[12] == 0.0f &&
        config.viewOffset[1].m[12] == 0.0f)
    {
        gCurrentVrStereoConfig = LoadVrStereoConfig({});
    }
}

void EndVrStereoMode(void)
{
    gVrStereoEnabled = false;
    gCurrentVrStereoConfig = {};
}

VrStereoConfig LoadVrStereoConfig(VrDeviceInfo device)
{
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
    config.viewOffset[1] = Mat4::translation(eyeOffset, 0.0f, 0.0f);

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

    if (screenWidth > 0.0f)
    {
        config.scale[0] = screenWidth / std::max(0.0001f, screenWidth);
        config.scaleIn[0] = 1.0f / std::max(0.0001f, config.scale[0]);
    }
    if (screenHeight > 0.0f)
    {
        config.scale[1] = screenHeight / std::max(0.0001f, screenHeight);
        config.scaleIn[1] = 1.0f / std::max(0.0001f, config.scale[1]);
    }

    return config;
}

void UnloadVrStereoConfig(VrStereoConfig config)
{
    const bool matchesActiveConfig =
        gCurrentVrStereoConfig.projection[0].m[0] == config.projection[0].m[0] &&
        gCurrentVrStereoConfig.projection[1].m[0] == config.projection[1].m[0] &&
        gCurrentVrStereoConfig.viewOffset[0].m[12] == config.viewOffset[0].m[12] &&
        gCurrentVrStereoConfig.viewOffset[1].m[12] == config.viewOffset[1].m[12];

    if (matchesActiveConfig || (config.projection[0].m[0] == 0.0f && config.projection[1].m[0] == 0.0f))
    {
        gCurrentVrStereoConfig = {};
        gVrStereoEnabled = false;
    }
}

SDL_Scancode ToSDLScancode(KeyboardKey key)
{
    const int value = static_cast<int>(key);
    if (value >= KEY_A && value <= KEY_Z)
    {
        return static_cast<SDL_Scancode>(SDL_SCANCODE_A + value - KEY_A);
    }
    if (value >= KEY_ONE && value <= KEY_NINE)
    {
        return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + value - KEY_ONE);
    }
    if (value >= KEY_F1 && value <= KEY_F12)
    {
        return static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + value - KEY_F1);
    }

    switch (key)
    {
    case KEY_NULL:
        return SDL_SCANCODE_UNKNOWN;
    case KEY_APOSTROPHE:
        return SDL_SCANCODE_APOSTROPHE;
    case KEY_COMMA:
        return SDL_SCANCODE_COMMA;
    case KEY_MINUS:
        return SDL_SCANCODE_MINUS;
    case KEY_PERIOD:
        return SDL_SCANCODE_PERIOD;
    case KEY_SLASH:
        return SDL_SCANCODE_SLASH;
    case KEY_ZERO:
        return SDL_SCANCODE_0;
    case KEY_SEMICOLON:
        return SDL_SCANCODE_SEMICOLON;
    case KEY_EQUAL:
        return SDL_SCANCODE_EQUALS;
    case KEY_LEFT_BRACKET:
        return SDL_SCANCODE_LEFTBRACKET;
    case KEY_BACKSLASH:
        return SDL_SCANCODE_BACKSLASH;
    case KEY_RIGHT_BRACKET:
        return SDL_SCANCODE_RIGHTBRACKET;
    case KEY_GRAVE:
        return SDL_SCANCODE_GRAVE;
    case KEY_SPACE:
        return SDL_SCANCODE_SPACE;
    case KEY_ESCAPE:
        return SDL_SCANCODE_ESCAPE;
    case KEY_ENTER:
        return SDL_SCANCODE_RETURN;
    case KEY_TAB:
        return SDL_SCANCODE_TAB;
    case KEY_BACKSPACE:
        return SDL_SCANCODE_BACKSPACE;
    case KEY_INSERT:
        return SDL_SCANCODE_INSERT;
    case KEY_DELETE:
        return SDL_SCANCODE_DELETE;
    case KEY_RIGHT:
        return SDL_SCANCODE_RIGHT;
    case KEY_LEFT:
        return SDL_SCANCODE_LEFT;
    case KEY_DOWN:
        return SDL_SCANCODE_DOWN;
    case KEY_UP:
        return SDL_SCANCODE_UP;
    case KEY_PAGE_UP:
        return SDL_SCANCODE_PAGEUP;
    case KEY_PAGE_DOWN:
        return SDL_SCANCODE_PAGEDOWN;
    case KEY_HOME:
        return SDL_SCANCODE_HOME;
    case KEY_END:
        return SDL_SCANCODE_END;
    case KEY_CAPS_LOCK:
        return SDL_SCANCODE_CAPSLOCK;
    case KEY_SCROLL_LOCK:
        return SDL_SCANCODE_SCROLLLOCK;
    case KEY_NUM_LOCK:
        return SDL_SCANCODE_NUMLOCKCLEAR;
    case KEY_PRINT_SCREEN:
        return SDL_SCANCODE_PRINTSCREEN;
    case KEY_PAUSE:
        return SDL_SCANCODE_PAUSE;
    case KEY_LEFT_SHIFT:
        return SDL_SCANCODE_LSHIFT;
    case KEY_LEFT_CONTROL:
        return SDL_SCANCODE_LCTRL;
    case KEY_LEFT_ALT:
        return SDL_SCANCODE_LALT;
    case KEY_LEFT_SUPER:
        return SDL_SCANCODE_LGUI;
    case KEY_RIGHT_SHIFT:
        return SDL_SCANCODE_RSHIFT;
    case KEY_RIGHT_CONTROL:
        return SDL_SCANCODE_RCTRL;
    case KEY_RIGHT_ALT:
        return SDL_SCANCODE_RALT;
    case KEY_RIGHT_SUPER:
        return SDL_SCANCODE_RGUI;
    case KEY_KB_MENU:
        return SDL_SCANCODE_APPLICATION;
    case KEY_KP_0:
        return SDL_SCANCODE_KP_0;
    case KEY_KP_1:
        return SDL_SCANCODE_KP_1;
    case KEY_KP_2:
        return SDL_SCANCODE_KP_2;
    case KEY_KP_3:
        return SDL_SCANCODE_KP_3;
    case KEY_KP_4:
        return SDL_SCANCODE_KP_4;
    case KEY_KP_5:
        return SDL_SCANCODE_KP_5;
    case KEY_KP_6:
        return SDL_SCANCODE_KP_6;
    case KEY_KP_7:
        return SDL_SCANCODE_KP_7;
    case KEY_KP_8:
        return SDL_SCANCODE_KP_8;
    case KEY_KP_9:
        return SDL_SCANCODE_KP_9;
    case KEY_KP_DECIMAL:
        return SDL_SCANCODE_KP_DECIMAL;
    case KEY_KP_DIVIDE:
        return SDL_SCANCODE_KP_DIVIDE;
    case KEY_KP_MULTIPLY:
        return SDL_SCANCODE_KP_MULTIPLY;
    case KEY_KP_SUBTRACT:
        return SDL_SCANCODE_KP_MINUS;
    case KEY_KP_ADD:
        return SDL_SCANCODE_KP_PLUS;
    case KEY_KP_ENTER:
        return SDL_SCANCODE_KP_ENTER;
    case KEY_KP_EQUAL:
        return SDL_SCANCODE_KP_EQUALS;
    case KEY_BACK:
        return SDL_SCANCODE_AC_BACK;
    case KEY_MENU:
        return SDL_SCANCODE_MENU;
    case KEY_VOLUME_UP:
        return SDL_SCANCODE_VOLUMEUP;
    case KEY_VOLUME_DOWN:
        return SDL_SCANCODE_VOLUMEDOWN;
    default:
        return SDL_SCANCODE_UNKNOWN;
    }
}

bool IsKeyDown(KeyboardKey key)
{
    EnsureInitialized();
    const auto scancode = ToSDLScancode(key);
    if (scancode == SDL_SCANCODE_UNKNOWN)
        return false;
    const auto i = static_cast<std::size_t>(scancode);
    return i < g_Win.m_aCurrentKeys.size() ? g_Win.m_aCurrentKeys[i] : false;
}

bool IsKeyPressed(KeyboardKey key)
{
    EnsureInitialized();
    const auto scancode = ToSDLScancode(key);
    if (scancode == SDL_SCANCODE_UNKNOWN)
        return false;
    const auto i = static_cast<std::size_t>(scancode);
    if (i >= g_Win.m_aCurrentKeys.size())
        return false;
    return g_Win.m_aCurrentKeys[i] && !g_Win.m_aPreviousKeys[i];
}

bool IsKeyReleased(KeyboardKey key)
{
    EnsureInitialized();
    const auto scancode = ToSDLScancode(key);
    if (scancode == SDL_SCANCODE_UNKNOWN)
        return false;
    const auto i = static_cast<std::size_t>(scancode);
    if (i >= g_Win.m_aCurrentKeys.size())
        return false;
    return !g_Win.m_aCurrentKeys[i] && g_Win.m_aPreviousKeys[i];
}

bool IsKeyUp(KeyboardKey key)
{
    return !IsKeyDown(key);
}

int GetKeyPressed()
{
    int k = g_LastKeyPressed;
    g_LastKeyPressed = 0;
    return k;
}

int GetCharPressed()
{
    int c = g_LastCharPressed;
    g_LastCharPressed = 0;
    return c;
}

void SetExitKey(KeyboardKey key)
{
    g_ExitKey = key;
}

bool IsMouseButtonDown(MouseButton button)
{
    EnsureInitialized();
    const auto i = static_cast<std::size_t>(button);
    return i < g_Win.m_aMouseButtons.size() ? g_Win.m_aMouseButtons[i] : false;
}

bool IsMouseButtonPressed(MouseButton button)
{
    EnsureInitialized();
    const auto i = static_cast<std::size_t>(button);
    if (i >= g_Win.m_aMouseButtons.size())
    {
        return false;
    }
    return g_Win.m_aMouseButtons[i] && !g_Win.m_aPreviousMouseButtons[i];
}

bool IsMouseButtonReleased(MouseButton button)
{
    EnsureInitialized();
    const auto i = static_cast<std::size_t>(button);
    if (i >= g_Win.m_aMouseButtons.size())
    {
        return false;
    }
    return !g_Win.m_aMouseButtons[i] && g_Win.m_aPreviousMouseButtons[i];
}

bool IsMouseButtonUp(MouseButton button)
{
    return !IsMouseButtonDown(button);
}

Vec2 GetMousePosition()
{
    EnsureInitialized();
    return g_Win.m_MousePosition;
}

Vec2 GetMouseWheelMoveV()
{
    EnsureInitialized();
    return g_Win.m_MouseWheel;
}

float GetMouseWheelMove()
{
    EnsureInitialized();
    return g_Win.m_MouseWheel.y;
}

Vec2 GetMouseDelta()
{
    if (g_Win.m_pWindow && SDL_GetWindowRelativeMouseMode(g_Win.m_pWindow))
    {
        float deltaX = 0.0f;
        float deltaY = 0.0f;
        SDL_GetRelativeMouseState(&deltaX, &deltaY);
        return Vec2{deltaX * gMouseScale.x, deltaY * gMouseScale.y};
    }

    Vec2 delta{g_Win.m_MousePosition.x - gMousePreviousPosition.x,
               g_Win.m_MousePosition.y - gMousePreviousPosition.y};
    gMousePreviousPosition = g_Win.m_MousePosition;
    return delta;
}

void SetMouseOffset(int offsetX, int offsetY)
{
    gMouseOffset = Vec2{static_cast<float>(offsetX), static_cast<float>(offsetY)};
}

void SetMouseScale(float scaleX, float scaleY)
{
    gMouseScale.x = (scaleX == 0.0f) ? 1.0f : scaleX;
    gMouseScale.y = (scaleY == 0.0f) ? 1.0f : scaleY;
}

void SetMousePosition(int x, int y)
{
    if (g_Win.m_pWindow)
    {
        const float inverseScaleX = (gMouseScale.x == 0.0f) ? 1.0f : gMouseScale.x;
        const float inverseScaleY = (gMouseScale.y == 0.0f) ? 1.0f : gMouseScale.y;
        const float sdlX = (static_cast<float>(x) / inverseScaleX) - gMouseOffset.x;
        const float sdlY = (static_cast<float>(y) / inverseScaleY) - gMouseOffset.y;
        SDL_WarpMouseInWindow(g_Win.m_pWindow, sdlX, sdlY);
        g_Win.m_MousePosition = Vec2{static_cast<float>(x), static_cast<float>(y)};
        gMousePreviousPosition = g_Win.m_MousePosition;
    }
}

void DisableCursor()
{
    if (g_Win.m_pWindow)
    {
        SDL_HideCursor();
        gCursorHidden = true;
    }
}

void EnableCursor()
{
    if (g_Win.m_pWindow)
    {
        SDL_ShowCursor();
        gCursorHidden = false;
    }
}

bool IsCursorHidden()
{
    return gCursorHidden;
}

void ShowCursor()
{
    if (g_Win.m_pWindow)
    {
        SDL_ShowCursor();
        gCursorHidden = false;
    }
}

void HideCursor()
{
    if (g_Win.m_pWindow)
    {
        SDL_HideCursor();
        gCursorHidden = true;
    }
}

bool IsCursorOnScreen()
{
    if (!g_Win.m_pWindow)
    {
        return false;
    }

    float mx = 0.0f, my = 0.0f;
    SDL_GetGlobalMouseState(&mx, &my);
    int wx = 0, wy = 0, ww = 0, wh = 0;
    SDL_GetWindowPosition(g_Win.m_pWindow, &wx, &wy);
    SDL_GetWindowSize(g_Win.m_pWindow, &ww, &wh);
    return (mx >= wx && mx <= (wx + ww) && my >= wy && my <= (wy + wh));
}

void SetMouseCursor(MouseCursor cursor)
{
    if (!g_Win.m_pWindow)
    {
        return;
    }

    SDL_SystemCursor sdl;
    switch (cursor)
    {
    case MOUSE_CURSOR_IBEAM:
        sdl = SDL_SYSTEM_CURSOR_TEXT;
        break;
    case MOUSE_CURSOR_CROSSHAIR:
        sdl = SDL_SYSTEM_CURSOR_CROSSHAIR;
        break;
    case MOUSE_CURSOR_POINTING_HAND:
        sdl = SDL_SYSTEM_CURSOR_POINTER;
        break;
    case MOUSE_CURSOR_RESIZE_EW:
        sdl = SDL_SYSTEM_CURSOR_EW_RESIZE;
        break;
    case MOUSE_CURSOR_RESIZE_NS:
        sdl = SDL_SYSTEM_CURSOR_NS_RESIZE;
        break;
    case MOUSE_CURSOR_RESIZE_NWSE:
        sdl = SDL_SYSTEM_CURSOR_NWSE_RESIZE;
        break;
    case MOUSE_CURSOR_RESIZE_NESW:
        sdl = SDL_SYSTEM_CURSOR_NESW_RESIZE;
        break;
    case MOUSE_CURSOR_RESIZE_ALL:
        sdl = SDL_SYSTEM_CURSOR_MOVE;
        break;
    case MOUSE_CURSOR_NOT_ALLOWED:
        sdl = SDL_SYSTEM_CURSOR_NOT_ALLOWED;
        break;
    case MOUSE_CURSOR_DEFAULT:
    case MOUSE_CURSOR_ARROW:
    default:
        sdl = SDL_SYSTEM_CURSOR_DEFAULT;
        break;
    }

    SDL_Cursor* pC = SDL_CreateSystemCursor(sdl);
    if (pC)
    {
        SDL_SetCursor(pC);
        SDL_DestroyCursor(pC);
    }
}

static SDL_Gamepad* OpenGamepadByIndex(int gamepad)
{
    int count = 0;
    SDL_JoystickID* pIds = SDL_GetGamepads(&count);
    if (!pIds || gamepad < 0 || gamepad >= count)
    {
        SDL_free(pIds);
        return nullptr;
    }

    SDL_Gamepad* pResult = SDL_OpenGamepad(pIds[gamepad]);
    SDL_free(pIds);
    return pResult;
}

static SDL_GamepadButton ToSDLGamepadButton(int button)
{
    switch (button)
    {
    case GAMEPAD_BUTTON_LEFT_FACE_UP:
        return SDL_GAMEPAD_BUTTON_DPAD_UP;
    case GAMEPAD_BUTTON_LEFT_FACE_RIGHT:
        return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    case GAMEPAD_BUTTON_LEFT_FACE_DOWN:
        return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    case GAMEPAD_BUTTON_LEFT_FACE_LEFT:
        return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    case GAMEPAD_BUTTON_RIGHT_FACE_UP:
        return SDL_GAMEPAD_BUTTON_NORTH;
    case GAMEPAD_BUTTON_RIGHT_FACE_RIGHT:
        return SDL_GAMEPAD_BUTTON_EAST;
    case GAMEPAD_BUTTON_RIGHT_FACE_DOWN:
        return SDL_GAMEPAD_BUTTON_SOUTH;
    case GAMEPAD_BUTTON_RIGHT_FACE_LEFT:
        return SDL_GAMEPAD_BUTTON_WEST;
    case GAMEPAD_BUTTON_LEFT_TRIGGER_1:
        return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    case GAMEPAD_BUTTON_RIGHT_TRIGGER_1:
        return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    case GAMEPAD_BUTTON_MIDDLE_LEFT:
        return SDL_GAMEPAD_BUTTON_BACK;
    case GAMEPAD_BUTTON_MIDDLE:
        return SDL_GAMEPAD_BUTTON_GUIDE;
    case GAMEPAD_BUTTON_MIDDLE_RIGHT:
        return SDL_GAMEPAD_BUTTON_START;
    case GAMEPAD_BUTTON_LEFT_THUMB:
        return SDL_GAMEPAD_BUTTON_LEFT_STICK;
    case GAMEPAD_BUTTON_RIGHT_THUMB:
        return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    default:
        return SDL_GAMEPAD_BUTTON_INVALID;
    }
}

int GetGamepadCount()
{
    int count = 0;
    SDL_JoystickID* pIds = SDL_GetGamepads(&count);
    SDL_free(pIds);
    return count;
}

bool IsGamepadAvailable(int gamepad)
{
    SDL_Gamepad* pController = OpenGamepadByIndex(gamepad);
    if (!pController)
        return false;
    SDL_CloseGamepad(pController);
    return true;
}

const char* GetGamepadName(int gamepad)
{
    SDL_Gamepad* pController = OpenGamepadByIndex(gamepad);
    if (!pController)
        return "UNKNOWN";
    const char* pN = SDL_GetGamepadName(pController);
    SDL_CloseGamepad(pController);
    return pN ? pN : "UNKNOWN";
}

float GetGamepadAxisMovement(int gamepad, int axis)
{
    SDL_Gamepad* pController = OpenGamepadByIndex(gamepad);
    if (!pController || axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT)
    {
        if (pController)
            SDL_CloseGamepad(pController);
        return 0.0f;
    }

    float value = static_cast<float>(SDL_GetGamepadAxis(
                      pController, static_cast<SDL_GamepadAxis>(axis))) /
                  32767.0f;
    SDL_CloseGamepad(pController);

    const float deadZone = GetGamepadAxisDeadZone(gamepad, static_cast<GamepadAxis>(axis));
    if (std::fabs(value) <= deadZone)
        return 0.0f;
    const float sign = value < 0.0f ? -1.0f : 1.0f;
    return sign * ((std::fabs(value) - deadZone) / (1.0f - deadZone));
}

bool IsGamepadButtonPressed(int gamepad, int button)
{
    if (gamepad < 0 || gamepad >= static_cast<int>(g_Win.m_aGamepadPressed.size()) ||
        button <= GAMEPAD_BUTTON_UNKNOWN || button >= GAMEPAD_BUTTON_COUNT)
    {
        return false;
    }
    return g_Win.m_aGamepadPressed[static_cast<std::size_t>(gamepad)][static_cast<std::size_t>(button)];
}

bool IsGamepadButtonDown(int gamepad, int button)
{
    if (button <= GAMEPAD_BUTTON_UNKNOWN || button >= GAMEPAD_BUTTON_COUNT)
        return false;
    const SDL_GamepadButton sdlButton = ToSDLGamepadButton(button);
    if (sdlButton == SDL_GAMEPAD_BUTTON_INVALID)
        return false;
    SDL_Gamepad* pController = OpenGamepadByIndex(gamepad);
    if (!pController)
        return false;
    const bool pressed = SDL_GetGamepadButton(pController, sdlButton);
    SDL_CloseGamepad(pController);
    return pressed;
}

bool IsGamepadButtonUp(int gamepad, int button)
{
    return !IsGamepadButtonDown(gamepad, button);
}

bool IsGamepadButtonReleased(int gamepad, int button)
{
    if (gamepad < 0 || gamepad >= static_cast<int>(g_Win.m_aGamepadReleased.size()) ||
        button <= GAMEPAD_BUTTON_UNKNOWN || button >= GAMEPAD_BUTTON_COUNT)
    {
        return false;
    }
    return g_Win.m_aGamepadReleased[static_cast<std::size_t>(gamepad)][static_cast<std::size_t>(button)];
}

int GetGamepadButtonPressed()
{
    const int result = g_Win.m_LastGamepadButtonPressed;
    g_Win.m_LastGamepadButtonPressed = -1;
    return result;
}

int GetGamepadAxisCount(int gamepad)
{
    SDL_Gamepad* pController = OpenGamepadByIndex(gamepad);
    if (!pController)
        return 0;
    SDL_CloseGamepad(pController);
    return SDL_GAMEPAD_AXIS_COUNT;
}

bool SetGamepadAxisDeadZone(int gamepad, GamepadAxis axis, float deadZone)
{
    if (gamepad < 0 || gamepad >= static_cast<int>(gGamepadDeadZones.size()) ||
        axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT ||
        !std::isfinite(deadZone) || deadZone < 0.0f || deadZone >= 1.0f)
    {
        return false;
    }
    gGamepadDeadZones[static_cast<std::size_t>(gamepad)][static_cast<std::size_t>(axis)] = deadZone;
    return true;
}

float GetGamepadAxisDeadZone(int gamepad, GamepadAxis axis)
{
    if (gamepad < 0 || gamepad >= static_cast<int>(gGamepadDeadZones.size()) ||
        axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT)
    {
        return 0.0f;
    }
    return gGamepadDeadZones[static_cast<std::size_t>(gamepad)][static_cast<std::size_t>(axis)];
}

void SetGamepadVibration(int gamepad, float lowFrequency, float highFrequency, float durationSeconds)
{
    SDL_Gamepad* pController = OpenGamepadByIndex(gamepad);
    if (!pController || !std::isfinite(lowFrequency) || !std::isfinite(highFrequency) ||
        !std::isfinite(durationSeconds) || durationSeconds < 0.0f)
    {
        if (pController)
            SDL_CloseGamepad(pController);
        return;
    }
    const auto toRumble = [](float value)
    {
        return static_cast<Uint16>(std::lround(Clamp(value, 0.0f, 1.0f) * 65535.0f));
    };
    const auto durationMs = static_cast<Uint32>(std::lround(durationSeconds * 1000.0f));
    SDL_RumbleGamepad(pController, toRumble(lowFrequency), toRumble(highFrequency), durationMs);
    SDL_CloseGamepad(pController);
}

int SetGamepadMappings(const char* pMappings)
{
    return pMappings ? SDL_AddGamepadMapping(pMappings) : -1;
}

bool AddGamepadMapping(const char* pMapping)
{
    return pMapping && SDL_AddGamepadMapping(pMapping) >= 0;
}

const char* GetGamepadMapping(int gamepad)
{
    SDL_Gamepad* pController = OpenGamepadByIndex(gamepad);
    if (!pController)
        return "";

    char* pMapping = SDL_GetGamepadMapping(pController);
    SDL_CloseGamepad(pController);
    if (!pMapping)
        return "";

    thread_local std::string result;
    result = pMapping;
    SDL_free(pMapping);
    return result.c_str();
}

static Mat4 BuildTransform(const Vec3& position, const Vec3& axis, float angle, const Vec3& scale)
{
    Mat4 translation = Mat4::translation(position.x, position.y, position.z);
    Mat4 scaleMat = Mat4::scale(scale.x, scale.y, scale.z);
    Mat4 rotation = Mat4::identity();
    if ((axis.x != 0.0f || axis.y != 0.0f || axis.z != 0.0f) && angle != 0.0f)
    {
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

static Vec3 TransformPoint(const Mat4& transform, const Vec3& point)
{
    return Vec3{
        transform.m[0] * point.x + transform.m[4] * point.y + transform.m[8] * point.z + transform.m[12],
        transform.m[1] * point.x + transform.m[5] * point.y + transform.m[9] * point.z + transform.m[13],
        transform.m[2] * point.x + transform.m[6] * point.y + transform.m[10] * point.z + transform.m[14]};
}

static void DrawModelWireframe(const Model& model, const Mat4& transform, Color color)
{
    if (!model.meshes)
        return;

    const Mat4 finalTransform = transform * model.transform;

    for (int i = 0; i < model.meshCount; ++i)
    {
        const Mesh& mesh = model.meshes[i];
        if (!mesh.vertices)
            continue;

        const bool hasIndices = mesh.indices != nullptr;
        const int triangleCount = mesh.triangleCount;

        for (int t = 0; t < triangleCount; ++t)
        {
            int idx0 = hasIndices ? mesh.indices[t * 3 + 0] : t * 3 + 0;
            int idx1 = hasIndices ? mesh.indices[t * 3 + 1] : t * 3 + 1;
            int idx2 = hasIndices ? mesh.indices[t * 3 + 2] : t * 3 + 2;

            Vec3 v0 = TransformPoint(finalTransform, Vec3{
                                                         mesh.vertices[idx0 * 3 + 0],
                                                         mesh.vertices[idx0 * 3 + 1],
                                                         mesh.vertices[idx0 * 3 + 2]});
            Vec3 v1 = TransformPoint(finalTransform, Vec3{
                                                         mesh.vertices[idx1 * 3 + 0],
                                                         mesh.vertices[idx1 * 3 + 1],
                                                         mesh.vertices[idx1 * 3 + 2]});
            Vec3 v2 = TransformPoint(finalTransform, Vec3{
                                                         mesh.vertices[idx2 * 3 + 0],
                                                         mesh.vertices[idx2 * 3 + 1],
                                                         mesh.vertices[idx2 * 3 + 2]});

            gRenderer.DrawLine3D(v0, v1, color);
            gRenderer.DrawLine3D(v1, v2, color);
            gRenderer.DrawLine3D(v2, v0, color);
        }
    }
}

void BeginDrawing()
{
    EnsureInitialized();
    gPreviousDebugRenderStats = gCurrentDebugRenderStats;
    gCurrentDebugRenderStats = {};
    gRenderer.BeginDrawing();
}

void EnableDebugInterface(bool enabled)
{
    gDebugInterfaceEnabled = enabled;
}

void EndDrawing()
{
    EnsureInitialized();
    if (gDebugInterfaceEnabled)
    {
        const RendererType backend = GetCurrentBackend();
        const int lineCount = 3;
        const int fontSize = 20;
        const int lineHeight = 24;
        const int firstY = std::max(0, GetScreenHeight() - lineCount * lineHeight - 12);
        char line[128];
        const char* pPrefix = backend == RendererType::OpenGL ? "OGL"
                             : backend == RendererType::Vulkan
#if defined(__APPLE__)
                                 ? "MVK"
#else
                                 ? "VK"
#endif
                                 : backend == RendererType::D3D11 ? "D3D11" : "Renderer";

        std::snprintf(line, sizeof(line), "%s Verts: %llu", pPrefix,
                      static_cast<unsigned long long>(gPreviousDebugRenderStats.m_Vertices));
        DrawDebugText(line, 10, firstY, fontSize, WHITE);

        std::snprintf(line, sizeof(line), "%s Calls: %llu", pPrefix,
                      static_cast<unsigned long long>(gPreviousDebugRenderStats.m_Calls));
        DrawDebugText(line, 10, firstY + lineHeight, fontSize, WHITE);

        std::snprintf(line, sizeof(line), "FPS: %d / DT: %.2f ms",
                      GetFPS(), static_cast<double>(GetFrameTime()) * 1000.0);
        DrawDebugText(line, 10, firstY + 2 * lineHeight, fontSize, WHITE);
    }
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
    const Rectangle rect = Rectangle{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)};
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

    const Vec2 dir = Vec2{endPos.x - startPos.x, endPos.y - startPos.y};
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);

    if (len <= EPSILON)
    {
        DrawCircle(startPos.x, startPos.y, thick * 0.5f, color);
        return;
    }

    const Vec2 n = Vec2{
        -dir.y / len * thick * 0.5f,
        dir.x / len * thick * 0.5f};

    DrawTriangle(
        Vec2{startPos.x + n.x, startPos.y + n.y},
        Vec2{startPos.x - n.x, startPos.y - n.y},
        Vec2{endPos.x + n.x, endPos.y + n.y},
        color);
    DrawTriangle(
        Vec2{endPos.x + n.x, endPos.y + n.y},
        Vec2{startPos.x - n.x, startPos.y - n.y},
        Vec2{endPos.x - n.x, endPos.y - n.y},
        color);
}

void DrawLineStrip(const Vec2* pPoints, int pointCount, Color color)
{
    if (!pPoints || pointCount < 2)
    {
        return;
    }

    for (int i = 0; i + 1 < pointCount; ++i)
    {
        DrawLineV(pPoints[i], pPoints[i + 1], color);
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
            std::min(startPos.y, endPos.y) - std::fabs(endPos.x - startPos.x) * 0.25f};
        const Vec2 cur = Vec2{
            u * u * startPos.x + 2.0f * u * t * control.x + t * t * endPos.x,
            u * u * startPos.y + 2.0f * u * t * control.y + t * t * endPos.y};

        DrawLineEx(last, cur, thick, color);
        last = cur;
    }
}

void DrawLineDashed(Vec2 startPos, Vec2 endPos, int dashSize, int spaceSize, Color color)
{
    Vec2 dir = Vec2{endPos.x - startPos.x, endPos.y - startPos.y};
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
        const Vec2 a = Vec2{startPos.x + dir.x * d, startPos.y + dir.y * d};
        const Vec2 b = Vec2{startPos.x + dir.x * e, startPos.y + dir.y * e};
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
        Vec2{-origin.x, -origin.y},
        Vec2{rec.width - origin.x, -origin.y},
        Vec2{rec.width - origin.x, rec.height - origin.y},
        Vec2{-origin.x, rec.height - origin.y}};

    for (auto& p : corners)
    {
        p = Vec2{
            rec.x + origin.x + p.x * cs - p.y * sn,
            rec.y + origin.y + p.x * sn + p.y * cs};
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
                color);
        }
    }
}

void DrawRectangleLinesEx(Rectangle rec, float thick, Color color)
{
    DrawLineEx(Vec2{rec.x, rec.y}, Vec2{rec.x + rec.width, rec.y}, thick, color);
    DrawLineEx(Vec2{rec.x + rec.width, rec.y}, Vec2{rec.x + rec.width, rec.y + rec.height}, thick, color);
    DrawLineEx(Vec2{rec.x + rec.width, rec.y + rec.height}, Vec2{rec.x, rec.y + rec.height}, thick, color);
    DrawLineEx(Vec2{rec.x, rec.y + rec.height}, Vec2{rec.x, rec.y}, thick, color);
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

    DrawLineEx(Vec2{rec.x + radius, rec.y}, Vec2{rec.x + rec.width - radius, rec.y}, thick, color);
    DrawLineEx(Vec2{rec.x + rec.width, rec.y + radius}, Vec2{rec.x + rec.width, rec.y + rec.height - radius}, thick, color);
    DrawLineEx(Vec2{rec.x + rec.width - radius, rec.y + rec.height}, Vec2{rec.x + radius, rec.y + rec.height}, thick, color);
    DrawLineEx(Vec2{rec.x, rec.y + rec.height - radius}, Vec2{rec.x, rec.y + radius}, thick, color);

    DrawCircleSectorLinesEx(Vec2{rec.x + radius, rec.y + radius}, radius, 180.0f, 270.0f, safeSegments, thick, color);
    DrawCircleSectorLinesEx(Vec2{rec.x + rec.width - radius, rec.y + radius}, radius, 270.0f, 360.0f, safeSegments, thick, color);
    DrawCircleSectorLinesEx(Vec2{rec.x + rec.width - radius, rec.y + rec.height - radius}, radius, 0.0f, 90.0f, safeSegments, thick, color);
    DrawCircleSectorLinesEx(Vec2{rec.x + radius, rec.y + rec.height - radius}, radius, 90.0f, 180.0f, safeSegments, thick, color);
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
        center.y + std::sin(startAngle * DEG2RAD) * radius};

    for (int i = 1; i <= segments; ++i)
    {
        const float a = (startAngle + (endAngle - startAngle) * static_cast<float>(i) / segments) * DEG2RAD;
        const Vec2 cur = Vec2{
            center.x + std::cos(a) * radius,
            center.y + std::sin(a) * radius};

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
        center.y + std::sin(startAngle * DEG2RAD) * radius};
    Vec2 last = first;

    DrawLineEx(center, first, thick, color);

    for (int i = 1; i <= segments; ++i)
    {
        const float a = (startAngle + (endAngle - startAngle) * static_cast<float>(i) / segments) * DEG2RAD;
        const Vec2 cur = Vec2{
            center.x + std::cos(a) * radius,
            center.y + std::sin(a) * radius};

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
    Vec2 last = Vec2{static_cast<float>(centerX) + radiusH, static_cast<float>(centerY)};

    for (int i = 1; i <= 64; ++i)
    {
        const float a = 2.0f * PI * static_cast<float>(i) / 64.0f;
        const Vec2 cur = Vec2{
            static_cast<float>(centerX) + std::cos(a) * radiusH,
            static_cast<float>(centerY) + std::sin(a) * radiusV};

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
            center.y + std::sin(a) * radius};

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

void DrawTriangleFan(const Vec2* pPoints, int pointCount, Color color)
{
    if (!pPoints || pointCount < 3)
    {
        return;
    }

    for (int i = 1; i + 1 < pointCount; ++i)
    {
        DrawTriangle(pPoints[0], pPoints[i], pPoints[i + 1], color);
    }
}

void DrawTriangleStrip(const Vec2* pPoints, int pointCount, Color color)
{
    if (!pPoints || pointCount < 3)
    {
        return;
    }

    for (int i = 0; i + 2 < pointCount; ++i)
    {
        if (i & 1)
        {
            DrawTriangle(pPoints[i + 1], pPoints[i], pPoints[i + 2], color);
        }
        else
        {
            DrawTriangle(pPoints[i], pPoints[i + 1], pPoints[i + 2], color);
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
    for (int i = 1; i <= 32; ++i)
    {
        const float t = static_cast<float>(i) / 32.0f;
        const Vec2 p = GetSplinePointBezierQuadratic(p1, c2, p3, t);
        DrawLineEx(last, p, thick, color);
        last = p;
    }
}

void DrawSplineSegmentBezierCubic(Vec2 p1, Vec2 c2, Vec2 c3, Vec2 p4, float thick, Color color)
{
    Vec2 last = p1;
    for (int i = 1; i <= 32; ++i)
    {
        const float t = static_cast<float>(i) / 32.0f;
        const Vec2 p = GetSplinePointBezierCubic(p1, c2, c3, p4, t);
        DrawLineEx(last, p, thick, color);
        last = p;
    }
}

void DrawSplineSegmentCatmullRom(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, float thick, Color color)
{
    Vec2 last = p2;
    for (int i = 1; i <= 32; ++i)
    {
        const float t = static_cast<float>(i) / 32.0f;
        const Vec2 p = GetSplinePointCatmullRom(p1, p2, p3, p4, t);
        DrawLineEx(last, p, thick, color);
        last = p;
    }
}

void DrawSplineSegmentBasis(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, float thick, Color color)
{
    Vec2 last = GetSplinePointBasis(p1, p2, p3, p4, 0.0f);
    for (int i = 1; i <= 32; ++i)
    {
        const float t = static_cast<float>(i) / 32.0f;
        const Vec2 p = GetSplinePointBasis(p1, p2, p3, p4, t);
        DrawLineEx(last, p, thick, color);
        last = p;
    }
}

void DrawSplineLinear(const Vec2* pPoints, int pointCount, float thick, Color color)
{
    if (!pPoints)
    {
        return;
    }

    for (int i = 0; i + 1 < pointCount; ++i)
    {
        DrawSplineSegmentLinear(pPoints[i], pPoints[i + 1], thick, color);
    }
}

void DrawSplineCatmullRom(const Vec2* pPoints, int pointCount, float thick, Color color)
{
    if (!pPoints)
    {
        return;
    }

    for (int i = 0; i + 3 < pointCount; ++i)
    {
        DrawSplineSegmentCatmullRom(pPoints[i], pPoints[i + 1], pPoints[i + 2], pPoints[i + 3], thick, color);
    }
}

void DrawSplineBasis(const Vec2* pPoints, int pointCount, float thick, Color color)
{
    if (!pPoints)
    {
        return;
    }

    for (int i = 0; i + 3 < pointCount; ++i)
    {
        DrawSplineSegmentBasis(pPoints[i], pPoints[i + 1], pPoints[i + 2], pPoints[i + 3], thick, color);
    }
}

void DrawSplineBezierQuadratic(const Vec2* pPoints, int pointCount, float thick, Color color)
{
    if (!pPoints)
    {
        return;
    }

    for (int i = 0; i + 2 < pointCount; i += 2)
    {
        DrawSplineSegmentBezierQuadratic(pPoints[i], pPoints[i + 1], pPoints[i + 2], thick, color);
    }
}

void DrawSplineBezierCubic(const Vec2* pPoints, int pointCount, float thick, Color color)
{
    if (!pPoints)
    {
        return;
    }

    for (int i = 0; i + 3 < pointCount; i += 3)
    {
        DrawSplineSegmentBezierCubic(pPoints[i], pPoints[i + 1], pPoints[i + 2], pPoints[i + 3], thick, color);
    }
}

Texture2D LoadTexture(const char* pFilePath)
{
    EnsureInitialized();
    ITexture it = gRenderer.LoadTexture(pFilePath);
    Texture2D t;
    t.id = it.m_Id;
    t.width = it.m_Width;
    t.height = it.m_Height;
    t.mipmaps = it.m_Mipmaps;
    t.format = it.m_Format;
    t.valid = it.m_Valid;
    return t;
}

Texture2D LoadTextureFromImage(Image image)
{
    EnsureInitialized();
    ITexture it = gRenderer.LoadTextureFromImage(image);
    return Texture2D{it.m_Id, it.m_Width, it.m_Height, it.m_Mipmaps, it.m_Format, it.m_Valid};
}

static bool PrepareCubemapFaces(Image image, CubemapLayout requestedLayout,
                                std::vector<unsigned char>& vFaces, int& faceSize)
{
    if (!IsImageValid(image))
    {
        TraceLog(LogLevel::Error, "TEXTURE", "LoadTextureCubemap: invalid source image");
        return false;
    }

    CubemapLayout layout = requestedLayout;
    if (layout == CUBEMAP_LAYOUT_AUTO_DETECT)
    {
        if (static_cast<int64_t>(image.width) == static_cast<int64_t>(image.height) * 6)
        {
            layout = CUBEMAP_LAYOUT_LINE_HORIZONTAL;
        }
        else if (static_cast<int64_t>(image.height) == static_cast<int64_t>(image.width) * 6)
        {
            layout = CUBEMAP_LAYOUT_LINE_VERTICAL;
        }
        else if (image.width % 3 == 0 && image.height % 4 == 0 &&
                 image.width / 3 == image.height / 4)
        {
            layout = CUBEMAP_LAYOUT_CROSS_THREE_BY_FOUR;
        }
        else if (image.width % 4 == 0 && image.height % 3 == 0 &&
                 image.width / 4 == image.height / 3)
        {
            layout = CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE;
        }
        else
        {
            TraceLog(LogLevel::Error, "TEXTURE", "LoadTextureCubemap: cannot detect cubemap layout from image dimensions");
            return false;
        }
    }

    int columns = 0;
    int rows = 0;
    switch (layout)
    {
    case CUBEMAP_LAYOUT_LINE_VERTICAL:
        columns = 1;
        rows = 6;
        break;
    case CUBEMAP_LAYOUT_LINE_HORIZONTAL:
        columns = 6;
        rows = 1;
        break;
    case CUBEMAP_LAYOUT_CROSS_THREE_BY_FOUR:
        columns = 3;
        rows = 4;
        break;
    case CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE:
        columns = 4;
        rows = 3;
        break;
    default:
        TraceLog(LogLevel::Error, "TEXTURE", TextFormat("LoadTextureCubemap: invalid layout value %d", static_cast<int>(requestedLayout)));
        return false;
    }

    if (image.width % columns != 0 || image.height % rows != 0 ||
        image.width / columns != image.height / rows)
    {
        TraceLog(LogLevel::Error, "TEXTURE", TextFormat("LoadTextureCubemap: image dimensions %dx%d do not match layout %d", image.width, image.height, static_cast<int>(layout)));
        return false;
    }

    faceSize = image.width / columns;
    const size_t facePixels = static_cast<size_t>(faceSize) * static_cast<size_t>(faceSize);
    if (facePixels > std::numeric_limits<size_t>::max() / 24)
    {
        TraceLog(LogLevel::Error, "TEXTURE", "LoadTextureCubemap: image dimensions are too large");
        return false;
    }

    Image rgbaImage = ImageCopy(image);
    if (!rgbaImage.data)
    {
        TraceLog(LogLevel::Error, "TEXTURE", "LoadTextureCubemap: failed to copy source image");
        return false;
    }
    ImageFormat(&rgbaImage, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    if (rgbaImage.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
    {
        UnloadImage(rgbaImage);
        TraceLog(LogLevel::Error, "TEXTURE", "LoadTextureCubemap: source image cannot be converted to RGBA8");
        return false;
    }

    std::array<std::array<int, 2>, 6> faceOrigins{};
    if (layout == CUBEMAP_LAYOUT_LINE_HORIZONTAL)
    {
        for (int face = 0; face < 6; ++face)
            faceOrigins[face] = {face, 0};
    }
    else if (layout == CUBEMAP_LAYOUT_LINE_VERTICAL)
    {
        for (int face = 0; face < 6; ++face)
            faceOrigins[face] = {0, face};
    }
    else if (layout == CUBEMAP_LAYOUT_CROSS_THREE_BY_FOUR)
    {
        faceOrigins = {{{1, 1}, {1, 3}, {1, 0}, {1, 2}, {0, 1}, {2, 1}}};
    }
    else
    {
        faceOrigins = {{{2, 1}, {0, 1}, {1, 0}, {1, 2}, {1, 1}, {3, 1}}};
    }

    vFaces.resize(facePixels * 24);
    const auto* pSource = static_cast<const unsigned char*>(rgbaImage.data);
    const size_t rowBytes = static_cast<size_t>(rgbaImage.width) * 4;
    const size_t faceBytes = facePixels * 4;
    for (size_t face = 0; face < faceOrigins.size(); ++face)
    {
        const size_t sourceX = static_cast<size_t>(faceOrigins[face][0]) * faceSize;
        const size_t sourceY = static_cast<size_t>(faceOrigins[face][1]) * faceSize;
        for (int y = 0; y < faceSize; ++y)
        {
            const unsigned char* pSourceRow =
                pSource + (sourceY + static_cast<size_t>(y)) * rowBytes + sourceX * 4;
            unsigned char* pDestinationRow =
                vFaces.data() + face * faceBytes + static_cast<size_t>(y) * faceSize * 4;
            std::memcpy(pDestinationRow, pSourceRow, static_cast<size_t>(faceSize) * 4);
        }
    }
    UnloadImage(rgbaImage);
    return true;
}

TextureCubemap LoadTextureCubemap(Image image, CubemapLayout layout)
{
    std::vector<unsigned char> vFaces;
    int faceSize = 0;
    if (!PrepareCubemapFaces(image, layout, vFaces, faceSize))
    {
        return {};
    }

    EnsureInitialized();
    const ITexture texture = gRenderer.LoadTextureCubemap(vFaces.data(), faceSize);
    return TextureCubemap{texture.m_Id, texture.m_Width, texture.m_Height, texture.m_Mipmaps,
                          texture.m_Format, texture.m_Valid};
}

void UpdateTexture(Texture2D texture, const void* pPixels)
{
    if (!pPixels || texture.id == 0)
        return;
    ITexture it{texture.id, texture.width, texture.height, texture.mipmaps, texture.format, texture.valid};
    gRenderer.UpdateTexture(it, pPixels);
}

void UpdateTextureRec(Texture2D texture, Rectangle rec, const void* pPixels)
{
    if (!pPixels || texture.id == 0)
        return;
    ITexture it{texture.id, texture.width, texture.height, texture.mipmaps, texture.format, texture.valid};
    gRenderer.UpdateTextureRegion(it, rec, pPixels);
}

void GenTextureMipmaps(Texture2D* pTexture)
{
    if (!pTexture || pTexture->id == 0)
        return;
#if defined(QC_ENABLE_OPENGL)
    if (g_pRenderer && g_pRenderer->GetType() == RendererType::OpenGL)
    {
        glBindTexture(GL_TEXTURE_2D, pTexture->id);
        glGenerateMipmap(GL_TEXTURE_2D);
        pTexture->mipmaps = 1 + static_cast<int>(std::floor(std::log2(static_cast<float>(std::max(pTexture->width, pTexture->height)))));
    }
#endif
}

void UnloadTexture(Texture2D texture)
{
    ITexture it{texture.id, texture.width, texture.height, texture.mipmaps, texture.format, texture.valid};
    gRenderer.UnloadTexture(it);
}

bool IsTextureValid(Texture2D texture)
{
    ITexture it{texture.id, texture.width, texture.height, texture.mipmaps, texture.format, texture.valid};
    return gRenderer.isTextureValid(it);
}

bool IsTextureReady(Texture2D texture)
{
    return IsTextureValid(texture);
}

void DrawTexture(const Texture2D& tex, float x, float y, Color tint)
{
    ITexture it{tex.id, tex.width, tex.height, tex.mipmaps, tex.format, tex.valid};
    gRenderer.DrawTexture(it, x, y, tint);
}

void DrawTextureV(Texture2D tex, Vec2 pos, Color tint)
{
    ITexture it{tex.id, tex.width, tex.height, tex.mipmaps, tex.format, tex.valid};
    gRenderer.DrawTextureV(it, pos, tint);
}

void DrawTextureEx(Texture2D tex, Vec2 pos, float rot, float scale, Color tint)
{
    ITexture it{tex.id, tex.width, tex.height, tex.mipmaps, tex.format, tex.valid};
    gRenderer.DrawTextureEx(it, pos, rot, scale, tint);
}

void DrawTextureRec(Texture2D tex, Rectangle source, Vec2 pos, Color tint)
{
    ITexture it{tex.id, tex.width, tex.height, tex.mipmaps, tex.format, tex.valid};
    gRenderer.DrawTextureRec(it, source, pos, tint);
}

void DrawTexturePro(Texture2D tex, Rectangle src, Rectangle dst, Vec2 origin, float rot, Color tint)
{
    ITexture it{tex.id, tex.width, tex.height, tex.mipmaps, tex.format, tex.valid};
    gRenderer.DrawTexturePro(it, src, dst, origin, rot, tint);
}

void DrawTextureTiled(Texture2D tex, float scale, Vec2 offset, Color tint)
{
    ITexture it{tex.id, tex.width, tex.height, tex.mipmaps, tex.format, tex.valid};
    gRenderer.DrawTextureTiled(it, scale, offset, tint);
}

void DrawTextureNPatch(Texture2D tex, NPatchInfo nPatchInfo, Rectangle dst, Vec2 origin, float rot, Color tint)
{
    ITexture it{tex.id, tex.width, tex.height, tex.mipmaps, tex.format, tex.valid};
    gRenderer.DrawTextureNPatch(it, nPatchInfo, dst, origin, rot, tint);
}

Texture2D GenCheckerTexture(int w, int h, int cellSize, Color a, Color b)
{
    EnsureInitialized();
    ITexture it = gRenderer.GenCheckerTexture(w, h, cellSize, a, b);
    return Texture2D{it.m_Id, it.m_Width, it.m_Height, it.m_Mipmaps, it.m_Format, it.m_Valid};
}

RenderTexture2D LoadRenderTexture(int w, int h)
{
    EnsureInitialized();
    IRenderTexture ir = gRenderer.LoadRenderTexture(w, h);
    RenderTexture2D rt;
    rt.id = ir.m_Id;
    rt.depthId = ir.m_DepthId;
    rt.texture = Texture2D{ir.m_Texture.m_Id, ir.m_Texture.m_Width, ir.m_Texture.m_Height, ir.m_Texture.m_Mipmaps, ir.m_Texture.m_Format, ir.m_Texture.m_Valid};
    return rt;
}

void UnloadRenderTexture(RenderTexture2D target)
{
    IRenderTexture ir;
    ir.m_Id = target.id;
    ir.m_DepthId = target.depthId;
    ir.m_Texture = ITexture{target.texture.id, target.texture.width, target.texture.height, target.texture.mipmaps, target.texture.format, target.texture.valid};
    gRenderer.UnloadRenderTexture(ir);
}

bool IsRenderTextureValid(RenderTexture2D target)
{
    IRenderTexture ir;
    ir.m_Id = target.id;
    ir.m_DepthId = target.depthId;
    ir.m_Texture = ITexture{target.texture.id, target.texture.width, target.texture.height, target.texture.mipmaps, target.texture.format, target.texture.valid};
    return gRenderer.isRenderTextureValid(ir);
}

Texture2D GetRenderTextureTexture(RenderTexture2D target)
{
    IRenderTexture ir;
    ir.m_Id = target.id;
    ir.m_DepthId = target.depthId;
    ir.m_Texture = ITexture{target.texture.id, target.texture.width, target.texture.height, target.texture.mipmaps, target.texture.format, target.texture.valid};
    ITexture it = gRenderer.GetRenderTextureTexture(ir);
    return Texture2D{it.m_Id, it.m_Width, it.m_Height, it.m_Mipmaps, it.m_Format, it.m_Valid};
}

Image LoadImageFromTexture(Texture2D texture)
{
    EnsureInitialized();
    if (!IsTextureValid(texture))
        return Image{};
    ITexture it{texture.id, texture.width, texture.height, texture.mipmaps, texture.format, texture.valid};
    Image image = gRenderer.ReadTextureImage(it);
    if (!IsImageValid(image))
    {
        TraceLog(LogLevel::Error, "IMAGE", "LoadImageFromTexture: failed to read texture pixels back to CPU");
    }
    return image;
}

Image LoadImageFromScreen(void)
{
    EnsureInitialized();
    Image image = gRenderer.ReadScreenImage();
    if (!IsImageValid(image))
    {
        TraceLog(LogLevel::Error, "IMAGE", "LoadImageFromScreen: failed to read backbuffer pixels to CPU");
    }
    return image;
}

void BeginTextureMode(RenderTexture2D target)
{
    EnsureInitialized();
    IRenderTexture ir;
    ir.m_Id = target.id;
    ir.m_DepthId = target.depthId;
    ir.m_Texture = ITexture{target.texture.id, target.texture.width, target.texture.height, target.texture.mipmaps, target.texture.format, target.texture.valid};
    gRenderer.BeginTextureMode(ir);
}

void EndTextureMode()
{
    EnsureInitialized();
    gRenderer.EndTextureMode();
}

Font LoadFont(const char* pFileName)
{
    EnsureInitialized();
    const int defaultFontSize = 32;
    qci::IFont iFont = gRenderer.LoadFont(pFileName, defaultFontSize, nullptr, 0);
    Font f;
    gRenderer.FillFont(iFont, f);
    return f;
}

void UnloadFont(Font font)
{
    if (font._rendererFontId != 0)
    {
        qci::IFont iFont{font._rendererFontId};
        gRenderer.UnloadFont(iFont);
    }
    delete[] font.glyphs;
    delete[] font.recs;
    font.glyphs = nullptr;
    font.recs = nullptr;
}

Font GetDefaultFont()
{
    EnsureInitialized();
    qci::IFont iFont = gRenderer.LoadFont(nullptr, 32, nullptr, 0);
    Font f;
    gRenderer.FillFont(iFont, f);
    return f;
}

static std::vector<int> DefaultCodepointsAPI()
{
    std::vector<int> vCps;
    vCps.reserve(95);
    for (int c = 32; c <= 126; ++c)
        vCps.push_back(c);
    return vCps;
}

Font LoadFontEx(const char* pFileName, int fontSize, const int* pCodepoints, int codepointCount)
{
    EnsureInitialized();
    qci::IFont iFont = gRenderer.LoadFont(pFileName, fontSize, pCodepoints, codepointCount);
    Font f;
    gRenderer.FillFont(iFont, f);
    return f;
}

Font LoadFontFromMemory(const char* pFileType, const unsigned char* pFileData, int dataSize,
                        int fontSize, const int* pCodepoints, int codepointCount)
{
    EnsureInitialized();
    qci::IFont iFont = gRenderer.LoadFontFromMemory(pFileType, pFileData, dataSize, fontSize, pCodepoints, codepointCount);
    Font f;
    gRenderer.FillFont(iFont, f);
    return f;
}

bool IsFontValid(Font font)
{
    return font.valid && font.texture.id != 0 && font.glyphs != nullptr && font.recs != nullptr;
}

GlyphInfo* LoadFontData(const unsigned char* pFileData, int dataSize, int fontSize,
                        const int* pCodepoints, int codepointCount, int type, int* pGlyphCount)
{
    if (pGlyphCount)
        *pGlyphCount = 0;
    const bool useUnicodeLayout = (type == 0 || type == 1 || type == 2);
    if (!useUnicodeLayout)
    {
        TraceLog(LogLevel::Warn, "FONT",
                 TextFormat("LoadFontData: unsupported font-load type %d, using unicode fallback", type));
    }
    if (!pFileData || dataSize <= 0 || fontSize <= 0)
        return nullptr;

    std::vector<int> vCps;
    const int* pCpsPtr = pCodepoints;
    int cpsCount = codepointCount;
    if (pCpsPtr == nullptr || cpsCount <= 0)
    {
        vCps = DefaultCodepointsAPI();
        pCpsPtr = vCps.data();
        cpsCount = static_cast<int>(vCps.size());
    }

    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0)
        return nullptr;

    FT_Face face = nullptr;
    if (FT_New_Memory_Face(library, pFileData, static_cast<FT_Long>(dataSize), 0, &face) != 0)
    {
        FT_Done_FreeType(library);
        return nullptr;
    }
    FT_Select_Charmap(face, FT_ENCODING_UNICODE);
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(fontSize));

    GlyphInfo* pGlyphs = static_cast<GlyphInfo*>(MemAlloc(sizeof(GlyphInfo) * static_cast<size_t>(cpsCount)));
    if (!pGlyphs)
    {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return nullptr;
    }

    int n = 0;
    for (int i = 0; i < cpsCount; ++i)
    {
        const int cp = pCpsPtr[i];
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0)
            continue;

        FT_GlyphSlot slot = face->glyph;
        const int gw = static_cast<int>(slot->bitmap.width);
        const int gh = static_cast<int>(slot->bitmap.rows);

        GlyphInfo& g = pGlyphs[n];
        g.value = cp;
        g.offsetX = slot->bitmap_left;
        g.offsetY = slot->bitmap_top;
        g.advanceX = static_cast<int>(slot->advance.x / 64.0f);

        if (gw > 0 && gh > 0 && slot->bitmap.buffer != nullptr)
        {
            unsigned char* pGdata = static_cast<unsigned char*>(MemAlloc(static_cast<size_t>(gw) * gh * 4));
            if (pGdata)
            {
                for (int y = 0; y < gh; ++y)
                {
                    for (int x = 0; x < gw; ++x)
                    {
                        const unsigned char a = slot->bitmap.buffer[y * slot->bitmap.pitch + x];
                        size_t o = (static_cast<size_t>(y) * gw + x) * 4;
                        pGdata[o + 0] = 255;
                        pGdata[o + 1] = 255;
                        pGdata[o + 2] = 255;
                        pGdata[o + 3] = a;
                    }
                }
                g.image = Image{pGdata, gw, gh, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            }
        }
        ++n;
    }

    FT_Done_Face(face);
    FT_Done_FreeType(library);

    if (n == 0)
    {
        MemFree(pGlyphs);
        return nullptr;
    }
    if (pGlyphCount)
        *pGlyphCount = n;
    return pGlyphs;
}

void UnloadFontData(GlyphInfo* pGlyphs, int glyphCount)
{
    if (!pGlyphs)
        return;
    for (int i = 0; i < glyphCount; ++i)
        MemFree(pGlyphs[i].image.data);
    MemFree(pGlyphs);
}

Image GenImageFontAtlas(const GlyphInfo* pGlyphs, Rectangle** ppGlyphRecs, int glyphCount,
                        int fontSize, int padding, int packMethod)
{
    if (ppGlyphRecs)
        *ppGlyphRecs = nullptr;
    const int effectiveFontSize = std::max(1, fontSize);
    const int effectivePadding = std::max(1, padding);
    const int effectivePackMethod = packMethod;
    if (effectivePackMethod != 0 && effectivePackMethod != 1)
    {
        TraceLog(LogLevel::Warn, "FONT",
                 TextFormat("GenImageFontAtlas: unsupported pack method %d, using default packing", effectivePackMethod));
    }
    if (!pGlyphs || glyphCount <= 0)
        return Image{};

    if (ppGlyphRecs)
        *ppGlyphRecs = new Rectangle[glyphCount]();

    int maxW = 1, maxH = 1;
    size_t totalArea = 0;
    for (int i = 0; i < glyphCount; ++i)
    {
        if (pGlyphs[i].image.data && pGlyphs[i].image.width > 0 && pGlyphs[i].image.height > 0)
        {
            const int w = pGlyphs[i].image.width, h = pGlyphs[i].image.height;
            maxW = std::max(maxW, w);
            maxH = std::max(maxH, h);
            totalArea += static_cast<size_t>(w + padding) * (h + padding);
        }
    }

    int atlasW = std::max(maxW + effectivePadding * 2, std::max(16, effectiveFontSize * 2));
    int atlasH = std::max(maxH + effectivePadding * 2, std::max(16, effectiveFontSize * 2));

    auto resize = [](std::vector<uint8_t>& vPx, int oldW, int newW, int oldH, int newH)
    {
        std::vector<uint8_t> np(static_cast<size_t>(newW) * newH * 4, 0);
        const int cw = std::min(oldW, newW), ch = std::min(oldH, newH);
        for (int y = 0; y < ch; ++y)
            std::memcpy(&np[static_cast<size_t>(y) * newW * 4],
                        &vPx[static_cast<size_t>(y) * oldW * 4],
                        static_cast<size_t>(cw) * 4);
        vPx.swap(np);
    };

    std::vector<uint8_t> vPx(static_cast<size_t>(atlasW) * atlasH * 4, 0);
    int penX = padding, penY = padding, rowHeight = 0;

    for (int i = 0; i < glyphCount; ++i)
    {
        const Image& gi = pGlyphs[i].image;
        if (!gi.data || gi.width <= 0 || gi.height <= 0)
        {
            if (ppGlyphRecs)
                (*ppGlyphRecs)[i] = Rectangle{};
            continue;
        }
        const int gw = gi.width, gh = gi.height;

        if (gw + padding * 2 > atlasW)
        {
            int nw = atlasW;
            while (nw < gw + padding * 2)
                nw *= 2;
            resize(vPx, atlasW, nw, atlasH, atlasH);
            atlasW = nw;
        }

        if (penX + gw + padding > atlasW)
        {
            penX = padding;
            penY += rowHeight + padding;
            rowHeight = 0;
        }

        if (penY + gh + padding > atlasH)
        {
            int nh = atlasH;
            while (nh < penY + gh + padding)
                nh *= 2;
            resize(vPx, atlasW, atlasW, atlasH, nh);
            atlasH = nh;
        }

        const unsigned char* pSrc = static_cast<const unsigned char*>(gi.data);
        for (int y = 0; y < gh; ++y)
        {
            std::memcpy(&vPx[(static_cast<size_t>(penY + y) * atlasW + penX) * 4],
                        &pSrc[static_cast<size_t>(y) * gw * 4],
                        static_cast<size_t>(gw) * 4);
        }

        if (ppGlyphRecs)
            (*ppGlyphRecs)[i] = Rectangle{static_cast<float>(penX), static_cast<float>(penY),
                                        static_cast<float>(gw), static_cast<float>(gh)};

        penX += gw + padding;
        rowHeight = std::max(rowHeight, gh);
    }

    Image atlas;
    atlas.data = MemAlloc(static_cast<size_t>(atlasW) * atlasH * 4);
    if (!atlas.data)
    {
        if (ppGlyphRecs)
        {
            delete[] *ppGlyphRecs;
            *ppGlyphRecs = nullptr;
        }
        return Image{};
    }
    std::memcpy(atlas.data, vPx.data(), static_cast<size_t>(atlasW) * atlasH * 4);
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
    if (!IsImageValid(image) || image.width <= 0 || image.height <= 0)
        return result;

    const int W = image.width, H = image.height;
    Color* pColors = LoadImageColors(image);
    if (!pColors)
        return result;

    std::vector<uint8_t> vVisited(static_cast<size_t>(W) * H, 0);
    struct Comp
    {
        int m_MinX, m_MinY, m_MaxX, m_MaxY;
    };
    std::vector<Comp> vComps;
    std::vector<std::pair<int, int>> vStack;

    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            const size_t idx = static_cast<size_t>(y) * W + x;
            if (vVisited[idx])
                continue;
            vVisited[idx] = 1;
            const Color c = pColors[idx];
            const bool isKey = (c.r == key.r && c.g == key.g && c.b == key.b);
            if (isKey)
                continue;

            Comp comp{x, y, x, y};
            vStack.clear();
            vStack.emplace_back(x, y);
            while (!vStack.empty())
            {
                const auto [cx, cy] = vStack.back();
                vStack.pop_back();
                const size_t ci = static_cast<size_t>(cy) * W + cx;
                if (vVisited[ci])
                    continue;
                vVisited[ci] = 1;
                const Color cc = pColors[ci];
                if (cc.r == key.r && cc.g == key.g && cc.b == key.b)
                    continue;
                comp.m_MinX = std::min(comp.m_MinX, cx);
                comp.m_MaxX = std::max(comp.m_MaxX, cx);
                comp.m_MinY = std::min(comp.m_MinY, cy);
                comp.m_MaxY = std::max(comp.m_MaxY, cy);
                if (cx > 0)
                    vStack.emplace_back(cx - 1, cy);
                if (cx < W - 1)
                    vStack.emplace_back(cx + 1, cy);
                if (cy > 0)
                    vStack.emplace_back(cx, cy - 1);
                if (cy < H - 1)
                    vStack.emplace_back(cx, cy + 1);
            }
            vComps.push_back(comp);
        }
    }

    UnloadImageColors(pColors);
    if (vComps.empty())
        return result;

    std::sort(vComps.begin(), vComps.end(), [](const Comp& a, const Comp& b)
              {
                  const int ay = (a.m_MinY + a.m_MaxY) / 2, by = (b.m_MinY + b.m_MaxY) / 2;
                  if (ay != by)
                      return ay < by;
                  return (a.m_MinX + a.m_MaxX) < (b.m_MinX + b.m_MaxX);
              });

    const int glyphCount = static_cast<int>(vComps.size());
    int nominalW = 0, nominalH = 0;
    for (const Comp& c : vComps)
    {
        nominalW = std::max(nominalW, c.m_MaxX - c.m_MinX + 1);
        nominalH = std::max(nominalH, c.m_MaxY - c.m_MinY + 1);
    }

    Color* pCols2 = LoadImageColors(image);
    unsigned char* pRgba = static_cast<unsigned char*>(MemAlloc(static_cast<size_t>(W) * H * 4));
    if (!pRgba)
    {
        UnloadImageColors(pCols2);
        return result;
    }
    for (int yy = 0; yy < H; ++yy)
    {
        for (int xx = 0; xx < W; ++xx)
        {
            const size_t o = static_cast<size_t>(yy) * W + xx;
            const Color c = pCols2[o];
            const bool kk = (c.r == key.r && c.g == key.g && c.b == key.b);
            pRgba[o * 4 + 0] = c.r;
            pRgba[o * 4 + 1] = c.g;
            pRgba[o * 4 + 2] = c.b;
            pRgba[o * 4 + 3] = kk ? 0 : 255;
        }
    }
    UnloadImageColors(pCols2);

    GlyphInfo* pGlyphs = static_cast<GlyphInfo*>(MemAlloc(sizeof(GlyphInfo) * static_cast<size_t>(glyphCount)));
    Rectangle* pRecs = new Rectangle[glyphCount];
    for (int i = 0; i < glyphCount; ++i)
    {
        const Comp& c = vComps[i];
        const int w = c.m_MaxX - c.m_MinX + 1, h = c.m_MaxY - c.m_MinY + 1;
        pRecs[i] = Rectangle{static_cast<float>(c.m_MinX), static_cast<float>(c.m_MinY),
                            static_cast<float>(w), static_cast<float>(h)};
        pGlyphs[i].value = firstChar + i;
        pGlyphs[i].offsetX = 0;
        pGlyphs[i].offsetY = nominalH;
        pGlyphs[i].advanceX = nominalW;
        unsigned char* pGd = static_cast<unsigned char*>(MemAlloc(static_cast<size_t>(w) * h * 4));
        if (pGd)
        {
            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    const size_t src = (static_cast<size_t>(c.m_MinY + y) * W + (c.m_MinX + x)) * 4;
                    const size_t dst = (static_cast<size_t>(y) * w + x) * 4;
                    pGd[dst + 0] = pRgba[src + 0];
                    pGd[dst + 1] = pRgba[src + 1];
                    pGd[dst + 2] = pRgba[src + 2];
                    pGd[dst + 3] = pRgba[src + 3];
                }
            }
        }
        pGlyphs[i].image = Image{pGd, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    }

    Image atlas;
    atlas.data = pRgba;
    atlas.width = W;
    atlas.height = H;
    atlas.mipmaps = 1;
    atlas.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;

    const ITexture it = gRenderer.LoadTextureFromImage(atlas);
    MemFree(atlas.data);

    if (!it.m_Valid || it.m_Id == 0)
    {
        for (int i = 0; i < glyphCount; ++i)
            MemFree(pGlyphs[i].image.data);
        MemFree(pGlyphs);
        delete[] pRecs;
        return result;
    }

    result.baseSize = (nominalH > 0) ? nominalH : image.height;
    result.glyphCount = glyphCount;
    result.glyphPadding = 0;
    result.valid = true;
    result._rendererFontId = 0;
    result.texture = Texture2D{it.m_Id, it.m_Width, it.m_Height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, true};
    result.recs = pRecs;
    result.glyphs = pGlyphs;
    return result;
}

bool ExportFontAsCode(Font font, const char* pFileName)
{
    if (!font.valid || !font.glyphs || !font.recs || !pFileName)
        return false;

    Image atlas = LoadImageFromTexture(font.texture);

    std::ofstream out(pFileName, std::ios::out | std::ios::trunc);
    if (!out)
    {
        if (IsImageValid(atlas))
            UnloadImage(atlas);
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
    for (int i = 0; i < font.glyphCount; ++i)
    {
        const GlyphInfo& g = font.glyphs[i];
        out << "    {" << g.value << ", " << g.offsetX << ", " << g.offsetY << ", " << g.advanceX << "}";
        if (i < font.glyphCount - 1)
            out << ",";
        out << "\n";
    }
    out << "};\n\n";

    out << "static const ExportRect exportedRecs[" << font.glyphCount << "] = {\n";
    for (int i = 0; i < font.glyphCount; ++i)
    {
        const Rectangle& r = font.recs[i];
        out << "    {" << r.x << "f, " << r.y << "f, " << r.width << "f, " << r.height << "f}";
        if (i < font.glyphCount - 1)
            out << ",";
        out << "\n";
    }
    out << "};\n\n";

    if (IsImageValid(atlas) && atlas.data && atlas.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
    {
        out << "static const unsigned char exportedAtlasRGBA[" << (size_t)atlas.width * atlas.height * 4 << "] = {\n";
        const unsigned char* p = static_cast<const unsigned char*>(atlas.data);
        const size_t total = static_cast<size_t>(atlas.width) * atlas.height * 4;
        for (size_t i = 0; i < total; ++i)
        {
            out << static_cast<unsigned int>(p[i]);
            if (i < total - 1)
                out << ",";
            if ((i + 1) % 24 == 0)
                out << "\n";
            else
                out << " ";
        }
        out << "\n};\n";
        out << "static const int exportedAtlasWidth = " << atlas.width << ";\n";
        out << "static const int exportedAtlasHeight = " << atlas.height << ";\n";
    }
    else
    {
        out << "// (atlas pixels unavailable: texture could not be read back)\n";
    }

    out.close();
    if (IsImageValid(atlas))
        UnloadImage(atlas);
    return true;
}

void DrawText(const char* pText, int x, int y, int fontSize, Color color)
{
    EnsureInitialized();
    gRenderer.DrawText(pText, x, y, fontSize, color);
}

void DrawDebugText(const char* pText, int x, int y, int fontSize, Color color)
{
    EnsureInitialized();
    gRenderer.DrawDebugText(pText, x, y, fontSize, color);
}

void DrawTextEx(Font font, const char* pText, Vec2 position, float fontSize, float spacing, Color tint)
{
    EnsureInitialized();
    qci::IFont iFont{font._rendererFontId};
    gRenderer.DrawTextEx(iFont, pText, position, fontSize, spacing, tint);
}

Vec2 MeasureTextEx(Font font, const char* pText, float fontSize, float spacing)
{
    qci::IFont iFont{font._rendererFontId};
    return gRenderer.MeasureTextEx(iFont, pText, fontSize, spacing);
}

int MeasureText(const char* pText, int fontSize)
{
    return gRenderer.MeasureText(pText, fontSize);
}

Shader LoadShader(const char* pVs, const char* pFs)
{
    EnsureInitialized();
    return gRenderer.LoadShader(pVs, pFs);
}

Shader LoadShaderFromMemory(const char* pVs, const char* pFs)
{
    EnsureInitialized();
    return gRenderer.LoadShaderFromMemory(pVs, pFs);
}

void Set3DLightEnabled(int index, bool enabled)
{
    EnsureInitialized();
    gRenderer.Set3DLightEnabled(index, enabled);
}

void UnloadShader(Shader shader)
{
    gRenderer.UnloadShader(shader);
}
bool IsShaderValid(const Shader& shader)
{
    return gRenderer.isShaderValid(const_cast<Shader&>(shader));
}
bool IsShaderReady(Shader shader)
{
    return IsShaderValid(shader);
}

namespace
{
    namespace fs = std::filesystem;

    static std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
                       {
                           return static_cast<char>(std::tolower(c));
                       });
        return value;
    }

    static char* AllocatePathString(const std::string& path)
    {
        if (path.empty())
            return nullptr;
        size_t size = path.size() + 1;
        char* pData = static_cast<char*>(std::malloc(size));
        if (!pData)
            return nullptr;
        std::memcpy(pData, path.c_str(), size);
        return pData;
    }

    static std::string NormalizeExtension(std::string ext)
    {
        if (!ext.empty() && ext[0] == '*')
        {
            ext.erase(0, 1);
        }
        if (!ext.empty() && ext[0] == '.')
        {
            return ToLower(ext);
        }
        if (!ext.empty())
        {
            return ToLower(std::string(".") + ext);
        }
        return std::string();
    }

    static bool CompareExtension(const fs::path& path, const std::string& filter)
    {
        const std::string ext = ToLower(path.extension().string());
        return ext == filter;
    }

    static bool MatchesFilter(const fs::path& path, const char* pFilter)
    {
        if (!pFilter || pFilter[0] == '\0' || std::strcmp(pFilter, "*.*") == 0 || std::strcmp(pFilter, "*") == 0)
        {
            return true;
        }

        const std::string f = pFilter;
        if (f.rfind("FILES", 0) == 0)
        {
            return fs::is_regular_file(path);
        }
        if (f.rfind("DIRS", 0) == 0)
        {
            return fs::is_directory(path);
        }
        if (f.size() > 0 && f[0] == '*')
        {
            const std::string normalized = NormalizeExtension(f.substr(1));
            return CompareExtension(path, normalized);
        }
        if (f.size() > 0 && f[0] == '.')
        {
            return CompareExtension(path, ToLower(f));
        }
        return CompareExtension(path, NormalizeExtension(f));
    }

    static std::vector<std::string> LoadDirectoryFilesInternal(const char* pBasePath, const char* pFilter, bool scanSubdirs)
    {
        std::vector<std::string> vResult;
        if (!pBasePath)
            return vResult;

        try
        {
            fs::path root(pBasePath);
            if (!fs::exists(root))
                return vResult;

            if (scanSubdirs)
            {
                for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied))
                {
                    if (!entry.exists())
                        continue;
                    const fs::path& path = entry.path();
                    if (MatchesFilter(path, pFilter))
                    {
                        vResult.emplace_back(path.string());
                    }
                }
            }
            else
            {
                for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied))
                {
                    if (!entry.exists())
                        continue;
                    const fs::path& path = entry.path();
                    if (MatchesFilter(path, pFilter))
                    {
                        vResult.emplace_back(path.string());
                    }
                }
            }
        }
        catch (const std::exception&)
        {
        }
        return vResult;
    }

    static FilePathList BuildFilePathList(const std::vector<std::string>& vPaths)
    {
        FilePathList list{};
        list.count = static_cast<unsigned int>(vPaths.size());
        if (list.count == 0)
            return list;

        list.paths = static_cast<char**>(std::malloc(sizeof(char*) * list.count));
        if (!list.paths)
        {
            list.count = 0;
            return list;
        }

        for (unsigned int i = 0; i < list.count; ++i)
        {
            list.paths[i] = AllocatePathString(vPaths[i]);
            if (!list.paths[i])
            {
                for (unsigned int j = 0; j < i; ++j)
                {
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

    static long FileTimeToTimeT(const fs::file_time_type& fileTime)
    {
        try
        {
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                fileTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());

            return static_cast<long>(std::chrono::system_clock::to_time_t(sctp));
        }
        catch (...)
        {
            return 0;
        }
    }
} // anonymous

int FileRename(const char* pFileName, const char* pFileRename)
{
    if (!pFileName || !pFileRename)
        return -1;
    try
    {
        std::filesystem::rename(pFileName, pFileRename);
        return 0;
    }
    catch (...)
    {
        return -1;
    }
}

int FileRemove(const char* pFileName)
{
    if (!pFileName)
        return -1;
    try
    {
        return std::filesystem::remove(pFileName) ? 0 : -1;
    }
    catch (...)
    {
        return -1;
    }
}

int FileCopy(const char* pSrcPath, const char* pDstPath)
{
    if (!pSrcPath || !pDstPath)
        return -1;
    try
    {
        std::filesystem::path target(pDstPath);
        if (target.has_parent_path())
        {
            std::filesystem::create_directories(target.parent_path());
        }
        std::filesystem::copy_file(pSrcPath, pDstPath, std::filesystem::copy_options::overwrite_existing);
        return 0;
    }
    catch (...)
    {
        return -1;
    }
}

int FileMove(const char* pSrcPath, const char* pDstPath)
{
    if (!pSrcPath || !pDstPath)
        return -1;
    try
    {
        std::filesystem::path target(pDstPath);
        if (target.has_parent_path())
        {
            std::filesystem::create_directories(target.parent_path());
        }
        std::filesystem::rename(pSrcPath, pDstPath);
        return 0;
    }
    catch (...)
    {
        try
        {
            if (FileCopy(pSrcPath, pDstPath) == 0)
            {
                return FileRemove(pSrcPath);
            }
        }
        catch (...)
        {
        }
        return -1;
    }
}

int FileTextReplace(const char* pFileName, const char* pSearch, const char* pReplacement)
{
    if (!pFileName || !pSearch || !pReplacement)
        return -1;
    try
    {
        std::ifstream input(pFileName, std::ios::binary);
        if (!input)
            return -1;
        std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        input.close();

        std::string needle = pSearch;
        std::string repl = pReplacement;
        size_t pos = 0;
        bool replaced = false;
        while ((pos = content.find(needle, pos)) != std::string::npos)
        {
            content.replace(pos, needle.length(), repl);
            pos += repl.length();
            replaced = true;
        }
        if (!replaced)
            return -1;

        std::ofstream output(pFileName, std::ios::binary | std::ios::trunc);
        if (!output)
            return -1;
        output << content;
        return 0;
    }
    catch (...)
    {
        return -1;
    }
}

int FileTextFindIndex(const char* pFileName, const char* pSearch)
{
    if (!pFileName || !pSearch)
        return -1;
    try
    {
        std::ifstream input(pFileName, std::ios::binary);
        if (!input)
            return -1;
        std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        const auto pos = content.find(pSearch);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    catch (...)
    {
        return -1;
    }
}

bool FileExists(const char* pFileName)
{
    if (!pFileName)
        return false;
    try
    {
        return std::filesystem::exists(pFileName) && std::filesystem::is_regular_file(pFileName);
    }
    catch (...)
    {
        return false;
    }
}

bool DirectoryExists(const char* pDirPath)
{
    if (!pDirPath)
        return false;
    try
    {
        return std::filesystem::exists(pDirPath) && std::filesystem::is_directory(pDirPath);
    }
    catch (...)
    {
        return false;
    }
}

bool IsFileExtension(const char* pFileName, const char* pExt)
{
    if (!pFileName || !pExt)
        return false;
    try
    {
        const auto fileExt = ToLower(std::filesystem::path(pFileName).extension().string());
        const std::string expected = ToLower(pExt[0] == '.' ? std::string(pExt) : std::string(".") + pExt);
        return fileExt == expected;
    }
    catch (...)
    {
        return false;
    }
}

int GetFileLength(const char* pFileName)
{
    if (!pFileName)
        return -1;
    try
    {
        auto size = std::filesystem::file_size(pFileName);
        return size > static_cast<uintmax_t>(std::numeric_limits<int>::max()) ? -1 : static_cast<int>(size);
    }
    catch (...)
    {
        return -1;
    }
}

long GetFileModTime(const char* pFileName)
{
    if (!pFileName)
        return 0;
    try
    {
        auto time = std::filesystem::last_write_time(pFileName);
        return FileTimeToTimeT(time);
    }
    catch (...)
    {
        return 0;
    }
}

const char* GetFileExtension(const char* pFileName)
{
    thread_local std::string buffer;
    buffer.clear();
    if (!pFileName)
        return buffer.c_str();
    try
    {
        buffer = std::filesystem::path(pFileName).extension().string();
    }
    catch (...)
    {
        buffer.clear();
    }
    return buffer.c_str();
}

const char* GetFileName(const char* pFilePath)
{
    thread_local std::string buffer;
    buffer.clear();
    if (!pFilePath)
        return buffer.c_str();
    try
    {
        buffer = std::filesystem::path(pFilePath).filename().string();
    }
    catch (...)
    {
        buffer.clear();
    }
    return buffer.c_str();
}

const char* GetFileNameWithoutExt(const char* pFilePath)
{
    thread_local std::string buffer;
    buffer.clear();
    if (!pFilePath)
        return buffer.c_str();
    try
    {
        buffer = std::filesystem::path(pFilePath).stem().string();
    }
    catch (...)
    {
        buffer.clear();
    }
    return buffer.c_str();
}

const char* GetDirectoryPath(const char* pFilePath)
{
    thread_local std::string buffer;
    buffer.clear();
    if (!pFilePath)
        return buffer.c_str();
    try
    {
        buffer = std::filesystem::path(pFilePath).parent_path().string();
    }
    catch (...)
    {
        buffer.clear();
    }
    return buffer.c_str();
}

const char* GetPrevDirectoryPath(const char* pDirPath)
{
    thread_local std::string buffer;
    buffer.clear();
    if (!pDirPath)
        return buffer.c_str();
    try
    {
        const auto path = std::filesystem::path(pDirPath).parent_path();
        buffer = path.parent_path().string();
    }
    catch (...)
    {
        buffer.clear();
    }
    return buffer.c_str();
}

const char* GetWorkingDirectory(void)
{
    thread_local std::string buffer;
    buffer.clear();
    try
    {
        buffer = std::filesystem::current_path().string();
    }
    catch (...)
    {
        buffer.clear();
    }
    return buffer.c_str();
}

const char* GetApplicationDirectory(void)
{
    thread_local std::string buffer;
    buffer.clear();
    const char* pPath = SDL_GetBasePath();
    if (pPath)
    {
        buffer = pPath;
    }
    return buffer.c_str();
}

int MakeDirectory(const char* pDirPath)
{
    if (!pDirPath)
        return -1;
    try
    {
        std::filesystem::create_directories(pDirPath);
        return 0;
    }
    catch (...)
    {
        return -1;
    }
}

int ChangeDirectory(const char* pDirPath)
{
    if (!pDirPath)
        return -1;
    try
    {
        std::filesystem::current_path(pDirPath);
        return 0;
    }
    catch (...)
    {
        return -1;
    }
}

bool IsPathFile(const char* pPath)
{
    if (!pPath)
        return false;
    try
    {
        return std::filesystem::exists(pPath) && std::filesystem::is_regular_file(pPath);
    }
    catch (...)
    {
        return false;
    }
}

bool IsPathDirectory(const char* pPath)
{
    if (!pPath)
        return false;
    try
    {
        return std::filesystem::exists(pPath) && std::filesystem::is_directory(pPath);
    }
    catch (...)
    {
        return false;
    }
}

bool IsPathAbsolute(const char* pPath)
{
    if (!pPath)
        return false;
    try
    {
        return std::filesystem::path(pPath).is_absolute();
    }
    catch (...)
    {
        return false;
    }
}

bool IsFileNameValid(const char* pFileName)
{
    if (!pFileName)
        return false;
    const std::string name(pFileName);
    if (name.empty() || name == "." || name == "..")
        return false;
    for (char c : name)
    {
        if (c == '\0')
            return false;
#if defined(_WIN32)
        const char invalid[] = R"(<>:\"/\|?*)";
        if (std::strchr(invalid, c))
            return false;
#endif
    }
#if defined(_WIN32)
    const std::string upper = ToLower(name);
    static const char* s_apReserved[] = {
        "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    std::string stem = upper;
    auto dotPos = stem.find('.');
    if (dotPos != std::string::npos)
        stem.resize(dotPos);
    for (const char* pName : s_apReserved)
    {
        if (stem == pName)
            return false;
    }
#endif
    return true;
}

FilePathList LoadDirectoryFiles(const char* pDirPath)
{
    return BuildFilePathList(LoadDirectoryFilesInternal(pDirPath, "*.*", false));
}

FilePathList LoadDirectoryFilesEx(const char* pBasePath, const char* pFilter, bool scanSubdirs)
{
    return BuildFilePathList(LoadDirectoryFilesInternal(pBasePath, pFilter, scanSubdirs));
}

void UnloadDirectoryFiles(FilePathList files)
{
    if (!files.paths)
        return;

    for (unsigned int i = 0; i < files.count; ++i)
    {
        if (files.paths[i])
            std::free(files.paths[i]);
    }

    std::free(files.paths);
}

bool IsFileDropped(void)
{
    return !g_Win.m_vDroppedFiles.empty();
}

FilePathList LoadDroppedFiles(void)
{
    FilePathList result = BuildFilePathList(g_Win.m_vDroppedFiles);

    g_Win.m_vDroppedFiles.clear();
    return result;
}

void UnloadDroppedFiles(FilePathList files)
{
    UnloadDirectoryFiles(files);
}

unsigned int GetDirectoryFileCount(const char* pDirPath)
{
    if (!pDirPath)
        return 0;
    try
    {
        unsigned int count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(pDirPath, std::filesystem::directory_options::skip_permission_denied))
        {
            if (entry.exists())
                ++count;
        }
        return count;
    }
    catch (...)
    {
        return 0;
    }
}

unsigned int GetDirectoryFileCountEx(const char* pBasePath, const char* pFilter, bool scanSubdirs)
{
    return static_cast<unsigned int>(LoadDirectoryFilesInternal(pBasePath, pFilter, scanSubdirs).size());
}

int GetShaderLocation(const Shader& shader, const char* pName)
{
    return gRenderer.GetShaderLocation(shader, pName);
}

int GetShaderLocation(const Shader& shader, ShaderLocationIndex locIndex)
{
    return gRenderer.GetShaderLocation(shader, locIndex);
}

int GetShaderAttributeLocation(const Shader& s, const char* pName)
{
    return gRenderer.GetShaderAttributeLocation(s, pName);
}

void SetShaderValue(const Shader& s, int loc, float value)
{
    gRenderer.SetShaderValue(s, loc, value);
}

void SetShaderValue(const Shader& s, int loc, int value)
{
    gRenderer.SetShaderValue(s, loc, value);
}

void SetShaderValue(const Shader& s, int loc, const Vec2& value)
{
    gRenderer.SetShaderValue(s, loc, value);
}

void SetShaderValue(const Shader& s, int loc, const Vec3& value)
{
    gRenderer.SetShaderValue(s, loc, value);
}

void SetShaderValue(const Shader& s, int loc, const Vec4& value)
{
    gRenderer.SetShaderValue(s, loc, value);
}

void SetShaderValue(const Shader& s, int loc, const Color& value)
{
    Color v = value;
    gRenderer.SetShaderValue(s, loc, v);
}

void SetShaderValue(const Shader& s, int loc, const void* pValue, int uniformType)
{
    gRenderer.SetShaderValue(s, loc, pValue, uniformType);
}

void SetShaderValueV(const Shader& s, int loc, const void* pValue, int uniformType, int count)
{
    gRenderer.SetShaderValueV(s, loc, pValue, uniformType, count);
}

void SetShaderValueMatrix(const Shader& s, int loc, const float* pM)
{
    gRenderer.SetShaderValueMatrix(s, loc, pM);
}

void SetShaderValueMatrix(const Shader& s, int loc, const Matrix& mat)
{
    gRenderer.SetShaderValueMatrix(s, loc, mat);
}

void SetShaderValueSampler(const Shader& s, int loc, int unit)
{
    gRenderer.SetShaderValueSampler(s, loc, unit);
}

void SetShaderValueTexture(const Shader& s, int loc, const Texture2D& texture)
{
    ITexture it{texture.id, texture.width, texture.height, texture.mipmaps, texture.format, texture.valid};
    gRenderer.SetShaderValueTexture(s, loc, it);
}

void SetShaderValueTextureUnit(const Shader& s, int loc, const Texture2D& texture, int textureUnit)
{
    ITexture it{texture.id, texture.width, texture.height, texture.mipmaps, texture.format, texture.valid};
    gRenderer.SetShaderValueTextureUnit(s, loc, it, textureUnit);
}

void UnloadVertexArray(unsigned int vaoId)
{
    (void)vaoId;
#if defined(QC_ENABLE_OPENGL)
    if (vaoId)
        glDeleteVertexArrays(1, &vaoId);
#endif
}

void UnloadVertexBuffer(unsigned int vboId)
{
    (void)vboId;
#if defined(QC_ENABLE_OPENGL)
    if (vboId)
        glDeleteBuffers(1, &vboId);
#endif
}

Material LoadMaterialDefault()
{
    Material material{};
    material.maps = new MaterialMap[MATERIAL_MAP_BRDF + 1]{};
    material.maps[MATERIAL_MAP_ALBEDO].color = WHITE;
    return material;
}

bool IsModelValid(Model model)
{
    return model.meshCount > 0 && model.meshes != nullptr;
}

Material* LoadMaterials(const char* pFileName, int* pMaterialCount)
{
    if (pFileName != nullptr && pFileName[0] != '\0')
    {
        TraceLog(LogLevel::Info, "ASSET",
                 TextFormat("LoadMaterials: using default material fallback for '%s'", pFileName));
    }
    if (pMaterialCount)
        *pMaterialCount = 1;
    Material* pMaterials = new Material[1];
    pMaterials[0] = LoadMaterialDefault();
    return pMaterials;
}

bool IsMaterialValid(Material material)
{
    return material.maps != nullptr;
}

void UnloadMaterial(Material material)
{
    delete[] material.maps;
    delete material.shader;
}

void SetMaterialTexture(Material* pMaterial, int mapType, Texture2D texture)
{
    if (!pMaterial || !pMaterial->maps || mapType < 0 || mapType > MATERIAL_MAP_BRDF)
        return;
    pMaterial->maps[mapType].texture = texture;
}

void SetModelMeshMaterial(Model* pModel, int meshId, int materialId)
{
    if (!pModel || !pModel->meshMaterial || meshId < 0 || meshId >= pModel->meshCount || materialId < 0 || materialId >= pModel->materialCount)
        return;
    pModel->meshMaterial[meshId] = materialId;
}

Model LoadModelFromMesh(Mesh mesh)
{
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

Model LoadModelFromMesh(const char* pName, Mesh mesh)
{
    Model model = LoadModelFromMesh(mesh);
    if (pName)
        model.directory = pName;
    return model;
}

void BeginShaderMode(const Shader& shader)
{
    gRenderer.BeginShaderMode(shader);
}

void EndShaderMode()
{
    gRenderer.EndShaderMode();
}

Camera2D CreateCamera2D()
{
    Camera2D c{};
    c.zoom = 1.f;
    return c;
}

void BeginMode2D(const Camera2D& camera)
{
    EnsureInitialized();
    gRenderer.BeginMode2D(camera);
}

void EndMode2D()
{
    gRenderer.EndMode2D();
}

Camera2D GetCamera2D()
{
    return gRenderer.GetCamera2D();
}

void UpdateCamera2D(Camera2D& camera, float targetX, float targetY, float smoothness)
{
    smoothness = std::clamp(smoothness, 0.f, 1.f);
    camera.target.x = camera.target.x * (1.f - smoothness) + targetX * smoothness;
    camera.target.y = camera.target.y * (1.f - smoothness) + targetY * smoothness;
}

Camera3D CreateCamera3D()
{
    Camera3D c{};
    c.position = {0.f, 0.f, 10.f};
    c.target = {0.f, 0.f, 0.f};
    c.up = {0.f, 1.f, 0.f};
    return c;
}

void BeginMode3D(const Camera3D& camera)
{
    EnsureInitialized();
    gRenderer.BeginMode3D(camera);
}

void EndMode3D()
{
    gRenderer.EndMode3D();
}

void PushMatrix()
{
    gRenderer.PushMatrix();
}

void PopMatrix()
{
    gRenderer.PopMatrix();
}

void Translate(const Vec3& t)
{
    gRenderer.Translate(t);
}

void Translate(float x, float y, float z)
{
    gRenderer.Translate(Vec3{x, y, z});
}

void Rotate(float angle, const Vec3& ax)
{
    gRenderer.Rotate(angle, ax);
}

void Rotate(float angle)
{
    gRenderer.Rotate(angle, Vec3{0, 0, 1});
}

void Scale(const Vec3& s)
{
    gRenderer.Scale(s);
}

void Scale(float s)
{
    gRenderer.Scale(Vec3{s, s, s});
}

void MultMatrix(const Mat4& m)
{
    gRenderer.MultMatrix(m);
}

void EnableBackfaceCulling()
{
    gRenderer.EnableBackfaceCulling();
}

void DisableBackfaceCulling()
{
    gRenderer.DisableBackfaceCulling();
}

const float* GetMatrixModelview()
{
    return gRenderer.GetMatrixModelview();
}

const float* GetMatrixProjection()
{
    return gRenderer.GetMatrixProjection();
}

void Set3DView(const Mat4& view, const Mat4& proj)
{
    gRenderer.Set3DView(view, proj);
}

void DrawLine3D(Vec3 start, Vec3 end, Color c)
{
    gRenderer.DrawLine3D(start, end, c);
}

void DrawPoint3D(Vec3 position, Color color)
{
    constexpr float s = 0.01f;
    DrawLine3D({position.x - s, position.y, position.z}, {position.x + s, position.y, position.z}, color);
    DrawLine3D({position.x, position.y - s, position.z}, {position.x, position.y + s, position.z}, color);
    DrawLine3D({position.x, position.y, position.z - s}, {position.x, position.y, position.z + s}, color);
}

void DrawCircle3D(Vec3 center, float radius, Vec3 rotationAxis, float rotationAngle, Color color)
{
    Vec3 axis = rotationAxis;
    const float axisLen = axis.length();
    if (axisLen <= EPSILON)
    {
        axis = Vec3{0.0f, 1.0f, 0.0f};
    }
    else
    {
        axis = axis * (1.0f / axisLen);
    }

    const Mat4 rotation = QuaternionToMatrix(QuaternionFromAxisAngle(axis, rotationAngle * DEG2RAD));
    Vec3 last = TransformPoint(rotation, Vec3{radius, 0.0f, 0.0f}) + center;

    for (int i = 1; i <= 64; ++i)
    {
        const float a = 2.0f * PI * static_cast<float>(i) / 64.0f;
        const Vec3 local{
            std::cos(a) * radius,
            std::sin(a) * radius,
            0.0f};
        const Vec3 cur = TransformPoint(rotation, local) + center;
        DrawLine3D(last, cur, color);
        last = cur;
    }
}

void DrawTriangle3D(Vec3 v1, Vec3 v2, Vec3 v3, Color color)
{
    DrawLine3D(v1, v2, color);
    DrawLine3D(v2, v3, color);
    DrawLine3D(v3, v1, color);
}

void DrawTriangleStrip3D(const Vec3* pPoints, int pointCount, Color color)
{
    if (!pPoints || pointCount < 3)
    {
        return;
    }

    for (int i = 0; i + 2 < pointCount; ++i)
    {
        DrawTriangle3D(pPoints[i], pPoints[i + 1], pPoints[i + 2], color);
    }
}

void DrawGrid(int slices, float spacing, Color color)
{
    gRenderer.DrawGrid(slices, spacing, color);
}

void DrawPlane(Vec3 center, Vec2 size, Color c)
{
    gRenderer.DrawPlane(center, size, c);
}

void DrawCube(Vec3 pos, float w, float h, float l, Color c)
{
    gRenderer.DrawCube(pos, w, h, l, c);
}

void DrawCubeV(Vec3 pos, Vec3 size, Color c)
{
    gRenderer.DrawCubeV(pos, size, c);
}

void DrawCubeWires(Vec3 pos, float w, float h, float l, Color c)
{
    gRenderer.DrawCubeWires(pos, w, h, l, c);
}

void DrawCubeWiresV(Vec3 pos, Vec3 size, Color c)
{
    gRenderer.DrawCubeWiresV(pos, size, c);
}

void DrawSphere(Vec3 center, float radius, Color c)
{
    gRenderer.DrawSphere(center, radius, c);
}

void DrawSphereEx(Vec3 center, float radius, int rings, int slices, Color c)
{
    gRenderer.DrawSphereEx(center, radius, rings, slices, c);
}

void DrawSphereWires(Vec3 center, float radius, int rings, int slices, Color c)
{
    gRenderer.DrawSphereWires(center, radius, rings, slices, c);
}

void DrawCylinder(Vec3 pos, float rTop, float rBot, float h, int slices, Color c)
{
    gRenderer.DrawCylinder(pos, rTop, rBot, h, slices, c);
}

void DrawCylinderEx(Vec3 s, Vec3 e, float rS, float rE, int sides, Color c)
{
    gRenderer.DrawCylinderEx(s, e, rS, rE, sides, c);
}

void DrawCylinderWires(Vec3 pos, float rTop, float rBot, float h, int slices, Color c)
{
    gRenderer.DrawCylinderWires(pos, rTop, rBot, h, slices, c);
}

void DrawCylinderWiresEx(Vec3 s, Vec3 e, float rS, float rE, int slices, Color c)
{
    gRenderer.DrawCylinderWiresEx(s, e, rS, rE, slices, c);
}

void DrawCapsule(Vec3 startPos, Vec3 endPos, float radius, int rings, int slices, Color color)
{
    DrawCylinderEx(startPos, endPos, radius, radius, slices, color);
    DrawSphereEx(startPos, radius, rings, slices, color);
    DrawSphereEx(endPos, radius, rings, slices, color);
}

void DrawCapsuleWires(Vec3 startPos, Vec3 endPos, float radius, int rings, int slices, Color color)
{
    DrawCylinderWiresEx(startPos, endPos, radius, radius, slices, color);
    DrawSphereWires(startPos, radius, rings, slices, color);
    DrawSphereWires(endPos, radius, rings, slices, color);
}

void DrawRay(Ray ray, Color color)
{
    DrawLine3D(ray.position, ray.position + ray.direction * 10000.0f, color);
}

Model LoadModel(const char* pFilePath)
{
    EnsureInitialized();
    return gRenderer.LoadModel(pFilePath);
}

void UnloadModel(Model& model)
{
    gRenderer.UnloadModel(model);
}

void DrawModel(Model model, Vec3 position, float scale, Color tint)
{
    Mat4 transform = BuildTransform(position, Vec3{0.0f, 1.0f, 0.0f}, 0.0f, Vec3{scale, scale, scale});
    gRenderer.DrawModelEx(model, transform, tint);
}

void DrawModelEx(Model model, Vec3 position, Vec3 rotationAxis,
                 float rotationAngle, Vec3 scale, Color tint)
{
    Mat4 transform = BuildTransform(position, rotationAxis, rotationAngle, scale);
    gRenderer.DrawModelEx(model, transform, tint);
}

void DrawModelEx(Model model, const Mat4& transform)
{
    gRenderer.DrawModelEx(model, transform);
}

void DrawModelWires(Model model, Vec3 position, float scale, Color tint)
{
    Mat4 transform = BuildTransform(position, Vec3{0.0f, 0.0f, 1.0f}, 0.0f, Vec3{scale, scale, scale});
    DrawModelWireframe(model, transform, tint);
}

void DrawModelWiresEx(Model model, Vec3 position, Vec3 rotationAxis,
                      float rotationAngle, Vec3 scale, Color tint)
{
    Mat4 transform = BuildTransform(position, rotationAxis, rotationAngle, scale);
    DrawModelWireframe(model, transform, tint);
}

namespace
{
    static Mesh CreateMesh(int vertexCount, int triangleCount)
    {
        Mesh mesh{};
        if (vertexCount <= 0)
            return mesh;
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

    static void FreeMeshCpuData(Mesh& mesh)
    {
        delete[] mesh.vertices;
        mesh.vertices = nullptr;
        delete[] mesh.texcoords;
        mesh.texcoords = nullptr;
        delete[] mesh.texcoords2;
        mesh.texcoords2 = nullptr;
        delete[] mesh.normals;
        mesh.normals = nullptr;
        delete[] mesh.tangents;
        mesh.tangents = nullptr;
        delete[] mesh.colors;
        mesh.colors = nullptr;
        delete[] mesh.indices;
        mesh.indices = nullptr;
        delete[] mesh.boneIndices;
        mesh.boneIndices = nullptr;
        delete[] mesh.boneWeights;
        mesh.boneWeights = nullptr;
        delete[] mesh.boneOffsets;
        mesh.boneOffsets = nullptr;
        mesh.boneOffsetCount = 0;
        delete[] mesh.animVertices;
        mesh.animVertices = nullptr;
        delete[] mesh.animNormals;
        mesh.animNormals = nullptr;
        delete[] mesh.bindVertices;
        mesh.bindVertices = nullptr;
        delete[] mesh.bindNormals;
        mesh.bindNormals = nullptr;
    }

    static Vec3 CalculateTriangleTangent(const Vec3& p0, const Vec3& p1, const Vec3& p2,
                                         const Vec2& uv0, const Vec2& uv1, const Vec2& uv2)
    {
        Vec3 edge1 = p1 - p0;
        Vec3 edge2 = p2 - p0;
        Vec2 deltaUV1{uv1.x - uv0.x, uv1.y - uv0.y};
        Vec2 deltaUV2{uv2.x - uv0.x, uv2.y - uv0.y};
        float denom = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
        if (std::fabs(denom) < EPSILON)
            return Vec3{1.0f, 0.0f, 0.0f};
        float inv = 1.0f / denom;
        Vec3 tangent{
            inv * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x),
            inv * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y),
            inv * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z)};
        return tangent.normalized();
    }

    static Vec3 GetHeightSample(const Image& heightmap, int x, int y)
    {
        if (!heightmap.data || x < 0 || y < 0 || x >= heightmap.width || y >= heightmap.height)
            return Vec3{0, 0, 0};
        float value = GetImageColor(heightmap, x, y).r / 255.0f;
        return Vec3{value, value, value};
    }
} // anonymous

void UploadMesh(Mesh* pMesh, bool dynamic)
{
    if (!pMesh)
        return;
    gRenderer.UploadMesh(*pMesh, dynamic);
}

void UpdateMeshBuffer(Mesh mesh, int index, const void* pData, int dataSize, int offset)
{
    gRenderer.UpdateMeshBuffer(mesh, index, pData, dataSize, offset);
}

void UnloadMesh(Mesh mesh)
{
    gRenderer.UnloadMesh(mesh);
}

void DrawMesh(Mesh mesh, Material material, Matrix transform)
{
    gRenderer.DrawMesh(mesh, material, transform);
}

void DrawMeshInstanced(Mesh mesh, Material material, const Matrix* pTransforms, int instances)
{
    gRenderer.DrawMeshInstanced(mesh, material, pTransforms, instances);
}

BoundingBox GetMeshBoundingBox(Mesh mesh)
{
    BoundingBox box{};
    if (!mesh.vertices || mesh.vertexCount <= 0)
        return box;

    box.min = Vec3{mesh.vertices[0], mesh.vertices[1], mesh.vertices[2]};
    box.max = box.min;
    for (int i = 1; i < mesh.vertexCount; ++i)
    {
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

BoundingBox GetModelBoundingBox(Model model)
{
    BoundingBox box{};
    if (!model.meshes || model.meshCount <= 0)
        return box;

    bool initialized = false;
    for (int i = 0; i < model.meshCount; ++i)
    {
        BoundingBox meshBox = GetMeshBoundingBox(model.meshes[i]);
        if (!initialized)
        {
            box = meshBox;
            initialized = true;
        }
        else
        {
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

void GenMeshTangents(Mesh* pMesh)
{
    if (!pMesh || !pMesh->vertices || !pMesh->normals || !pMesh->texcoords || pMesh->vertexCount <= 0)
        return;
    if (!pMesh->tangents)
        pMesh->tangents = new float[pMesh->vertexCount * 3];
    std::fill(pMesh->tangents, pMesh->tangents + pMesh->vertexCount * 3, 0.0f);

    if (!pMesh->indices)
        return;

    for (int t = 0; t < pMesh->triangleCount; ++t)
    {
        int i0 = pMesh->indices[t * 3 + 0];
        int i1 = pMesh->indices[t * 3 + 1];
        int i2 = pMesh->indices[t * 3 + 2];
        if (i0 >= pMesh->vertexCount || i1 >= pMesh->vertexCount || i2 >= pMesh->vertexCount)
            continue;

        Vec3 p0{pMesh->vertices[i0 * 3 + 0], pMesh->vertices[i0 * 3 + 1], pMesh->vertices[i0 * 3 + 2]};
        Vec3 p1{pMesh->vertices[i1 * 3 + 0], pMesh->vertices[i1 * 3 + 1], pMesh->vertices[i1 * 3 + 2]};
        Vec3 p2{pMesh->vertices[i2 * 3 + 0], pMesh->vertices[i2 * 3 + 1], pMesh->vertices[i2 * 3 + 2]};
        Vec2 uv0{pMesh->texcoords[i0 * 2 + 0], pMesh->texcoords[i0 * 2 + 1]};
        Vec2 uv1{pMesh->texcoords[i1 * 2 + 0], pMesh->texcoords[i1 * 2 + 1]};
        Vec2 uv2{pMesh->texcoords[i2 * 2 + 0], pMesh->texcoords[i2 * 2 + 1]};

        Vec3 tangent = CalculateTriangleTangent(p0, p1, p2, uv0, uv1, uv2);
        for (int idx : {i0, i1, i2})
        {
            pMesh->tangents[idx * 3 + 0] += tangent.x;
            pMesh->tangents[idx * 3 + 1] += tangent.y;
            pMesh->tangents[idx * 3 + 2] += tangent.z;
        }
    }

    for (int i = 0; i < pMesh->vertexCount; ++i)
    {
        Vec3 t{pMesh->tangents[i * 3 + 0], pMesh->tangents[i * 3 + 1], pMesh->tangents[i * 3 + 2]};
        Vec3 n{pMesh->normals[i * 3 + 0], pMesh->normals[i * 3 + 1], pMesh->normals[i * 3 + 2]};
        Vec3 tangent = t.normalized();
        if (tangent.length() > 0.0f)
        {
            pMesh->tangents[i * 3 + 0] = tangent.x;
            pMesh->tangents[i * 3 + 1] = tangent.y;
            pMesh->tangents[i * 3 + 2] = tangent.z;
        }
        else
        {
            pMesh->tangents[i * 3 + 0] = 1.0f;
            pMesh->tangents[i * 3 + 1] = 0.0f;
            pMesh->tangents[i * 3 + 2] = 0.0f;
        }
    }
}

bool ExportMesh(Mesh mesh, const char* pFileName)
{
    if (!pFileName || !mesh.vertices || mesh.vertexCount <= 0)
        return false;
    std::ofstream out(pFileName, std::ios::binary);
    if (!out)
        return false;

    bool hasUV = mesh.texcoords != nullptr;
    bool hasNormal = mesh.normals != nullptr;

    out << "# Generated by QuarkCore\n";
    for (int i = 0; i < mesh.vertexCount; ++i)
    {
        out << "v " << mesh.vertices[i * 3 + 0] << " "
            << mesh.vertices[i * 3 + 1] << " "
            << mesh.vertices[i * 3 + 2] << "\n";
    }
    if (hasUV)
    {
        for (int i = 0; i < mesh.vertexCount; ++i)
        {
            out << "vt " << mesh.texcoords[i * 2 + 0] << " "
                << mesh.texcoords[i * 2 + 1] << "\n";
        }
    }
    if (hasNormal)
    {
        for (int i = 0; i < mesh.vertexCount; ++i)
        {
            out << "vn " << mesh.normals[i * 3 + 0] << " "
                << mesh.normals[i * 3 + 1] << " "
                << mesh.normals[i * 3 + 2] << "\n";
        }
    }

    if (mesh.indices && mesh.triangleCount > 0)
    {
        for (int t = 0; t < mesh.triangleCount; ++t)
        {
            int a = mesh.indices[t * 3 + 0] + 1;
            int b = mesh.indices[t * 3 + 1] + 1;
            int c = mesh.indices[t * 3 + 2] + 1;
            if (hasUV && hasNormal)
            {
                out << "f " << a << "/" << a << "/" << a << " "
                    << b << "/" << b << "/" << b << " "
                    << c << "/" << c << "/" << c << "\n";
            }
            else if (hasUV)
            {
                out << "f " << a << "/" << a << " "
                    << b << "/" << b << " "
                    << c << "/" << c << "\n";
            }
            else if (hasNormal)
            {
                out << "f " << a << "//" << a << " "
                    << b << "//" << b << " "
                    << c << "//" << c << "\n";
            }
            else
            {
                out << "f " << a << " " << b << " " << c << "\n";
            }
        }
    }
    else
    {
        for (int i = 0; i + 2 < mesh.vertexCount; i += 3)
        {
            out << "f " << (i + 1) << " " << (i + 2) << " " << (i + 3) << "\n";
        }
    }

    return true;
}

bool ExportMeshAsCode(Mesh mesh, const char* pFileName)
{
    if (!pFileName || !mesh.vertices || mesh.vertexCount <= 0)
        return false;
    std::ofstream out(pFileName, std::ios::binary);
    if (!out)
        return false;

    out << "#include \"QuarkCore/Quark3D.hpp\"\n\n";
    out << "static float meshVertices[] = {\n";
    for (int i = 0; i < mesh.vertexCount; ++i)
    {
        out << "    " << mesh.vertices[i * 3 + 0] << "f, "
            << mesh.vertices[i * 3 + 1] << "f, "
            << mesh.vertices[i * 3 + 2] << "f,\n";
    }
    out << "};\n\n";

    if (mesh.normals)
    {
        out << "static float meshNormals[] = {\n";
        for (int i = 0; i < mesh.vertexCount; ++i)
        {
            out << "    " << mesh.normals[i * 3 + 0] << "f, "
                << mesh.normals[i * 3 + 1] << "f, "
                << mesh.normals[i * 3 + 2] << "f,\n";
        }
        out << "};\n\n";
    }

    if (mesh.texcoords)
    {
        out << "static float meshTexcoords[] = {\n";
        for (int i = 0; i < mesh.vertexCount; ++i)
        {
            out << "    " << mesh.texcoords[i * 2 + 0] << "f, "
                << mesh.texcoords[i * 2 + 1] << "f,\n";
        }
        out << "};\n\n";
    }

    if (mesh.indices && mesh.triangleCount > 0)
    {
        out << "static unsigned short meshIndices[] = {\n";
        for (int i = 0; i < mesh.triangleCount * 3; ++i)
        {
            out << "    " << mesh.indices[i] << ",\n";
        }
        out << "};\n\n";
    }

    out << "Mesh mesh = {};\n";
    out << "mesh.vertexCount = " << mesh.vertexCount << ";\n";
    out << "mesh.triangleCount = " << mesh.triangleCount << ";\n";
    out << "mesh.vertices = meshVertices;\n";
    if (mesh.normals)
        out << "mesh.normals = meshNormals;\n";
    if (mesh.texcoords)
        out << "mesh.texcoords = meshTexcoords;\n";
    if (mesh.indices && mesh.triangleCount > 0)
        out << "mesh.indices = meshIndices;\n";

    return true;
}

Mesh GenMeshPoly(int sides, float radius)
{
    if (sides < 3)
        return Mesh{};
    int vertexCount = sides + 1;
    int triangleCount = sides - 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);
    float angleStep = 2.0f * PI / sides;

    mesh.vertices[0] = 0.0f;
    mesh.vertices[1] = 0.0f;
    mesh.vertices[2] = 0.0f;
    mesh.normals[0] = 0.0f;
    mesh.normals[1] = 0.0f;
    mesh.normals[2] = 1.0f;
    mesh.texcoords[0] = 0.5f;
    mesh.texcoords[1] = 0.5f;

    for (int i = 0; i < sides; ++i)
    {
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

    for (int i = 0; i < triangleCount; ++i)
    {
        mesh.indices[i * 3 + 0] = (unsigned short)0;
        mesh.indices[i * 3 + 1] = (unsigned short)(i + 1);
        mesh.indices[i * 3 + 2] = (unsigned short)(i + 2);
    }

    return mesh;
}

Mesh GenMeshPlane(float width, float length, int resX, int resZ)
{
    if (width <= 0.0f || length <= 0.0f || resX <= 0 || resZ <= 0)
        return Mesh{};
    int vertsX = resX + 1;
    int vertsZ = resZ + 1;
    int vertexCount = vertsX * vertsZ;
    int triangleCount = resX * resZ * 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    for (int z = 0; z < vertsZ; ++z)
    {
        for (int x = 0; x < vertsX; ++x)
        {
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
    for (int z = 0; z < resZ; ++z)
    {
        for (int x = 0; x < resX; ++x)
        {
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

Mesh GenMeshCube(float width, float height, float length)
{
    float hw = width * 0.5f;
    float hh = height * 0.5f;
    float hl = length * 0.5f;
    Mesh mesh = CreateMesh(24, 12);
    const Vec3 positions[24] = {
        {-hw, -hh, hl}, {hw, -hh, hl}, {hw, hh, hl}, {-hw, hh, hl}, {-hw, -hh, -hl}, {-hw, hh, -hl}, {hw, hh, -hl}, {hw, -hh, -hl}, {-hw, hh, -hl}, {-hw, hh, hl}, {hw, hh, hl}, {hw, hh, -hl}, {-hw, -hh, -hl}, {hw, -hh, -hl}, {hw, -hh, hl}, {-hw, -hh, hl}, {hw, -hh, -hl}, {hw, hh, -hl}, {hw, hh, hl}, {hw, -hh, hl}, {-hw, -hh, -hl}, {-hw, -hh, hl}, {-hw, hh, hl}, {-hw, hh, -hl}};
    const Vec3 normals[24] = {
        {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, -1, 0}, {0, -1, 0}, {0, -1, 0}, {0, -1, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}};
    const Vec2 uvs[24] = {
        {0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 24; ++i)
    {
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
        0, 1, 2, 0, 2, 3,
        4, 5, 6, 4, 6, 7,
        8, 9, 10, 8, 10, 11,
        12, 13, 14, 12, 14, 15,
        16, 17, 18, 16, 18, 19,
        20, 21, 22, 20, 22, 23};
    std::copy(std::begin(indices), std::end(indices), mesh.indices);
    return mesh;
}

Mesh GenMeshSphere(float radius, int rings, int slices)
{
    if (radius <= 0.0f || rings < 2 || slices < 3)
        return Mesh{};
    int vertexCount = (rings + 1) * (slices + 1);
    int triangleCount = rings * slices * 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    int v = 0;
    for (int r = 0; r <= rings; ++r)
    {
        float phi = PI * r / rings;
        for (int s = 0; s <= slices; ++s)
        {
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
    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < slices; ++s)
        {
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

Mesh GenMeshHemiSphere(float radius, int rings, int slices)
{
    if (radius <= 0.0f || rings < 1 || slices < 3)
        return Mesh{};
    int vertexCount = (rings + 1) * (slices + 1);
    int triangleCount = rings * slices;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    int v = 0;
    for (int r = 0; r <= rings; ++r)
    {
        float phi = 0.5f * PI * r / rings;
        for (int s = 0; s <= slices; ++s)
        {
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
    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < slices; ++s)
        {
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

Mesh GenMeshCylinder(float radius, float height, int slices)
{
    if (radius <= 0.0f || height <= 0.0f || slices < 3)
        return Mesh{};
    int vertexCount = (slices + 1) * 2 + 2;
    int triangleCount = slices * 4;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    int v = 0;
    for (int i = 0; i <= slices; ++i)
    {
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
    for (int i = 0; i < slices; ++i)
    {
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

Mesh GenMeshCone(float radius, float height, int slices)
{
    if (radius <= 0.0f || height <= 0.0f || slices < 3)
        return Mesh{};
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

    for (int i = 0; i < slices; ++i)
    {
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
    for (int i = 0; i < slices; ++i)
    {
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

Mesh GenMeshTorus(float radius, float size, int radSeg, int sides)
{
    if (radius <= 0.0f || size <= 0.0f || radSeg < 3 || sides < 3)
        return Mesh{};
    int vertexCount = radSeg * sides;
    int triangleCount = radSeg * sides * 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    int v = 0;
    for (int ring = 0; ring < radSeg; ++ring)
    {
        float u = 2.0f * PI * ring / radSeg;
        Vec3 center{std::cos(u) * radius, 0.0f, std::sin(u) * radius};
        Vec3 ringDir{-std::sin(u), 0.0f, std::cos(u)};
        Vec3 ringUp{0.0f, 1.0f, 0.0f};
        for (int side = 0; side < sides; ++side)
        {
            float vAngle = 2.0f * PI * side / sides;
            float cx = std::cos(vAngle) * size;
            float cy = std::sin(vAngle) * size;
            Vec3 position = center + ringDir * cx + ringUp * cy;
            Vec3 normal = Vec3{ringDir.x * cx + ringUp.x * cy,
                               ringDir.y * cx + ringUp.y * cy,
                               ringDir.z * cx + ringUp.z * cy}
                              .normalized();
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
    for (int ring = 0; ring < radSeg; ++ring)
    {
        for (int side = 0; side < sides; ++side)
        {
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

Mesh GenMeshKnot(float radius, float size, int radSeg, int sides)
{
    if (radius <= 0.0f || size <= 0.0f || radSeg < 3 || sides < 3)
        return Mesh{};
    int vertexCount = radSeg * sides;
    int triangleCount = radSeg * sides * 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    auto knotPos = [&](float t)
    {
        float x = (2.0f + std::cos(3.0f * t)) * std::cos(2.0f * t);
        float y = (2.0f + std::cos(3.0f * t)) * std::sin(2.0f * t);
        float z = std::sin(3.0f * t);
        return Vec3{x, y, z} * radius;
    };

    auto knotTangent = [&](float t)
    {
        float dx = -2.0f * std::sin(2.0f * t) - 3.0f * std::sin(3.0f * t) * std::cos(2.0f * t) - 2.0f * std::sin(2.0f * t) * std::cos(3.0f * t);
        float dy = 2.0f * std::cos(2.0f * t) + 3.0f * std::sin(3.0f * t) * std::sin(2.0f * t) + 2.0f * std::cos(2.0f * t) * std::cos(3.0f * t);
        float dz = 3.0f * std::cos(3.0f * t);
        return Vec3{dx, dy, dz}.normalized();
    };

    for (int i = 0; i < radSeg; ++i)
    {
        float u = 2.0f * PI * i / radSeg;
        Vec3 center = knotPos(u);
        Vec3 tangent = knotTangent(u);
        Vec3 normal = Vec3{-tangent.y, tangent.x, 0.0f}.normalized();
        Vec3 binormal = tangent.cross(normal).normalized();
        for (int j = 0; j < sides; ++j)
        {
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
    for (int i = 0; i < radSeg; ++i)
    {
        int nextRing = (i + 1) % radSeg;
        for (int j = 0; j < sides; ++j)
        {
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

Mesh GenMeshHeightmap(Image heightmap, Vec3 size)
{
    if (!heightmap.data || heightmap.width <= 0 || heightmap.height <= 0)
        return Mesh{};
    int width = heightmap.width;
    int height = heightmap.height;
    int vertexCount = width * height;
    int triangleCount = (width - 1) * (height - 1) * 2;
    Mesh mesh = CreateMesh(vertexCount, triangleCount);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
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
    for (int y = 0; y < height - 1; ++y)
    {
        for (int x = 0; x < width - 1; ++x)
        {
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

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
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

Mesh GenMeshCubicmap(Image cubicmap, Vec3 cubeSize)
{
    if (!cubicmap.data || cubicmap.width <= 0 || cubicmap.height <= 0)
        return Mesh{};
    int width = cubicmap.width;
    int height = cubicmap.height;
    std::vector<Mesh> vCubes;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            Color pixel = GetImageColor(cubicmap, x, y);
            if (pixel.a == 0)
                continue;
            Mesh cube = GenMeshCube(cubeSize.x, cubeSize.y, cubeSize.z);
            float px = ((float)x - width * 0.5f + 0.5f) * cubeSize.x;
            float pz = ((float)y - height * 0.5f + 0.5f) * cubeSize.z;
            for (int v = 0; v < cube.vertexCount; ++v)
            {
                cube.vertices[v * 3 + 0] += px;
                cube.vertices[v * 3 + 1] += cubeSize.y * 0.5f;
                cube.vertices[v * 3 + 2] += pz;
            }
            vCubes.push_back(std::move(cube));
        }
    }

    if (vCubes.empty())
        return Mesh{};
    int totalVerts = 0;
    int totalTris = 0;
    for (auto& cube : vCubes)
    {
        totalVerts += cube.vertexCount;
        totalTris += cube.triangleCount;
    }

    Mesh mesh = CreateMesh(totalVerts, totalTris);
    int vOffset = 0;
    int iOffset = 0;
    for (auto& cube : vCubes)
    {
        for (int v = 0; v < cube.vertexCount; ++v)
        {
            mesh.vertices[(vOffset + v) * 3 + 0] = cube.vertices[v * 3 + 0];
            mesh.vertices[(vOffset + v) * 3 + 1] = cube.vertices[v * 3 + 1];
            mesh.vertices[(vOffset + v) * 3 + 2] = cube.vertices[v * 3 + 2];
            mesh.normals[(vOffset + v) * 3 + 0] = cube.normals[v * 3 + 0];
            mesh.normals[(vOffset + v) * 3 + 1] = cube.normals[v * 3 + 1];
            mesh.normals[(vOffset + v) * 3 + 2] = cube.normals[v * 3 + 2];
            mesh.texcoords[(vOffset + v) * 2 + 0] = cube.texcoords[v * 2 + 0];
            mesh.texcoords[(vOffset + v) * 2 + 1] = cube.texcoords[v * 2 + 1];
        }
        for (int t = 0; t < cube.triangleCount * 3; ++t)
        {
            mesh.indices[iOffset + t] = (unsigned short)(cube.indices[t] + vOffset);
        }
        vOffset += cube.vertexCount;
        iOffset += cube.triangleCount * 3;
        FreeMeshCpuData(cube);
    }
    return mesh;
}

void DrawBoundingBox(BoundingBox box, Color color)
{
    Vec3 vertices[8] = {
        {box.min.x, box.min.y, box.min.z},
        {box.max.x, box.min.y, box.min.z},
        {box.max.x, box.max.y, box.min.z},
        {box.min.x, box.max.y, box.min.z},
        {box.min.x, box.min.y, box.max.z},
        {box.max.x, box.min.y, box.max.z},
        {box.max.x, box.max.y, box.max.z},
        {box.min.x, box.max.y, box.max.z}};

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

void DrawBillboard(const Camera3D& camera, Texture2D texture, Vec3 position, float scale, Color tint)
{
    Rectangle source{0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)};
    Vec2 size{static_cast<float>(texture.width) * scale, static_cast<float>(texture.height) * scale};
    DrawBillboardRec(camera, texture, source, position, size, tint);
}

void DrawBillboardRec(const Camera3D& camera, Texture2D texture, Rectangle source,
                      Vec3 position, Vec2 size, Color tint)
{
    DrawBillboardPro(camera, texture, source, position, camera.up, size, Vec2{0.5f, 0.5f}, 0.0f, tint);
}

void DrawBillboardPro(const Camera3D& camera, Texture2D texture, Rectangle source,
                      Vec3 position, Vec3 up, Vec2 size, Vec2 origin, float rotation, Color tint)
{
    Vec3 screenPos = GetWorldToScreen(position, camera);
    Vec3 screenUp = GetWorldToScreen(position + up, camera);
    float angle = std::atan2(screenUp.y - screenPos.y, screenUp.x - screenPos.x) * (180.0f / 3.14159265359f);
    angle += rotation;

    Rectangle dest{
        screenPos.x - size.x * origin.x,
        screenPos.y - size.y * origin.y,
        size.x,
        size.y};
    Vec2 originPixels{size.x * origin.x, size.y * origin.y};
    DrawTexturePro(texture, source, dest, originPixels, angle, tint);
}

Vec2 GetWorldToScreen2D(Vec2 position, Camera2D camera)
{
    float dx = position.x - camera.target.x;
    float dy = position.y - camera.target.y;
    float cosA = std::cos(camera.rotation * 3.14159265359f / 180.f);
    float sinA = std::sin(camera.rotation * 3.14159265359f / 180.f);
    return Vec2{
        camera.offset.x + (dx * cosA - dy * sinA) * camera.zoom,
        camera.offset.y + (dx * sinA + dy * cosA) * camera.zoom};
}

Vec2 GetScreenToWorld2D(Vec2 position, Camera2D camera)
{
    float cosA = std::cos(camera.rotation * 3.14159265359f / 180.f);
    float sinA = std::sin(camera.rotation * 3.14159265359f / 180.f);
    float scale = camera.zoom;
    float tx = camera.offset.x - camera.target.x * scale * cosA - camera.target.y * scale * sinA;
    float ty = camera.offset.y + camera.target.x * scale * sinA - camera.target.y * scale * cosA;
    float x = (position.x - tx) * scale * cosA - (position.y - ty) * scale * sinA;
    float y = (position.x - tx) * scale * sinA + (position.y - ty) * scale * cosA;
    return Vec2{camera.target.x + x / (scale * scale),
                camera.target.y + y / (scale * scale)};
}

Vec3 GetWorldToScreen(Vec3 position, Camera3D camera)
{
    Vec3 forward = (camera.target - camera.position).normalized();
    Vec3 right = forward.cross(camera.up).normalized();
    Vec3 up = right.cross(forward);
    Vec3 rel = position - camera.position;
    float cX = rel.dot(right), cY = rel.dot(up), cZ = rel.dot(forward);

    float sw = static_cast<float>(GetScreenWidth());
    float sh = static_cast<float>(GetScreenHeight());
    if (sw <= 0.f || sh <= 0.f)
        return Vec3{0.f, 0.f, cZ};

    float aspect = sw / sh;
    float fovRad = camera.fovy * 3.14159265359f / 180.f;
    float halfH = std::tan(fovRad * .5f);
    float halfW = halfH * aspect;

    float ndcX, ndcY;
    if (camera.projection == CAMERA_ORTHOGRAPHIC)
    {
        float os = camera.fovy > 0.f ? camera.fovy : 1.f;
        ndcX = cX / (os * aspect);
        ndcY = cY / os;
    }
    else
    {
        if (cZ == 0.f)
            cZ = 1e-6f;
        ndcX = cX / (cZ * halfW);
        ndcY = cY / (cZ * halfH);
    }
    return Vec3{(ndcX * .5f + .5f) * sw, (.5f - ndcY * .5f) * sh, cZ};
}

Ray GetScreenToWorldRay(Vec2 mouse, Camera3D camera)
{
    return GetScreenToWorldRayEx(mouse, camera, GetScreenWidth(), GetScreenHeight());
}

Ray GetScreenToWorldRayEx(Vec2 mouse, Camera3D camera, int width, int height)
{
    Ray ray;
    ray.position = camera.position;
    Vec3 forward = (camera.target - camera.position).normalized();
    Vec3 right = forward.cross(camera.up).normalized();
    Vec3 up = right.cross(forward);

    float sw = static_cast<float>(width);
    float sh = static_cast<float>(height);
    if (sw <= 0.f || sh <= 0.f)
        return ray;

    float aspect = sw / sh;
    float fovRad = camera.fovy * 3.14159265359f / 180.f;
    float fovH = 2.f * std::tan(fovRad / 2.f);
    float fovW = fovH * aspect;
    float x = (mouse.x / sw - .5f) * fovW;
    float y = (.5f - mouse.y / sh) * fovH;

    ray.direction = Vec3{
        forward.x + right.x * x + up.x * y,
        forward.y + right.y * x + up.y * y,
        forward.z + right.z * x + up.z * y}
                        .normalized();
    return ray;
}

Vec2 GetWorldToScreenEx(Vec3 position, Camera3D camera, int width, int height)
{
    Vec3 forward = (camera.target - camera.position).normalized();
    Vec3 right = forward.cross(camera.up).normalized();
    Vec3 up = right.cross(forward);
    Vec3 rel = position - camera.position;
    float cX = rel.dot(right), cY = rel.dot(up), cZ = rel.dot(forward);

    float sw = static_cast<float>(width);
    float sh = static_cast<float>(height);
    if (sw <= 0.f || sh <= 0.f)
        return Vec2{};

    float aspect = sw / sh;
    float fovRad = camera.fovy * 3.14159265359f / 180.f;
    float halfH = std::tan(fovRad * .5f);
    float halfW = halfH * aspect;

    float ndcX, ndcY;
    if (camera.projection == CAMERA_ORTHOGRAPHIC)
    {
        float os = camera.fovy > 0.f ? camera.fovy : 1.f;
        ndcX = cX / (os * aspect);
        ndcY = cY / os;
    }
    else
    {
        if (cZ == 0.f)
            cZ = 1e-6f;
        ndcX = cX / (cZ * halfW);
        ndcY = cY / (cZ * halfH);
    }
    return Vec2{(ndcX * .5f + .5f) * sw, (.5f - ndcY * .5f) * sh};
}

bool CheckCollisionRecs(Rectangle a, Rectangle b)
{
    return !(a.x + a.width < b.x || b.x + b.width < a.x ||
             a.y + a.height < b.y || b.y + b.height < a.y);
}

bool CheckCollisionCircles(Vec2 c1, float r1, Vec2 c2, float r2)
{
    float dx = c2.x - c1.x, dy = c2.y - c1.y;
    return std::sqrt(dx * dx + dy * dy) < (r1 + r2);
}

bool CheckCollisionPointRec(Vec2 point, Rectangle rect)
{
    return point.x >= rect.x && point.x <= rect.x + rect.width &&
           point.y >= rect.y && point.y <= rect.y + rect.height;
}

bool CheckCollisionPointCircle(Vec2 point, Vec2 center, float radius)
{
    float dx = point.x - center.x, dy = point.y - center.y;
    return std::sqrt(dx * dx + dy * dy) <= radius;
}

bool CheckCollisionCircleRec(Vec2 center, float radius, Rectangle rec)
{
    const float closestX = std::clamp(center.x, rec.x, rec.x + rec.width);
    const float closestY = std::clamp(center.y, rec.y, rec.y + rec.height);
    const float dx = center.x - closestX, dy = center.y - closestY;
    return (dx * dx + dy * dy) <= (radius * radius);
}

bool CheckCollisionCircleLine(Vec2 center, float radius, Vec2 p1, Vec2 p2)
{
    const float ldx = p2.x - p1.x, ldy = p2.y - p1.y;
    const float len2 = ldx * ldx + ldy * ldy;
    if (len2 <= 1e-9f)
    {
        const float ddx = center.x - p1.x, ddy = center.y - p1.y;
        return (ddx * ddx + ddy * ddy) <= (radius * radius);
    }
    float t = ((center.x - p1.x) * ldx + (center.y - p1.y) * ldy) / len2;
    t = std::clamp(t, 0.0f, 1.0f);
    const float px = p1.x + t * ldx, py = p1.y + t * ldy;
    const float distX = center.x - px, distY = center.y - py;
    return (distX * distX + distY * distY) <= (radius * radius);
}

bool CheckCollisionPointTriangle(Vec2 point, Vec2 p1, Vec2 p2, Vec2 p3)
{
    auto sign = [](Vec2 a, Vec2 b, Vec2 c)
    {
        return (a.x - c.x) * (b.y - c.y) - (b.x - c.x) * (a.y - c.y);
    };
    const float d1 = sign(point, p1, p2);
    const float d2 = sign(point, p2, p3);
    const float d3 = sign(point, p3, p1);
    const bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    const bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
    return !(hasNeg && hasPos);
}

bool CheckCollisionPointLine(Vec2 point, Vec2 p1, Vec2 p2, int threshold)
{
    const float ldx = p2.x - p1.x, ldy = p2.y - p1.y;
    const float len2 = ldx * ldx + ldy * ldy;
    if (len2 <= 1e-9f)
    {
        const float ddx = p1.x - point.x, ddy = p1.y - point.y;
        return std::sqrt(ddx * ddx + ddy * ddy) <= static_cast<float>(threshold);
    }
    float t = ((point.x - p1.x) * ldx + (point.y - p1.y) * ldy) / len2;
    t = std::clamp(t, 0.0f, 1.0f);
    const float px = p1.x + t * ldx, py = p1.y + t * ldy;
    const float ddx = px - point.x, ddy = py - point.y;
    return std::sqrt(ddx * ddx + ddy * ddy) <= static_cast<float>(threshold);
}

bool CheckCollisionPointPoly(Vec2 point, const Vec2* pPoints, int pointCount)
{
    if (!pPoints || pointCount < 3)
        return false;
    bool inside = false;
    int j = pointCount - 1;
    for (int i = 0; i < pointCount; ++i)
    {
        const Vec2 vi = pPoints[i];
        const Vec2 vj = pPoints[j];
        if (((vi.y > point.y) != (vj.y > point.y)) &&
            (point.x < (vj.x - vi.x) * (point.y - vi.y) / (vj.y - vi.y) + vi.x))
        {
            inside = !inside;
        }
        j = i;
    }
    return inside;
}

bool CheckCollisionLines(Vec2 startPos1, Vec2 endPos1, Vec2 startPos2, Vec2 endPos2, Vec2* pCollisionPoint)
{
    const float d1x = endPos1.x - startPos1.x, d1y = endPos1.y - startPos1.y;
    const float d2x = endPos2.x - startPos2.x, d2y = endPos2.y - startPos2.y;
    const float denom = d1x * d2y - d1y * d2x;
    if (std::fabs(denom) <= 1e-9f)
        return false;

    const float rx = startPos2.x - startPos1.x, ry = startPos2.y - startPos1.y;
    const float t = (rx * d2y - ry * d2x) / denom;
    const float u = (rx * d1y - ry * d1x) / denom;
    if (t < 0.0f || t > 1.0f || u < 0.0f || u > 1.0f)
        return false;

    if (pCollisionPoint)
        *pCollisionPoint = Vec2{startPos1.x + d1x * t, startPos1.y + d1y * t};
    return true;
}

Rectangle GetCollisionRec(Rectangle rec1, Rectangle rec2)
{
    Rectangle out{};
    const float x1 = std::max(rec1.x, rec2.x);
    const float y1 = std::max(rec1.y, rec2.y);
    const float x2 = std::min(rec1.x + rec1.width, rec2.x + rec2.width);
    const float y2 = std::min(rec1.y + rec1.height, rec2.y + rec2.height);
    if (x2 < x1 || y2 < y1)
        return out;
    out.x = x1;
    out.y = y1;
    out.width = x2 - x1;
    out.height = y2 - y1;
    return out;
}

bool CheckCollisionSpheres(Vec3 center1, float radius1, Vec3 center2, float radius2)
{
    const float dx = center2.x - center1.x;
    const float dy = center2.y - center1.y;
    const float dz = center2.z - center1.z;
    const float dist2 = dx * dx + dy * dy + dz * dz;
    const float r = radius1 + radius2;
    return dist2 <= (r * r);
}

bool CheckCollisionBoxes(BoundingBox box1, BoundingBox box2)
{
    return (box1.max.x >= box2.min.x && box2.max.x >= box1.min.x) &&
           (box1.max.y >= box2.min.y && box2.max.y >= box1.min.y) &&
           (box1.max.z >= box2.min.z && box2.max.z >= box1.min.z);
}

bool CheckCollisionBoxSphere(BoundingBox box, Vec3 center, float radius)
{
    const float closestX = std::clamp(center.x, box.min.x, box.max.x);
    const float closestY = std::clamp(center.y, box.min.y, box.max.y);
    const float closestZ = std::clamp(center.z, box.min.z, box.max.z);
    const float dx = center.x - closestX;
    const float dy = center.y - closestY;
    const float dz = center.z - closestZ;
    return (dx * dx + dy * dy + dz * dz) <= (radius * radius);
}

RayCollision GetRayCollisionSphere(Ray ray, Vec3 center, float radius)
{
    RayCollision result{};
    const Vec3 dir = ray.direction;
    const float a = dir.dot(dir);
    if (a <= 0.0f)
        return result;

    const Vec3 oc = ray.position - center;
    const float b = 2.0f * dir.dot(oc);
    const float c = oc.dot(oc) - radius * radius;
    const float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f)
        return result;

    const float t = (-b - std::sqrt(discriminant)) / (2.0f * a);
    if (t < 0.0f)
        return result;

    result.hit = true;
    result.distance = t;
    result.point = ray.position + dir * t;
    result.normal = (result.point - center).normalized();
    return result;
}

RayCollision GetRayCollisionMesh(Ray ray, Mesh mesh, Matrix transform)
{
    RayCollision result{};
    if (!mesh.vertices || mesh.vertexCount <= 0)
        return result;

    bool hitAny = false;
    float bestDistance = INFINITY;
    const bool indexed = (mesh.indices != nullptr) && (mesh.triangleCount > 0);

    const int triangleCount = mesh.triangleCount > 0 ? mesh.triangleCount : mesh.vertexCount / 3;
    for (int i = 0; i < triangleCount; ++i)
    {
        int v0, v1, v2;
        if (indexed)
        {
            v0 = mesh.indices[i * 3 + 0];
            v1 = mesh.indices[i * 3 + 1];
            v2 = mesh.indices[i * 3 + 2];
        }
        else
        {
            v0 = i * 3 + 0;
            v1 = i * 3 + 1;
            v2 = i * 3 + 2;
        }
        if (v0 < 0 || v1 < 0 || v2 < 0 || v0 >= mesh.vertexCount ||
            v1 >= mesh.vertexCount || v2 >= mesh.vertexCount)
            continue;

        const Vec3 a = transform * Vec3{mesh.vertices[v0 * 3 + 0], mesh.vertices[v0 * 3 + 1], mesh.vertices[v0 * 3 + 2]};
        const Vec3 b = transform * Vec3{mesh.vertices[v1 * 3 + 0], mesh.vertices[v1 * 3 + 1], mesh.vertices[v1 * 3 + 2]};
        const Vec3 c = transform * Vec3{mesh.vertices[v2 * 3 + 0], mesh.vertices[v2 * 3 + 1], mesh.vertices[v2 * 3 + 2]};

        const RayCollision tri = GetRayCollisionTriangle(ray, a, b, c);
        if (tri.hit && tri.distance < bestDistance)
        {
            bestDistance = tri.distance;
            result = tri;
            hitAny = true;
        }
    }

    return hitAny ? result : RayCollision{};
}

RayCollision GetRayCollisionQuad(Ray ray, Vec3 p1, Vec3 p2, Vec3 p3, Vec3 p4)
{
    const RayCollision tri1 = GetRayCollisionTriangle(ray, p1, p2, p3);
    const RayCollision tri2 = GetRayCollisionTriangle(ray, p1, p3, p4);
    if (tri1.hit && tri2.hit)
        return (tri1.distance < tri2.distance) ? tri1 : tri2;
    return tri1.hit ? tri1 : tri2;
}

Color Fade(Color color, float alpha)
{
    color.a = static_cast<unsigned char>(color.a * alpha);
    return color;
}
Color ColorAlpha(Color color, float alpha)
{
    color.a = static_cast<unsigned char>(255.f * alpha);
    return color;
}

Color ColorTint(Color color, Color tint)
{
    return Color{
        static_cast<unsigned char>((color.r / 255.f) * (tint.r / 255.f) * 255.f),
        static_cast<unsigned char>((color.g / 255.f) * (tint.g / 255.f) * 255.f),
        static_cast<unsigned char>((color.b / 255.f) * (tint.b / 255.f) * 255.f),
        color.a};
}

Color ColorBrightness(Color color, float factor)
{
    return Color{
        static_cast<unsigned char>(std::clamp(color.r * factor, 0.f, 255.f)),
        static_cast<unsigned char>(std::clamp(color.g * factor, 0.f, 255.f)),
        static_cast<unsigned char>(std::clamp(color.b * factor, 0.f, 255.f)),
        color.a};
}

Color ColorContrast(Color color, float contrast)
{
    auto channel = [&](unsigned char ch) -> unsigned char
    {
        float v = (ch / 255.f - .5f) * contrast + .5f;
        return static_cast<unsigned char>(std::clamp(v * 255.f, 0.f, 255.f));
    };
    return Color{channel(color.r), channel(color.g), channel(color.b), color.a};
}

Color GetColor(unsigned int hex)
{
    return Color{
        static_cast<unsigned char>((hex >> 16) & 0xFF),
        static_cast<unsigned char>((hex >> 8) & 0xFF),
        static_cast<unsigned char>(hex & 0xFF),
        255};
}

Color ColorFromNormalized(float r, float g, float b, float a)
{
    return Color{
        static_cast<unsigned char>(std::clamp(r, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(g, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(b, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(a, 0.0f, 1.0f) * 255.0f)};
}

bool ColorIsEqual(Color col1, Color col2)
{
    return (col1.r == col2.r) && (col1.g == col2.g) &&
           (col1.b == col2.b) && (col1.a == col2.a);
}

int ColorToInt(Color color)
{
    return (static_cast<int>(color.a) << 24) |
           (static_cast<int>(color.r) << 16) |
           (static_cast<int>(color.g) << 8) |
           static_cast<int>(color.b);
}

Vec4 ColorNormalize(Color color)
{
    return Vec4{
        color.r / 255.0f,
        color.g / 255.0f,
        color.b / 255.0f,
        color.a / 255.0f};
}

Vec3 ColorToHSV(Color color)
{
    float r = color.r / 255.0f;
    float g = color.g / 255.0f;
    float b = color.b / 255.0f;

    float max = std::max(std::max(r, g), b);
    float min = std::min(std::min(r, g), b);
    float delta = max - min;

    float hue = 0.0f;
    float saturation = (max > 0.0001f) ? (delta / max) : 0.0f;
    float value = max;

    if (delta > 0.0001f)
    {
        if (max == r)
            hue = (g - b) / delta + ((g < b) ? 6.0f : 0.0f);
        else if (max == g)
            hue = (b - r) / delta + 2.0f;
        else
            hue = (r - g) / delta + 4.0f;
        hue *= 60.0f;
    }
    return Vec3{hue, saturation, value};
}

Color ColorFromHSV(float hue, float saturation, float value)
{
    float h = hue;
    while (h < 0.0f)
        h += 360.0f;
    h = std::fmod(h, 360.0f) / 60.0f;
    int sector = static_cast<int>(h);
    float f = h - sector;
    float p = value * (1.0f - saturation);
    float q = value * (1.0f - saturation * f);
    float t = value * (1.0f - saturation * (1.0f - f));

    float r = 0.0f, g = 0.0f, b = 0.0f;
    switch (sector)
    {
    case 0:
        r = value;
        g = t;
        b = p;
        break;
    case 1:
        r = q;
        g = value;
        b = p;
        break;
    case 2:
        r = p;
        g = value;
        b = t;
        break;
    case 3:
        r = p;
        g = q;
        b = value;
        break;
    case 4:
        r = t;
        g = p;
        b = value;
        break;
    default:
        r = value;
        g = p;
        b = q;
        break;
    }
    return Color{
        static_cast<unsigned char>(std::clamp(r, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(g, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(b, 0.0f, 1.0f) * 255.0f),
        255};
}

Color ColorAlphaBlend(Color dst, Color src, Color tint)
{
    Color srcTint{
        static_cast<unsigned char>(static_cast<int>(src.r) * static_cast<int>(tint.r) / 255),
        static_cast<unsigned char>(static_cast<int>(src.g) * static_cast<int>(tint.g) / 255),
        static_cast<unsigned char>(static_cast<int>(src.b) * static_cast<int>(tint.b) / 255),
        static_cast<unsigned char>(static_cast<int>(src.a) * static_cast<int>(tint.a) / 255)};
    float alpha = static_cast<float>(
                      static_cast<int>(srcTint.a) + static_cast<int>(dst.a) * (255 - static_cast<int>(srcTint.a)) / 255) /
                  255.0f;
    float multiplier = (alpha <= 0.0f) ? 0.0f : (static_cast<float>(srcTint.a) / 255.0f / alpha);
    auto chan = [&](unsigned char d, unsigned char s) -> unsigned char
    {
        return static_cast<unsigned char>(std::clamp(
            static_cast<float>(d) * (1.0f - multiplier) + static_cast<float>(s) * multiplier, 0.0f, 255.0f));
    };
    return Color{
        chan(dst.r, srcTint.r),
        chan(dst.g, srcTint.g),
        chan(dst.b, srcTint.b),
        static_cast<unsigned char>(std::clamp(alpha * 255.0f, 0.0f, 255.0f))};
}

Color ColorLerp(Color color1, Color color2, float factor)
{
    return Color{
        static_cast<unsigned char>(std::clamp(color1.r + (color2.r - color1.r) * factor, 0.0f, 255.0f)),
        static_cast<unsigned char>(std::clamp(color1.g + (color2.g - color1.g) * factor, 0.0f, 255.0f)),
        static_cast<unsigned char>(std::clamp(color1.b + (color2.b - color1.b) * factor, 0.0f, 255.0f)),
        static_cast<unsigned char>(std::clamp(color1.a + (color2.a - color1.a) * factor, 0.0f, 255.0f))};
}

void WaitTime(double seconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(seconds * 1000.0)));
}

int GetRandomValue(int min, int max)
{
    if (min > max)
        std::swap(min, max);
    return min + (std::rand() % (max - min + 1));
}

int* LoadRandomSequence(unsigned int count, int min, int max)
{
    if (count == 0)
        return nullptr;
    if (min > max)
        std::swap(min, max);

    int* pSequence = static_cast<int*>(std::malloc(static_cast<size_t>(count) * sizeof(int)));
    if (!pSequence)
        return nullptr;

    for (unsigned int i = 0; i < count; ++i)
    {
        pSequence[i] = GetRandomValue(min, max);
    }

    return pSequence;
}

void UnloadRandomSequence(int* pSequence)
{
    std::free(pSequence);
}

void SetRandomSeed(unsigned int seed)
{
    std::srand(seed);
}

int GetGlyphIndex(Font font, int codepoint)
{
    if (!font.valid || !font.glyphs || font.glyphCount <= 0)
        return 0;
    for (int i = 0; i < font.glyphCount; ++i)
    {
        if (font.glyphs[i].value == codepoint)
            return i;
    }
    for (int i = 0; i < font.glyphCount; ++i)
    {
        if (font.glyphs[i].value == 63)
            return i;
    }
    return 0;
}

GlyphInfo GetGlyphInfo(Font font, int codepoint)
{
    if (!font.valid || !font.glyphs)
        return GlyphInfo{};
    return font.glyphs[GetGlyphIndex(font, codepoint)];
}

Rectangle GetGlyphAtlasRec(Font font, int codepoint)
{
    if (!font.valid || !font.recs)
        return Rectangle{};
    return font.recs[GetGlyphIndex(font, codepoint)];
}

Vec2 MeasureTextCodepoints(Font font, const int* pCodepoints, int length, float fontSize, float spacing)
{
    if (!pCodepoints || !font.valid || !font.glyphs || font.baseSize <= 0)
        return {};
    const float scale = fontSize / static_cast<float>(font.baseSize);
    const float lineHeight = static_cast<float>(std::max(gTextLineSpacing > 0 ? gTextLineSpacing : font.baseSize, 1)) * scale;
    float maxW = 0.0f, x = 0.0f;
    int lines = 1;
    for (int i = 0; i < length; ++i)
    {
        const int cp = pCodepoints[i];
        if (cp == '\n')
        {
            maxW = std::max(maxW, x);
            x = 0.0f;
            ++lines;
            continue;
        }
        const int index = GetGlyphIndex(font, cp);
        x += font.glyphs[index].advanceX * scale + spacing;
    }
    maxW = std::max(maxW, x);
    return Vec2{maxW, lineHeight * static_cast<float>(lines)};
}

void DrawTextCodepoint(Font font, int codepoint, Vec2 position, float fontSize, Color tint)
{
    if (!font.valid || !font.glyphs || !font.recs || font.texture.id == 0 || font.baseSize <= 0)
        return;
    const int index = GetGlyphIndex(font, codepoint);
    if (font.glyphs[index].image.data == nullptr)
        return;
    const float scale = fontSize / static_cast<float>(font.baseSize);
    const float w = static_cast<float>(font.glyphs[index].image.width) * scale;
    const float h = static_cast<float>(font.glyphs[index].image.height) * scale;
    ITexture it{font.texture.id, font.texture.width, font.texture.height, font.texture.mipmaps, font.texture.format, font.texture.valid};
    gRenderer.DrawTexturePro(it, font.recs[index], Rectangle{position.x, position.y, w, h}, Vec2{}, 0.0f, tint);
}

void DrawTextCodepoints(Font font, const int* pCodepoints, int codepointCount, Vec2 position, float fontSize, float spacing, Color tint)
{
    if (!pCodepoints || !font.valid || !font.glyphs || !font.recs || font.texture.id == 0 || font.baseSize <= 0)
        return;
    const float scale = fontSize / static_cast<float>(font.baseSize);
    const float lineHeight = static_cast<float>(std::max(gTextLineSpacing > 0 ? gTextLineSpacing : font.baseSize, 1)) * scale;
    ITexture it{font.texture.id, font.texture.width, font.texture.height, font.texture.mipmaps, font.texture.format, font.texture.valid};
    float x = 0.0f, y = 0.0f;
    for (int i = 0; i < codepointCount; ++i)
    {
        const int cp = pCodepoints[i];
        if (cp == '\n')
        {
            x = 0.0f;
            y += lineHeight;
            continue;
        }
        const int index = GetGlyphIndex(font, cp);
        if (font.glyphs[index].image.data != nullptr)
        {
            const float w = static_cast<float>(font.glyphs[index].image.width) * scale;
            const float h = static_cast<float>(font.glyphs[index].image.height) * scale;
            gRenderer.DrawTexturePro(it, font.recs[index], Rectangle{position.x + x, position.y + y, w, h}, Vec2{}, 0.0f, tint);
        }
        x += font.glyphs[index].advanceX * scale + spacing;
    }
}

void DrawTextPro(Font font, const char* pText, Vec2 position, Vec2 origin, float rotation, float fontSize, float spacing, Color tint)
{
    if (!pText || !font.valid || !font.glyphs || !font.recs || font.texture.id == 0 || font.baseSize <= 0)
        return;
    const float scale = fontSize / static_cast<float>(font.baseSize);
    const float cosR = std::cos(rotation * DEG2RAD);
    const float sinR = std::sin(rotation * DEG2RAD);
    ITexture it{font.texture.id, font.texture.width, font.texture.height, font.texture.mipmaps, font.texture.format, font.texture.valid};
    Vec2 textOffset{0, 0};

    const char* p = pText;
    while (*p != '\0')
    {
        const unsigned char lead = static_cast<unsigned char>(*p);
        int cp = 0, seq = 0;
        if ((lead & 0x80) == 0)
        {
            cp = lead;
            seq = 1;
        }
        else if ((lead & 0xE0) == 0xC0)
        {
            cp = lead & 0x1F;
            seq = 2;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            cp = lead & 0x0F;
            seq = 3;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            cp = lead & 0x07;
            seq = 4;
        }
        else
        {
            cp = lead;
            seq = 1;
        }
        for (int k = 1; k < seq && p[k] != '\0'; ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(p[k]) & 0x3F);

        const int index = GetGlyphIndex(font, cp);
        if (font.glyphs[index].image.data != nullptr)
        {
            Vec2 charPos{position.x + textOffset.x, position.y + textOffset.y};
            charPos.x -= origin.x * cosR - origin.y * sinR;
            charPos.y -= origin.x * sinR + origin.y * cosR;
            const float w = static_cast<float>(font.glyphs[index].image.width) * scale;
            const float h = static_cast<float>(font.glyphs[index].image.height) * scale;
            gRenderer.DrawTexturePro(it, font.recs[index],
                                     Rectangle{charPos.x - w / 2.0f, charPos.y - h / 2.0f, w, h},
                                     Vec2{w / 2.0f, h / 2.0f}, rotation, tint);
        }
        textOffset.x += font.glyphs[index].advanceX * scale + spacing;
        p += seq;
    }
}

void SetTextLineSpacing(int spacing)
{
    gTextLineSpacing = spacing;
}

void DrawFPS(int posX, int posY)
{
    const int fps = GetFPS();
    Color color = LIME;
    if (fps < 30)
        color = Color{230, 41, 55, 255}; // RED
    else if (fps < 50)
        color = Color{190, 33, 55, 255}; // MAROON
    else
        color = LIME;
    DrawText(TextFormat("%d FPS", fps), posX, posY, 20, color);
}

int GetCodepoint(const char* pText, int* pCodepointSize)
{
    if (pText == nullptr)
        return 0x3f;

    int cp = 0x3f;
    int size = 1;
    const unsigned char firstByte = static_cast<unsigned char>(*pText);

    int extra = 0;
    if ((firstByte & 0x80) == 0)
    {
        cp = firstByte;
    }
    else if ((firstByte & 0xE0) == 0xC0)
    {
        cp = firstByte & 0x1F;
        extra = 1;
    }
    else if ((firstByte & 0xF0) == 0xE0)
    {
        cp = firstByte & 0x0F;
        extra = 2;
    }
    else if ((firstByte & 0xF8) == 0xF0)
    {
        cp = firstByte & 0x07;
        extra = 3;
    }

    for (int i = 0; i < extra; ++i)
    {
        const unsigned char nextByte = static_cast<unsigned char>(pText[1 + i]);
        if ((nextByte & 0xC0) == 0x80)
        {
            cp = (cp << 6) + (nextByte & 0x3F);
            ++size;
        }
        else
        {
            cp = 0x3f;
            break;
        }
    }

    if (pCodepointSize != nullptr)
        *pCodepointSize = size;
    return cp;
}

int GetCodepointNext(const char* pText, int* pCodepointSize)
{
    if (pText == nullptr)
        return 0x3f;

    const unsigned char b0 = static_cast<unsigned char>(pText[0]);
    if ((b0 == 0xEF) && (static_cast<unsigned char>(pText[1]) == 0xBB) &&
        (static_cast<unsigned char>(pText[2]) == 0xBF))
        pText += 3;

    const unsigned char firstByte = static_cast<unsigned char>(*pText);
    int cp = 0x3f;
    int size = 0;
    int extra = 0;

    if ((firstByte & 0x80) == 0)
    {
        cp = firstByte;
        size = 1;
    }
    else if ((firstByte & 0xE0) == 0xC0)
    {
        cp = firstByte & 0x1F;
        size = 2;
        extra = 1;
    }
    else if ((firstByte & 0xF0) == 0xE0)
    {
        cp = firstByte & 0x0F;
        size = 3;
        extra = 2;
    }
    else if ((firstByte & 0xF8) == 0xF0)
    {
        cp = firstByte & 0x07;
        size = 4;
        extra = 3;
    }
    else
    {
        size = 1;
    }

    for (int i = 0; i < extra; ++i)
    {
        const unsigned char nextByte = static_cast<unsigned char>(pText[1 + i]);
        if ((nextByte & 0xC0) == 0x80)
        {
            cp = (cp << 6) + (nextByte & 0x3F);
        }
        else
        {
            cp = 0x3f;
            size = 1;
            break;
        }
    }

    if (pCodepointSize != nullptr)
        *pCodepointSize = size;
    return cp;
}

int GetCodepointPrevious(const char* pText, int* pCodepointSize)
{
    if (pText == nullptr)
        return 0x3f;

    int count = 1;
    while ((static_cast<unsigned char>(pText[-count]) & 0xC0) == 0x80)
        ++count;

    const char* pStart = pText - count;
    const unsigned char firstByte = static_cast<unsigned char>(*pStart);

    int charBytes = 0;
    if ((firstByte & 0x80) == 0)
        charBytes = 1;
    else if ((firstByte & 0xE0) == 0xC0)
        charBytes = 2;
    else if ((firstByte & 0xF0) == 0xE0)
        charBytes = 3;
    else if ((firstByte & 0xF8) == 0xF0)
        charBytes = 4;

    if (charBytes == count)
    {
        int cp = firstByte & (0xFF >> (charBytes + 1));
        bool valid = true;
        for (int i = 1; i < charBytes; ++i)
        {
            const unsigned char nextByte = static_cast<unsigned char>(pStart[i]);
            if ((nextByte & 0xC0) == 0x80)
            {
                cp = (cp << 6) + (nextByte & 0x3F);
            }
            else
            {
                valid = false;
                cp = 0x3f;
                break;
            }
        }
        if (pCodepointSize != nullptr)
            *pCodepointSize = valid ? charBytes : 1;
        return cp;
    }

    if (pCodepointSize != nullptr)
        *pCodepointSize = 1;
    return 0x3f;
}

const char* CodepointToUTF8(int codepoint, int* pUtf8Size)
{
    static thread_local char s_aUtf8[5] = {0};
    int size = 0;

    if (codepoint <= 0x7F)
    {
        s_aUtf8[0] = static_cast<char>(codepoint);
        size = 1;
    }
    else if (codepoint <= 0x7FF)
    {
        s_aUtf8[0] = static_cast<char>(0xC0 | (codepoint >> 6));
        s_aUtf8[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 2;
    }
    else if (codepoint <= 0xFFFF)
    {
        s_aUtf8[0] = static_cast<char>(0xE0 | (codepoint >> 12));
        s_aUtf8[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        s_aUtf8[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 3;
    }
    else if (codepoint <= 0x10FFFF)
    {
        s_aUtf8[0] = static_cast<char>(0xF0 | (codepoint >> 18));
        s_aUtf8[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        s_aUtf8[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        s_aUtf8[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        size = 4;
    }
    else
    {
        s_aUtf8[0] = '?';
        size = 1;
    }

    if (pUtf8Size != nullptr)
        *pUtf8Size = size;
    s_aUtf8[size] = 0;
    return s_aUtf8;
}

int GetCodepointCount(const char* pText)
{
    if (pText == nullptr)
        return 0;
    int length = 0;
    const char* pPtr = pText;
    while (*pPtr != '\0')
    {
        int size = 0;
        GetCodepoint(pPtr, &size);
        pPtr += size;
        ++length;
    }
    return length;
}

int* LoadCodepoints(const char* pText, int* pCount)
{
    if (pCount != nullptr)
        *pCount = 0;
    if (pText == nullptr)
        return nullptr;

    int length = 0;
    const char* pPtr = pText;
    while (*pPtr != '\0')
    {
        int size = 0;
        GetCodepoint(pPtr, &size);
        pPtr += size;
        ++length;
    }

    int* pCodepoints = static_cast<int*>(MemAlloc(static_cast<size_t>(length + 1) * sizeof(int)));
    if (pCodepoints == nullptr)
        return nullptr;

    int index = 0;
    pPtr = pText;
    while (*pPtr != '\0')
    {
        int size = 0;
        pCodepoints[index] = GetCodepoint(pPtr, &size);
        pPtr += size;
        ++index;
    }
    pCodepoints[length] = 0;

    if (pCount != nullptr)
        *pCount = length;
    return pCodepoints;
}

void UnloadCodepoints(int* pCodepoints)
{
    if (pCodepoints != nullptr)
        MemFree(pCodepoints);
}

char* LoadUTF8(const int* pCodepoints, int length)
{
    if (pCodepoints == nullptr || length <= 0)
        return nullptr;

    int total = 0;
    for (int i = 0; i < length; ++i)
    {
        int utf8Size = 0;
        CodepointToUTF8(pCodepoints[i], &utf8Size);
        total += utf8Size;
    }

    char* pText = static_cast<char*>(MemAlloc(static_cast<size_t>(total + 1)));
    if (pText == nullptr)
        return nullptr;

    int index = 0;
    for (int i = 0; i < length; ++i)
    {
        int utf8Size = 0;
        const char* pUtf8 = CodepointToUTF8(pCodepoints[i], &utf8Size);
        for (int j = 0; j < utf8Size; ++j)
            pText[index++] = pUtf8[j];
    }
    pText[index] = '\0';
    return pText;
}

void UnloadUTF8(char* pText)
{
    if (pText != nullptr)
        MemFree(pText);
}

namespace
{
    Transform AiMatrixToTransform(const aiMatrix4x4& m)
    {
        Transform result;
        aiVector3D t, s;
        aiQuaternion r;
        m.Decompose(s, r, t);
        result.translation = Vec3{t.x, t.y, t.z};
        result.rotation = Quaternion{r.x, r.y, r.z, r.w};
        result.scale = Vec3{s.x, s.y, s.z};
        return result;
    }

    Transform TransformLerp(const Transform& a, const Transform& b, float amount)
    {
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

    unsigned int CountSceneNodes(aiNode* pNode)
    {
        if (pNode == nullptr)
            return 0;
        unsigned int count = 1;
        for (unsigned int c = 0; c < pNode->mNumChildren; ++c)
        {
            count += CountSceneNodes(pNode->mChildren[c]);
        }
        return count;
    }

    void BuildSkeletonFromHierarchy(aiNode* pNode, ModelSkeleton& skeleton, int parent)
    {
        if (pNode == nullptr)
            return;

        const unsigned int id = skeleton.boneCount;
        const std::string nodeName = pNode->mName.length > 0 ? pNode->mName.C_Str() : "node";
        skeleton.bones[id].parent = parent;
        CopyFixedString(skeleton.bones[id].name, sizeof(skeleton.bones[id].name), nodeName.c_str());
        skeleton.bindPose[id] = AiMatrixToTransform(pNode->mTransformation);
        skeleton.boneCount++;

        for (unsigned int c = 0; c < pNode->mNumChildren; ++c)
        {
            BuildSkeletonFromHierarchy(pNode->mChildren[c], skeleton, static_cast<int>(id));
        }
    }

    std::vector<Transform> EvaluateAnimationPose(const ModelAnimation& anim, float frame)
    {
        std::vector<Transform> vLocals(anim.boneCount);
        if (anim.boneCount == 0 || anim.keyframeCount <= 0 || anim.keyframePoses == nullptr)
        {
            return vLocals;
        }
        const int frameCount = anim.keyframeCount;
        int f0 = static_cast<int>(frame) % frameCount;
        if (f0 < 0)
            f0 += frameCount;
        const int f1 = (f0 + 1) % frameCount;
        const float amount = Clamp(frame - std::floor(frame), 0.0f, 1.0f);
        for (unsigned int b = 0; b < anim.boneCount; ++b)
        {
            vLocals[b] = TransformLerp(anim.keyframePoses[f0][b], anim.keyframePoses[f1][b], amount);
        }
        return vLocals;
    }

    void ApplySkinningToMesh(Model& model, Mesh& mesh)
    {
        if (mesh.vertices == nullptr || mesh.vertexCount <= 0)
            return;
        if (mesh.boneIndices == nullptr || mesh.boneWeights == nullptr || model.boneMatrices == nullptr)
            return;
        if (model.skeleton.bones == nullptr || model.skeleton.boneCount == 0)
            return;

        if (mesh.animVertices == nullptr)
        {
            mesh.animVertices = new float[static_cast<size_t>(mesh.vertexCount) * 3u];
        }
        if (mesh.animNormals == nullptr)
        {
            mesh.animNormals = new float[static_cast<size_t>(mesh.vertexCount) * 3u];
        }
        if (mesh.bindVertices == nullptr)
        {
            mesh.bindVertices = new float[static_cast<size_t>(mesh.vertexCount) * 3u];
            std::copy(mesh.vertices, mesh.vertices + mesh.vertexCount * 3, mesh.bindVertices);
        }
        if (mesh.normals != nullptr && mesh.bindNormals == nullptr)
        {
            mesh.bindNormals = new float[static_cast<size_t>(mesh.vertexCount) * 3u];
            std::copy(mesh.normals, mesh.normals + mesh.vertexCount * 3, mesh.bindNormals);
        }

        for (int v = 0; v < mesh.vertexCount; ++v)
        {
            const int base = v * 3;
            Vec3 pos{0.0f, 0.0f, 0.0f};
            Vec3 normal{0.0f, 0.0f, 0.0f};
            float totalWeight = 0.0f;

            for (int slot = 0; slot < 4; ++slot)
            {
                const int boneIndex = static_cast<int>(mesh.boneIndices[static_cast<size_t>(v) * 4u + static_cast<size_t>(slot)]);
                const float weight = mesh.boneWeights[static_cast<size_t>(v) * 4u + static_cast<size_t>(slot)];
                if (boneIndex < 0 || boneIndex >= static_cast<int>(model.skeleton.boneCount) || weight <= 0.0f)
                {
                    continue;
                }

                const Mat4 boneMatrix = model.boneMatrices[static_cast<size_t>(boneIndex)] *
                                        ((mesh.boneOffsets != nullptr && boneIndex < mesh.boneOffsetCount)
                                             ? mesh.boneOffsets[boneIndex]
                                             : Mat4::identity());
                const Vec4 localPos{
                    mesh.bindVertices[base + 0],
                    mesh.bindVertices[base + 1],
                    mesh.bindVertices[base + 2],
                    1.0f};
                const Vec4 worldPos = boneMatrix * localPos;
                pos += Vec3{worldPos.x, worldPos.y, worldPos.z} * weight;

                Vec3 localNormal{0.0f, 0.0f, 0.0f};
                if (mesh.normals != nullptr)
                {
                    localNormal = Vec3{
                        mesh.bindNormals[base + 0],
                        mesh.bindNormals[base + 1],
                        mesh.bindNormals[base + 2]};
                }
                else
                {
                    localNormal = Vec3{0.0f, 1.0f, 0.0f};
                }

                const Vec4 worldNormal = boneMatrix * Vec4{localNormal.x, localNormal.y, localNormal.z, 0.0f};
                normal += Vec3{worldNormal.x, worldNormal.y, worldNormal.z} * weight;
                totalWeight += weight;
            }

            if (totalWeight > 0.0f)
            {
                const float invWeight = 1.0f / totalWeight;
                pos = pos * invWeight;
                normal = normal * invWeight;
                const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
                if (length > 0.0001f)
                {
                    normal = normal * (1.0f / length);
                }
            }
            else
            {
                pos = Vec3{mesh.bindVertices[base + 0], mesh.bindVertices[base + 1], mesh.bindVertices[base + 2]};
                if (mesh.normals != nullptr)
                {
                    normal = Vec3{mesh.bindNormals[base + 0], mesh.bindNormals[base + 1], mesh.bindNormals[base + 2]};
                }
                else
                {
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
            if (mesh.normals != nullptr)
            {
                mesh.normals[base + 0] = normal.x;
                mesh.normals[base + 1] = normal.y;
                mesh.normals[base + 2] = normal.z;
            }
        }
    }

    void ApplyPoseToModel(Model model, const ModelSkeleton& skel,
                          const std::vector<Transform>& vLocals,
                          const ModelAnimation& anim, int f0)
    {
        const unsigned int boneCount = static_cast<unsigned int>(vLocals.size());
        if (boneCount == 0)
            return;

        std::vector<Mat4> vGlobals(boneCount, Mat4::identity());
        for (unsigned int b = 0; b < boneCount; ++b)
        {
            Mat4 local = TransformToMatrix(vLocals[b].translation, vLocals[b].rotation, vLocals[b].scale);
            if (skel.bones != nullptr && skel.bones[b].parent >= 0)
            {
                vGlobals[b] = vGlobals[static_cast<size_t>(skel.bones[b].parent)] * local;
            }
            else
            {
                vGlobals[b] = local;
            }
        }

        if (model.boneMatrices != nullptr)
        {
            for (unsigned int b = 0; b < boneCount; ++b)
            {
                model.boneMatrices[b] = vGlobals[b];
            }
        }

        model.currentPose = (anim.keyframePoses != nullptr) ? anim.keyframePoses[f0] : nullptr;
    }

    void CollectSceneNodes(aiNode* pNode, std::vector<aiNode*>& vNodes)
    {
        if (pNode == nullptr)
            return;
        vNodes.push_back(pNode);
        for (unsigned int i = 0; i < pNode->mNumChildren; ++i)
        {
            CollectSceneNodes(pNode->mChildren[i], vNodes);
        }
    }
} // anonymous

void qcPopulateModelSkeleton(const aiScene* pScene, Model& model)
{
    if (pScene == nullptr || pScene->mRootNode == nullptr)
        return;
    const unsigned int nodeCount = CountSceneNodes(pScene->mRootNode);
    if (nodeCount == 0)
        return;

    ModelSkeleton skel{};
    skel.boneCount = 0;
    skel.bones = new BoneInfo[nodeCount];
    skel.bindPose = new Transform[nodeCount];
    BuildSkeletonFromHierarchy(pScene->mRootNode, skel, -1);

    model.skeleton = skel;
    delete[] model.boneMatrices;
    model.boneMatrices = (model.skeleton.boneCount > 0) ? new Matrix[model.skeleton.boneCount] : nullptr;
}

int qcFindSkeletonBoneIndex(const aiScene* pScene, const Model& model, const aiBone* pBone)
{
    if (pScene == nullptr || pBone == nullptr || model.skeleton.bones == nullptr)
        return -1;

    std::vector<aiNode*> vNodes;
    CollectSceneNodes(pScene->mRootNode, vNodes);
    if (pBone->mNode != nullptr)
    {
        for (size_t i = 0; i < vNodes.size() && i < model.skeleton.boneCount; ++i)
        {
            if (vNodes[i] == pBone->mNode)
                return static_cast<int>(i);
        }
    }

    const char* pBoneName = pBone->mName.C_Str();
    for (size_t i = 0; i < vNodes.size() && i < model.skeleton.boneCount; ++i)
    {
        if (vNodes[i]->mNumMeshes == 0 &&
            std::strncmp(model.skeleton.bones[i].name, pBoneName, sizeof(model.skeleton.bones[i].name)) == 0 &&
            std::strlen(pBoneName) < sizeof(model.skeleton.bones[i].name))
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void qcSetMeshBoneOffset(Mesh& mesh, unsigned int boneIndex, unsigned int boneCount,
                         const aiMatrix4x4& offset)
{
    if (boneIndex >= boneCount || boneCount == 0)
        return;

    if (mesh.boneOffsets == nullptr || mesh.boneOffsetCount != static_cast<int>(boneCount))
    {
        delete[] mesh.boneOffsets;
        mesh.boneOffsets = new Matrix[boneCount];
        mesh.boneOffsetCount = static_cast<int>(boneCount);
        for (unsigned int i = 0; i < boneCount; ++i)
        {
            mesh.boneOffsets[i] = Mat4::identity();
        }
    }

    Matrix& matrix = mesh.boneOffsets[boneIndex];
    matrix.m[0] = offset.a1;
    matrix.m[4] = offset.a2;
    matrix.m[8] = offset.a3;
    matrix.m[12] = offset.a4;
    matrix.m[1] = offset.b1;
    matrix.m[5] = offset.b2;
    matrix.m[9] = offset.b3;
    matrix.m[13] = offset.b4;
    matrix.m[2] = offset.c1;
    matrix.m[6] = offset.c2;
    matrix.m[10] = offset.c3;
    matrix.m[14] = offset.c4;
    matrix.m[3] = offset.d1;
    matrix.m[7] = offset.d2;
    matrix.m[11] = offset.d3;
    matrix.m[15] = offset.d4;
}

void qcFreeModelSkeleton(Model& model)
{
    delete[] model.skeleton.bones;
    model.skeleton.bones = nullptr;
    delete[] model.skeleton.bindPose;
    model.skeleton.bindPose = nullptr;
    model.skeleton.boneCount = 0;
    model.currentPose = nullptr;
    delete[] model.boneMatrices;
    model.boneMatrices = nullptr;
}

static double AnimSampleTime(double startTime, double endTime, int frame, int totalFrames)
{
    const double span = (endTime > startTime) ? (endTime - startTime) : 1.0;
    const double t = static_cast<double>(frame) / static_cast<double>(std::max(1, totalFrames));
    return startTime + span * t;
}

static aiVector3D InterpolateVectorKeys(const aiVectorKey* pKeys, unsigned int keyCount, double time)
{
    if (keyCount == 1)
        return pKeys[0].mValue;

    for (unsigned int k = 0; k + 1 < keyCount; ++k)
    {
        const double t0 = pKeys[k].mTime;
        const double t1 = pKeys[k + 1].mTime;
        const bool isLastSegment = (k + 1 == keyCount - 1);

        if (time >= t0 && (time <= t1 || isLastSegment))
        {
            const double weight = (t1 > t0) ? ((time - t0) / (t1 - t0)) : 0.0;
            const aiVector3D& a = pKeys[k].mValue;
            const aiVector3D& b = pKeys[k + 1].mValue;
            return a + (b - a) * static_cast<float>(weight);
        }
    }
    return pKeys[0].mValue;
}

static aiQuaternion InterpolateRotationKeys(const aiQuatKey* pKeys, unsigned int keyCount, double time)
{
    if (keyCount == 1)
        return pKeys[0].mValue;

    for (unsigned int k = 0; k + 1 < keyCount; ++k)
    {
        const double t0 = pKeys[k].mTime;
        const double t1 = pKeys[k + 1].mTime;
        const bool isLastSegment = (k + 1 == keyCount - 1);

        if (time >= t0 && (time <= t1 || isLastSegment))
        {
            const double weight = (t1 > t0) ? ((time - t0) / (t1 - t0)) : 0.0;
            aiQuaternion result;
            aiQuaternion::Interpolate(result, pKeys[k].mValue, pKeys[k + 1].mValue, static_cast<float>(weight));
            result.Normalize();
            return result;
        }
    }
    return pKeys[0].mValue;
}

static void SampleBoneChannel(const aiNodeAnim* pChannel, int frame, int totalFrames, Transform& outPose)
{
    if (pChannel->mNumPositionKeys > 0)
    {
        const double time = AnimSampleTime(
            pChannel->mPositionKeys[0].mTime,
            pChannel->mPositionKeys[pChannel->mNumPositionKeys - 1].mTime,
            frame, totalFrames);
        const aiVector3D pos = InterpolateVectorKeys(pChannel->mPositionKeys, pChannel->mNumPositionKeys, time);
        outPose.translation = Vec3{pos.x, pos.y, pos.z};
    }

    if (pChannel->mNumRotationKeys > 0)
    {
        const double time = AnimSampleTime(
            pChannel->mRotationKeys[0].mTime,
            pChannel->mRotationKeys[pChannel->mNumRotationKeys - 1].mTime,
            frame, totalFrames);
        const aiQuaternion rot = InterpolateRotationKeys(pChannel->mRotationKeys, pChannel->mNumRotationKeys, time);
        outPose.rotation = Quaternion{rot.x, rot.y, rot.z, rot.w};
    }

    if (pChannel->mNumScalingKeys > 0)
    {
        const double time = AnimSampleTime(
            pChannel->mScalingKeys[0].mTime,
            pChannel->mScalingKeys[pChannel->mNumScalingKeys - 1].mTime,
            frame, totalFrames);
        const aiVector3D scl = InterpolateVectorKeys(pChannel->mScalingKeys, pChannel->mNumScalingKeys, time);
        outPose.scale = Vec3{scl.x, scl.y, scl.z};
    }
}

static std::vector<const aiNodeAnim*> MapChannelsToBones(const aiAnimation* pAnim,
                                                         const ModelSkeleton& skel,
                                                         aiNode* pRoot)
{
    std::vector<const aiNodeAnim*> vChannels(skel.boneCount, nullptr);
    std::vector<aiNode*> vNodes;
    CollectSceneNodes(pRoot, vNodes);

    for (unsigned int c = 0; c < pAnim->mNumChannels; ++c)
    {
        const aiNodeAnim* pNodeAnim = pAnim->mChannels[c];
        const std::string boneName = pNodeAnim->mNodeName.C_Str();

        int match = -1;
        for (size_t b = 0; b < vNodes.size(); ++b)
        {
            if (boneName != vNodes[b]->mName.C_Str())
                continue;
            if (match < 0)
                match = static_cast<int>(b);
            if (vNodes[b]->mNumMeshes == 0)
            {
                match = static_cast<int>(b);
                break;
            }
        }
        if (match >= 0 && static_cast<unsigned int>(match) < skel.boneCount)
        {
            vChannels[static_cast<size_t>(match)] = pNodeAnim;
        }
    }
    return vChannels;
}

static unsigned int ComputeKeyframeCount(const std::vector<const aiNodeAnim*>& vChannels)
{
    unsigned int frames = 0;
    for (const aiNodeAnim* channel : vChannels)
    {
        if (channel == nullptr)
            continue;
        frames = std::max(frames, channel->mNumPositionKeys);
        frames = std::max(frames, channel->mNumRotationKeys);
        frames = std::max(frames, channel->mNumScalingKeys);
    }
    return frames;
}

static void BuildKeyframePoses(const ModelSkeleton& skel, const std::vector<const aiNodeAnim*>& vChannels,
                               ModelAnimation& dst)
{
    dst.keyframePoses = new ModelAnimPose[dst.keyframeCount];
    for (int f = 0; f < dst.keyframeCount; ++f)
    {
        dst.keyframePoses[f] = new Transform[dst.boneCount];
        for (unsigned int b = 0; b < dst.boneCount; ++b)
        {
            dst.keyframePoses[f][b] = skel.bindPose[b];
        }
    }

    for (unsigned int b = 0; b < skel.boneCount; ++b)
    {
        const aiNodeAnim* pChannel = vChannels[b];
        if (pChannel == nullptr)
            continue;

        for (int f = 0; f < dst.keyframeCount; ++f)
        {
            SampleBoneChannel(pChannel, f, dst.keyframeCount, dst.keyframePoses[f][b]);
        }
    }
}

ModelAnimation* LoadModelAnimations(const char* pFileName, int* pAnimCount)
{
    if (pAnimCount != nullptr)
        *pAnimCount = 0;
    if (pFileName == nullptr || *pFileName == '\0')
        return nullptr;

    Assimp::Importer importer;
    const aiScene* pScene = importer.ReadFile(
        pFileName,
        aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_PopulateArmatureData);
    if (!pScene || (pScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || !pScene->mRootNode)
    {
        TraceLog(LogLevel::Error, "MODEL", TextFormat("[Anim] Failed to load animations from %s: %s", pFileName, importer.GetErrorString()));
        return nullptr;
    }

    if (pScene->mNumAnimations == 0)
    {
        TraceLog(LogLevel::Warn, "MODEL", TextFormat("[Anim] No animations found in %s", pFileName));
        return nullptr;
    }

    ModelSkeleton skel{};
    const unsigned int nodeCount = CountSceneNodes(pScene->mRootNode);
    skel.bones = new BoneInfo[nodeCount];
    skel.bindPose = new Transform[nodeCount];
    BuildSkeletonFromHierarchy(pScene->mRootNode, skel, -1);

    const int count = static_cast<int>(pScene->mNumAnimations);
    ModelAnimation* pResult = new ModelAnimation[count];

    for (unsigned int a = 0; a < pScene->mNumAnimations; ++a)
    {
        const aiAnimation* pAnim = pScene->mAnimations[a];
        ModelAnimation& dst = pResult[a];
        dst.boneCount = skel.boneCount;
        dst.keyframeCount = 0;

        if (pAnim->mName.length > 0)
        {
            CopyFixedString(dst.name, sizeof(dst.name), pAnim->mName.C_Str());
        }

        const std::vector<const aiNodeAnim*> vChannels = MapChannelsToBones(pAnim, skel, pScene->mRootNode);
        const unsigned int frames = ComputeKeyframeCount(vChannels);
        if (frames == 0)
            continue;

        dst.keyframeCount = static_cast<int>(frames);
        BuildKeyframePoses(skel, vChannels, dst);
    }

    delete[] skel.bones;
    delete[] skel.bindPose;

    if (pAnimCount != nullptr)
        *pAnimCount = count;
    return pResult;
}

void UnloadModelAnimations(ModelAnimation* pAnimations, int animCount)
{
    if (pAnimations == nullptr)
        return;
    for (int a = 0; a < animCount; ++a)
    {
        ModelAnimation& anim = pAnimations[a];
        if (anim.keyframePoses)
        {
            for (int f = 0; f < anim.keyframeCount; ++f)
            {
                delete[] anim.keyframePoses[f];
            }
            delete[] anim.keyframePoses;
            anim.keyframePoses = nullptr;
        }
        anim.keyframeCount = 0;
    }
    delete[] pAnimations;
}

bool IsModelAnimationValid(Model model, ModelAnimation anim)
{
    if (model.skeleton.bones == nullptr || anim.boneCount != model.skeleton.boneCount)
        return false;
    if (anim.keyframePoses == nullptr || anim.keyframeCount <= 0)
        return false;
    return true;
}

static int WrapFrameIndex(float frame, int keyframeCount)
{
    int wrapped = static_cast<int>(frame) % keyframeCount;
    if (wrapped < 0)
        wrapped += keyframeCount;
    return wrapped;
}

static void ApplySkinningToAllMeshes(Model& model)
{
    if (model.meshes == nullptr)
        return;
    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex)
    {
        Mesh& mesh = model.meshes[meshIndex];
        ApplySkinningToMesh(model, mesh);
        if (mesh.vaoId != 0)
            UpdateMeshBuffer(mesh, 0, mesh.vertices,
                             mesh.vertexCount * 3 * static_cast<int>(sizeof(float)), 0);
    }
}

void UpdateModelAnimation(Model model, ModelAnimation anim, float frame)
{
    if (anim.boneCount == 0 || anim.keyframeCount <= 0 || anim.keyframePoses == nullptr)
        return;

    std::vector<Transform> vLocals = EvaluateAnimationPose(anim, frame);
    const int f0 = WrapFrameIndex(frame, anim.keyframeCount);
    ApplyPoseToModel(model, model.skeleton, vLocals, anim, f0);
    ApplySkinningToAllMeshes(model);
}

void UpdateModelAnimationEx(Model model, ModelAnimation animA, float frameA,
                            ModelAnimation animB, float frameB, float blend)
{
    if (animA.boneCount == 0 || animA.keyframeCount <= 0 || animA.keyframePoses == nullptr)
        return;

    const float b = Clamp(blend, 0.0f, 1.0f);
    std::vector<Transform> vLocals = EvaluateAnimationPose(animA, frameA);

    const bool canBlendWithB = (animB.boneCount == animA.boneCount) && animB.keyframeCount > 0 && animB.keyframePoses != nullptr && b > 0.0f;
    if (canBlendWithB)
    {
        std::vector<Transform> vLocalsB = EvaluateAnimationPose(animB, frameB);
        for (unsigned int i = 0; i < vLocals.size(); ++i)
        {
            vLocals[i] = TransformLerp(vLocals[i], vLocalsB[i], b);
        }
    }

    const int f0 = WrapFrameIndex(frameA, animA.keyframeCount);
    ApplyPoseToModel(model, model.skeleton, vLocals, animA, f0);
    ApplySkinningToAllMeshes(model);
}
