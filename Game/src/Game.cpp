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
	: simulation(materialRegistry)
{
	LoadMaterials();

	std::error_code ignored;
	std::filesystem::create_directories(CanvasesPath(), ignored);

	// The chrome is fixed; the grid centres in what's left, so the layout survives a resize
	// (compute_layout re-runs each frame off the live window size).
#if !defined(PLATFORM_WEB)
	// Web sizes the canvas itself (web_sync_viewport); raylib's own resize handler would
	// fight it back to CSS pixels, so the flag stays off there.
	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
#endif
	InitWindow(windowSize.x, windowSize.y, "scree");
#if defined(PLATFORM_WEB)
	// Before LoadFonts so the atlas rasterises at the device-pixel size.
	WebSyncViewport();
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
				(AssetsPath() + "/window-icon-" + std::to_string(px) + ".png").c_str());
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
	// the top-left corner. windowSize stays the size it restores down to.
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
	emissivePixels.resize(GridSize * GridSize);

	Image blank = GenImageColor(GridSize, GridSize, BLACK);
	tex = LoadTextureFromImage(blank);
	emissiveTex = LoadTextureFromImage(blank);
	UnloadImage(blank);                      // GPU has it now; drop the CPU copy
	SetTextureFilter(tex, TEXTURE_FILTER_POINT);
	SetTextureFilter(emissiveTex, TEXTURE_FILTER_BILINEAR);
	SetTextureWrap(emissiveTex, TEXTURE_WRAP_CLAMP);

	bloomTarget = LoadRenderTexture(static_cast<int>(BloomSize), static_cast<int>(BloomSize));
	bloomScratch = LoadRenderTexture(static_cast<int>(BloomSize), static_cast<int>(BloomSize));
	SetTextureFilter(bloomTarget.texture, TEXTURE_FILTER_BILINEAR);
	SetTextureFilter(bloomScratch.texture, TEXTURE_FILTER_BILINEAR);
	SetTextureWrap(bloomTarget.texture, TEXTURE_WRAP_CLAMP);
	SetTextureWrap(bloomScratch.texture, TEXTURE_WRAP_CLAMP);

	blurShader = LoadShaderFromMemory(nullptr, BlurShader);
	compositeShader = LoadShaderFromMemory(nullptr, CompositeShader);
	blurDirectionLoc = GetShaderLocation(blurShader, "direction");
	compositeIntensityLoc = GetShaderLocation(compositeShader, "intensity");

	grid.Create(GridSize, GridSize,  &materialRegistry);
	// The renderer only converts dirty chunks, so a chunk untouched since startup would never
	// be written. Clear marks every chunk, so frame one fills the whole buffer.
	grid.Clear();
	ComputeLayout();
	srand(static_cast<unsigned int>(time(NULL)));
}

void scree::Game::ComputeLayout()
{
	windowSize.x = GetScreenWidth();
	windowSize.y = GetScreenHeight();

	if (materialLoadErrorMessage.empty())
	{
		layoutBannerH = 0.0f;
	}
	else
	{
		// A single log line is often long enough to wrap, so count the rows it will
		// actually occupy rather than the newlines it contains.
		const float text_width = static_cast<float>(windowSize.x)
			- ui::metrics::BannerTextInset - ui::metrics::BannerTextRight;
		const int per_line = std::max(20, static_cast<int>(text_width / ui::metrics::BannerGlyphW));

		int lines = 0;
		for (std::size_t start = 0; start < materialLoadErrorMessage.size(); )
		{
			std::size_t end = materialLoadErrorMessage.find('\n', start);
			if (end == std::string::npos) end = materialLoadErrorMessage.size();

			const int length = static_cast<int>(end - start);
			lines += std::max(1, (length + per_line - 1) / per_line);
			start = end + 1;
		}

		lines = std::clamp(lines, 1, ui::metrics::BannerMaxLines);
		layoutBannerH = lines * ui::metrics::BannerLineH + ui::metrics::BannerPad;
	}

	const float top = ui::metrics::TopBarH + layoutBannerH;
	canvasRegion = Rectangle{
		ui::metrics::LeftRailW,
		top,
		static_cast<float>(windowSize.x) - ui::metrics::LeftRailW,
		static_cast<float>(windowSize.y) - top - ui::metrics::BottomBarH
	};

	// Never magnified past 1:1 -- a fractional tile size larger than a pixel would make
	// some grid cells a pixel wider than their neighbours under point filtering.
	const float room = ui::metrics::CanvasPad * 2.0f;
	const float fit = std::min(canvasRegion.width - room, canvasRegion.height - room) / GridSize;
	tileSize = std::clamp(fit, 0.1f, 1.0f);

	const float side = GridSize * tileSize;
	gridOffset.x = canvasRegion.x + (canvasRegion.width - side) * 0.5f;
	gridOffset.y = canvasRegion.y + (canvasRegion.height - side) * 0.5f;
	frame = Rectangle{ gridOffset.x, gridOffset.y, side, side };
}

void scree::Game::UpdateStats()
{
	statsDeltaAccum += delta;
	if (++statsCounter < StatsInterval) return;

	if (statsDeltaAccum > 0.0f)
		fpsDisplay = statsCounter / statsDeltaAccum;
	statsCounter = 0;
	statsDeltaAccum = 0.0f;

	awakeChunkCount = 0;
	for (int chunk_y = 0; chunk_y < grid.GetHeightChunks(); chunk_y++)
		for (int chunk_x = 0; chunk_x < grid.GetWidthChunks(); chunk_x++)
			if (grid.IsChunkActive(chunk_x, chunk_y)) awakeChunkCount++;

	int particles = 0;
	for (int y = 0; y < grid.GetHeightPx(); y++)
	{
		const Block* row = grid.Row(y);
		for (int x = 0; x < grid.GetWidthPx(); x++)
			if (row[x].id != MaterialRegistry::AIR_ID) particles++;
	}
	particleCount = particles;
}

void scree::Game::Update()
{
	if (!paused)
	{
		for (int i = 0; i < updatesPerFrame; i++)
			simulation.UpdateGrid(grid);
	}
	else if (stepOnce)
	{
		simulation.UpdateGrid(grid);
	}

	stepOnce = false;
}

void scree::Game::Render()
{
	auto render_clock = std::chrono::high_resolution_clock::now();

	ComputeLayout();
	// Re-tints the chrome off the chosen theme. Done before drawing so the backgrounds agree
	// with the panels rlImGui draws on top; the ease inside makes a theme switch sweep, not snap.
	ui::UpdateTheme(ui::ThemeOptions[ui::ActiveTheme].seed, delta);

	BeginDrawing();
	ClearBackground(ui::theme.WindowBg);
	DrawRectangleRec(canvasRegion, ui::theme.CanvasBg);

	// UpdateGrid marks the mask, but the brush and material reloads also write between updates
	// (and are the only writes while paused). Marking again here picks those up.
	grid.MarkChunksForRender();

	// Only changed chunks are converted; the rest of `pixels` still holds last frame's values.
	// The upload still covers the whole buffer: one UpdateTexture (~0.2ms) beats per-chunk
	// UpdateTextureRec, which pays driver overhead per call.
	for (int y = 0; y != GridSize; y++)
	{
		int chunk_y = y >> CHUNK_SHIFT; // equivalent to y / CHUNK_SIZE, but faster
		for (int chunk_x = 0; chunk_x != grid.GetWidthChunks(); chunk_x++)
		{
			if (!grid.ShouldRenderChunk(chunk_x, chunk_y)) continue;
			
			int x0 = chunk_x << CHUNK_SHIFT; // equivalent to chunk_x * CHUNK_SIZE, but faster
			const Block* row = grid.Row(y);
			Color* out = pixels.data() + y * GridSize;
			Color* glow = emissivePixels.data() + y * GridSize;
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

				out[x] = row[x].color.ToRaylibColor();

				const std::uint8_t emission = materialRegistry.Get(row[x].id).emission;
				glow[x] = emission
					? Color{ static_cast<unsigned char>(out[x].r * emission / 255),
							 static_cast<unsigned char>(out[x].g * emission / 255),
							 static_cast<unsigned char>(out[x].b * emission / 255), 255 }
					: Color{ 0, 0, 0, 255 };
			}
		}
	}

	grid.ClearChunksToRender();

	UpdateTexture(tex, pixels.data());

	Rectangle src{ 0, 0, (float)GridSize, (float)GridSize };

	if (bloomEnabled)
	{
		UpdateTexture(emissiveTex, emissivePixels.data());
		RenderBloom();
	}

	DrawTexturePro(tex, src, frame, Vector2{ 0, 0 }, 0.0f, WHITE);

	if (bloomEnabled)
	{
		SetShaderValue(compositeShader, compositeIntensityLoc, &bloomIntensity, SHADER_UNIFORM_FLOAT);
		BeginBlendMode(BLEND_ADDITIVE);
		BeginShaderMode(compositeShader);
		DrawTexturePro(bloomTarget.texture, Rectangle{ 0, 0, (float)BloomSize, -(float)BloomSize },
			frame, Vector2{ 0, 0 }, 0.0f, WHITE);
		EndShaderMode();
		EndBlendMode();
	}

	DrawRectangleLinesEx(frame, 1, ui::col::GridEdge);

	if (showActiveChunks)
	{
		activeChunks.clear();
		grid.GetActiveChunks(activeChunks);
		for (auto chunk: activeChunks)
		{
			DrawRectangleLines(static_cast<int>(chunk.first * CHUNK_SIZE * tileSize + gridOffset.x),
				static_cast<int>(chunk.second * CHUNK_SIZE * tileSize + gridOffset.y),
				static_cast<int>(CHUNK_SIZE * tileSize), static_cast<int>(CHUNK_SIZE * tileSize),
				ui::col::ChunkEdge);
		}
	}

	// WantCaptureMouse is last frame's -- ImGui has not started this one yet -- which is
	// close enough to keep the brush ring off the panels.
	if (!ImGui::GetIO().WantCaptureMouse)
	{
		auto mouse_pos = GetMousePosition();
		DrawCircleLines(static_cast<int>(mouse_pos.x), static_cast<int>(mouse_pos.y),
			cursorRadius * tileSize, ui::theme.Ring);
	}

	auto ui_clock = std::chrono::high_resolution_clock::now();
	rlImGuiBegin();
	UI();
	rlImGuiEnd();
	uiTime = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - ui_clock).count();

	// EndDrawing below blocks on the 60 FPS limiter (pure idle), so it's left out of the render span.
	renderTime = std::chrono::duration<float>(
		std::chrono::high_resolution_clock::now() - render_clock).count() - uiTime;

	EndDrawing();
}

void scree::Game::RenderBloom()
{
	const Rectangle bloom_src{ 0, 0, (float)BloomSize, -(float)BloomSize };
	const Rectangle bloom_dest{ 0, 0, (float)BloomSize, (float)BloomSize };

	BeginTextureMode(bloomTarget);
	ClearBackground(BLACK);
	DrawTexturePro(emissiveTex, Rectangle{ 0, 0, (float)GridSize, (float)GridSize },
		bloom_dest, Vector2{ 0, 0 }, 0.0f, WHITE);
	EndTextureMode();

	auto blur_pass = [&](RenderTexture2D& to, RenderTexture2D& from, Vector2 direction)
	{
		SetShaderValue(blurShader, blurDirectionLoc, &direction, SHADER_UNIFORM_VEC2);
		BeginTextureMode(to);
		ClearBackground(BLACK);
		BeginShaderMode(blurShader);
		DrawTexturePro(from.texture, bloom_src, bloom_dest, Vector2{ 0, 0 }, 0.0f, WHITE);
		EndShaderMode();
		EndTextureMode();
	};

	for (int pass = 0; pass < bloomPasses; pass++)
	{
		const float step = bloomRadius * (pass + 1) / (float)BloomSize;
		blur_pass(bloomScratch, bloomTarget, Vector2{ step, 0.0f });
		blur_pass(bloomTarget, bloomScratch, Vector2{ 0.0f, step });
	}
}

void scree::Game::ProcessInputs()
{
	HandleHotkeys();

	bool mouse_left_down = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
	bool mouse_right_down = IsMouseButtonDown(MOUSE_RIGHT_BUTTON);

	Vector2 mouse_pos = GetMousePosition();
	mouse_pos.x -= gridOffset.x;
	mouse_pos.y -= gridOffset.y;

	int mouse_x = static_cast<int>(mouse_pos.x / tileSize), mouse_y = static_cast<int>(mouse_pos.y / tileSize);
	
	
	if ((!mouse_left_down && !mouse_right_down) || ImGui::GetIO().WantCaptureMouse) {
		isDrawingCursor = false;
	}
	else if ((mouse_left_down || mouse_right_down) && !isDrawingCursor) {
		cursorStart = mouse_pos;
		isDrawingCursor = true;
	}

	

	if (isDrawingCursor)
	{
		Vector2 cursor_end = mouse_pos;
		auto line = GetLine(cursorStart, cursor_end);
		MaterialID material = static_cast<MaterialID>(mouse_left_down ? selectedMaterial : MaterialRegistry::AIR_ID);
		DrawStroke(line, material, cursorRadius);
		cursorStart = cursor_end;
	}
	
}

void scree::Game::BeginNewMaterial()
{
	newMaterial = MaterialData();
	newMaterialName.clear();
	newMaterialTransitions.clear();
	newMaterialReactions.clear();
	editedMaterial = &newMaterial;
	editedMaterialID = 0;
}

void scree::Game::BeginEditMaterial(MaterialID id)
{
	BeginNewMaterial();
	editedMaterialID = id;
	editedMaterial = &materialRegistry.GetMutable(id);
	newMaterialName = materialRegistry.GetName(id);
	newMaterialTransitions = materialRegistry.ReadTransitions(editedMaterial->lifespanData.onDeathTransitionSpan);
	newMaterialReactions = materialRegistry.ReadReactions(editedMaterial->reactionSpan);
}

void scree::Game::CommitMaterial()
{
	if (Editing()) {
		materialRegistry.RenameMaterial(editedMaterialID, newMaterialName);
		materialRegistry.ReplaceMaterial(editedMaterialID, *editedMaterial,
			newMaterialTransitions, newMaterialReactions);
	}
	else {
		materialRegistry.AddMaterial(newMaterialName, newMaterial,
			newMaterialTransitions, newMaterialReactions);
	}

	BeginNewMaterial();
}

void scree::Game::DeleteMaterial(MaterialID id)
{
	auto remap = materialRegistry.DeleteMaterial(id);
	if (remap.empty()) return;

	grid.Remap(remap);
	selectedMaterial = remap[selectedMaterial];
	// The erase shifted the materials vector, so editedMaterial cannot be trusted.
	BeginNewMaterial();
}

void scree::Game::ImportMaterials()
{
	std::string picked = OpenFileDialog("JSON files\0*.json\0All files\0*.*\0", "Import materials", AssetsPath());
	if (picked.empty()) return;

	customFilePath = picked;
	LoadMaterials();
}

bool scree::Game::ExportCanvas(std::string path)
{
	const CanvasResult result = SaveCanvas(grid, materialRegistry, path);
	materialLoadErrorMessage = result.message;
	materialLoadFailed = !result.ok;
	return result.ok;
}

bool scree::Game::ImportCanvas(std::string path)
{
	const CanvasResult result = LoadCanvas(grid, materialRegistry, path);
	materialLoadErrorMessage = result.message;
	materialLoadFailed = !result.ok;
	return result.ok;
}

void scree::Game::ExportMaterials()
{
	const std::string suggested = customFilePath.empty() ? "custom_materials.json" : customFilePath;
	std::string picked = SaveFileDialog("JSON files\0*.json\0All files\0*.*\0", "Save materials",
		AssetsPath(), suggested);
	if (picked.empty()) return;

	if (!materialRegistry.SaveCustomMaterials(picked)) {
		materialLoadErrorMessage = "Failed to write " + picked + ".";
		materialLoadFailed = true;
		return;
	}

	customFilePath = picked;
}

void scree::Game::ClearCustomMaterials()
{
	customFilePath.clear();
	LoadMaterials();
}

std::string scree::Game::AssetsPath() const
{
	return std::string(GetApplicationDirectory()) + "assets";
}

std::string scree::Game::CanvasesPath() const
{
	return AssetsPath() + "/canvases";
}

std::string scree::Game::MaterialsPath() const
{
	return AssetsPath() + "/materials.json";
}

void scree::Game::HandleHotkeys()
{
	if (ImGui::GetIO().WantCaptureKeyboard) return;

	if (IsKeyPressed(KEY_SPACE)) paused = !paused;
	if (IsKeyPressed(KEY_C)) grid.Clear();
	if (IsKeyPressed(KEY_R)) LoadMaterials();

	// 1-9 map onto the first nine file materials, which is what the rail's "1-9" says.
	for (int key = KEY_ONE; key <= KEY_NINE; key++)
	{
		if (!IsKeyPressed(key)) continue;
		const int id = key - KEY_ONE + 1;
		if (id < materialRegistry.GetMaterialsCount())
			selectedMaterial = static_cast<MaterialID>(id);
	}
}

void scree::Game::Tick()
{
#if defined(PLATFORM_WEB)
	WebSyncViewport();
#endif
	using clock = std::chrono::high_resolution_clock;
	auto since = [](clock::time_point from) {
		return std::chrono::duration<float>(clock::now() - from).count();
	};

	delta = since(deltaClock);
	deltaClock = clock::now();

	auto bench_clock = clock::now();
	ProcessInputs();
	inputTime = since(bench_clock);

	bench_clock = clock::now();
	Update();
	updateTime = since(bench_clock);

	// Render() sets renderTime itself, measured to exclude the frame-limiter wait.
	Render();

	UpdateStats();
}

#if defined(PLATFORM_WEB)
#include <emscripten.h>

// Render at device pixels for crispness: size the canvas backing store to
// innerWidth*dpr and drive the UI scale by dpr so physical sizes hold. CSS keeps the
// canvas filling the window, so the browser maps device pixels 1:1.
void scree::Game::WebSyncViewport()
{
	const double dpr = EM_ASM_DOUBLE({ return window.devicePixelRatio || 1; });
	const int dw = (int)(EM_ASM_INT({ return window.innerWidth; }) * dpr + 0.5);
	const int dh = (int)(EM_ASM_INT({ return window.innerHeight; }) * dpr + 0.5);

	if (dw != webLastW || dh != webLastH)
	{
		webLastW = dw;
		webLastH = dh;
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

void scree::Game::Run()
{
	deltaClock = std::chrono::high_resolution_clock::now();
#if defined(PLATFORM_WEB)
	// Browsers own the frame loop; a blocking while() would hang the tab. main()'s
	// try/catch cannot cover a tick that runs in a later callback, so it guards here.
	// simulate_infinite_loop=0: return instead of unwinding by throw, which under
	// -fwasm-exceptions would run ~Game() while the callback still holds this.
	emscripten_set_main_loop_arg(
		[](void* self) {
			try { static_cast<Game*>(self)->Tick(); }
			catch (const std::exception& e) { TraceLog(LOG_ERROR, "tick: %s", e.what()); }
		}, this, 0, 0);
#else
	while (!WindowShouldClose())
		Tick();
#endif
}

void scree::Game::DrawStroke(const std::vector<Vector2>& points, const MaterialID material, const float radius)
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
				grid.GetAt(x, y).id == MaterialRegistry::AIR_ID)
			{
				grid.SetAt(x, y, materialRegistry.CreateBlock(material));
			}
		}
	}
}

bool scree::Game::LoadMaterials()
{
	MaterialRegistry new_registry;
	bool success = new_registry.LoadMaterials(MaterialsPath(), customFilePath);
	materialLoadErrorMessage = Log::FormatLogs(new_registry.GetLogs());
	// A rejected material still counts as failure for the banner even though the load
	// itself succeeded -- something in the file is unusable either way.
	materialLoadFailed = !new_registry.GetLogs().empty() &&
		Log::Worst(new_registry.GetLogs()) != Log::Severity::Warning;
	if (success) {
		auto remap = GetRegistryChanges(new_registry);
		materialRegistry = std::move(new_registry);
		grid.Remap(remap);
		selectedMaterial = MaterialRegistry::AIR_ID;
		// editedMaterial would dangle into the registry that was just replaced.
		BeginNewMaterial();
	}
	
	return success;
}

std::vector<scree::MaterialID> scree::Game::GetRegistryChanges(const MaterialRegistry& new_registry)
{
	std::vector<MaterialID> remap(materialRegistry.GetMaterialsCount(), MaterialRegistry::AIR_ID);

	for (int i = 0; i < materialRegistry.GetMaterialsCount(); i++)
	{
		MaterialID id = static_cast<MaterialID>(i);
		auto name = materialRegistry.GetName(id);
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
