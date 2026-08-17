#pragma once

#include <string>
#include <vector>

namespace scree {
	class Grid;
	class MaterialRegistry;

	inline constexpr int CanvasVersion = 1;

	struct CanvasResult {
		bool ok = false;
		// Empty when there is nothing to report; set on failure and on dropped materials.
		std::string message;
		// Palette names the registry did not have, which loaded as Air.
		std::vector<std::string> missing;
	};

	CanvasResult SaveCanvas(const Grid& grid, const MaterialRegistry& registry, const std::string& path);
	CanvasResult LoadCanvas(Grid& grid, const MaterialRegistry& registry, const std::string& path);
}
