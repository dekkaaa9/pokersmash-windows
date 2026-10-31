# pokersmash - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(pokersmash LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

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

rexglue_setup_target(pokersmash)

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
    # DLL staging and the build-config define.
    rexglue_configure_target(pokersmash_launcher)

    # The launcher starts the game, so stage it next to the launcher on every
    # build. The game itself is only built when its target is requested.
    add_dependencies(pokersmash_launcher pokersmash)
    add_custom_command(TARGET pokersmash_launcher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:pokersmash>"
            "$<TARGET_FILE_DIR:pokersmash_launcher>"
        VERBATIM)
endif()
