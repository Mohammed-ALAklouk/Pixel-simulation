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

		void Update_grid(Grid& grid);
		void Update_grid_directional(Grid& grid, int gravityDirection);
		void Update_pixel(Grid& grid, int x, int y, int gravityDirection);
		// tile_material is the caller's already-resolved material for tile. Nothing any
		// of these does can change it without also ending the tile's update, so it is
		// passed down rather than looked up three more times.
		bool update_pixel_lifespan(Grid& grid, Block& tile, const MaterialData& tile_material, int x, int y);
		// True when a self transition rewrote the tile, so the caller must skip movement.
		bool update_pixel_reaction(Grid& grid, Block& tile, const MaterialData& tile_material, int x, int y);
		void update_pixel_movement(Grid& grid, Block& tile, const MaterialData& tile_material, int x, int y);

	private:
		// Bound once at construction. Game reloads materials by assigning into the same
		// registry object, so this reference stays valid across a reload.
		const MaterialRegistry& m_registry;
	};
}