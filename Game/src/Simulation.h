#pragma once
#include "Grid.h"

namespace PS
{
	class Simulation
	{
	public:
		Simulation() = default;
		
		static void Update_grid(Grid& grid);
		static void Update_pixel(Grid& grid, int x, int y);
	private:
	};
}