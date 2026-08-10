#pragma once

#include "Grid.h"
#include "Simulation.h"
#include "MaterialRegistry.h"
#include "vec2i.h"

#include <imgui.h>
#include <rlImGui.h>
#include <raylib.h>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace scree
{
	
	class Game
	{
	public:
		Game();
		
		void update();
		void render();
		void proccessInputs();
		void UI();
		void run();
		void draw_cursor(Vector2 pos, MaterialID material);
		bool load_materials();
		std::vector<MaterialID> get_registry_changes(const MaterialRegistry& new_registry);

		bool in_bound(int x, int y)			const { return (x >= 0 && y >= 0 && x < GridSize && y < GridSize); }

		
		std::vector<Vector2> GetLine(Vector2 start, Vector2 end);

		static constexpr uint32_t GridSize = 640;
		Vec2i Window_size = Vec2i{ 1400, 900 };
		Vector2 Grid_offset = Vector2{ 0.0f, 0.0f };
		float TileSize = 1;
		
		// Declared before grid and simulation: both bind to it at construction, and
		// member initialisation follows declaration order.
		MaterialRegistry material_registry;
		Grid grid;
		Simulation simulation;
		MaterialID SelectedMaterial = 0;

		std::vector<Color> pixels;
		// tex
		Texture2D tex;
		Rectangle frame;


		Vector2 mouse_down_pos;

		bool is_drawing_cursor = false;
		Vector2 cursor_start;
		float cursor_radius = 15;

		std::vector<std::pair<int, int>> active_chunks;

		int  updates_per_frame = 3;

		float input_time = 0;
		float UI_time = 0;
		float update_time = 0;
		float render_time = 0;
		float delta = 0;

		bool show_active_chunks = false;
		bool show_bench_marks = false;
		bool paused = false;

		std::string material_load_error_message;
	};
}