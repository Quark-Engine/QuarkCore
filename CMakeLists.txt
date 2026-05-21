cmake_minimum_required(VERSION 3.16)
project(QuarkCore VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QC_BUILD_EXAMPLES "Build QuarkCore examples" ON)

function(qc_lib_dir OUT_VAR LIB_NAME)
    set(BASE "${CMAKE_CURRENT_SOURCE_DIR}/external/${LIB_NAME}/lib")
    set(${OUT_VAR}_DEBUG   "${BASE}/debug"   PARENT_SCOPE)
    set(${OUT_VAR}_RELEASE "${BASE}/release" PARENT_SCOPE)
    set(${OUT_VAR} "$<IF:$<CONFIG:Debug>,${BASE}/debug,${BASE}/release>" PARENT_SCOPE)
endfunction()

set(VULKAN_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/Vulkan")

add_library(Vulkan::Vulkan INTERFACE IMPORTED GLOBAL)
target_include_directories(Vulkan::Vulkan INTERFACE "${VULKAN_EXTERNAL_DIR}/include")

if(WIN32)
    set(VULKAN_LIB_PATH "${VULKAN_EXTERNAL_DIR}/lib/vulkan-1.lib")
    set(VULKAN_DLL_PATH "${VULKAN_EXTERNAL_DIR}/lib/vulkan-1.dll")

    if(NOT EXISTS "${VULKAN_LIB_PATH}")
        message(FATAL_ERROR "Vulkan import library not found: ${VULKAN_LIB_PATH}\n"
                            "Place vulkan-1.lib in external/Vulkan/lib/")
    endif()
    if(NOT EXISTS "${VULKAN_DLL_PATH}")
        message(FATAL_ERROR "Vulkan DLL not found: ${VULKAN_DLL_PATH}\n"
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

set(SDL3_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL3")

add_library(SDL3::SDL3 SHARED IMPORTED GLOBAL)
target_include_directories(SDL3::SDL3 INTERFACE "${SDL3_EXTERNAL_DIR}/include")

if(WIN32)
    set_target_properties(SDL3::SDL3 PROPERTIES
        IMPORTED_IMPLIB_DEBUG   "${SDL3_EXTERNAL_DIR}/lib/debug/SDL3.lib"
        IMPORTED_IMPLIB_RELEASE "${SDL3_EXTERNAL_DIR}/lib/release/SDL3.lib"
        IMPORTED_LOCATION_DEBUG   "${SDL3_EXTERNAL_DIR}/lib/debug/SDL3.dll"
        IMPORTED_LOCATION_RELEASE "${SDL3_EXTERNAL_DIR}/lib/release/SDL3.dll"
    )
elseif(APPLE)
    set_target_properties(SDL3::SDL3 PROPERTIES
        IMPORTED_LOCATION_DEBUG   "${SDL3_EXTERNAL_DIR}/lib/debug/libSDL3.0.dylib"
        IMPORTED_LOCATION_RELEASE "${SDL3_EXTERNAL_DIR}/lib/release/libSDL3.0.dylib"
    )
else()
    set_target_properties(SDL3::SDL3 PROPERTIES
        IMPORTED_LOCATION_DEBUG   "${SDL3_EXTERNAL_DIR}/lib/debug/libSDL3.so.0.4.8"
        IMPORTED_LOCATION_RELEASE "${SDL3_EXTERNAL_DIR}/lib/release/libSDL3.so.0.4.8"
    )
endif()

set(ZLIB_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/assimp")

add_library(ZLIB::ZLIB STATIC IMPORTED GLOBAL)
target_include_directories(ZLIB::ZLIB INTERFACE "${ZLIB_EXTERNAL_DIR}/include")

if(WIN32)
    set_target_properties(ZLIB::ZLIB PROPERTIES
        IMPORTED_LOCATION_DEBUG   "${ZLIB_EXTERNAL_DIR}/lib/debug/zlibstaticd.lib"
        IMPORTED_LOCATION_RELEASE "${ZLIB_EXTERNAL_DIR}/lib/release/zlibstatic.lib"
    )
endif()

set(ASSIMP_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/assimp")

add_library(assimp::assimp SHARED IMPORTED GLOBAL)
target_include_directories(assimp::assimp INTERFACE "${ASSIMP_EXTERNAL_DIR}/include")

if(WIN32)
    set_target_properties(assimp::assimp PROPERTIES
        IMPORTED_IMPLIB_DEBUG   "${ASSIMP_EXTERNAL_DIR}/lib/debug/assimp-vc143-mtd.lib"
        IMPORTED_IMPLIB_RELEASE "${ASSIMP_EXTERNAL_DIR}/lib/release/assimp-vc143-mt.lib"
        IMPORTED_LOCATION_DEBUG   "${ASSIMP_EXTERNAL_DIR}/lib/debug/assimp-vc143-mtd.dll"
        IMPORTED_LOCATION_RELEASE "${ASSIMP_EXTERNAL_DIR}/lib/release/assimp-vc143-mt.dll"
    )
elseif(APPLE)
    set_target_properties(assimp::assimp PROPERTIES
        IMPORTED_LOCATION_DEBUG   "${ASSIMP_EXTERNAL_DIR}/lib/debug/libassimp.6.0.5.dylib"
        IMPORTED_LOCATION_RELEASE "${ASSIMP_EXTERNAL_DIR}/lib/release/libassimp.6.0.5.dylib"
    )
else()
    set_target_properties(assimp::assimp PROPERTIES
        IMPORTED_LOCATION_DEBUG   "${ASSIMP_EXTERNAL_DIR}/lib/debug/libassimp.so.6.0.5"
        IMPORTED_LOCATION_RELEASE "${ASSIMP_EXTERNAL_DIR}/lib/release/libassimp.so.6.0.5"
    )
endif()

set(FREETYPE_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/freetype")

add_library(freetype SHARED IMPORTED GLOBAL)
target_include_directories(freetype INTERFACE "${FREETYPE_EXTERNAL_DIR}/include")

if(WIN32)
    set_target_properties(freetype PROPERTIES
        IMPORTED_IMPLIB_DEBUG   "${FREETYPE_EXTERNAL_DIR}/lib/debug/freetyped.lib"
        IMPORTED_IMPLIB_RELEASE "${FREETYPE_EXTERNAL_DIR}/lib/release/freetype.lib"
        IMPORTED_LOCATION_DEBUG   "${FREETYPE_EXTERNAL_DIR}/lib/debug/freetyped.dll"
        IMPORTED_LOCATION_RELEASE "${FREETYPE_EXTERNAL_DIR}/lib/release/freetype.dll"
    )
elseif(APPLE)
    set_target_properties(freetype PROPERTIES
        IMPORTED_LOCATION_DEBUG   "${FREETYPE_EXTERNAL_DIR}/lib/debug/libfreetyped.6.20.6.dylib"
        IMPORTED_LOCATION_RELEASE "${FREETYPE_EXTERNAL_DIR}/lib/release/libfreetype.6.20.6.dylib"
    )
else()
    set_target_properties(freetype PROPERTIES
        IMPORTED_LOCATION_DEBUG   "${FREETYPE_EXTERNAL_DIR}/lib/debug/libfreetype.so.6.20.6"
        IMPORTED_LOCATION_RELEASE "${FREETYPE_EXTERNAL_DIR}/lib/release/libfreetype.so.6.20.6"
    )
endif()

set(GLAD_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/glad")

add_library(glad STATIC "${GLAD_EXTERNAL_DIR}/src/glad.c")
target_include_directories(glad PUBLIC "${GLAD_EXTERNAL_DIR}/include")
target_compile_options(glad PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)

find_package(OpenGL REQUIRED)

add_library(QuarkCore SHARED
    src/QuarkCore.cpp
    src/QuarkEvent.cpp
    src/QuarkLights.cpp
    src/QuarkWindow.cpp
    src/QuarkImage.cpp
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
)

target_compile_definitions(QuarkCore PRIVATE
    QUARKCORE_BUILD_DLL
    NOMINMAX
)

target_link_libraries(QuarkCore
    PUBLIC
        SDL3::SDL3
        glad
        OpenGL::GL
        assimp::assimp
        freetype
        Vulkan::Vulkan
)

set_target_properties(QuarkCore PROPERTIES UNITY_BUILD ON)

if(MSVC)
    target_compile_options(QuarkCore PRIVATE /W4 /permissive- /MP)
else()
    target_compile_options(QuarkCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(QC_BUILD_EXAMPLES)
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

    set(EXAMPLE_SOURCES
        examples/sandbox.cpp
        examples/textures_and_shaders.cpp
        examples/primitives_3d.cpp
        examples/meshes.cpp
        examples/vulkan.cpp
        examples/light.cpp
        examples/file_operations.cpp
        examples/3d_light.cpp
    )

    list(LENGTH EXAMPLE_TARGETS _count)
    math(EXPR _last "${_count} - 1")
    foreach(i RANGE ${_last})
        list(GET EXAMPLE_TARGETS ${i} _target)
        list(GET EXAMPLE_SOURCES ${i} _source)

        add_executable(${_target} ${_source})
        target_link_libraries(${_target} PRIVATE QuarkCore)
        set_target_properties(${_target} PROPERTIES UNITY_BUILD ON)
    endforeach()

    if(WIN32)
        set(_RUNTIME_DLLS
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE:assimp::assimp>
            $<TARGET_FILE:freetype>
            "${VULKAN_RUNTIME_DLL}"
        )

        add_custom_command(TARGET QuarkCore POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_RUNTIME_DLLS}
                $<TARGET_FILE_DIR:QuarkCore>
            COMMENT "Copying DLLs next to QuarkCore..."
        )

        foreach(_target ${EXAMPLE_TARGETS})
            add_custom_command(TARGET ${_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:QuarkCore>
                    ${_RUNTIME_DLLS}
                    $<TARGET_FILE_DIR:${_target}>
                COMMENT "Copying DLLs for ${_target}..."
            )
        endforeach()
    endif()
endif()