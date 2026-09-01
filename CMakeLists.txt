# ========================================================
#
#   QuarkCore CMake Configuration
#   By Quark Engine Development Team
#
# ========================================================

cmake_minimum_required(VERSION 3.16)
project(QuarkCore VERSION 0.1.0 LANGUAGES CXX C)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QC_BUILD_EXAMPLES "Build QuarkCore examples" ON)

set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

function(qc_lib_dir OUT_VAR LIB_NAME)
    set(BASE "${CMAKE_CURRENT_SOURCE_DIR}/external/${LIB_NAME}/lib")
    set(${OUT_VAR}_DEBUG   "${BASE}/debug"   PARENT_SCOPE)
    set(${OUT_VAR}_RELEASE "${BASE}/release" PARENT_SCOPE)
    set(${OUT_VAR} "$<IF:$<CONFIG:Debug>,${BASE}/debug,${BASE}/release>" PARENT_SCOPE)
endfunction()

set(VULKAN_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/Vulkan")

set(VULKAN_EXTRA_INCLUDES
    "${VULKAN_EXTERNAL_DIR}/include"
    "${VULKAN_EXTERNAL_DIR}/include/vulkan"
    "${VULKAN_EXTERNAL_DIR}/include/shaderc"
    "${VULKAN_EXTERNAL_DIR}/include/spirv_cross"
    "${VULKAN_EXTERNAL_DIR}/include/spirv-headers"
    "${VULKAN_EXTERNAL_DIR}/include/spirv-tools"
    "${VULKAN_EXTERNAL_DIR}/include/glslang"
)

if(WIN32)
    add_library(Vulkan::Vulkan INTERFACE IMPORTED GLOBAL)
    target_include_directories(Vulkan::Vulkan INTERFACE ${VULKAN_EXTRA_INCLUDES})

    set(VULKAN_LIB_PATH "${VULKAN_EXTERNAL_DIR}/lib/vulkan-1.lib")
    set(VULKAN_DLL_PATH "${VULKAN_EXTERNAL_DIR}/lib/vulkan-1.dll")

    if(NOT EXISTS "${VULKAN_LIB_PATH}")
        message(FATAL_ERROR "Vulkan import library not found: ${VULKAN_LIB_PATH}\nPlace vulkan-1.lib in external/Vulkan/lib/")
    endif()
    if(NOT EXISTS "${VULKAN_DLL_PATH}")
        message(FATAL_ERROR "Vulkan DLL not found: ${VULKAN_DLL_PATH}\nPlace vulkan-1.dll in external/Vulkan/lib/")
    endif()

    target_link_libraries(Vulkan::Vulkan INTERFACE "${VULKAN_LIB_PATH}")
    set(VULKAN_RUNTIME_DLL "${VULKAN_DLL_PATH}" CACHE FILEPATH "" FORCE)

    function(qc_add_shaderc_lib TARGET_NAME RELEASE_FILE DEBUG_FILE)
        set(_release "${VULKAN_EXTERNAL_DIR}/lib/${RELEASE_FILE}")
        set(_debug   "${VULKAN_EXTERNAL_DIR}/lib/${DEBUG_FILE}")

        if(NOT EXISTS "${_release}")
            message(FATAL_ERROR "Missing library: ${_release}\nPlace ${RELEASE_FILE} in external/Vulkan/lib/")
        endif()
        if(NOT EXISTS "${_debug}")
            message(FATAL_ERROR "Missing library: ${_debug}\nPlace ${DEBUG_FILE} in external/Vulkan/lib/")
        endif()

        add_library(${TARGET_NAME} STATIC IMPORTED GLOBAL)
        target_include_directories(${TARGET_NAME} INTERFACE ${VULKAN_EXTRA_INCLUDES})
        set_target_properties(${TARGET_NAME} PROPERTIES
            IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
            IMPORTED_LOCATION_DEBUG   "${_debug}"
            IMPORTED_LOCATION_RELEASE "${_release}"
            MAP_IMPORTED_CONFIG_MINSIZEREL Release
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        )
    endfunction()

    function(qc_add_shared_spirv_lib TARGET_NAME BASE_NAME)
        set(_lib_rel "${VULKAN_EXTERNAL_DIR}/lib/${BASE_NAME}.lib")
        set(_lib_dbg "${VULKAN_EXTERNAL_DIR}/lib/${BASE_NAME}d.lib")
        set(_dll_rel "${VULKAN_EXTERNAL_DIR}/lib/${BASE_NAME}.dll")
        set(_dll_dbg "${VULKAN_EXTERNAL_DIR}/lib/${BASE_NAME}d.dll")

        foreach(_file IN ITEMS "${_lib_rel}" "${_lib_dbg}" "${_dll_rel}" "${_dll_dbg}")
            if(NOT EXISTS "${_file}")
                message(FATAL_ERROR "Missing required file: ${_file}\nPlace it in external/Vulkan/lib/")
            endif()
        endforeach()

        add_library(${TARGET_NAME} SHARED IMPORTED GLOBAL)
        target_include_directories(${TARGET_NAME} INTERFACE ${VULKAN_EXTRA_INCLUDES})
        
        set_target_properties(${TARGET_NAME} PROPERTIES
            IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
            IMPORTED_IMPLIB_DEBUG     "${_lib_dbg}"
            IMPORTED_IMPLIB_RELEASE   "${_lib_rel}"
            IMPORTED_LOCATION_DEBUG   "${_dll_dbg}"
            IMPORTED_LOCATION_RELEASE "${_dll_rel}"
            MAP_IMPORTED_CONFIG_DEBUG Debug
            MAP_IMPORTED_CONFIG_RELEASE Release
            MAP_IMPORTED_CONFIG_MINSIZEREL Release
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        )
    endfunction()

    qc_add_shaderc_lib(shaderc_lib           "shaderc.lib"           "shadercd.lib")
    qc_add_shaderc_lib(glslang_lib           "glslang.lib"           "glslangd.lib")
    qc_add_shaderc_lib(shaderc_util_lib      "shaderc_util.lib"      "shaderc_utild.lib")
    qc_add_shaderc_lib(SPIRV_lib             "SPIRV.lib"             "SPIRVd.lib")

    qc_add_shared_spirv_lib(SPIRV_Tools_opt_lib "SPIRV-Tools-opt")
    qc_add_shared_spirv_lib(SPIRV_Tools_lib     "SPIRV-Tools")

    add_library(shaderc_combined INTERFACE IMPORTED GLOBAL)
    target_link_libraries(shaderc_combined INTERFACE
        shaderc_lib
        glslang_lib
        shaderc_util_lib
        SPIRV_lib
        SPIRV_Tools_opt_lib
        SPIRV_Tools_lib
    )

    set(SPIRV_CROSS_CORE_LIB_DEBUG   "${VULKAN_EXTERNAL_DIR}/lib/spirv-cross-cored.lib")
    set(SPIRV_CROSS_CORE_LIB_RELEASE "${VULKAN_EXTERNAL_DIR}/lib/spirv-cross-core.lib")
    set(SPIRV_CROSS_GLSL_LIB_DEBUG   "${VULKAN_EXTERNAL_DIR}/lib/spirv-cross-glsld.lib")
    set(SPIRV_CROSS_GLSL_LIB_RELEASE "${VULKAN_EXTERNAL_DIR}/lib/spirv-cross-glsl.lib")

    foreach(_lib SPIRV_CROSS_CORE_LIB_DEBUG SPIRV_CROSS_CORE_LIB_RELEASE
                 SPIRV_CROSS_GLSL_LIB_DEBUG SPIRV_CROSS_GLSL_LIB_RELEASE)
        if(NOT EXISTS "${${_lib}}")
            message(FATAL_ERROR "Missing library: ${${_lib}}\nPlace it in external/Vulkan/lib/")
        endif()
    endforeach()

    add_library(spirv-cross-core STATIC IMPORTED GLOBAL)
    target_include_directories(spirv-cross-core INTERFACE ${VULKAN_EXTRA_INCLUDES})
    set_target_properties(spirv-cross-core PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG   "${SPIRV_CROSS_CORE_LIB_DEBUG}"
        IMPORTED_LOCATION_RELEASE "${SPIRV_CROSS_CORE_LIB_RELEASE}"
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    )

    add_library(spirv-cross-glsl STATIC IMPORTED GLOBAL)
    target_include_directories(spirv-cross-glsl INTERFACE ${VULKAN_EXTRA_INCLUDES})
    set_target_properties(spirv-cross-glsl PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG   "${SPIRV_CROSS_GLSL_LIB_DEBUG}"
        IMPORTED_LOCATION_RELEASE "${SPIRV_CROSS_GLSL_LIB_RELEASE}"
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    )

    target_link_libraries(spirv-cross-glsl INTERFACE spirv-cross-core)

else()
    find_package(Vulkan REQUIRED)
    set_target_properties(Vulkan::Vulkan PROPERTIES IMPORTED_GLOBAL TRUE)

    if(APPLE)
        set(SHADERC_NAMES shaderc_combined shaderc)
    else()
        set(SHADERC_NAMES shaderc shaderc_combined)
    endif()

    find_library(SHADERC_LIB
        NAMES ${SHADERC_NAMES}
        PATHS /opt/homebrew/lib /usr/lib /usr/local/lib
              /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu
        REQUIRED
    )
    find_library(SPIRV_CROSS_GLSL_LIB
        NAMES spirv-cross-glsl
        PATHS /opt/homebrew/lib /usr/lib /usr/local/lib
              /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu
        REQUIRED
    )
    find_library(SPIRV_CROSS_CORE_LIB
        NAMES spirv-cross-core
        PATHS /opt/homebrew/lib /usr/lib /usr/local/lib
              /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu
        REQUIRED
    )
endif()

set(SDL3_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL3")
add_library(SDL3::SDL3 SHARED IMPORTED GLOBAL)
target_include_directories(SDL3::SDL3 INTERFACE "${SDL3_EXTERNAL_DIR}/include")

if(MSVC)
    set_target_properties(SDL3::SDL3 PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_IMPLIB_DEBUG   "${SDL3_EXTERNAL_DIR}/lib/debug/SDL3.lib"
        IMPORTED_IMPLIB_RELEASE "${SDL3_EXTERNAL_DIR}/lib/release/SDL3.lib"
        IMPORTED_LOCATION_DEBUG   "${SDL3_EXTERNAL_DIR}/lib/debug/SDL3.dll"
        IMPORTED_LOCATION_RELEASE "${SDL3_EXTERNAL_DIR}/lib/release/SDL3.dll"
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    )
elseif(WIN32)
    set_target_properties(SDL3::SDL3 PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_IMPLIB_DEBUG   "${SDL3_EXTERNAL_DIR}/lib/debug/libSDL3.dll.a"
        IMPORTED_IMPLIB_RELEASE "${SDL3_EXTERNAL_DIR}/lib/release/libSDL3.dll.a"
        IMPORTED_LOCATION_DEBUG   "${SDL3_EXTERNAL_DIR}/lib/debug/SDL3.dll"
        IMPORTED_LOCATION_RELEASE "${SDL3_EXTERNAL_DIR}/lib/release/SDL3.dll"
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    )
elseif(APPLE)
    set_target_properties(SDL3::SDL3 PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION "${SDL3_EXTERNAL_DIR}/lib/release/libSDL3.0.dylib"
        IMPORTED_LOCATION_DEBUG   "${SDL3_EXTERNAL_DIR}/lib/debug/libSDL3.0.dylib"
        IMPORTED_LOCATION_RELEASE "${SDL3_EXTERNAL_DIR}/lib/release/libSDL3.0.dylib"
    )
else()
    set_target_properties(SDL3::SDL3 PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION "${SDL3_EXTERNAL_DIR}/lib/release/libSDL3.so.0.4.8"
        IMPORTED_LOCATION_DEBUG   "${SDL3_EXTERNAL_DIR}/lib/debug/libSDL3.so.0.4.8"
        IMPORTED_LOCATION_RELEASE "${SDL3_EXTERNAL_DIR}/lib/release/libSDL3.so.0.4.8"
    )
endif()

find_package(ZLIB QUIET)

if(NOT TARGET ZLIB::ZLIB)
    set(ZLIB_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/assimp")
    add_library(ZLIB::ZLIB STATIC IMPORTED GLOBAL)
    target_include_directories(ZLIB::ZLIB INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

    if(MSVC)
        set_target_properties(ZLIB::ZLIB PROPERTIES
            IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
            IMPORTED_LOCATION_DEBUG   "${ZLIB_EXTERNAL_DIR}/lib/debug/zlibstaticd.lib"
            IMPORTED_LOCATION_RELEASE "${ZLIB_EXTERNAL_DIR}/lib/release/zlibstatic.lib"
            MAP_IMPORTED_CONFIG_MINSIZEREL Release
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        )
    elseif(WIN32)
        set_target_properties(ZLIB::ZLIB PROPERTIES
            IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
            IMPORTED_LOCATION_DEBUG   "${ZLIB_EXTERNAL_DIR}/lib/debug/zlibstaticd.a"
            IMPORTED_LOCATION_RELEASE "${ZLIB_EXTERNAL_DIR}/lib/release/zlibstatic.a"
            MAP_IMPORTED_CONFIG_MINSIZEREL Release
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        )
    else()
        set_target_properties(ZLIB::ZLIB PROPERTIES
            IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
            IMPORTED_LOCATION_DEBUG   "${ZLIB_EXTERNAL_DIR}/lib/debug/libzlibstaticd.a"
            IMPORTED_LOCATION_RELEASE "${ZLIB_EXTERNAL_DIR}/lib/release/libzlibstatic.a"
            MAP_IMPORTED_CONFIG_MINSIZEREL Release
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        )
    endif()
endif()

set(ASSIMP_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/assimp")
add_library(assimp::assimp SHARED IMPORTED GLOBAL)
target_include_directories(assimp::assimp INTERFACE "${ASSIMP_EXTERNAL_DIR}/include")

if(MSVC)
    set_target_properties(assimp::assimp PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_IMPLIB_DEBUG   "${ASSIMP_EXTERNAL_DIR}/lib/debug/assimp-vc143-mtd.lib"
        IMPORTED_IMPLIB_RELEASE "${ASSIMP_EXTERNAL_DIR}/lib/release/assimp-vc143-mt.lib"
        IMPORTED_LOCATION_DEBUG   "${ASSIMP_EXTERNAL_DIR}/lib/debug/assimp-vc143-mtd.dll"
        IMPORTED_LOCATION_RELEASE "${ASSIMP_EXTERNAL_DIR}/lib/release/assimp-vc143-mt.dll"
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    )
elseif(WIN32)
    set_target_properties(assimp::assimp PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_IMPLIB_DEBUG   "${ASSIMP_EXTERNAL_DIR}/lib/debug/libassimpd.dll.a"
        IMPORTED_IMPLIB_RELEASE "${ASSIMP_EXTERNAL_DIR}/lib/release/libassimp.dll.a"
        IMPORTED_LOCATION_DEBUG   "${ASSIMP_EXTERNAL_DIR}/lib/debug/assimp-vc143-mtd.dll"
        IMPORTED_LOCATION_RELEASE "${ASSIMP_EXTERNAL_DIR}/lib/release/assimp-vc143-mt.dll"
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    )
elseif(APPLE)
    set_target_properties(assimp::assimp PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION "${ASSIMP_EXTERNAL_DIR}/lib/release/libassimp.6.0.5.dylib"
        IMPORTED_LOCATION_DEBUG   "${ASSIMP_EXTERNAL_DIR}/lib/debug/libassimpd.6.0.5.dylib"
        IMPORTED_LOCATION_RELEASE "${ASSIMP_EXTERNAL_DIR}/lib/release/libassimp.6.0.5.dylib"
    )
else()
    set_target_properties(assimp::assimp PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION "${ASSIMP_EXTERNAL_DIR}/lib/release/libassimp.so.6.0.5"
        IMPORTED_LOCATION_DEBUG   "${ASSIMP_EXTERNAL_DIR}/lib/debug/libassimpd.so.6.0.5"
        IMPORTED_LOCATION_RELEASE "${ASSIMP_EXTERNAL_DIR}/lib/release/libassimp.so.6.0.5"
    )
endif()

set(FREETYPE_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/freetype")
add_library(freetype SHARED IMPORTED GLOBAL)
target_include_directories(freetype INTERFACE "${FREETYPE_EXTERNAL_DIR}/include")

if(MSVC)
    set_target_properties(freetype PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_IMPLIB_DEBUG   "${FREETYPE_EXTERNAL_DIR}/lib/debug/freetyped.lib"
        IMPORTED_IMPLIB_RELEASE "${FREETYPE_EXTERNAL_DIR}/lib/release/freetype.lib"
        IMPORTED_LOCATION_DEBUG   "${FREETYPE_EXTERNAL_DIR}/lib/debug/freetyped.dll"
        IMPORTED_LOCATION_RELEASE "${FREETYPE_EXTERNAL_DIR}/lib/release/freetype.dll"
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    )
elseif(WIN32)
    set_target_properties(freetype PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_IMPLIB_DEBUG   "${FREETYPE_EXTERNAL_DIR}/lib/debug/libfreetyped.dll.a"
        IMPORTED_IMPLIB_RELEASE "${FREETYPE_EXTERNAL_DIR}/lib/release/libfreetype.dll.a"
        IMPORTED_LOCATION_DEBUG   "${FREETYPE_EXTERNAL_DIR}/lib/debug/freetyped.dll"
        IMPORTED_LOCATION_RELEASE "${FREETYPE_EXTERNAL_DIR}/lib/release/freetype.dll"
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    )
elseif(APPLE)
    set_target_properties(freetype PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION "${FREETYPE_EXTERNAL_DIR}/lib/release/libfreetype.6.20.6.dylib"
        IMPORTED_LOCATION_DEBUG   "${FREETYPE_EXTERNAL_DIR}/lib/debug/libfreetyped.6.20.6.dylib"
        IMPORTED_LOCATION_RELEASE "${FREETYPE_EXTERNAL_DIR}/lib/release/libfreetype.6.20.6.dylib"
    )
else()
    set_target_properties(freetype PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION "${FREETYPE_EXTERNAL_DIR}/lib/release/libfreetype.so.6.20.6"
        IMPORTED_LOCATION_DEBUG   "${FREETYPE_EXTERNAL_DIR}/lib/debug/libfreetyped.so.6.20.6"
        IMPORTED_LOCATION_RELEASE "${FREETYPE_EXTERNAL_DIR}/lib/release/libfreetype.so.6.20.6"
    )
endif()

set(GLAD_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/glad")
add_library(glad STATIC "${GLAD_EXTERNAL_DIR}/src/glad.c")
target_include_directories(glad PUBLIC "${GLAD_EXTERNAL_DIR}/include")
set_target_properties(glad PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(OPENAL_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/OpenAL")
add_library(OpenAL::OpenAL SHARED IMPORTED GLOBAL)
target_include_directories(OpenAL::OpenAL INTERFACE "${OPENAL_EXTERNAL_DIR}/include")

if(MSVC)
    set_target_properties(OpenAL::OpenAL PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_IMPLIB_DEBUG   "${OPENAL_EXTERNAL_DIR}/lib/debug/OpenAL32.lib"
        IMPORTED_IMPLIB_RELEASE "${OPENAL_EXTERNAL_DIR}/lib/release/OpenAL32.lib"
        IMPORTED_LOCATION_DEBUG   "${OPENAL_EXTERNAL_DIR}/lib/debug/OpenAL32.dll"
        IMPORTED_LOCATION_RELEASE "${OPENAL_EXTERNAL_DIR}/lib/release/OpenAL32.dll"
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    )
elseif(WIN32)
    set_target_properties(OpenAL::OpenAL PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_IMPLIB_DEBUG   "${OPENAL_EXTERNAL_DIR}/lib/debug/libOpenAL32.dll.a"
        IMPORTED_IMPLIB_RELEASE "${OPENAL_EXTERNAL_DIR}/lib/release/libOpenAL32.dll.a"
        IMPORTED_LOCATION_DEBUG   "${OPENAL_EXTERNAL_DIR}/lib/debug/OpenAL32.dll"
        IMPORTED_LOCATION_RELEASE "${OPENAL_EXTERNAL_DIR}/lib/release/OpenAL32.dll"
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
    )
elseif(APPLE)
    set_target_properties(OpenAL::OpenAL PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG   "${OPENAL_EXTERNAL_DIR}/lib/debug/libopenal.1.25.2.dylib"
        IMPORTED_LOCATION_RELEASE "${OPENAL_EXTERNAL_DIR}/lib/release/libopenal.1.25.2.dylib"
        IMPORTED_LOCATION "${OPENAL_EXTERNAL_DIR}/lib/release/libopenal.1.25.2.dylib"
    )
else()
    set_target_properties(OpenAL::OpenAL PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG   "${OPENAL_EXTERNAL_DIR}/lib/debug/libopenal.so.1.25.2"
        IMPORTED_LOCATION_RELEASE "${OPENAL_EXTERNAL_DIR}/lib/release/libopenal.so.1.25.2"
        IMPORTED_LOCATION "${OPENAL_EXTERNAL_DIR}/lib/release/libopenal.so.1.25.2"
    )
endif()

add_library(QuarkCore STATIC
    src/QuarkEvent.cpp
    src/QuarkLights.cpp
    src/QuarkWindow.cpp
    src/QuarkImage.cpp
    src/QuarkUtils.cpp
)

if(WIN32)
    set_target_properties(QuarkCore PROPERTIES OUTPUT_NAME QuarkCoreCommon)
endif()

if(WIN32)
    target_compile_definitions(QuarkCore PRIVATE _WIN32)
elseif(APPLE)
    target_compile_definitions(QuarkCore PRIVATE __APPLE__)
elseif(UNIX)
    target_compile_definitions(QuarkCore PRIVATE __linux__)
endif()

target_include_directories(QuarkCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${VULKAN_EXTRA_INCLUDES}
    /usr/include/spirv_cross
    /usr/include/spirv-cross
    /opt/homebrew/include
    /opt/homebrew/include/spirv_cross
)

target_compile_definitions(QuarkCore PRIVATE QUARKCORE_BUILD_DLL NOMINMAX)

if(WIN32)
    target_link_libraries(QuarkCore
        PUBLIC
            SDL3::SDL3
            assimp::assimp
            freetype
            ZLIB::ZLIB
            OpenAL::OpenAL
    )
else()
    target_link_libraries(QuarkCore
        PUBLIC
            SDL3::SDL3
            assimp::assimp
            freetype
            ZLIB::ZLIB
            OpenAL::OpenAL
    )
endif()

set_target_properties(QuarkCore PROPERTIES UNITY_BUILD OFF)

if(APPLE)
    set_target_properties(QuarkCore PROPERTIES
        BUILD_RPATH "@loader_path"
        INSTALL_RPATH "@loader_path"
    )
elseif(UNIX)
    set_target_properties(QuarkCore PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

if(MSVC)
    target_compile_options(QuarkCore PRIVATE /W4 /permissive- /MP)
    target_link_options(QuarkCore PRIVATE /IGNORE:4099)
else()
    target_compile_options(QuarkCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

function(qc_configure_backend_target TARGET_NAME BACKEND_NAME)
    add_library(${TARGET_NAME} SHARED src/QuarkCore.cpp)
    target_link_libraries(${TARGET_NAME} PUBLIC QuarkCore)
    if(MSVC)
        target_link_options(${TARGET_NAME} PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:QuarkCore>")
    elseif(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(${TARGET_NAME} PRIVATE
            "-Wl,--whole-archive" "$<TARGET_FILE:QuarkCore>" "-Wl,--no-whole-archive"
        )
    endif()

    if(BACKEND_NAME STREQUAL "OPENGL")
        find_package(OpenGL REQUIRED)
        target_sources(${TARGET_NAME} PRIVATE src/Renderer/QuarkGL/QuarkGLRenderer.cpp)
        target_compile_definitions(${TARGET_NAME} PRIVATE QC_ENABLE_OPENGL)
        target_compile_definitions(${TARGET_NAME} INTERFACE QC_ENABLE_OPENGL)
        target_link_libraries(${TARGET_NAME} PUBLIC
            SDL3::SDL3
            glad
            OpenGL::GL
            assimp::assimp
            freetype
            OpenAL::OpenAL
        )
    elseif(BACKEND_NAME STREQUAL "VULKAN")
        target_sources(${TARGET_NAME} PRIVATE
            src/Renderer/QuarkVulkan/QuarkVkCommon.hpp
            src/Renderer/QuarkVulkan/QuarkVkDescriptorSetManager.cpp
            src/Renderer/QuarkVulkan/QuarkVkDescriptorSetManager.hpp
            src/Renderer/QuarkVulkan/QuarkVkDevice.hpp
            src/Renderer/QuarkVulkan/QuarkVkDevice.cpp
            src/Renderer/QuarkVulkan/QuarkVkPipeline.hpp
            src/Renderer/QuarkVulkan/QuarkVkPipeline.cpp
            src/Renderer/QuarkVulkan/QuarkVkRenderTarget.hpp
            src/Renderer/QuarkVulkan/QuarkVkRenderTarget.cpp
            src/Renderer/QuarkVulkan/QuarkVkSwapChain.hpp
            src/Renderer/QuarkVulkan/QuarkVkSwapChain.cpp
            src/Renderer/QuarkVulkan/QuarkVkResources.hpp
            src/Renderer/QuarkVulkan/QuarkVkResources.cpp
            src/Renderer/QuarkVulkan/QuarkVkCommandContext.hpp
            src/Renderer/QuarkVulkan/QuarkVkCommandContext.cpp
            src/Renderer/QuarkVulkan/QuarkVkShaderCompiler.hpp
            src/Renderer/QuarkVulkan/QuarkVkShaderCompiler.cpp
            src/Renderer/QuarkVulkan/QuarkVkRenderPass.hpp
            src/Renderer/QuarkVulkan/QuarkVkRenderPass.cpp
            src/Renderer/QuarkVulkan/QuarkVkFrameManager.hpp
            src/Renderer/QuarkVulkan/QuarkVkFrameManager.cpp
            src/Renderer/QuarkVulkan/QuarkVkFramebufferManager.hpp
            src/Renderer/QuarkVulkan/QuarkVkFramebufferManager.cpp
            src/Renderer/QuarkVulkan/QuarkVkRenderer.cpp
            src/Renderer/QuarkVulkan/QuarkVkDraw2D.cpp
            src/Renderer/QuarkVulkan/QuarkVkDraw3D.cpp
            src/Renderer/QuarkVulkan/QuarkVkShader.cpp
            src/Renderer/QuarkVulkan/QuarkVkTexture.cpp
            src/Renderer/QuarkVulkan/QuarkVkModel.cpp
            src/Renderer/QuarkVulkan/QuarkVkFont.cpp
            src/Renderer/QuarkVulkan/QuarkVkGpuAllocator.cpp
        )
        target_compile_definitions(${TARGET_NAME} PRIVATE
            QC_ENABLE_VULKAN
        )
        target_compile_definitions(${TARGET_NAME} INTERFACE QC_ENABLE_VULKAN)
        if(WIN32)
            target_link_libraries(${TARGET_NAME} PUBLIC
                SDL3::SDL3
                assimp::assimp
                freetype
                Vulkan::Vulkan
                shaderc_combined
                spirv-cross-glsl
                OpenAL::OpenAL
            )
        else()
            target_link_libraries(${TARGET_NAME} PUBLIC
                SDL3::SDL3
                assimp::assimp
                freetype
                Vulkan::Vulkan
                ${SHADERC_LIB}
                ${SPIRV_CROSS_GLSL_LIB}
                ${SPIRV_CROSS_CORE_LIB}
                OpenAL::OpenAL
            )
        endif()
    elseif(BACKEND_NAME STREQUAL "D3D11")
        if(NOT WIN32)
            message(FATAL_ERROR "The D3D11 backend is only available on Windows")
        endif()
        target_sources(${TARGET_NAME} PRIVATE
            src/Renderer/QuarkDX11/QuarkD3D11Renderer.cpp
            src/Renderer/QuarkDX11/QuarkD3D11Device.cpp
            src/Renderer/QuarkDX11/QuarkD3D11SwapChain.cpp
            src/Renderer/QuarkDX11/QuarkD3D11Resources.cpp
            src/Renderer/QuarkDX11/QuarkD3D11Pipeline.cpp
            src/Renderer/QuarkDX11/QuarkD3D11CommandContext.cpp
            src/Renderer/QuarkDX11/QuarkD3D11ShaderCompiler.cpp
        )
        target_compile_definitions(${TARGET_NAME} PRIVATE QC_ENABLE_D3D11)
        target_compile_definitions(${TARGET_NAME} INTERFACE QC_ENABLE_D3D11)
        target_link_libraries(${TARGET_NAME} PUBLIC
            SDL3::SDL3
            d3d11
            dxgi
            d3dcompiler
            dxguid
            OpenAL::OpenAL
        )
    else()
        message(FATAL_ERROR "Unknown QuarkCore backend: ${BACKEND_NAME}")
    endif()

    target_include_directories(${TARGET_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_definitions(${TARGET_NAME} PRIVATE QUARKCORE_BUILD_DLL NOMINMAX)
    set_target_properties(${TARGET_NAME} PROPERTIES UNITY_BUILD OFF)

    if(WIN32 AND BACKEND_NAME STREQUAL "OPENGL")
        set_target_properties(${TARGET_NAME} PROPERTIES OUTPUT_NAME QuarkCore)
    elseif(WIN32 AND BACKEND_NAME STREQUAL "D3D11")
        set_target_properties(${TARGET_NAME} PROPERTIES OUTPUT_NAME QuarkCore-Direct3D11)
    endif()

    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /W4 /permissive- /MP)
        target_link_options(${TARGET_NAME} PRIVATE /IGNORE:4099)
    else()
        target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

qc_configure_backend_target(QuarkCore-OpenGL OPENGL)
qc_configure_backend_target(QuarkCore-Vulkan VULKAN)
if(WIN32)
    qc_configure_backend_target(QuarkCore-D3D11 D3D11)
endif()
add_library(QuarkCore::OpenGL ALIAS QuarkCore-OpenGL)
add_library(QuarkCore::Vulkan ALIAS QuarkCore-Vulkan)
if(WIN32)
    add_library(QuarkCore::D3D11 ALIAS QuarkCore-D3D11)
endif()

if(BUILD_TESTING)
    add_executable(quark_vulkan_validation_tests
        tests/QuarkVulkanValidationTests.cpp
    )
    target_include_directories(quark_vulkan_validation_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/external/Vulkan/include
    )
    target_compile_definitions(quark_vulkan_validation_tests PRIVATE
        VMA_STATIC_VULKAN_FUNCTIONS=1
    )
    if(WIN32)
        target_link_libraries(quark_vulkan_validation_tests PRIVATE
            Vulkan::Vulkan
        )
    else()
        target_link_libraries(quark_vulkan_validation_tests PRIVATE
            Vulkan::Vulkan
        )
    endif()
    add_test(NAME QuarkVulkanValidation COMMAND quark_vulkan_validation_tests)

    add_executable(quark_model_animation_tests
        tests/QuarkModelAnimationTests.cpp
    )
    target_link_libraries(quark_model_animation_tests PRIVATE QuarkCore-OpenGL)
    target_include_directories(quark_model_animation_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    add_test(NAME QuarkModelAnimation COMMAND quark_model_animation_tests)

    add_executable(quark_vr_stereo_tests
        tests/QuarkModelAnimationTests.cpp
    )
    target_link_libraries(quark_vr_stereo_tests PRIVATE QuarkCore-OpenGL)
    target_include_directories(quark_vr_stereo_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    add_test(NAME QuarkVRStereo COMMAND quark_vr_stereo_tests)
endif()

if(QC_BUILD_EXAMPLES)
    set(EXAMPLE_TARGETS
        quarkcore_opengl
        quarkcore_audio
        quarkcore_textures_and_shaders
        quarkcore_3d_primitives
        quarkcore_meshes
        quarkcore_vulkan
        quarkcore_vulkan_shaders
        quarkcore_vulkan_3d_lights
        quarkcore_files
        quarkcore_3d_lights
    )

    set(EXAMPLE_SOURCES
        examples/opengl.cpp
        examples/audio.cpp
        examples/textures_and_shaders.cpp
        examples/primitives_3d.cpp
        examples/meshes.cpp
        examples/vulkan.cpp
        examples/vulkan_shaders.cpp
        examples/vulkan_3d_lights.cpp
        examples/file_operations.cpp
        examples/3d_light.cpp
    )

    if(WIN32)
        list(APPEND EXAMPLE_TARGETS quarkcore_d3d11 quarkcore_d3d11_shaders)
        list(APPEND EXAMPLE_SOURCES examples/d3d11.cpp examples/d3d11_shaders.cpp)
    endif()

    list(LENGTH EXAMPLE_TARGETS _count)
    math(EXPR _last "${_count} - 1")

    if(WIN32)
        set(_RUNTIME_DLLS
            $<IF:$<CONFIG:Debug>,${SDL3_EXTERNAL_DIR}/lib/debug/SDL3.dll,${SDL3_EXTERNAL_DIR}/lib/release/SDL3.dll>
            $<IF:$<CONFIG:Debug>,${ASSIMP_EXTERNAL_DIR}/lib/debug/assimp-vc143-mtd.dll,${ASSIMP_EXTERNAL_DIR}/lib/release/assimp-vc143-mt.dll>
            $<IF:$<CONFIG:Debug>,${FREETYPE_EXTERNAL_DIR}/lib/debug/freetyped.dll,${FREETYPE_EXTERNAL_DIR}/lib/release/freetype.dll>
            $<IF:$<CONFIG:Debug>,${OPENAL_EXTERNAL_DIR}/lib/debug/OpenAL32.dll,${OPENAL_EXTERNAL_DIR}/lib/release/OpenAL32.dll>
            "${VULKAN_EXTERNAL_DIR}/lib/SPIRV-Tools.dll"
            "${VULKAN_EXTERNAL_DIR}/lib/SPIRV-Toolsd.dll"
            "${VULKAN_EXTERNAL_DIR}/lib/SPIRV-Tools-opt.dll"
            "${VULKAN_EXTERNAL_DIR}/lib/SPIRV-Tools-optd.dll"
            "${VULKAN_RUNTIME_DLL}"
        )

        add_custom_command(TARGET QuarkCore POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_RUNTIME_DLLS}
                $<TARGET_FILE_DIR:QuarkCore>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VULKAN_EXTERNAL_DIR}/lib/SPIRV-Tools-optd.dll"
                "$<TARGET_FILE_DIR:QuarkCore>/SPIRV-Tools-opt.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VULKAN_EXTERNAL_DIR}/lib/SPIRV-Toolsd.dll"
                "$<TARGET_FILE_DIR:QuarkCore>/SPIRV-Tools.dll"
            COMMENT "Copying DLLs next to QuarkCore..."
        )

        foreach(i RANGE ${_last})
            list(GET EXAMPLE_TARGETS ${i} _target)
            list(GET EXAMPLE_SOURCES ${i} _source)

            set(_backend_target QuarkCore-OpenGL)
            if(_target MATCHES "^quarkcore_vulkan" OR _target STREQUAL "quarkcore_meshes")
                set(_backend_target QuarkCore-Vulkan)
            elseif(_target STREQUAL "quarkcore_d3d11" OR _target STREQUAL "quarkcore_d3d11_shaders")
                set(_backend_target QuarkCore-D3D11)
            endif()

            add_executable(${_target} ${_source})
            target_link_libraries(${_target} PRIVATE ${_backend_target})

            if((_target STREQUAL "quarkcore_d3d11" OR _target STREQUAL "quarkcore_d3d11_shaders") AND EXISTS "$ENV{WINDIR}/System32/d3dcompiler_47.dll")
                add_custom_command(TARGET ${_target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$ENV{WINDIR}/System32/d3dcompiler_47.dll"
                        "$<TARGET_FILE_DIR:${_target}>/d3dcompiler_47.dll"
                    COMMENT "Copying D3DCompiler runtime next to D3D11 example..."
                )
            endif()

            if(_target STREQUAL "quarkcore_3d_lights")
                add_custom_command(TARGET ${_target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                        $<TARGET_FILE_DIR:${_target}>/resources
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/lighting.vs
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/lighting.fs
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/shadow_depth.vs
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/shadow_depth.fs
                        $<TARGET_FILE_DIR:${_target}>/resources
                )
            endif()

            if(_target STREQUAL "quarkcore_opengl")
                add_custom_command(TARGET ${_target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                        $<TARGET_FILE_DIR:${_target}>/resources
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/opengl.png
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/lantern.mtl
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/lantern.obj
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/light.png
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/prison.png
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/rustymetal.png
                        $<TARGET_FILE_DIR:${_target}>/resources
                )
            endif()

            if(_target STREQUAL "quarkcore_vulkan_3d_lights")
                add_custom_command(TARGET ${_target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                        $<TARGET_FILE_DIR:${_target}>/resources
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/vulkan_lighting.vs
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/vulkan_lighting.fs
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/vulkan_shadow_depth.vs
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/vulkan_shadow_depth.fs
                        $<TARGET_FILE_DIR:${_target}>/resources
                )
            endif()

            if(_target STREQUAL "quarkcore_vulkan")
                add_custom_command(TARGET ${_target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                        $<TARGET_FILE_DIR:${_target}>/resources
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/vulkan.png
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/lantern.mtl
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/lantern.obj
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/light.png
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/prison.png
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/rustymetal.png
                        $<TARGET_FILE_DIR:${_target}>/resources
                )
            endif()

            if(_target STREQUAL "quarkcore_d3d11")
                add_custom_command(TARGET ${_target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                        $<TARGET_FILE_DIR:${_target}>/resources
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/directx11.png
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/lantern.mtl
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/lantern.obj
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/light.png
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/prison.png
                        ${CMAKE_CURRENT_SOURCE_DIR}/examples/resources/rustymetal.png
                        $<TARGET_FILE_DIR:${_target}>/resources
                )
            endif()

        endforeach()
    else()
        set(_RUNTIME_LIBS
            $<TARGET_FILE:assimp::assimp>
            $<TARGET_FILE:freetype>
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE:OpenAL::OpenAL>
        )

        if(NOT APPLE)
            add_custom_command(TARGET QuarkCore POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${_RUNTIME_LIBS}
                    $<TARGET_FILE_DIR:QuarkCore>
                COMMAND ${CMAKE_COMMAND} -E rm -f $<TARGET_FILE_DIR:QuarkCore>/$<IF:$<CONFIG:Debug>,libfreetyped.so.6,libfreetype.so.6>
                COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE_NAME:freetype> $<TARGET_FILE_DIR:QuarkCore>/$<IF:$<CONFIG:Debug>,libfreetyped.so.6,libfreetype.so.6>
                COMMAND ${CMAKE_COMMAND} -E rm -f $<TARGET_FILE_DIR:QuarkCore>/$<IF:$<CONFIG:Debug>,libassimpd.so.6,libassimp.so.6>
                COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE_NAME:assimp::assimp> $<TARGET_FILE_DIR:QuarkCore>/$<IF:$<CONFIG:Debug>,libassimpd.so.6,libassimp.so.6>
                COMMAND ${CMAKE_COMMAND} -E rm -f $<TARGET_FILE_DIR:QuarkCore>/libSDL3.so.0
                COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE_NAME:SDL3::SDL3> $<TARGET_FILE_DIR:QuarkCore>/libSDL3.so.0
                COMMAND ${CMAKE_COMMAND} -E rm -f $<TARGET_FILE_DIR:QuarkCore>/$<IF:$<CONFIG:Debug>,libopenal.so.1,libopenal.so.1>
                COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE_NAME:OpenAL::OpenAL> $<TARGET_FILE_DIR:QuarkCore>/$<IF:$<CONFIG:Debug>,libopenal.so.1,libopenal.so.1>
                COMMENT "Creating shared library symlinks next to QuarkCore (Linux)..."
            )
        endif()

        foreach(i RANGE ${_last})
            list(GET EXAMPLE_TARGETS ${i} _target)
            list(GET EXAMPLE_SOURCES ${i} _source)

            set(_backend_target QuarkCore-OpenGL)
            if(_target MATCHES "^quarkcore_vulkan")
                set(_backend_target QuarkCore-Vulkan)
            endif()

            add_executable(${_target} ${_source})
            target_link_libraries(${_target} PRIVATE ${_backend_target})

            set_target_properties(${_target} PROPERTIES
                BUILD_RPATH "$ORIGIN"
                INSTALL_RPATH "$ORIGIN"
            )
        endforeach()
    endif()
endif()
