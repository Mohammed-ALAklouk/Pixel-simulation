cmake_minimum_required(VERSION 3.21)

project(PixelSimulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# premake's startproject "Game" -- makes F5 work straight after opening the solution.
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Game)

add_subdirectory(Game)
