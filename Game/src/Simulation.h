#pragma once
#include "Grid.h"
#include "MaterialRegistry.h"
#include "vec2i.h"
#include "fast_rand.h"

#include <array>
#include <cstdint>

namespace PS
{
	class Simulation
	{
	public:
		Simulation() = default;
		
		static void Update_grid(Grid& grid);
		static void Update_grid_drectional(Grid& grid, int gravityDirection);
		static void Update_pixel(Grid& grid, int x, int y, int gravityDirection);
		static bool update_pixel_lifespan(Grid& grid, Block& tile, int x, int y);
		static void update_pixel_reaction(Grid& grid, Block& tile, int x, int y);
		static void update_pixel_movement(Grid& grid, Block& tile, int x, int y);
	};
}