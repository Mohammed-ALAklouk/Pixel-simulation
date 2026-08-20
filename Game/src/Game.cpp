#include "Game.h"
#include "UI.h"
#include "FileDialog.h"
#include "Canvas.h"

#include <filesystem>

#if defined(PLATFORM_WEB)
#define GLSL_HEADER "#version 300 es\nprecision highp float;\n"
#else
#define GLSL_HEADER "#version 330\n"
#endif

namespace
{
	const char* BlurShader = GLSL_HEADER R"(
in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 direction;

const float offset[3] = float[](0.0, 1.3846153846, 3.2307692308);
const float weight[3] = float[](0.2270270270, 0.3162162162, 0.0702702703);

void main()
{
    vec3 sum = texture(texture0, fragTexCoord).rgb*weight[0];

    for (int i = 1; i < 3; i++)
    {
        sum += texture(texture0, fragTexCoord + direction*offset[i]).rgb*weight[i];
        sum += texture(texture0, fragTexCoord - direction*offset[i]).rgb*weight[i];
    }

    finalColor = vec4(sum, 1.0);
}
)";

	const char* CompositeShader = GLSL_HEADER R"(
in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform float intensity;

void main()
{
    finalColor = vec4(texture(texture0, fragTexCoord).rgb*intensity, 1.0);
}
)";
}

scree::Game::Game()
	: simulation(material_registry)
{
	load_materials();

	std::error_code ignored;
	std::filesystem::create_directories(canvases_path(), ignored);

	// The chrome is fixed; the grid centres in what's left, so the layout survives a resize
	// (compute_layout re-runs each frame off the live window size).
#if !defined(PLATFORM_WEB)
	// Web sizes the canvas itself (web_sync_viewport); raylib's own resize handler would
	// fight it back to CSS pixels, so the flag stays off there.
	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
#endif
	InitWindow(Window_size.x, Window_size.y, "scree");
#if defined(PLATFORM_WEB)
	// Before LoadFonts so the atlas rasterises at the device-pixel size.
	web_sync_viewport();
#endif
	// Window/taskbar icon at several sizes so the OS picks a crisp one rather than downscaling.
	// Loaded from assets/; missing sizes are skipped, a fully missing set leaves the default icon.
	{
		const int icon_sizes[] = { 16, 32, 48, 64, 256 };
		Image icons[5];
		int icon_count = 0;
		for (int px : icon_sizes)
		{
			Image img = LoadImage(
				(assets_path() + "/window-icon-" + std::to_string(px) + ".png").c_str());
			if (img.data != nullptr) icons[icon_count++] = img;
		}
		if (icon_count > 0)
		{
			SetWindowIcons(icons, icon_count);
			for (int i = 0; i < icon_count; ++i) UnloadImage(icons[i]);
		}
	}
	// Maximised after the window exists, not via FLAG_WINDOW_MAXIMIZED: that flag skips the
	// resize callback, so raylib keeps reporting the requested size and everything renders into
	// the top-left corner. Window_size stays the size it restores down to.
#if defined(PLATFORM_WEB)
	// The canvas tracks window.innerWidth/Height, clamped to this min; a desktop-sized
	// floor would force the canvas past a small viewport, so keep it low and let it reflow.
	SetWindowMinSize(640, 480);
#else
	MaximizeWindow();
	SetWindowMinSize(1300, 820);
#endif
	SetTargetFPS(60);
	rlImGuiSetup(true);
	ui::LoadFonts();
	// Before ApplyTheme, so the first frame is already on the saved theme rather than easing from the default.
	ui::LoadSettings();
	ui::ApplyTheme();

	pixels.resize(GridSize * GridSize);
	emissive_pixels.resize(GridSize * GridSize);

	Image blank = GenImageColor(GridSize, GridSize, BLACK);
	tex = LoadTextureFromImage(blank);
	emissive_tex = LoadTextureFromImage(blank);
	UnloadImage(blank);                      // GPU has it now; drop the CPU copy
	SetTextureFilter(tex, TEXTURE_FILTER_POINT);
	SetTextureFilter(emissive_tex, TEXTURE_FILTER_BILINEAR);
	SetTextureWrap(emissive_tex, TEXTURE_WRAP_CLAMP);

	bloom_target = LoadRenderTexture(static_cast<int>(BloomSize), static_cast<int>(BloomSize));
	bloom_scratch = LoadRenderTexture(static_cast<int>(BloomSize), static_cast<int>(BloomSize));
	SetTextureFilter(bloom_target.texture, TEXTURE_FILTER_BILINEAR);
	SetTextureFilter(bloom_scratch.texture, TEXTURE_FILTER_BILINEAR);
	SetTextureWrap(bloom_target.texture, TEXTURE_WRAP_CLAMP);
	SetTextureWrap(bloom_scratch.texture, TEXTURE_WRAP_CLAMP);

	blur_shader = LoadShaderFromMemory(nullptr, BlurShader);
	composite_shader = LoadShaderFromMemory(nullptr, CompositeShader);
	blur_direction_loc = GetShaderLocation(blur_shader, "direction");
	composite_intensity_loc = GetShaderLocation(composite_shader, "intensity");

	grid.Create(GridSize, GridSize,  &material_registry);
	// The renderer only converts dirty chunks, so a chunk untouched since startup would never
	// be written. Clear marks every chunk, so frame one fills the whole buffer.
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
	// Re-tints the chrome off the chosen theme. Done before drawing so the backgrounds agree
	// with the panels rlImGui draws on top; the ease inside makes a theme switch sweep, not snap.
	ui::UpdateTheme(ui::ThemeOptions[ui::ActiveTheme].seed, delta);

	BeginDrawing();
	ClearBackground(ui::theme.WindowBg);
	DrawRectangleRec(canvas_region, ui::theme.CanvasBg);

	// Update_grid marks the mask, but the brush and material reloads also write between updates
	// (and are the only writes while paused). Marking again here picks those up.
	grid.Mark_chunks_for_render();

	// Only changed chunks are converted; the rest of `pixels` still holds last frame's values.
	// The upload still covers the whole buffer: one UpdateTexture (~0.2ms) beats per-chunk
	// UpdateTextureRec, which pays driver overhead per call.
	for (int y = 0; y != GridSize; y++)
	{
		int chunk_y = y >> CHUNK_SHIFT; // equivalent to y / CHUNK_SIZE, but faster
		for (int chunk_x = 0; chunk_x != grid.Get_width_chunks(); chunk_x++)
		{
			if (!grid.Should_render_chunk(chunk_x, chunk_y)) continue;
			
			int x0 = chunk_x << CHUNK_SHIFT; // equivalent to chunk_x * CHUNK_SIZE, but faster
			const Block* row = grid.Row(y);
			Color* out = pixels.data() + y * GridSize;
			Color* glow = emissive_pixels.data() + y * GridSize;
			// Air is written clear rather than black so the backdrop drawn under the grid
			// shows through it; every other material stays fully opaque.
			for (int x = x0; x < x0 + CHUNK_SIZE; x++)
			{
				if (row[x].id == MaterialRegistry::AIR_ID)
				{
					out[x] = Color{ 0, 0, 0, 0 };
					glow[x] = Color{ 0, 0, 0, 255 };
					continue;
				}

				out[x] = row[x].color.toRaylibColor();

				const std::uint8_t emission = material_registry.Get(row[x].id).emission;
				glow[x] = emission
					? Color{ static_cast<unsigned char>(out[x].r * emission / 255),
							 static_cast<unsigned char>(out[x].g * emission / 255),
							 static_cast<unsigned char>(out[x].b * emission / 255), 255 }
					: Color{ 0, 0, 0, 255 };
			}
		}
	}

	grid.Clear_chunks_to_render();

	UpdateTexture(tex, pixels.data());

	Rectangle src{ 0, 0, (float)GridSize, (float)GridSize };

	if (bloom_enabled)
	{
		UpdateTexture(emissive_tex, emissive_pixels.data());
		render_bloom();
	}

	DrawTexturePro(tex, src, frame, Vector2{ 0, 0 }, 0.0f, WHITE);

	if (bloom_enabled)
	{
		SetShaderValue(composite_shader, composite_intensity_loc, &bloom_intensity, SHADER_UNIFORM_FLOAT);
		BeginBlendMode(BLEND_ADDITIVE);
		BeginShaderMode(composite_shader);
		DrawTexturePro(bloom_target.texture, Rectangle{ 0, 0, (float)BloomSize, -(float)BloomSize },
			frame, Vector2{ 0, 0 }, 0.0f, WHITE);
		EndShaderMode();
		EndBlendMode();
	}

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

	// EndDrawing below blocks on the 60 FPS limiter (pure idle), so it's left out of the render span.
	render_time = std::chrono::duration<float>(
		std::chrono::high_resolution_clock::now() - render_clock).count() - UI_time;

	EndDrawing();
}

void scree::Game::render_bloom()
{
	const Rectangle bloom_src{ 0, 0, (float)BloomSize, -(float)BloomSize };
	const Rectangle bloom_dest{ 0, 0, (float)BloomSize, (float)BloomSize };

	BeginTextureMode(bloom_target);
	ClearBackground(BLACK);
	DrawTexturePro(emissive_tex, Rectangle{ 0, 0, (float)GridSize, (float)GridSize },
		bloom_dest, Vector2{ 0, 0 }, 0.0f, WHITE);
	EndTextureMode();

	auto blur_pass = [&](RenderTexture2D& to, RenderTexture2D& from, Vector2 direction)
	{
		SetShaderValue(blur_shader, blur_direction_loc, &direction, SHADER_UNIFORM_VEC2);
		BeginTextureMode(to);
		ClearBackground(BLACK);
		BeginShaderMode(blur_shader);
		DrawTexturePro(from.texture, bloom_src, bloom_dest, Vector2{ 0, 0 }, 0.0f, WHITE);
		EndShaderMode();
		EndTextureMode();
	};

	for (int pass = 0; pass < bloom_passes; pass++)
	{
		const float step = bloom_radius * (pass + 1) / (float)BloomSize;
		blur_pass(bloom_scratch, bloom_target, Vector2{ step, 0.0f });
		blur_pass(bloom_target, bloom_scratch, Vector2{ 0.0f, step });
	}
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

void scree::Game::begin_new_material()
{
	newMaterial = MaterialData();
	newMaterialName.clear();
	newMaterialTransitions.clear();
	newMaterialReactions.clear();
	editedMaterial = &newMaterial;
	editedMaterialID = 0;
}

void scree::Game::begin_edit_material(MaterialID id)
{
	begin_new_material();
	editedMaterialID = id;
	editedMaterial = &material_registry.GetMutable(id);
	newMaterialName = material_registry.GetName(id);
	newMaterialTransitions = material_registry.ReadTransitions(editedMaterial->lifespanData.OnDeathTransitionSpan);
	newMaterialReactions = material_registry.ReadReactions(editedMaterial->reactionSpan);
}

void scree::Game::commit_material()
{
	if (editing()) {
		material_registry.RenameMaterial(editedMaterialID, newMaterialName);
		material_registry.ReplaceMaterial(editedMaterialID, *editedMaterial,
			newMaterialTransitions, newMaterialReactions);
	}
	else {
		material_registry.AddMaterial(newMaterialName, newMaterial,
			newMaterialTransitions, newMaterialReactions);
	}

	begin_new_material();
}

void scree::Game::delete_material(MaterialID id)
{
	auto remap = material_registry.DeleteMaterial(id);
	if (remap.empty()) return;

	grid.Remap(remap);
	SelectedMaterial = remap[SelectedMaterial];
	// The erase shifted the materials vector, so editedMaterial cannot be trusted.
	begin_new_material();
}

void scree::Game::import_materials()
{
	std::string picked = OpenFileDialog("JSON files\0*.json\0All files\0*.*\0", "Import materials", assets_path());
	if (picked.empty()) return;

	custom_file_path = picked;
	load_materials();
}

bool scree::Game::export_canvas(std::string path)
{
	const CanvasResult result = SaveCanvas(grid, material_registry, path);
	material_load_error_message = result.message;
	material_load_failed = !result.ok;
	return result.ok;
}

bool scree::Game::import_canvas(std::string path)
{
	const CanvasResult result = LoadCanvas(grid, material_registry, path);
	material_load_error_message = result.message;
	material_load_failed = !result.ok;
	return result.ok;
}

void scree::Game::export_materials()
{
	const std::string suggested = custom_file_path.empty() ? "custom_materials.json" : custom_file_path;
	std::string picked = SaveFileDialog("JSON files\0*.json\0All files\0*.*\0", "Save materials",
		assets_path(), suggested);
	if (picked.empty()) return;

	if (!material_registry.SaveCustomMaterials(picked)) {
		material_load_error_message = "Failed to write " + picked + ".";
		material_load_failed = true;
		return;
	}

	custom_file_path = picked;
}

void scree::Game::clear_custom_materials()
{
	custom_file_path.clear();
	load_materials();
}

std::string scree::Game::assets_path() const
{
	return std::string(GetApplicationDirectory()) + "assets";
}

std::string scree::Game::canvases_path() const
{
	return assets_path() + "/canvases";
}

std::string scree::Game::materials_path() const
{
	return assets_path() + "/materials.json";
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

void scree::Game::tick()
{
#if defined(PLATFORM_WEB)
	web_sync_viewport();
#endif
	using clock = std::chrono::high_resolution_clock;
	auto since = [](clock::time_point from) {
		return std::chrono::duration<float>(clock::now() - from).count();
	};

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

#if defined(PLATFORM_WEB)
#include <emscripten.h>

// Render at device pixels for crispness: size the canvas backing store to
// innerWidth*dpr and drive the UI scale by dpr so physical sizes hold. CSS keeps the
// canvas filling the window, so the browser maps device pixels 1:1.
void scree::Game::web_sync_viewport()
{
	const double dpr = EM_ASM_DOUBLE({ return window.devicePixelRatio || 1; });
	const int dw = (int)(EM_ASM_INT({ return window.innerWidth; }) * dpr + 0.5);
	const int dh = (int)(EM_ASM_INT({ return window.innerHeight; }) * dpr + 0.5);

	if (dw != web_last_w || dh != web_last_h)
	{
		web_last_w = dw;
		web_last_h = dh;
		SetWindowSize(dw, dh);
		// glfwSetWindowSize also writes the canvas CSS size; put it back to fill the window.
		EM_ASM({
			var c = document.getElementById('canvas');
			if (c) { c.style.width = '100vw'; c.style.height = '100vh'; }
		});
	}
	ui::metrics::UiScale = (float)dpr;
}
#endif

void scree::Game::run()
{
	delta_clock = std::chrono::high_resolution_clock::now();
#if defined(PLATFORM_WEB)
	// Browsers own the frame loop; a blocking while() would hang the tab. main()'s
	// try/catch cannot cover a tick that runs in a later callback, so it guards here.
	// simulate_infinite_loop=0: return instead of unwinding by throw, which under
	// -fwasm-exceptions would run ~Game() while the callback still holds this.
	emscripten_set_main_loop_arg(
		[](void* self) {
			try { static_cast<Game*>(self)->tick(); }
			catch (const std::exception& e) { TraceLog(LOG_ERROR, "tick: %s", e.what()); }
		}, this, 0, 0);
#else
	while (!WindowShouldClose())
		tick();
#endif
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
	bool success = new_registry.LoadMaterials(materials_path(), custom_file_path);
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
		// editedMaterial would dangle into the registry that was just replaced.
		begin_new_material();
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
