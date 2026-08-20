#include "Canvas.h"

#include "Grid.h"
#include "MaterialRegistry.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>

scree::CanvasResult scree::SaveCanvas(const Grid& grid, const MaterialRegistry& registry, const std::string& path)
{
	const int width = grid.GetWidthPx();
	const int height = grid.GetHeightPx();

	std::vector<MaterialID> palette;
	std::vector<int> indexOf(registry.GetMaterialsCount(), -1);
	nlohmann::json runs = nlohmann::json::array();

	int runIndex = 0;
	int runLifespan = 0;
	int runCount = 0;

	for (int y = 0; y < height; y++) {
		const Block* row = grid.Row(y);
		for (int x = 0; x < width; x++) {
			const Block& block = row[x];
			if (block.id >= indexOf.size())
				return { false, "The grid holds a material the registry does not.", {} };

			if (indexOf[block.id] < 0) {
				indexOf[block.id] = static_cast<int>(palette.size());
				palette.push_back(block.id);
			}

			const int index = indexOf[block.id];
			if (runCount && index == runIndex && block.lifespan == runLifespan) {
				runCount++;
				continue;
			}

			if (runCount) runs.push_back({ runIndex, runLifespan, runCount });
			runIndex = index;
			runLifespan = block.lifespan;
			runCount = 1;
		}
	}
	if (runCount) runs.push_back({ runIndex, runLifespan, runCount });

	nlohmann::json out;
	out["version"] = CanvasVersion;
	out["width"] = width;
	out["height"] = height;
	out["palette"] = nlohmann::json::array();
	for (MaterialID id : palette)
		out["palette"].push_back(registry.GetName(id));
	out["runs"] = std::move(runs);

	std::ofstream file(path);
	if (!file.is_open())
		return { false, "Failed to write " + path + ".", {} };

	file << out.dump() << "\n";
	if (!file.good())
		return { false, "Failed to write " + path + ".", {} };

	return { true, "", {} };
}

scree::CanvasResult scree::LoadCanvas(Grid& grid, const MaterialRegistry& registry, const std::string& path)
{
	std::ifstream file(path);
	if (!file.is_open())
		return { false, "Failed to open " + path + ".", {} };

	nlohmann::json data;
	try {
		data = nlohmann::json::parse(std::string(std::istreambuf_iterator<char>(file), {}));
	}
	catch (const nlohmann::json::exception& e) {
		return { false, "Failed to parse " + path + ": " + e.what(), {} };
	}

	if (!data.is_object() || !data.contains("palette") || !data["palette"].is_array()
		|| !data.contains("runs") || !data["runs"].is_array())
		return { false, path + " is not a canvas file.", {} };

	const int version = data.value("version", 0);
	if (version != CanvasVersion)
		return { false, "Canvas version " + std::to_string(version) + " cannot be read; this build expects "
			+ std::to_string(CanvasVersion) + ".", {} };

	const int width = data.value("width", 0);
	const int height = data.value("height", 0);
	if (width != grid.GetWidthPx() || height != grid.GetHeightPx())
		return { false, "Canvas is " + std::to_string(width) + "x" + std::to_string(height)
			+ ", but the grid is " + std::to_string(grid.GetWidthPx()) + "x"
			+ std::to_string(grid.GetHeightPx()) + ".", {} };

	std::vector<MaterialID> lookup;
	std::vector<std::string> missing;
	try {
		for (const auto& entry : data["palette"]) {
			const auto name = entry.get<std::string>();
			lookup.push_back(registry.GetMaterialID(name));
			if (!registry.HasMaterial(name)) missing.push_back(name);
		}
	}
	catch (const nlohmann::json::exception&) {
		return { false, path + " has a malformed palette.", {} };
	}

	// Nothing is written until every check above has passed.
	grid.Clear();

	const std::size_t total = static_cast<std::size_t>(width) * height;
	std::size_t cursor = 0;
	for (const auto& run : data["runs"]) {
		if (!run.is_array() || run.size() != 3) continue;
		if (!run[0].is_number_integer() || !run[1].is_number_integer() || !run[2].is_number_integer()) continue;

		const int index = run[0].get<int>();
		const int lifespan = run[1].get<int>();
		const int count = run[2].get<int>();
		if (index < 0 || index >= static_cast<int>(lookup.size()) || count <= 0) continue;

		for (int i = 0; i < count && cursor < total; i++, cursor++) {
			grid.CreateAt(static_cast<std::uint16_t>(cursor % width),
				static_cast<std::uint16_t>(cursor / width),
				lookup[index], static_cast<std::uint8_t>(std::clamp(lifespan, 0, 255)));
		}
	}

	std::string message;
	if (!missing.empty()) {
		message = "Not loaded, so those tiles became Air: ";
		for (std::size_t i = 0; i < missing.size(); ++i)
			message += (i ? ", " : "") + missing[i];
		message += ".";
	}

	return { true, message, missing };
}
