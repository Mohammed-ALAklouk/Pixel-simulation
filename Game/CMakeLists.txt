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
    src/MaterialRegistry.cpp

    src/Block.h
    src/Chunk.h
    src/Game.h
    src/Grid.h
    
    src/Simulation.h
    src/rgb.h
    src/fast_rand.h
    src/vec2i.h
    src/MaterialData.h
    src/MaterialRegistry.h
)

target_link_libraries(Game PRIVATE imgui-backend nlohmann_json::nlohmann_json)

# The target stays `Game` -- it is referenced by VS_STARTUP_PROJECT and by the
# asset-link commands below -- while the binary it produces ships as Scree.exe.
# OUTPUT_NAME does not affect TARGET_FILE_DIR, so the junction below is unmoved.
#
# Plain path, no generator expression: VS's folder/CMake mode reads this
# property raw from the file API and never evaluates genexes.
set_target_properties(Game PROPERTIES
    OUTPUT_NAME Scree
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# MaterialRegistry.h opens files under "assets/" relative to the exe's own
# directory (GetApplicationDirectory), not the working directory, so it finds
# them no matter how the exe was launched. A link (not a copy) points the build
# output's assets/ straight at the source assets/, so editing a source file --
# e.g. via the in-game "Reload material file" button -- is instantly visible
# with no rebuild, in dev and in a standalone build alike.
#
# Windows gets a junction rather than `cmake -E create_symlink`: real symlinks
# there need either admin rights or Developer Mode, junctions never do.
# `rmdir` (no /s) only unlinks the junction itself, never the linked-to
# content, so this is safe to rerun every build; `if exist` guards the first
# build, when there's nothing there yet to unlink.
#
# SHELL_PATH and VERBATIM are both load-bearing here. The quotes below are
# CMake's own string delimiters, not shell quotes -- CMake re-quotes an argument
# only when it contains a space -- and cmd's builtins read a leading `/` as a
# switch. So a forward-slash path under a space-free directory came out bare and
# cmd rejected it: `D:/a/repo/...` failed with `Invalid switch - "a"`.
# SHELL_PATH converts to backslashes, VERBATIM adds quotes when they're needed.
if (WIN32)
    set(game_assets_link "$<SHELL_PATH:$<TARGET_FILE_DIR:Game>/assets>")
    add_custom_command(TARGET Game POST_BUILD
        COMMAND cmd /c if exist ${game_assets_link} rmdir ${game_assets_link}
        COMMAND cmd /c mklink /J ${game_assets_link}
                "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/assets>"
        VERBATIM
    )
else()
    # `rm -f` on a symlink-to-directory removes the link, not what it points at
    # (std::filesystem::remove does not follow symlinks), so this too is safe to
    # rerun. It is needed because create_symlink fails if the path already exists.
    add_custom_command(TARGET Game POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -f "$<TARGET_FILE_DIR:Game>/assets"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:Game>/assets"
        VERBATIM
    )
endif()
