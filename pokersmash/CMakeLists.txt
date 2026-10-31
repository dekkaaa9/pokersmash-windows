# pokersmash - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(pokersmash LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Locate the ReXGlue SDK.
#
# The SDK lives outside this repository, so find_package(rexglue) needs to be
# told where to look. Two ways:
#
#   1. -DCMAKE_PREFIX_PATH=<path>   SDK install prefix (any location)
#   2. The documented layout: work/rexglue-sdk next to the repository, already
#      built and installed to out/install/win-amd64.
#
# Without this, a fresh clone fails at find_package(rexglue).
#
# Note: pointing REXSDK_DIR at the SDK *source* tree also works in principle,
# but the SDK's own build applies -msse4.1 per target and rexcore does not get
# it when pulled in via add_subdirectory, so the source-tree route fails to
# compile. Build and install the SDK instead.
# ---------------------------------------------------------------------------
if(NOT CMAKE_PREFIX_PATH)
    # The project lives in <repo>/pokersmash, so the SDK checkout sits one
    # level up, next to the repository root.
    set(_rexglue_sdk_install
        "${CMAKE_CURRENT_SOURCE_DIR}/../work/rexglue-sdk/out/install/win-amd64")

    if(EXISTS "${_rexglue_sdk_install}/lib/cmake/rexglue")
        set(CMAKE_PREFIX_PATH "${_rexglue_sdk_install}"
            CACHE PATH "Search prefix for find_package()" FORCE)
        message(STATUS "Using installed ReXGlue SDK: ${CMAKE_PREFIX_PATH}")
    else()
        message(FATAL_ERROR
            "ReXGlue SDK not found. Either:\n"
            "  - Pass -DCMAKE_PREFIX_PATH=<path to the SDK install prefix>\n"
            "  - Build and install the SDK to work/rexglue-sdk next to this repository\n"
            "See README.md, 'Build and install the ReXGlue SDK'.")
    endif()
endif()

include(generated/rexglue.cmake)

# ---------------------------------------------------------------------------
# Codegen must have run before the first configure.
#
# generated/rexglue.cmake reads generated/default/sources.cmake at configure
# time to build the recompiled source list. Without it the recompiled objects
# are silently left out of the link and you get a confusing
# "undefined symbol: rex::PPCImageConfig" at link time instead.
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/sources.cmake")
    message(FATAL_ERROR
        "Codegen has not been run yet - generated/default/sources.cmake is missing.\n"
        "Run this first:\n"
        "  rexglue codegen pokersmash_manifest.toml\n"
        "It needs the game data in place (../extracted/default.xex).\n"
        "See README.md, 'Generate the code, then configure and build'.")
endif()

# Sources
set(POKERSMASH_SOURCES
    src/main.cpp
    src/stubs.cpp
)

if(WIN32)
    add_executable(pokersmash WIN32 ${POKERSMASH_SOURCES})
else()
    add_executable(pokersmash ${POKERSMASH_SOURCES})
endif()

# GPU_PLUGINS xenos makes rexglue_configure_target stage rexgpu-xenos.dll next
# to the executable. The plugin is runtime-loaded rather than linked, so
# TARGET_RUNTIME_DLLS does not pick it up on its own.
rexglue_setup_target(pokersmash GPU_PLUGINS xenos)

# rexruntime.dll links the Tracy profiler client, but it is not part of the link
# interface, so TARGET_RUNTIME_DLLS does not stage it. Without it the runtime
# fails to resolve the import at startup.
if(TARGET rex::TracyClient)
    add_custom_command(TARGET pokersmash POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:rex::TracyClient>"
            "$<TARGET_FILE_DIR:pokersmash>"
        VERBATIM)
endif()

# ---------------------------------------------------------------------------
# Launcher
#
# A standalone GUI that configures and starts pokersmash.exe. It links the same
# runtime (for the SDL3 window, D3D12 presenter and Dear ImGui layer) but takes
# no part in codegen: no recompiled sources, no GPU plugin, no guest code. It is
# configured with rexglue_configure_target directly rather than
# rexglue_setup_target, which is what would pull in the generated code.
# ---------------------------------------------------------------------------
if(WIN32)
    set(LAUNCHER_SOURCES
        src/launcher/main.cpp
        src/launcher/launcher_app.cpp
    )

    add_executable(pokersmash_launcher WIN32 ${LAUNCHER_SOURCES})

    target_include_directories(pokersmash_launcher PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(pokersmash_launcher PRIVATE rex::runtime)

    # Provides the SDL3 entry point, the ReXApp base source, the Windows runtime
    # DLL staging and the build-config define. GPU_PLUGINS xenos also stages
    # rexgpu-xenos.dll, which the launcher passes on to the game it spawns.
    rexglue_configure_target(pokersmash_launcher GPU_PLUGINS xenos)

    if(TARGET rex::TracyClient)
        add_custom_command(TARGET pokersmash_launcher POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:rex::TracyClient>"
                "$<TARGET_FILE_DIR:pokersmash_launcher>"
            VERBATIM)
    endif()

    # The launcher starts the game, so stage it next to the launcher on every
    # build. The game itself is only built when its target is requested.
    add_dependencies(pokersmash_launcher pokersmash)
    add_custom_command(TARGET pokersmash_launcher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:pokersmash>"
            "$<TARGET_FILE_DIR:pokersmash_launcher>"
        VERBATIM)
endif()
