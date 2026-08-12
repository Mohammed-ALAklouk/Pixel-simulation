#include "Game.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>

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
	for (int y = 0; y < GridSize; y++)
		for (int x = 0; x < GridSize; x++)
			pixels[y * GridSize + x] = grid.Get_at(x, y).color.toRaylibColor();

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

		for (auto point: line)
			draw_cursor(point, material);

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

void scree::Game::draw_cursor(Vector2 pos, MaterialID material)
{
	int radius = static_cast<int>(cursor_radius);
	int radiusSquared = radius * radius;

	for (int x = -radius; x < radius; x++)
	{
		for (int y = -radius; y < radius; y++)
		{
			int world_x = static_cast<int>(pos.x) + x;
			int world_y = static_cast<int>(pos.y) + y;

			if (!in_bound(world_x, world_y))
				continue;

			if (x * x + y * y < radiusSquared &&
				(material == MaterialRegistry::AIR_ID || grid.Get_at(world_x, world_y).id == MaterialRegistry::AIR_ID))
			{
				grid.Set_at(world_x, world_y, material_registry.CreateBlock(material));
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
