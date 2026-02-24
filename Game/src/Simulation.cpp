#include "Simulation.h"

void PS::Simulation::Update_grid(Grid& grid)
{
	grid.Reset_proccessed();

	for (int y = grid.Get_height_px() - 1; y >= 0; y--)
	{
		int x_increment = rand() % 2 ? 1 : -1;
		int start = x_increment == 1 ? 0 : grid.Get_width_px() - 1;
		int end = x_increment == 1 ? grid.Get_width_px() : -1;

		for (int x = start; x != end; x += x_increment)
		{
			if (!grid.Is_in_bounds(x, y)) break;

			if (!grid.Is_chunk_active_at_pixel(x, y))
			{
				x += (CHUNK_SIZE - 1) * x_increment;
				continue;
			}

			Update_pixel(grid, x, y);
		}
	}
}

void PS::Simulation::Update_pixel(Grid& grid, int x, int y)
{
	if (grid.Is_processed(x, y)) return;

	auto tile = grid.Get_at(x, y);
	if (tile.id == BlockID::Air) return;

	// Falling tiles
	if (tile.Can_fall())
	{
		if (grid.Is_in_bounds(x, y + 1) && grid.Get_at(x, y + 1).Is_fluid() && grid.Get_at(x, y + 1).id != tile.id)
		{
			grid.swap_pixels({ x,y }, { x, y + 1 });
			return;
		}
	}

	// Cacading tiles 
	if (tile.Can_cascade())
	{
		if (rand() % 100 <= 50)
		{
			int direction = rand() % 2 ? 1 : -1;

			if (grid.Is_in_bounds(x + direction, y + 1))
			{
				auto checked_tile = grid.Get_at(x + direction, y + 1);
				if (checked_tile.Is_fluid() && checked_tile.Get_density() < tile.Get_density())
				{
					grid.swap_pixels({ x,y }, { x + direction, y + 1 });
					return;
				}
			}

			if (grid.Is_in_bounds(x - direction, y + 1))
			{
				auto checked_tile = grid.Get_at(x - direction, y + 1);
				if (checked_tile.Is_fluid() && checked_tile.Get_density() < tile.Get_density())
				{
					grid.swap_pixels({ x,y }, { x - direction, y + 1 });
					return;
				}
			}
		}
	}

	// water
	if ((tile.id == Water))
	{
		int direction = rand() % 2 ? 1 : -1;

		// side to side
		if (grid.Is_in_bounds(x + direction, y))
		{
			auto checked_tile = grid.Get_at(x + direction, y);
			if (checked_tile.id == BlockID::Air)
			{
				grid.swap_pixels({ x,y }, { x + direction, y });
				return;
			}
		}

		if (grid.Is_in_bounds(x - direction, y))
		{
			auto checked_tile = grid.Get_at(x - direction, y);
			if (checked_tile.id == BlockID::Air)
			{
				grid.swap_pixels({ x,y }, { x - direction, y });
				return;
			}
		}
	}
}
