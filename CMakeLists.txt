cmake_minimum_required(VERSION 3.21)

project(Scree LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# premake's startproject "Game" -- makes F5 work straight after opening the solution.
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Game)

# Must precede include(FetchContent): that module creates this cache entry
# itself, and a non-FORCE set() on an already-cached variable does nothing.
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.deps" CACHE PATH "")
include(FetchContent)

FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        6.0
    GIT_SHALLOW    TRUE
)

set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE
)

# Neither of these ships a CMakeLists, so MakeAvailable only downloads them --
# Game/CMakeLists.txt compiles them into the imgui-backend target.
# rlImGui's Raylib_6_0 tag was built against ImGui 1.92.1 and needs the new
# font API, so these two versions move together.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.1
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(rlimgui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG        Raylib_6_0
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(raylib nlohmann_json imgui rlimgui)

add_subdirectory(Game)

# Off when Scree is consumed as a subproject, and never installed or packaged -- the
# release workflow copies Scree.exe by name, so the test binary cannot follow it.
option(SCREE_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
if (SCREE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
