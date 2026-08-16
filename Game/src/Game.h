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

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>


namespace scree
{
	class Game
	{
	public:
		Game();
		
		void update();
		void render();
		void processInputs();
		void UI();
		void run();
		// Chrome sizes are fixed, so this only has to place the grid in what is left over.
		void compute_layout();
		void update_stats();
		void handle_hotkeys();
		void draw_stroke(const std::vector<Vector2>& points, const MaterialID material, const float radius);
		bool load_materials();
		// Resolved against the executable, not the working directory, so it holds wherever
		// the exe was launched from. Shared by the loader and the top bar's link.
		std::string materials_path() const;
		std::vector<MaterialID> get_registry_changes(const MaterialRegistry& new_registry);

		bool in_bound(int x, int y)			const { return (x >= 0 && y >= 0 && x < GridSize && y < GridSize); }

		
		std::vector<Vector2> GetLine(Vector2 start, Vector2 end);

		static constexpr uint32_t GridSize = 640;
		// Sized so the 640x640 grid still lands at 1:1 once the chrome has taken its share,
		// including when the loader banner is up.
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
		// Static dither drawn behind the grid, so empty space reads as textured rather
		// than as a black hole. Built once in the constructor.
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
		bool new_material_pannel_open = true;
		bool paused = false;
		// Set by the STEP button, consumed by the next update. The UI runs inside render(),
		// after update(), so the step it asks for lands on the following frame.
		bool step_once = false;

		std::string material_load_error_message;
		// Whether the message above is a failed load or only warnings, which is all the
		// banner needs to colour itself.
		bool material_load_failed = false;

		// 0 when there is nothing to report, which is what keeps the banner from taking
		// up any space at all in the common case.
		float layout_banner_h = 0.0f;
		// The region the grid is centred inside, once the chrome has taken its share.
		Rectangle canvas_region{};

		MaterialData newMaterial{};
		std::string newMaterialName = "";
		std::vector<Transition> newMaterialTransitions;
		std::vector<EditReaction> newMaterialReactions;
		// Points at newMaterial while creating; at a registry entry while editing one.
		MaterialData* editedMaterial = &newMaterial;
		MaterialID editedMaterialID = 0;
		bool confirm_delete_open = false;
		MaterialID pending_delete_id = 0;

		bool editing() const { return editedMaterial != &newMaterial; }
		void begin_new_material();
		void begin_edit_material(MaterialID id);
		void commit_material();
		void delete_material(MaterialID id);
		// Counting particles means walking all 640x640 blocks, so the readouts refresh on
		// a slower cadence than the frame does.
		static constexpr int StatsInterval = 12;
		int particle_count = 0;
		int awake_chunk_count = 0;
		int stats_counter = 0;
		float stats_delta_accum = 0.0f;
		float fps_display = 60.0f;
	};
}