cmake_minimum_required(VERSION 3.21)

project(PixelSimulator LANGUAGES CXX)

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
FetchContent_MakeAvailable(raylib)

add_subdirectory(Game)
