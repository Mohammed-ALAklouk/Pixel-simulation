#include "Simulation.h"

void PS::Simulation::Update_grid(Grid& grid)
{
	grid.Reset_proccessed();
	grid.Mark_chunks_for_next_update();
	Update_grid_drectional(grid, 1);
	Update_grid_drectional(grid, -1);
}

void PS::Simulation::Update_grid_drectional(Grid& grid, int gravityDirection)
{
	int y_increment = gravityDirection == 1 ? -1 : 1;
	int y_start = y_increment == 1 ? 0 : grid.Get_height_px() - 1;
	int y_end = y_increment == 1 ? grid.Get_height_px() : -1;

	for (int y = y_start; y != y_end; y += y_increment)
	{
		int x_increment = fast_rand() & 1 ? 1 : -1;
		int start = x_increment == 1 ? 0 : grid.Get_width_px() - 1;
		int end = x_increment == 1 ? grid.Get_width_px() : -1;

		for (int x = start; x != end; x += x_increment)
		{
			if (!grid.Is_chunk_active_at_pixel(x, y))
			{
				x += (CHUNK_SIZE - 1) * x_increment;
				if (x >= grid.Get_width_px() || x < 0) break;
				continue;
			}

			Update_pixel(grid, x, y, gravityDirection);
		}
	}
}

void PS::Simulation::Update_pixel(Grid& grid, int x, int y, int gravityDirection)
{
	if (grid.Is_processed(x, y)) return;

	auto&& tile = grid.Get_at(x, y);
	if (tile.id == MaterialRegistry::AIR_ID) return;
	
	auto&& tile_material = MaterialRegistry::Get(tile.id);
	if (tile_material.Gravity_direction != gravityDirection) return;

	if (fast_rand() % 10000 < tile_material.Decay_rate )
	{
		grid.Set_at(x, y, Block::Create(MaterialRegistry::AIR_ID));
		grid.set_processed(x, y);
		return;
	}
	else if (tile_material.Decay_rate > 0)
	{
		grid.set_chunk_active_at_pixel(x, y);
	}

	// Acid interactions
	if (tile.id == MaterialRegistry::ACID_ID)
	{
		int direction = fast_rand() & 1 ? 1 : -1;
		int axis = fast_rand() & 1 ? 0 : 1;

		int next_x = axis == 0 ? x + direction : x;
		int next_y = axis == 1 ? y + direction : y;

		if (grid.Is_in_bounds(next_x, next_y))
		{
			auto& checked_tile = grid.Get_at(next_x, next_y);
			auto& checked_tile_material = MaterialRegistry::Get(checked_tile.id);
			if (fast_rand() % 100 < checked_tile_material.Corrosion_chance)
			{
				grid.Set_at(x, y, Block::Create(MaterialRegistry::AIR_ID));
				grid.set_processed(x, y);

				grid.Set_at(next_x, next_y, Block::Create(MaterialRegistry::AIR_ID));
				grid.set_processed(next_x, next_y);
				return;
			}
		}
	}

	// Fire interactions
	if (tile.id == MaterialRegistry::FIRE_ID)
	{
		int direction = fast_rand() & 1 ? 1 : -1;
		int axis = fast_rand() & 1 ? 0 : 1;

		int next_x = axis == 0 ? x + direction : x;
		int next_y = axis == 1 ? y + direction : y;

		if (grid.Is_in_bounds(next_x, next_y))
		{
			auto& checked_tile = grid.Get_at(next_x, next_y);
			auto& checked_tile_material = MaterialRegistry::Get(checked_tile.id);
			if (fast_rand() % 100 < checked_tile_material.Burn_chance)
			{
				grid.Set_at(next_x, next_y, Block::Create(MaterialRegistry::FIRE_ID));
				grid.set_processed(next_x, next_y);
				return;
			}

			if (checked_tile.id == MaterialRegistry::WATER_ID)
			{
				grid.Set_at(x, y, Block::Create(MaterialRegistry::AIR_ID));
				grid.set_processed(x, y);

				grid.Set_at(next_x, next_y, Block::Create(MaterialRegistry::STEAM_ID));
				grid.set_processed(next_x, next_y);
				return;
			}
		}
	}

	// Falling tiles
	if (tile_material.Can_fall && grid.Is_in_bounds(x, y + gravityDirection))
	{
		auto& next_tile = grid.Get_at(x, y + gravityDirection);
		auto& next_tile_material = MaterialRegistry::Get(next_tile.id);
		if (tile_material.Density > next_tile_material.Density)
		{
			grid.swap_pixels({ x,y }, { x, y + gravityDirection });
			return;
		}
	}

	// Cacading tiles 
	if (tile_material.Can_caascade)
	{
		if (fast_rand() & 1)
		{
			int direction = fast_rand() & 1 ? 1 : -1;
			

			if (grid.Is_in_bounds(x + direction, y + gravityDirection))
			{
				auto& checked_tile = grid.Get_at(x + direction, y + gravityDirection);
				auto& checked_tile_material = MaterialRegistry::Get(checked_tile.id);
				if (checked_tile_material.Is_fluid && checked_tile_material.Density < tile_material.Density)
				{
					grid.swap_pixels({ x,y }, { x + direction, y + gravityDirection });
					return;
				}
			}

			if (grid.Is_in_bounds(x - direction, y + gravityDirection))
			{
				auto& checked_tile = grid.Get_at(x - direction, y + gravityDirection);
				auto& checked_tile_material = MaterialRegistry::Get(checked_tile.id);
				if (checked_tile_material.Is_fluid && checked_tile_material.Density < tile_material.Density)
				{
					grid.swap_pixels({ x,y }, { x - direction, y + gravityDirection });
					return;
				}
			}
		}
	}

	// Liquid tiles
	if ((MaterialRegistry::Get(tile.id).Is_liquid))
	{
		int direction = fast_rand() & 1 ? 1 : -1;

		if (grid.Is_in_bounds(x + direction, y))
		{
			auto& checked_tile = grid.Get_at(x + direction, y);
			if (checked_tile.id == MaterialRegistry::AIR_ID)
			{
				grid.swap_pixels({ x,y }, { x + direction, y });
				return;
			}
		}
	}
}
