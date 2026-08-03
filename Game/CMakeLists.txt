# --- Vendored dependencies --------------------------------------------------

set(SFML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/SFML/lib/cmake/SFML")
find_package(SFML 2.6 REQUIRED COMPONENTS graphics window system)

# imgui-SFML's renderer makes raw GL calls, so it needs OpenGL directly.
find_package(OpenGL REQUIRED)

# Single header, included as <json.hpp>.
add_library(json_header INTERFACE)
target_include_directories(json_header
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/nlohmann")

# Dear ImGui + the imgui-SFML backend. premake picked these up via its **.cpp
# glob; here they get their own target so they build once and stay out of the
# game's warning output.
add_library(imgui-sfml STATIC
    Dependencies/ImGui/ImGui/imgui.cpp
    Dependencies/ImGui/ImGui/imgui_draw.cpp
    Dependencies/ImGui/ImGui/imgui_tables.cpp
    Dependencies/ImGui/ImGui/imgui_widgets.cpp
    Dependencies/ImGui/ImGui/imgui_demo.cpp
    Dependencies/ImGui/ImGui/imgui-SFML.cpp
)
target_include_directories(imgui-sfml PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/ImGui"        # for <ImGui/imgui.h>
    "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/ImGui/ImGui"  # for its own "imgui.h"
)
target_link_libraries(imgui-sfml PUBLIC sfml-graphics sfml-window sfml-system OpenGL::GL)

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
)

target_link_libraries(Game PRIVATE imgui-sfml json_header)

set_target_properties(Game PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Game>")

# MaterialRegistry.h opens "materials.json" relative to the working directory,
# and the SFML DLLs have to sit next to the exe.
add_custom_command(TARGET Game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/Materials.json"
            "$<TARGET_FILE_DIR:Game>/materials.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:Game> "$<TARGET_FILE_DIR:Game>"
    COMMAND_EXPAND_LISTS
)
