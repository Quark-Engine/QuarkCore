cmake_minimum_required(VERSION 3.25)

project(QuarkCore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(VCPKG_MANIFEST_INSTALL        OFF CACHE BOOL "" FORCE)
set(VCPKG_APPLOCAL_DEPS           OFF CACHE BOOL "" FORCE)
set(X_VCPKG_APPLOCAL_DEPS_INSTALL OFF CACHE BOOL "" FORCE)

include(FetchContent)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(FETCHCONTENT_QUIET ON)

if(WIN32)
    FetchContent_Declare(zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG        v1.3.1
        GIT_SHALLOW    TRUE
        SYSTEM
    )
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)

    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
    set(ZLIB_FOUND          TRUE)
    set(ZLIB_INCLUDE_DIRS   "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
    set(ZLIB_LIBRARIES      zlibstatic)
    set(ZLIB_VERSION_STRING "1.3.1")

    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.4.0
        GIT_SHALLOW    TRUE
        SYSTEM
    )
    set(SDL_SHARED ON  CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST   OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
    set(QUARK_SDL_TARGET SDL3::SDL3)

    FetchContent_Declare(glew
        GIT_REPOSITORY https://github.com/Perlmint/glew-cmake.git
        GIT_TAG        glew-cmake-2.2.0
        GIT_SHALLOW    TRUE
        SYSTEM
    )
    set(glew-cmake_BUILD_SHARED ON  CACHE BOOL "" FORCE)
    set(glew-cmake_BUILD_STATIC OFF CACHE BOOL "" FORCE)
    set(ONLY_LIBS               ON  CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glew)
    set(QUARK_GLEW_TARGET libglew_shared)

    FetchContent_Declare(assimp
        GIT_REPOSITORY https://github.com/assimp/assimp.git
        GIT_TAG        v5.4.3
        GIT_SHALLOW    TRUE
        SYSTEM
    )
    set(ASSIMP_BUILD_TESTS        OFF CACHE BOOL "" FORCE)
    set(ASSIMP_INSTALL            OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ZLIB         OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS         ON  CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(assimp)
    set(BUILD_SHARED_LIBS         OFF CACHE BOOL "" FORCE)
    set(QUARK_ASSIMP_TARGET assimp::assimp)

    FetchContent_Declare(libpng
        GIT_REPOSITORY https://github.com/glennrp/libpng.git
        GIT_TAG        v1.6.47
        GIT_SHALLOW    TRUE
        SYSTEM
    )
    set(PNG_SHARED     ON  CACHE BOOL "" FORCE)
    set(PNG_STATIC     OFF CACHE BOOL "" FORCE)
    set(PNG_TESTS      OFF CACHE BOOL "" FORCE)
    set(PNG_TOOLS      OFF CACHE BOOL "" FORCE)
    set(PNG_BUILD_ZLIB OFF CACHE BOOL "" FORCE)
    set(CMAKE_SKIP_INSTALL_RULES TRUE)
    FetchContent_MakeAvailable(libpng)
    set(CMAKE_SKIP_INSTALL_RULES FALSE)
    set(QUARK_PNG_TARGET png_shared)

    if(TARGET png_shared AND TARGET zlibstatic)
        target_include_directories(png_shared PRIVATE
            "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}"
        )
        target_link_libraries(png_shared PRIVATE zlibstatic)
    endif()

    foreach(t SDL3 SDL_uclibc libglew_shared png16 png_shared zlib zlibstatic assimp)
        if(TARGET ${t})
            target_compile_options(${t} PRIVATE /w)
        endif()
    endforeach()

else()
    #  Debian/Ubuntu:  sudo apt install libsdl3-dev libglew-dev libpng-dev libassimp-dev
    #  Arch:           sudo pacman -S sdl3 glew libpng assimp
    #  Fedora:         sudo dnf install SDL3-devel glew-devel libpng-devel assimp-devel
    #  macOS:          brew install sdl3 glew libpng assimp
    find_package(SDL3   REQUIRED)
    find_package(GLEW   REQUIRED)
    find_package(PNG    REQUIRED)
    find_package(assimp REQUIRED)

    set(QUARK_SDL_TARGET   SDL3::SDL3)
    set(QUARK_GLEW_TARGET  GLEW::GLEW)
    set(QUARK_PNG_TARGET   PNG::PNG)
    set(QUARK_ASSIMP_TARGET assimp::assimp)

endif()

find_package(OpenGL REQUIRED)

add_library(QuarkCore STATIC
    src/QuarkCore.cpp
)

target_include_directories(QuarkCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(QuarkCore PUBLIC
    ${QUARK_SDL_TARGET}
    ${QUARK_GLEW_TARGET}
    ${QUARK_PNG_TARGET}
    OpenGL::GL
    ${QUARK_ASSIMP_TARGET}
)

target_compile_definitions(QuarkCore PRIVATE NOMINMAX)

if(MSVC)
    target_compile_options(QuarkCore PRIVATE /W4 /permissive-)
else()
    target_compile_options(QuarkCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(quarkcore_sandbox examples/sandbox.cpp)
target_link_libraries(quarkcore_sandbox PRIVATE QuarkCore)

if(WIN32)
    add_custom_command(TARGET quarkcore_sandbox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE:libglew_shared>
            $<TARGET_FILE:png_shared>
            $<TARGET_FILE:assimp::assimp>
            $<TARGET_FILE_DIR:quarkcore_sandbox>
        COMMENT "Copying DLLs..."
    )
endif()