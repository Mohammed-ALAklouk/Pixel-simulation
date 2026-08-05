# --- Dear ImGui + the rlImGui raylib backend --------------------------------
#
# Neither project ships a CMakeLists of its own, so their sources are compiled
# here into one static library. ImGui's core must be compiled exactly once per
# binary, which is why it lives in a target rather than in Game's source list.

add_library(imgui-backend STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${rlimgui_SOURCE_DIR}/rlImGui.cpp"
 "src/vec2i.h")
target_include_directories(imgui-backend PUBLIC
    "${imgui_SOURCE_DIR}"
    "${rlimgui_SOURCE_DIR}"
)
target_link_libraries(imgui-backend PUBLIC raylib)

# --- Game -------------------------------------------------------------------

add_executable(Game
    src/main.cpp
    src/Game.cpp
    src/Simulation.cpp

    src/Block.h
    src/Chunk.h
    src/Game.h
    src/Grid.h
    src/MaterialRegistry.h
    src/Simulation.h
    src/rgba.h
 "src/vec2i.h")

target_link_libraries(Game PRIVATE imgui-backend nlohmann_json::nlohmann_json)

# Plain path, no generator expression: VS's folder/CMake mode reads this
# property raw from the file API and never evaluates genexes.
set_target_properties(Game PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# MaterialRegistry.h opens files under "assets/" relative to the exe's own
# directory (GetApplicationDirectory), not the working directory, so it finds
# them no matter how the exe was launched. A junction (not a copy) links the
# build output's assets/ straight to the source assets/, so editing a source
# file -- e.g. via the in-game "Reload material file" button -- is instantly
# visible with no rebuild, in dev and in a standalone build alike.
# `rmdir` (no /s) only unlinks the junction itself, never the linked-to
# content, so this is safe to rerun every build; `if exist` guards the first
# build, when there's nothing there yet to unlink.
add_custom_command(TARGET Game POST_BUILD
    COMMAND cmd /c if exist "$<TARGET_FILE_DIR:Game>/assets" rmdir "$<TARGET_FILE_DIR:Game>/assets"
    COMMAND cmd /c mklink /J "$<TARGET_FILE_DIR:Game>/assets" "${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
