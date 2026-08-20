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
#include <fstream>
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
		
		void Update();
		void Render();
		void RenderBloom();
		void ProcessInputs();
		void UI();
		void Run();
		void Tick();
#if defined(PLATFORM_WEB)
		void WebSyncViewport();
		int webLastW = 0, webLastH = 0;
#endif
		// Chrome sizes are fixed, so this only has to place the grid in what is left over.
		void ComputeLayout();
		void UpdateStats();
		void HandleHotkeys();
		void DrawStroke(const std::vector<Vector2>& points, const MaterialID material, const float radius);
		bool LoadMaterials();
		bool ImportCanvas(std::string path);
		bool ExportCanvas(std::string path);
		// Resolved against the executable, not the working directory. Shared by the loader and the top bar's link.
		std::string AssetsPath() const;
		std::string CanvasesPath() const;
		std::string MaterialsPath() const;
		// Opens a picker and reloads with the chosen file as the custom overlay.
		void ImportMaterials();
		void ExportMaterials();
		void ClearCustomMaterials();
		std::vector<MaterialID> GetRegistryChanges(const MaterialRegistry& new_registry);

		bool InBound(int x, int y)			const { return (x >= 0 && y >= 0 && x < GridSize && y < GridSize); }

		
		std::vector<Vector2> GetLine(Vector2 start, Vector2 end);

		static constexpr uint32_t GridSize = 640;
		static constexpr uint32_t BloomSize = GridSize / 2;
		// Sized so the 640x640 grid still lands at 1:1 once the chrome has taken its share,
		// including when the loader banner is up.
		Vec2i windowSize = Vec2i{ 1400, 900 };
		Vector2 gridOffset = Vector2{ 0.0f, 0.0f };
		float tileSize = 1;
		
		// Declared before grid and simulation: both bind to it at construction, and
		// member initialisation follows declaration order.
		MaterialRegistry materialRegistry;
		Grid grid;
		Simulation simulation;
		MaterialID selectedMaterial = 0;

		std::vector<Color> pixels;
		std::vector<Color> emissivePixels;
		Texture2D tex;
		Texture2D emissiveTex;
		RenderTexture2D bloomTarget;
		RenderTexture2D bloomScratch;
		Shader blurShader;
		Shader compositeShader;
		int blurDirectionLoc = 0;
		int compositeIntensityLoc = 0;

		bool bloomEnabled = true;
		float bloomIntensity = 1.4f;
		float bloomRadius = 2.0f;
		int bloomPasses = 3;
		// Static dither drawn behind the grid, so empty space reads as textured rather
		// than as a black hole. Built once in the constructor.
		Rectangle frame;


		Vector2 mouseDownPos;

		bool isDrawingCursor = false;
		Vector2 cursorStart;
		float cursorRadius = 15;

		std::vector<std::pair<int, int>> activeChunks;

		int  updatesPerFrame = 3;

		float inputTime = 0;
		float uiTime = 0;
		float updateTime = 0;
		float renderTime = 0;
		float delta = 0;
		std::chrono::high_resolution_clock::time_point deltaClock;

		bool showActiveChunks = false;
		bool showBenchMarks = false;
		bool newMaterialPanelOpen = false;
		bool paused = false;
		// Set by the STEP button, consumed by the next update. The UI runs inside Render(),
		// after Update(), so the step it asks for lands on the following frame.
		bool stepOnce = false;

		std::string materialLoadErrorMessage;
		// Failed load vs. only warnings -- all the banner needs to colour itself.
		bool materialLoadFailed = false;

		// 0 when there's nothing to report, so the banner takes up no space in the common case.
		float layoutBannerH = 0.0f;
		// The region the grid is centred inside, once the chrome has taken its share.
		Rectangle canvasRegion{};

		MaterialData newMaterial{};
		std::string newMaterialName = "";
		std::vector<Transition> newMaterialTransitions;
		std::vector<EditReaction> newMaterialReactions;
		// Points at newMaterial while creating; at a registry entry while editing one.
		MaterialData* editedMaterial = &newMaterial;
		MaterialID editedMaterialID = 0;
		bool confirmDeleteOpen = false;
		MaterialID pendingDeleteId = 0;

		bool Editing() const { return editedMaterial != &newMaterial; }
		void BeginNewMaterial();
		void BeginEditMaterial(MaterialID id);
		void CommitMaterial();
		void DeleteMaterial(MaterialID id);
		// Counting particles means walking all 640x640 blocks, so the readouts refresh on
		// a slower cadence than the frame does.
		static constexpr int StatsInterval = 12;
		int particleCount = 0;
		int awakeChunkCount = 0;
		int statsCounter = 0;
		float statsDeltaAccum = 0.0f;
		float fpsDisplay = 60.0f;

		std::string customFilePath = "";
	};
}