#pragma once
#include "Grid.h"
#include "MaterialRegistry.h"
#include "vec2i.h"
#include "fast_rand.h"

#include <array>
#include <cstdint>

namespace scree
{
	class Simulation
	{
	public:
		explicit Simulation(const MaterialRegistry& material_registry)
			: m_registry(material_registry) {}

		void UpdateGrid(Grid& grid);
		void UpdateGridDirectional(Grid& grid, int gravityDirection);
		void UpdatePixel(Grid& grid, int x, int y, int gravityDirection);
		// tile_material is the caller's resolved material; nothing here changes it without also
		// ending the tile's update, so it's passed down rather than re-looked-up.
		bool UpdatePixelLifespan(Grid& grid, Block& tile, const MaterialData& tile_material, int x, int y);
		// True when a self transition rewrote the tile, so the caller must skip movement.
		bool UpdatePixelReaction(Grid& grid, Block& tile, const MaterialData& tile_material, int x, int y);
		void UpdatePixelMovement(Grid& grid, Block& tile, const MaterialData& tile_material, int x, int y);

	private:
		// Bound once; Game reloads by assigning into the same registry object, so this stays valid.
		const MaterialRegistry& m_registry;
	};
}