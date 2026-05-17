cmake_minimum_required(VERSION 3.25)
project(QuarkCore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QC_BUILD_EXAMPLES "Build QuarkCore examples" ON)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)

set(VCPKG_MANIFEST_INSTALL        OFF CACHE BOOL "" FORCE)
set(VCPKG_APPLOCAL_DEPS           OFF CACHE BOOL "" FORCE)
set(X_VCPKG_APPLOCAL_DEPS_INSTALL OFF CACHE BOOL "" FORCE)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

set(VULKAN_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/Vulkan")

add_library(Vulkan::Vulkan INTERFACE IMPORTED GLOBAL)

target_include_directories(Vulkan::Vulkan INTERFACE
    "${VULKAN_EXTERNAL_DIR}/include"
)

if(WIN32)
    set(VULKAN_LIB_PATH "${VULKAN_EXTERNAL_DIR}/lib/vulkan-1.lib")
    set(VULKAN_DLL_PATH "${VULKAN_EXTERNAL_DIR}/lib/vulkan-1.dll")

    if(NOT EXISTS "${VULKAN_LIB_PATH}")
        message(FATAL_ERROR
            "Vulkan import library not found: ${VULKAN_LIB_PATH}\n"
            "Place vulkan-1.lib in external/Vulkan/lib/")
    endif()
    if(NOT EXISTS "${VULKAN_DLL_PATH}")
        message(FATAL_ERROR
            "Vulkan DLL not found: ${VULKAN_DLL_PATH}\n"
            "Place vulkan-1.dll in external/Vulkan/lib/")
    endif()

    target_link_libraries(Vulkan::Vulkan INTERFACE "${VULKAN_LIB_PATH}")

    set(VULKAN_RUNTIME_DLL "${VULKAN_DLL_PATH}" CACHE FILEPATH "" FORCE)
else()
    find_library(VULKAN_LOADER_LIB
        NAMES vulkan vulkan-1
        HINTS "${VULKAN_EXTERNAL_DIR}/lib"
    )
    if(VULKAN_LOADER_LIB)
        target_link_libraries(Vulkan::Vulkan INTERFACE "${VULKAN_LOADER_LIB}")
    else()
        target_link_libraries(Vulkan::Vulkan INTERFACE -lvulkan)
    endif()
endif()

FetchContent_Declare(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG        v1.3.2
    GIT_SHALLOW    TRUE
    SYSTEM
)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib)

if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

set(ZLIB_FOUND        TRUE                                    CACHE BOOL   "" FORCE)
set(ZLIB_INCLUDE_DIR  "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}" CACHE STRING "" FORCE)
set(ZLIB_LIBRARY      zlibstatic                              CACHE STRING "" FORCE)
set(ZLIB_INCLUDE_DIRS "${ZLIB_INCLUDE_DIR}"                   CACHE STRING "" FORCE)
set(ZLIB_LIBRARIES    "${ZLIB_LIBRARY}"                       CACHE STRING "" FORCE)
set(ZLIB_VERSION_STRING "1.3.1"                               CACHE STRING "" FORCE)

list(APPEND VCPKG_MANIFEST_FEATURES "")
set(VCPKG_INSTALLED_DIR "" CACHE PATH "" FORCE)

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.8
    GIT_SHALLOW    TRUE
    SYSTEM
)
set(SDL_SHARED ON  CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glew
    GIT_REPOSITORY https://github.com/Perlmint/glew-cmake.git
    GIT_TAG        glew-cmake-2.3.1
    GIT_SHALLOW    TRUE
    SYSTEM
)
set(glew-cmake_BUILD_SHARED ON  CACHE BOOL "" FORCE)
set(glew-cmake_BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(ONLY_LIBS               ON  CACHE BOOL "" FORCE)

FetchContent_Declare(assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG        v6.0.5
    GIT_SHALLOW    TRUE
    SYSTEM
)
set(ASSIMP_BUILD_TESTS        OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL            OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB         OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS         ON  CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SDL3 glew assimp)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(libpng
    GIT_REPOSITORY https://github.com/glennrp/libpng.git
    GIT_TAG        v1.6.58
    GIT_SHALLOW    TRUE
    SYSTEM
)
set(PNG_SHARED     ON  CACHE BOOL "" FORCE)
set(PNG_STATIC     OFF CACHE BOOL "" FORCE)
set(PNG_TESTS      OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS      OFF CACHE BOOL "" FORCE)
set(PNG_BUILD_ZLIB OFF CACHE BOOL "" FORCE)

set(_CMAKE_FIND_PACKAGE_REDIRECTS_DIR "" CACHE PATH "" FORCE)
set(CMAKE_SKIP_INSTALL_RULES TRUE)
FetchContent_MakeAvailable(libpng)
set(CMAKE_SKIP_INSTALL_RULES FALSE)

if(TARGET png_shared AND TARGET zlibstatic)
    target_include_directories(png_shared PRIVATE
        "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}"
    )
    target_link_libraries(png_shared PRIVATE zlibstatic)
endif()

find_package(OpenGL REQUIRED)

FetchContent_Declare(freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG        VER-2-14-3
    GIT_SHALLOW    TRUE
    SYSTEM
)
set(FT_DISABLE_ZLIB     ON  CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    ON  CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON  CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON  CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON  CACHE BOOL "" FORCE)
set(FT_REQUIRE_ZLIB     OFF CACHE BOOL "" FORCE)
set(FT_REQUIRE_PNG      OFF CACHE BOOL "" FORCE)
set(CMAKE_SKIP_INSTALL_RULES TRUE)
FetchContent_MakeAvailable(freetype)
set(CMAKE_SKIP_INSTALL_RULES FALSE)

foreach(t SDL3 SDL_uclibc libglew_shared png16 png_shared zlib zlibstatic assimp freetype)
    if(TARGET ${t})
        target_compile_options(${t} PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)
    endif()
endforeach()

add_library(QuarkCore SHARED
    src/QuarkCore.cpp
    src/QuarkEvent.cpp
    src/QuarkLights.cpp
    src/QuarkWindow.cpp
    imgui/qcImGui.cpp
    src/Renderer/QuarkGLRenderer.cpp
    src/Renderer/QuarkVulkan/QuarkVkRenderer.cpp
    src/Renderer/QuarkVulkan/QuarkVkDraw2D.cpp
    src/Renderer/QuarkVulkan/QuarkVkDraw3D.cpp
    src/Renderer/QuarkVulkan/QuarkVkShader.cpp
    src/Renderer/QuarkVulkan/QuarkVkTexture.cpp
    src/Renderer/QuarkVulkan/QuarkVkModel.cpp
    src/Renderer/QuarkVulkan/QuarkVkFont.cpp
)

target_include_directories(QuarkCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
)

target_compile_definitions(QuarkCore PRIVATE QUARKCORE_BUILD_DLL)

target_link_libraries(QuarkCore
    PUBLIC
    SDL3::SDL3
    libglew_shared
    png_shared
    OpenGL::GL
    assimp::assimp
    freetype
    Vulkan::Vulkan
    imgui
)

target_compile_definitions(QuarkCore PRIVATE NOMINMAX)
set_target_properties(QuarkCore PROPERTIES UNITY_BUILD ON)

if(MSVC)
    target_compile_options(QuarkCore PRIVATE /W4 /permissive- /MP)
else()
    target_compile_options(QuarkCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(QC_BUILD_EXAMPLES)
    add_executable(quarkcore_sandbox examples/sandbox.cpp)
    target_link_libraries(quarkcore_sandbox PRIVATE QuarkCore)
    set_target_properties(quarkcore_sandbox PROPERTIES UNITY_BUILD ON)

    add_executable(quarkcore_textures_and_shaders examples/textures_and_shaders.cpp)
    target_link_libraries(quarkcore_textures_and_shaders PRIVATE QuarkCore)
    set_target_properties(quarkcore_textures_and_shaders PROPERTIES UNITY_BUILD ON)

    add_executable(quarkcore_3d_primitives examples/primitives_3d.cpp)
    target_link_libraries(quarkcore_3d_primitives PRIVATE QuarkCore)
    set_target_properties(quarkcore_3d_primitives PROPERTIES UNITY_BUILD ON)

    add_executable(quarkcore_meshes examples/meshes.cpp)
    target_link_libraries(quarkcore_meshes PRIVATE QuarkCore)
    set_target_properties(quarkcore_meshes PROPERTIES UNITY_BUILD ON)

    add_executable(quarkcore_vulkan examples/vulkan.cpp)
    target_link_libraries(quarkcore_vulkan PRIVATE QuarkCore)
    set_target_properties(quarkcore_vulkan PROPERTIES UNITY_BUILD ON)

    add_executable(quarkcore_lights examples/light.cpp)
    target_link_libraries(quarkcore_lights PRIVATE QuarkCore)
    set_target_properties(quarkcore_lights PROPERTIES UNITY_BUILD ON)

    add_executable(quarkcore_files examples/file_operations.cpp)
    target_link_libraries(quarkcore_files PRIVATE QuarkCore)
    set_target_properties(quarkcore_files PROPERTIES UNITY_BUILD ON)

    add_executable(quarkcore_3d_lights examples/3d_light.cpp)
    target_link_libraries(quarkcore_3d_lights PRIVATE QuarkCore)
    set_target_properties(quarkcore_3d_lights PROPERTIES UNITY_BUILD ON)

    if(WIN32)
        add_custom_command(TARGET QuarkCore POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3>
                $<TARGET_FILE:libglew_shared>
                $<TARGET_FILE:png_shared>
                $<TARGET_FILE:ZLIB::ZLIB>
                $<TARGET_FILE:assimp::assimp>
                "${VULKAN_RUNTIME_DLL}"
                $<TARGET_FILE_DIR:QuarkCore>
            COMMENT "Copying DLLs next to QuarkCore..."
        )

        set(EXAMPLE_TARGETS
            quarkcore_sandbox
            quarkcore_textures_and_shaders
            quarkcore_3d_primitives
            quarkcore_meshes
            quarkcore_vulkan
            quarkcore_lights
            quarkcore_files
            quarkcore_3d_lights
        )
        foreach(target ${EXAMPLE_TARGETS})
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:QuarkCore>
                    $<TARGET_FILE:SDL3::SDL3>
                    $<TARGET_FILE:libglew_shared>
                    $<TARGET_FILE:png_shared>
                    $<TARGET_FILE:ZLIB::ZLIB>
                    $<TARGET_FILE:assimp::assimp>
                    "${VULKAN_RUNTIME_DLL}"
                    $<TARGET_FILE_DIR:${target}>
                COMMENT "Copying DLLs for ${target}..."
            )
        endforeach()
    endif()
endif()
