cmake_minimum_required(VERSION 3.22)

include(cmake/GrooveIds.cmake)

# Universal binary by default on macOS (Apple Silicon + Intel). Must be set
# before project(). Pass -DCMAKE_OSX_ARCHITECTURES=arm64 for faster dev builds.
if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(GrooveKnobs VERSION ${FF_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(GK_BUILD_TESTS "Build the test suite and screenshot generator" ON)
option(GK_BUILD_TOOLS "Build the offline tools (ff_render)" ON)
option(GK_WARNINGS_AS_ERRORS "Treat warnings in our code as errors" OFF)
# Off by default: a build copying into ~/Library/Audio/Plug-Ins replaced the
# plug-ins under a running DAW (Live: "plug-in not found" when it scanned a
# half-copied bundle). Install with the .pkg / .exe; -DGK_COPY_PLUGINS=ON to opt in.
option(GK_COPY_PLUGINS "Copy plugins to the plug-in folders after build" OFF)

include(cmake/GrooveWarnings.cmake)
include(cmake/GrooveSigning.cmake)

# ---------------------------------------------------------------------------
# Dependencies (pinned). Sources are shared between build trees in .deps/.
# ---------------------------------------------------------------------------
include(FetchContent)
if(NOT FETCHCONTENT_BASE_DIR OR FETCHCONTENT_BASE_DIR STREQUAL "${CMAKE_BINARY_DIR}/_deps")
    set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.deps" CACHE PATH "" FORCE)
endif()

set(GK_JUCE_TAG   "8.0.15")
set(GK_CATCH2_TAG "v3.16.1")

FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${GK_JUCE_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(juce)

# ---------------------------------------------------------------------------
# Shared module + plugins + tools + tests
# ---------------------------------------------------------------------------
juce_add_module(modules/groove_core ALIAS_NAMESPACE groove)

# Shared UI assets (display font + its SIL OFL licence). Consumers of
# groove_core must link GrooveCoreAssets.
juce_add_binary_data(GrooveCoreAssets
    HEADER_NAME GrooveCoreAssets.h
    NAMESPACE   GrooveAssets
    SOURCES
        modules/groove_core/assets/LilitaOne-Regular.ttf
        modules/groove_core/assets/LilitaOne-OFL.txt)
set_target_properties(GrooveCoreAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_subdirectory(plugins/french_filter)
add_subdirectory(plugins/boogie_trio)
add_subdirectory(plugins/backwash)
add_subdirectory(plugins/pew_pew)
add_subdirectory(plugins/sing_it_back)
add_subdirectory(plugins/kick_lock)
add_subdirectory(plugins/kick_lock_send)
add_subdirectory(plugins/slapper)
add_subdirectory(plugins/crackle)

if(GK_BUILD_TOOLS)
    add_subdirectory(tools/ff_render)
    add_subdirectory(tools/asset_prep)
    add_subdirectory(tools/bt_render)
    add_subdirectory(tools/bw_render)
    add_subdirectory(tools/gb_render)
    add_subdirectory(tools/sib_render)
    add_subdirectory(tools/kl_render)
    add_subdirectory(tools/cr_render)
endif()

if(GK_BUILD_TESTS)
    FetchContent_Declare(catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        ${GK_CATCH2_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(catch2)
    enable_testing()
    add_subdirectory(tests)
endif()
