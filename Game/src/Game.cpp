#include "Game.h"

scree::Game::Game()
	: simulation(material_registry)
{
	load_materials();

	InitWindow(Window_size.x, Window_size.y, "scree");
	int grid_size_px = static_cast<int>(GridSize * TileSize);
	Grid_offset.x = static_cast<float>((Window_size.x - grid_size_px) / 2);
	Grid_offset.y = static_cast<float>((Window_size.y - grid_size_px) / 2);
	SetTargetFPS(60);
	rlImGuiSetup(true);

	pixels.resize(GridSize * GridSize);

	Image blank = GenImageColor(GridSize, GridSize, BLACK);
	tex = LoadTextureFromImage(blank);
	UnloadImage(blank);                      // GPU has it now; drop the CPU copy
	SetTextureFilter(tex, TEXTURE_FILTER_POINT);

	frame = Rectangle{ Grid_offset.x, Grid_offset.y,
					   GridSize * TileSize, GridSize * TileSize };

	grid.Create(GridSize, GridSize,  &material_registry);
	srand(static_cast<unsigned int>(time(NULL)));
}

void scree::Game::update()
{
	if (!paused)
	{
		for (size_t i = 0; i < updates_per_frame; i++)
			simulation.Update_grid(grid);
	}
}

void scree::Game::render()
{
	BeginDrawing();
	ClearBackground(BLACK);

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
			for (int x = x0; x < x0 + CHUNK_SIZE; x++)
				out[x] = row[x].color.toRaylibColor();
		}
	}

	grid.Clear_chunks_to_render();

	UpdateTexture(tex, pixels.data());

	Rectangle src{ 0, 0, (float)GridSize, (float)GridSize };
	DrawTexturePro(tex, src, frame, Vector2{ 0, 0 }, 0.0f, WHITE);
	DrawRectangleLinesEx(frame, 5, WHITE);

	if (show_active_chunks)
	{
		active_chunks.clear();
		grid.Get_active_chunks(active_chunks);
		for (auto chunk: active_chunks)
		{
			DrawRectangleLines(static_cast<int>(chunk.first * 32 * TileSize + Grid_offset.x),
				static_cast<int>(chunk.second * 32 * TileSize + Grid_offset.y),
				static_cast<int>(32 * TileSize), static_cast<int>(32 * TileSize), RED);
		}
	}

	auto mouse_pos = GetMousePosition();

	DrawCircleLines(static_cast<int>(mouse_pos.x), static_cast<int>(mouse_pos.y), cursor_radius * TileSize, WHITE);
	
	auto ui_clock = std::chrono::high_resolution_clock::now();
	rlImGuiBegin();
	UI();
	rlImGuiEnd();
	UI_time = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - ui_clock).count();

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

void scree::Game::UI()
{
	ImGui::Begin("Material menu");
	
	if (material_load_error_message != "") {
		ImGui::Text("%s", material_load_error_message.c_str());
		if (ImGui::Button("Clear error message"))
			material_load_error_message = "";
	}

	for (int i = 0; i < material_registry.GetMaterialsCount(); i++)
	{
		MaterialID id = static_cast<MaterialID>(i);
		if (ImGui::Button(material_registry.GetName(id).c_str()))
			SelectedMaterial = id;
	}

	ImGui::Text("%s", material_registry.GetName(SelectedMaterial).c_str());
	auto mouse_pos = GetMousePosition();
	mouse_pos.x -= Grid_offset.x;
	mouse_pos.y -= Grid_offset.y;
	int hover_x = static_cast<int>(mouse_pos.x), hover_y = static_cast<int>(mouse_pos.y);
	if (grid.Is_in_bounds(hover_x, hover_y))
	{
		auto tile = grid.Get_at(static_cast<std::uint16_t>(hover_x), static_cast<std::uint16_t>(hover_y));
		ImGui::Text("Pixel under mouse %s", material_registry.GetName(tile.id).c_str());
	}

	if (ImGui::Button("Clear"))
		grid.Clear();

	ImGui::Checkbox("Pause", &paused);
	ImGui::SliderInt("Updates per frame", &updates_per_frame, 1, 10);
	if (ImGui::Button("Reload material file"))
		load_materials();

	ImGui::Checkbox("Show active chunks", &show_active_chunks);
	ImGui::Checkbox("Show benchmarks", &show_bench_marks);
	
	ImGui::End();

	if (show_bench_marks)
	{
		ImGui::Begin("BenchMarks");

		ImGui::Text("FPS: %f", 1.0f / delta);
		ImGui::Text("Update time: %f", update_time);
		ImGui::Text("Input time: %f", input_time);
		ImGui::Text("UI time: %f", UI_time);
		ImGui::Text("Render time: %f", render_time);
		
		ImGui::End();
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

		// render() times the UI block itself, so take it back out.
		bench_clock = clock::now();
		render();
		render_time = since(bench_clock) - UI_time;
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
	std::string path = std::string(GetApplicationDirectory()) + "assets/materials.json";
	bool success = new_registry.LoadMaterials(path);
	material_load_error_message = Log::FormatLogs(new_registry.GetLogs());
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
