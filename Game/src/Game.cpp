#include "Game.h"
#include "UI.h"

scree::Game::Game()
	: simulation(material_registry)
{
	load_materials();

	// The chrome is fixed but the grid is centred in whatever is left, so the layout
	// survives a resize -- compute_layout re-runs every frame off the live window size.
	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	InitWindow(Window_size.x, Window_size.y, "scree");
	// Maximised after the window exists, not via FLAG_WINDOW_MAXIMIZED: the flag makes GLFW
	// open it maximised without raising the resize callback, so raylib keeps reporting the
	// requested size and the whole interface renders into the top-left corner of a much
	// larger window. Window_size stays the size it restores down to.
	MaximizeWindow();
	SetWindowMinSize(1300, 820);
	SetTargetFPS(60);
	rlImGuiSetup(true);
	ui::LoadFonts();
	// Before ApplyTheme, whose initial UpdateTheme snaps the chrome to ActiveTheme -- so the
	// first frame is already on the saved theme rather than easing over from the default.
	ui::LoadSettings();
	ui::ApplyTheme();

	pixels.resize(GridSize * GridSize);

	Image blank = GenImageColor(GridSize, GridSize, BLACK);
	tex = LoadTextureFromImage(blank);
	UnloadImage(blank);                      // GPU has it now; drop the CPU copy
	SetTextureFilter(tex, TEXTURE_FILTER_POINT);

	// The backdrop, from the prototype: empty space there is a two-step dither rather than
	// flat black. Generated once -- it never changes, so it costs one texture and one draw.
	{
		std::vector<Color> backdrop(static_cast<std::size_t>(GridSize) * GridSize);
		for (Color& px : backdrop)
			px = (fast_rand() & 1) ? ui::col::GridBgHigh : ui::col::GridBgLow;

		Image noise{ backdrop.data(), static_cast<int>(GridSize), static_cast<int>(GridSize), 1,
					 PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
		grid_bg = LoadTextureFromImage(noise);   // uploads a copy; `backdrop` can go
		SetTextureFilter(grid_bg, TEXTURE_FILTER_POINT);
	}

	grid.Create(GridSize, GridSize,  &material_registry);
	// `pixels` starts fully transparent, and the renderer only converts chunks marked
	// dirty -- so a chunk untouched since startup would never be written and the canvas
	// behind would show through it. Clear marks every chunk, so frame one fills the lot.
	grid.Clear();
	compute_layout();
	srand(static_cast<unsigned int>(time(NULL)));
}

void scree::Game::compute_layout()
{
	Window_size.x = GetScreenWidth();
	Window_size.y = GetScreenHeight();

	if (material_load_error_message.empty())
	{
		layout_banner_h = 0.0f;
	}
	else
	{
		// A single log line is often long enough to wrap, so count the rows it will
		// actually occupy rather than the newlines it contains.
		const float text_width = static_cast<float>(Window_size.x)
			- ui::metrics::BannerTextInset - ui::metrics::BannerTextRight;
		const int per_line = std::max(20, static_cast<int>(text_width / ui::metrics::BannerGlyphW));

		int lines = 0;
		for (std::size_t start = 0; start < material_load_error_message.size(); )
		{
			std::size_t end = material_load_error_message.find('\n', start);
			if (end == std::string::npos) end = material_load_error_message.size();

			const int length = static_cast<int>(end - start);
			lines += std::max(1, (length + per_line - 1) / per_line);
			start = end + 1;
		}

		lines = std::clamp(lines, 1, ui::metrics::BannerMaxLines);
		layout_banner_h = lines * ui::metrics::BannerLineH + ui::metrics::BannerPad;
	}

	const float top = ui::metrics::TopBarH + layout_banner_h;
	canvas_region = Rectangle{
		ui::metrics::LeftRailW,
		top,
		static_cast<float>(Window_size.x) - ui::metrics::LeftRailW,
		static_cast<float>(Window_size.y) - top - ui::metrics::BottomBarH
	};

	// Never magnified past 1:1 -- a fractional tile size larger than a pixel would make
	// some grid cells a pixel wider than their neighbours under point filtering.
	const float room = ui::metrics::CanvasPad * 2.0f;
	const float fit = std::min(canvas_region.width - room, canvas_region.height - room) / GridSize;
	TileSize = std::clamp(fit, 0.1f, 1.0f);

	const float side = GridSize * TileSize;
	Grid_offset.x = canvas_region.x + (canvas_region.width - side) * 0.5f;
	Grid_offset.y = canvas_region.y + (canvas_region.height - side) * 0.5f;
	frame = Rectangle{ Grid_offset.x, Grid_offset.y, side, side };
}

void scree::Game::update_stats()
{
	stats_delta_accum += delta;
	if (++stats_counter < StatsInterval) return;

	if (stats_delta_accum > 0.0f)
		fps_display = stats_counter / stats_delta_accum;
	stats_counter = 0;
	stats_delta_accum = 0.0f;

	awake_chunk_count = 0;
	for (int chunk_y = 0; chunk_y < grid.Get_height_chunks(); chunk_y++)
		for (int chunk_x = 0; chunk_x < grid.Get_width_chunks(); chunk_x++)
			if (grid.Is_chunk_active(chunk_x, chunk_y)) awake_chunk_count++;

	int particles = 0;
	for (int y = 0; y < grid.Get_height_px(); y++)
	{
		const Block* row = grid.Row(y);
		for (int x = 0; x < grid.Get_width_px(); x++)
			if (row[x].id != MaterialRegistry::AIR_ID) particles++;
	}
	particle_count = particles;
}

void scree::Game::update()
{
	if (!paused)
	{
		for (int i = 0; i < updates_per_frame; i++)
			simulation.Update_grid(grid);
	}
	else if (step_once)
	{
		simulation.Update_grid(grid);
	}

	step_once = false;
}

void scree::Game::render()
{
	auto render_clock = std::chrono::high_resolution_clock::now();

	compute_layout();
	// Re-tints the whole chrome off the theme chosen in the bottom bar. Done before drawing
	// so the backgrounds below agree with the panels rlImGui puts on top of them; the ease
	// inside means switching theme sweeps across rather than snapping.
	ui::UpdateTheme(ui::ThemeOptions[ui::ActiveTheme].seed, delta);

	BeginDrawing();
	ClearBackground(ui::theme.WindowBg);
	DrawRectangleRec(canvas_region, ui::theme.CanvasBg);

	// Update_grid marks the mask itself, but it is not the only thing that writes to
	// the grid: the brush and a material reload both land between updates, and while
	// paused they are the only writes there are. Marking again here picks those up.
	grid.Mark_chunks_for_render();

	// Only the chunks that changed are converted -- the rest of `pixels` still holds
	// what they looked like last frame, which is still what they look like now. The
	// upload below is unchanged and still covers the whole buffer: one UpdateTexture
	// costs ~0.2ms, while a per-chunk UpdateTextureRec pays driver overhead per call
	// and overtakes it once a scene gets busy.
	for (int y = 0; y != GridSize; y++)
	{
		int chunk_y = y >> CHUNK_SHIFT; // equivalent to y / CHUNK_SIZE, but faster
		for (int chunk_x = 0; chunk_x != grid.Get_width_chunks(); chunk_x++)
		{
			if (!grid.Should_render_chunk(chunk_x, chunk_y)) continue;
			
			int x0 = chunk_x << CHUNK_SHIFT; // equivalent to chunk_x * CHUNK_SIZE, but faster
			const Block* row = grid.Row(y);
			Color* out = pixels.data() + y * GridSize;
			// Air is written clear rather than black so the backdrop drawn under the grid
			// shows through it; every other material stays fully opaque.
			for (int x = x0; x < x0 + CHUNK_SIZE; x++)
				out[x] = (row[x].id == MaterialRegistry::AIR_ID)
					? Color{ 0, 0, 0, 0 }
					: row[x].color.toRaylibColor();
		}
	}

	grid.Clear_chunks_to_render();

	UpdateTexture(tex, pixels.data());

	Rectangle src{ 0, 0, (float)GridSize, (float)GridSize };
	DrawTexturePro(grid_bg, src, frame, Vector2{ 0, 0 }, 0.0f, WHITE);
	DrawTexturePro(tex, src, frame, Vector2{ 0, 0 }, 0.0f, WHITE);
	DrawRectangleLinesEx(frame, 1, ui::col::GridEdge);

	if (show_active_chunks)
	{
		active_chunks.clear();
		grid.Get_active_chunks(active_chunks);
		for (auto chunk: active_chunks)
		{
			DrawRectangleLines(static_cast<int>(chunk.first * CHUNK_SIZE * TileSize + Grid_offset.x),
				static_cast<int>(chunk.second * CHUNK_SIZE * TileSize + Grid_offset.y),
				static_cast<int>(CHUNK_SIZE * TileSize), static_cast<int>(CHUNK_SIZE * TileSize),
				ui::col::ChunkEdge);
		}
	}

	// WantCaptureMouse is last frame's -- ImGui has not started this one yet -- which is
	// close enough to keep the brush ring off the panels.
	if (!ImGui::GetIO().WantCaptureMouse)
	{
		auto mouse_pos = GetMousePosition();
		DrawCircleLines(static_cast<int>(mouse_pos.x), static_cast<int>(mouse_pos.y),
			cursor_radius * TileSize, ui::theme.Ring);
	}

	auto ui_clock = std::chrono::high_resolution_clock::now();
	rlImGuiBegin();
	UI();
	rlImGuiEnd();
	UI_time = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - ui_clock).count();

	// Everything above is the actual work of building the frame. EndDrawing below blocks on
	// the 60 FPS frame limiter -- pure idle, not rendering -- so it is left out of the span.
	render_time = std::chrono::duration<float>(
		std::chrono::high_resolution_clock::now() - render_clock).count() - UI_time;

	EndDrawing();
}

void scree::Game::processInputs()
{
	float mouse_wheel = GetMouseWheelMove();
	cursor_radius += mouse_wheel;
	if (cursor_radius < 1)
		cursor_radius = 1;
	if (cursor_radius > 100)
		cursor_radius = 100;


	handle_hotkeys();

	bool mouse_left_down = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
	bool mouse_right_down = IsMouseButtonDown(MOUSE_RIGHT_BUTTON);

	Vector2 mouse_pos = GetMousePosition();
	mouse_pos.x -= Grid_offset.x;
	mouse_pos.y -= Grid_offset.y;

	int mouse_x = static_cast<int>(mouse_pos.x / TileSize), mouse_y = static_cast<int>(mouse_pos.y / TileSize);
	
	
	if ((!mouse_left_down && !mouse_right_down) || ImGui::GetIO().WantCaptureMouse) {
		is_drawing_cursor = false;
	}
	else if ((mouse_left_down || mouse_right_down) && !is_drawing_cursor) {
		cursor_start = mouse_pos;
		is_drawing_cursor = true;
	}

	

	if (is_drawing_cursor)
	{
		Vector2 cursor_end = mouse_pos;
		auto line = GetLine(cursor_start, cursor_end);
		MaterialID material = static_cast<MaterialID>(mouse_left_down ? SelectedMaterial : MaterialRegistry::AIR_ID);
		draw_stroke(line, material, cursor_radius);
		cursor_start = cursor_end;
	}
	
}

std::string scree::Game::materials_path() const
{
	return std::string(GetApplicationDirectory()) + "assets/materials.json";
}

void scree::Game::handle_hotkeys()
{
	if (ImGui::GetIO().WantCaptureKeyboard) return;

	if (IsKeyPressed(KEY_SPACE)) paused = !paused;
	if (IsKeyPressed(KEY_C)) grid.Clear();
	if (IsKeyPressed(KEY_R)) load_materials();

	// 1-9 map onto the first nine file materials, which is what the rail's "1-9" says.
	for (int key = KEY_ONE; key <= KEY_NINE; key++)
	{
		if (!IsKeyPressed(key)) continue;
		const int id = key - KEY_ONE + 1;
		if (id < material_registry.GetMaterialsCount())
			SelectedMaterial = static_cast<MaterialID>(id);
	}
}

void scree::Game::run()
{
	using clock = std::chrono::high_resolution_clock;
	auto since = [](clock::time_point from) {
		return std::chrono::duration<float>(clock::now() - from).count();
	};

	auto delta_clock = clock::now();
	while (!WindowShouldClose())
	{
		delta = since(delta_clock);
		delta_clock = clock::now();

		auto bench_clock = clock::now();
		processInputs();
		input_time = since(bench_clock);

		bench_clock = clock::now();
		update();
		update_time = since(bench_clock);

		// render() sets render_time itself, measured to exclude the frame-limiter wait.
		render();

		update_stats();
	}
}

void scree::Game::draw_stroke(const std::vector<Vector2>& points, const MaterialID material, const float radius)
{
	int min_y = std::numeric_limits<int>::max();
	int max_y = std::numeric_limits<int>::min();
	for (auto point : points) {
		min_y = std::min(min_y, int(point.y - radius));
		max_y = std::max(max_y, int(point.y + radius));
	}

	for (int y = min_y; y <= max_y; y++)
	{
		if (y < 0 || y >= GridSize) continue;
		int min_x = std::numeric_limits<int>::max();
		int max_x = std::numeric_limits<int>::min();

		for (auto point : points) {
			int vertical_distance = y - point.y;
			int horizontal_reach_squared = int(radius) * int(radius) - vertical_distance * vertical_distance;
			if (horizontal_reach_squared <= 0) continue;
			int half_width = int(sqrt(horizontal_reach_squared));
			if (half_width * half_width >= horizontal_reach_squared) half_width--;
			min_x = std::min(min_x, int(point.x) - half_width);
			max_x = std::max(max_x, int(point.x) + half_width);
		}

		if (min_x > max_x) continue;
		min_x = std::max(min_x, 0);
		max_x = std::min(max_x, int(GridSize) - 1);

		for (int x = min_x; x <= max_x; x++)
		{
			if (material == MaterialRegistry::AIR_ID ||
				grid.Get_at(x, y).id == MaterialRegistry::AIR_ID)
			{
				grid.Set_at(x, y, material_registry.CreateBlock(material));
			}
		}
	}
}

bool scree::Game::load_materials()
{
	MaterialRegistry new_registry;
	bool success = new_registry.LoadMaterials(materials_path());
	material_load_error_message = Log::FormatLogs(new_registry.GetLogs());
	// A rejected material still counts as failure for the banner even though the load
	// itself succeeded -- something in the file is unusable either way.
	material_load_failed = !new_registry.GetLogs().empty() &&
		Log::Worst(new_registry.GetLogs()) != Log::Severity::Warning;
	if (success) {
		auto remap = get_registry_changes(new_registry);
		material_registry = std::move(new_registry);
		grid.Remap(remap);
		SelectedMaterial = MaterialRegistry::AIR_ID;
	}
	
	return success;
}

std::vector<scree::MaterialID> scree::Game::get_registry_changes(const MaterialRegistry& new_registry)
{
	std::vector<MaterialID> remap(material_registry.GetMaterialsCount(), MaterialRegistry::AIR_ID);

	for (int i = 0; i < material_registry.GetMaterialsCount(); i++)
	{
		MaterialID id = static_cast<MaterialID>(i);
		auto name = material_registry.GetName(id);
		for (int j = 0; j < new_registry.GetMaterialsCount(); j++)
		{
			MaterialID new_id = static_cast<MaterialID>(j);
			if (new_registry.GetName(new_id) == name)
			{
				remap[id] = new_id;
				break;
			}
		}
	}
	return remap;
}

std::vector<Vector2> scree::Game::GetLine(Vector2 start, Vector2 end)
{
	if (start.x == end.x && start.y == end.y) return { start };
	
	std::vector<Vector2> points;
	
	float dx = end.x - start.x;
	float dy = end.y - start.y;

	int steps = static_cast<int>(std::max(fabs(dy), fabs(dx)));

	float Xinc = dx / steps;
	float Yinc = dy / steps;

	for (int i = 0; i < steps; i++)
	{
		int final_x = static_cast<int>(start.x + Xinc * i);
		int final_y = static_cast<int>(start.y + Yinc * i);
		points.push_back(Vector2{ static_cast<float>(final_x), static_cast<float>(final_y) });
	}


	return points;
}
