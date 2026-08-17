# Catch2 lands in .deps alongside the runtime dependencies -- FETCHCONTENT_BASE_DIR is
# set once at the top level and applies here too.
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# MaterialRegistry.cpp is compiled into the test binary rather than linked out of Game:
# Game is an executable, and carving a library out of it for one source file is churn
# for its own sake.
add_executable(tests
    test_material_parsing.cpp
    test_canvas.cpp
    ../Game/src/MaterialRegistry.cpp
    ../Game/src/Canvas.cpp
)
target_include_directories(tests PRIVATE ../Game/src)

# raylib is here for rgb.h's Color, which is a plain struct -- nothing in these tests
# opens a window.
target_link_libraries(tests PRIVATE
    Catch2::Catch2WithMain
    nlohmann_json::nlohmann_json
    raylib
)

# Lets the golden test read the shipped asset where it lives, so there is no copy to
# fall out of date.
target_compile_definitions(tests PRIVATE
    SCREE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/Game/assets")

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(tests)
