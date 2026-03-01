#pragma once
#include "Grid.h"
#include "MaterialRegistry.h"

namespace PS
{
	inline int fast_rand()
	{
		static uint32_t state = 123456789;
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return static_cast<int>(state & 0x7FFFFFFF);
	}

	class Simulation
	{
	public:
		Simulation() = default;
		
		static void Update_grid(Grid& grid);
		static void Update_grid_drectional(Grid& grid, int gravityDirection);
		static void Update_pixel(Grid& grid, int x, int y, int gravityDirection);
	private:
	};
}