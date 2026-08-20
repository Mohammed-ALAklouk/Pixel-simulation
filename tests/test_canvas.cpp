#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <nlohmann/json.hpp>
#include "Canvas.h"
#include "Grid.h"
#include "MaterialRegistry.h"

using namespace scree;

namespace {
	struct TempFile {
		std::filesystem::path path;

		explicit TempFile(const std::string& name)
			: path(std::filesystem::temp_directory_path() / name) {}

		TempFile(const std::string& name, std::string_view contents)
			: path(std::filesystem::temp_directory_path() / name)
		{
			std::ofstream file(path);
			file << contents;
		}

		~TempFile()
		{
			std::error_code ignored;
			std::filesystem::remove(path, ignored);
		}

		std::string str() const { return path.string(); }
	};

	// Air 0, Sand 1, Fire 2. Fire ticks, so its tiles carry a meaningful lifespan.
	constexpr const char* DOC = R"({
		"tags": [],
		"materials": [
			{ "name": "Sand", "movement": { "can_fall": true } },
			{ "name": "Fire", "lifespan": { "initial": 200, "tick": 4, "on_death": [ { "material": "Sand", "weight": 1 } ] } }
		]
	})";

	constexpr MaterialID SAND = 1;
	constexpr MaterialID FIRE = 2;

	// A whole number of chunks, so Grid::Create does not round the size up.
	constexpr std::uint16_t W = 64;
	constexpr std::uint16_t H = 32;

	void BuildRegistry(MaterialRegistry& registry)
	{
		REQUIRE(registry.LoadMaterialsFromJSON(DOC));
		REQUIRE(registry.GetLogs().empty());
	}
}

TEST_CASE("a canvas round-trips ids and lifespans")
{
	MaterialRegistry registry;
	BuildRegistry(registry);

	Grid source;
	source.Create(W, H, &registry);
	source.CreateAt(0, 0, SAND, 255);
	source.CreateAt(5, 3, SAND, 255);
	source.CreateAt(63, 31, FIRE, 137);
	source.CreateAt(20, 10, FIRE, 42);

	TempFile file("scree_canvas_roundtrip.json");
	REQUIRE(SaveCanvas(source, registry, file.str()).ok);

	Grid loaded;
	loaded.Create(W, H, &registry);
	const CanvasResult result = LoadCanvas(loaded, registry, file.str());

	INFO(result.message);
	REQUIRE(result.ok);
	CHECK(result.missing.empty());

	for (std::uint16_t y = 0; y < H; y++)
		for (std::uint16_t x = 0; x < W; x++) {
			CHECK(loaded.GetAt(x, y).id == source.GetAt(x, y).id);
			CHECK(loaded.GetAt(x, y).lifespan == source.GetAt(x, y).lifespan);
		}
}

TEST_CASE("an empty canvas collapses to a single run")
{
	MaterialRegistry registry;
	BuildRegistry(registry);

	Grid grid;
	grid.Create(W, H, &registry);

	TempFile file("scree_canvas_empty.json");
	REQUIRE(SaveCanvas(grid, registry, file.str()).ok);

	std::ifstream in(file.str());
	nlohmann::json data = nlohmann::json::parse(in);

	CHECK(data["runs"].size() == 1);
	CHECK(data["runs"][0][2] == W * H);
	CHECK(data["palette"].size() == 1);
	CHECK(data["palette"][0] == "Air");
}

TEST_CASE("the palette holds only the materials the grid uses")
{
	MaterialRegistry registry;
	BuildRegistry(registry);

	Grid grid;
	grid.Create(W, H, &registry);
	grid.CreateAt(1, 1, SAND, 255);

	TempFile file("scree_canvas_palette.json");
	REQUIRE(SaveCanvas(grid, registry, file.str()).ok);

	std::ifstream in(file.str());
	nlohmann::json data = nlohmann::json::parse(in);

	CHECK(data["palette"].size() == 2);
	CHECK(data["palette"][0] == "Air");
	CHECK(data["palette"][1] == "Sand");
}

TEST_CASE("a material the registry does not have loads as air and is reported")
{
	MaterialRegistry source;
	BuildRegistry(source);

	Grid grid;
	grid.Create(W, H, &source);
	grid.CreateAt(2, 2, SAND, 255);
	grid.CreateAt(3, 3, FIRE, 100);

	TempFile file("scree_canvas_missing.json");
	REQUIRE(SaveCanvas(grid, source, file.str()).ok);

	MaterialRegistry thinner;
	REQUIRE(thinner.LoadMaterialsFromJSON(R"({ "tags": [], "materials": [{ "name": "Sand" }] })"));

	Grid loaded;
	loaded.Create(W, H, &thinner);
	const CanvasResult result = LoadCanvas(loaded, thinner, file.str());

	REQUIRE(result.ok);
	REQUIRE(result.missing.size() == 1);
	CHECK(result.missing[0] == "Fire");
	CHECK(loaded.GetAt(2, 2).id == SAND);
	CHECK(loaded.GetAt(3, 3).id == MaterialRegistry::AIR_ID);
}

TEST_CASE("a canvas whose size differs from the grid is rejected untouched")
{
	MaterialRegistry registry;
	BuildRegistry(registry);

	Grid source;
	source.Create(W, H, &registry);
	source.CreateAt(0, 0, SAND, 255);

	TempFile file("scree_canvas_size.json");
	REQUIRE(SaveCanvas(source, registry, file.str()).ok);

	Grid wider;
	wider.Create(W * 2, H, &registry);
	wider.CreateAt(4, 4, FIRE, 90);

	const CanvasResult result = LoadCanvas(wider, registry, file.str());

	CHECK_FALSE(result.ok);
	CHECK_FALSE(result.message.empty());
	CHECK(wider.GetAt(4, 4).id == FIRE);
}

TEST_CASE("a canvas from a future version is rejected")
{
	MaterialRegistry registry;
	BuildRegistry(registry);

	TempFile file("scree_canvas_version.json", R"({
		"version": 99, "width": 64, "height": 32, "palette": ["Air"], "runs": [[0, 255, 2048]]
	})");

	Grid grid;
	grid.Create(W, H, &registry);

	CHECK_FALSE(LoadCanvas(grid, registry, file.str()).ok);
}

TEST_CASE("a malformed canvas is rejected, not thrown out of")
{
	MaterialRegistry registry;
	BuildRegistry(registry);

	Grid grid;
	grid.Create(W, H, &registry);

	TempFile broken("scree_canvas_broken.json", R"({ "version": 1, "runs": [)");
	CHECK_FALSE(LoadCanvas(grid, registry, broken.str()).ok);

	TempFile wrongShape("scree_canvas_shape.json", R"({ "version": 1, "width": 64, "height": 32 })");
	CHECK_FALSE(LoadCanvas(grid, registry, wrongShape.str()).ok);

	CHECK_FALSE(LoadCanvas(grid, registry, "no/such/canvas.json").ok);
}

TEST_CASE("a run with an out-of-range palette index is skipped, not indexed")
{
	MaterialRegistry registry;
	BuildRegistry(registry);

	TempFile file("scree_canvas_badindex.json", R"({
		"version": 1, "width": 64, "height": 32,
		"palette": ["Air", "Sand"],
		"runs": [[9, 255, 10], [1, 255, 5], [-3, 255, 4], [0, 255, 2033]]
	})");

	Grid grid;
	grid.Create(W, H, &registry);
	const CanvasResult result = LoadCanvas(grid, registry, file.str());

	REQUIRE(result.ok);
	CHECK(grid.GetAt(0, 0).id == SAND);
	CHECK(grid.GetAt(4, 0).id == SAND);
	CHECK(grid.GetAt(5, 0).id == MaterialRegistry::AIR_ID);
}

TEST_CASE("runs longer than the grid stop at the last pixel")
{
	MaterialRegistry registry;
	BuildRegistry(registry);

	TempFile file("scree_canvas_overrun.json", R"({
		"version": 1, "width": 64, "height": 32,
		"palette": ["Air", "Sand"],
		"runs": [[1, 255, 999999]]
	})");

	Grid grid;
	grid.Create(W, H, &registry);

	REQUIRE(LoadCanvas(grid, registry, file.str()).ok);
	CHECK(grid.GetAt(0, 0).id == SAND);
	CHECK(grid.GetAt(W - 1, H - 1).id == SAND);
}
