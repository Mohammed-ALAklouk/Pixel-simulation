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
)
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
)

target_link_libraries(Game PRIVATE imgui-backend nlohmann_json::nlohmann_json)

# Plain path, no generator expression: VS's folder/CMake mode reads this
# property raw from the file API and never evaluates genexes. Running from the
# source dir also means Materials.json is read in place, so the in-game
# "Reload material file" button picks up your edits.
set_target_properties(Game PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# MaterialRegistry.h opens "materials.json" relative to the working directory.
# Copying it next to the exe is what makes the build output runnable standalone.
# raylib links statically, so there are no DLLs to place beside it.
add_custom_command(TARGET Game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/Materials.json"
            "$<TARGET_FILE_DIR:Game>/materials.json"
)
