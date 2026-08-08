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

	auto & tile = grid.Get_at(x, y);
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

		std::array<Vec2i, 4> directions = { {{0, 1}, {direction, 0}, {-direction, 0}, {0, -1}} };

		for (auto dir : directions)
		{
			int next_x = x + dir.x;
			int next_y = y + dir.y;

			if (grid.Is_in_bounds(next_x, next_y))
			{
				auto& checked_tile = grid.Get_at(next_x, next_y);
				auto& checked_tile_material = MaterialRegistry::Get(checked_tile.id);
				
				
				if (checked_tile.id == MaterialRegistry::WATER_ID)
				{
					if (fast_rand() % 100 < 70)
						grid.Set_at(next_x, next_y, Block::Create(MaterialRegistry::STEAM_ID));
					else
						grid.Set_at(next_x, next_y, Block::Create(MaterialRegistry::DIRTY_WATER_ID));

					grid.set_processed(next_x, next_y);

					grid.Set_at(x, y, Block::Create(MaterialRegistry::DIRTY_WATER_ID));
					grid.set_processed(x, y);
					grid.set_chunk_active_at_pixel(x, y);
					grid.set_chunk_active_at_pixel(next_x, next_y);
					return;
				}

				if (checked_tile_material.Corrosion_chance > 0)
				{
					if (fast_rand() % 100 < checked_tile_material.Corrosion_chance)
					{
						if (fast_rand() % 100 < 70)
							grid.Set_at(next_x, next_y, Block::Create(MaterialRegistry::SMOKE_ID));
						else
							grid.Set_at(next_x, next_y, Block::Create(MaterialRegistry::AIR_ID));

						grid.set_processed(next_x, next_y);
					
						if (fast_rand() % 100 < 5)
						{
								grid.Set_at(x, y, Block::Create(MaterialRegistry::DIRTY_WATER_ID));
								grid.set_processed(x, y);
								return;
						}
						return;
					}

					grid.set_chunk_active_at_pixel(next_x, next_y);
				}
			}
		}
	}
	
	// Fire interactions
	if (tile.id == MaterialRegistry::FIRE_ID)
	{
		auto lifeSpan = tile.lifespan;
		
		grid.set_chunk_active_at_pixel(x, y);

		int direction = fast_rand() & 1 ? 1 : -1;
		std::array<Vec2i, 4> directions = { { {0, 1}, {direction, 0}, {-direction, 0}, {0, -1} } };

		bool hasFlammable_neighbors = false;
		for (auto dir: directions)
		{
			int next_x = x + dir.x;
			int next_y = y + dir.y;

			if (grid.Is_in_bounds(next_x, next_y))
			{
				auto& checked_tile = grid.Get_at(next_x, next_y);
				auto& checked_tile_material = MaterialRegistry::Get(checked_tile.id);
				if (checked_tile_material.Burn_chance > 0)
					hasFlammable_neighbors = true;

				if (fast_rand() % 100 < checked_tile_material.Burn_chance)
				{
					
					grid.Set_at(next_x, next_y, Block::Create(MaterialRegistry::FIRE_ID));
					grid.set_processed(next_x, next_y);

					if (fast_rand() % 100 < 1)
					{
						grid.Set_at(x, y, Block::Create(MaterialRegistry::HOT_ASH_ID));
						grid.set_processed(x, y);
						return;
					}
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

		// life span
		tile.lifespan -= fast_rand() % 15 + 1; 
		if (tile.lifespan > lifeSpan)
		{
			tile.lifespan = 0;

			if (fast_rand() % 100 < 25) {
				auto new_tile = Block::Create(MaterialRegistry::SMOKE_ID);
				new_tile.lifespan = 255;
				grid.Set_at(x, y, new_tile);
			}
			else
				grid.Set_at(x, y, Block::Create(MaterialRegistry::AIR_ID));

			grid.set_processed(x, y);
			return;
		}

		// color interpolation
		float life_ratio = tile.lifespan / 255.0f;
		tile.color.r = 150 + (105 * life_ratio);
		tile.color.g = 255 * life_ratio;

		if (hasFlammable_neighbors)
			return;
	}
	
	// Hot ash interactions
	if (tile.id == MaterialRegistry::HOT_ASH_ID)
	{
		if (tile.lifespan == 0)
		{
			tile.lifespan = 255;
			tile.id = MaterialRegistry::ASH_ID;
			return;
		}

		int direction = fast_rand() & 1 ? 1 : -1;
		std::array<Vec2i, 4> directions = { { {0, 1}, {direction, 0}, {-direction, 0}, {0, -1} } };
		bool has_flamable_neighbers = false;
		for (auto dir : directions)
		{
			int next_x = x + dir.x;
			int next_y = y + dir.y;

			if (grid.Is_in_bounds(next_x, next_y))
			{
				auto& checked_tile = grid.Get_at(next_x, next_y);
				auto& checked_tile_material = MaterialRegistry::Get(checked_tile.id);
				if (checked_tile_material.Burn_chance > 0) has_flamable_neighbers= true;

				if (fast_rand() % 100 < checked_tile_material.Burn_chance)
				{
					grid.Set_at(next_x, next_y, Block::Create(MaterialRegistry::FIRE_ID));
					grid.set_processed(next_x, next_y);
				}

				if (checked_tile.id == MaterialRegistry::WATER_ID)
				{
					grid.Set_at(x, y, Block::Create(MaterialRegistry::ASH_ID));
					grid.set_processed(x, y);

					grid.Set_at(next_x, next_y, Block::Create(MaterialRegistry::STEAM_ID));
					grid.set_processed(next_x, next_y);
					return;
				}
			}
		}

		if (!has_flamable_neighbers)
			tile.lifespan -= 1;
	}

	// Lava interactions
	if (tile.id == MaterialRegistry::LAVA_ID)
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
			}

			if (checked_tile.id == MaterialRegistry::WATER_ID)
			{
				if (grid.Is_in_bounds(next_x, next_y) && grid.Get_at(next_x, next_y).id == MaterialRegistry::WATER_ID)
				{
					auto new_tile = Block::Create(MaterialRegistry::HOT_STONE_ID);
					new_tile.lifespan = 10;
					grid.Set_at(next_x, next_y, new_tile);
					grid.set_processed(next_x, next_y);
				}
			}
		}
	}

	// Hot stone interactions
	if (tile.id == MaterialRegistry::HOT_STONE_ID)
	{
		if (tile.lifespan == 0)
		{
			tile.lifespan = 255;
			tile.id = MaterialRegistry::STONE_ID;
			return;
		}
		
		std::array<Vec2i, 4> directions = { { {0, 1}, {0, -1}, {1, 0}, {-1, 0} } };
		for (auto dir : directions) 
		{
			int next_x = x + dir.x;
			int next_y = y + dir.y;
			if (grid.Is_in_bounds(next_x, next_y) && grid.Get_at(next_x, next_y).id == MaterialRegistry::WATER_ID)
			{
				if (fast_rand() & 1)
				{
					auto t = Block::Create(MaterialRegistry::HOT_STONE_ID);
					t.lifespan = tile.lifespan - 1;

					grid.Set_at(next_x, next_y, t);
				}
				else
				{
					grid.Set_at(next_x, next_y, Block::Create(MaterialRegistry::STEAM_ID));
				}

				grid.set_processed(next_x, next_y);
			}
		}

		tile.lifespan -= 1;
		return;
	}

	// Falling tiles
	if (tile_material.Can_fall && grid.Is_in_bounds(x, y + gravityDirection))
	{ 
		if (fast_rand() % 100 < tile_material.Scatter_chance)
		{
			int direction = fast_rand() & 1 ? 1 : -1;
			int directions[2] = { direction, -direction };
			for (size_t i = 0; i < 2; i++)
			{
				if (grid.Is_in_bounds(x + directions[i], y + gravityDirection))
				{
					auto& checked_tile = grid.Get_at(x + directions[i], y + gravityDirection);
					auto& checked_tile_material = MaterialRegistry::Get(checked_tile.id);
					if (checked_tile_material.Is_fluid && checked_tile_material.Density < tile_material.Density)
					{
						grid.swap_pixels({ x,y }, { x + directions[i], y + gravityDirection });
						return;
					}
				}
			}
		}

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
	if (tile_material.Is_liquid)
	{
		bool is_on_surface = false;
		if (grid.Is_in_bounds(x, y - 1) && grid.Get_at(x, y - 1).id == MaterialRegistry::AIR_ID)
		{
			is_on_surface = true;
		}

		int direction = fast_rand() & 1 ? 1 : -1;

		if (is_on_surface)
		{
			int max_skims = 10; 
			int target_x = x;

			for (int i = 1; i <= max_skims; i++)
			{
				int check_x = x + (i * direction);
				if (!grid.Is_in_bounds(check_x, y)) break;

				auto& side_tile = grid.Get_at(check_x, y);
				auto& side_material = MaterialRegistry::Get(side_tile.id);

				if (side_tile.id != MaterialRegistry::AIR_ID &&
					(!side_material.Is_fluid || side_material.Density >= tile_material.Density))
				{
					break;
				}

				target_x = check_x;

				// Check if there is an empty space or lighter fluid directly below this new spot
				if (grid.Is_in_bounds(check_x, y + 1))
				{
					auto& below_tile = grid.Get_at(check_x, y + 1);
					auto& below_material = MaterialRegistry::Get(below_tile.id);

					if (below_tile.id == MaterialRegistry::AIR_ID ||
						(below_material.Is_fluid && tile_material.Density > below_material.Density))
					{
						break; // Found the cliff edge, stop scanning and take it
					}
				}
			}

			if (target_x != x)
			{
				grid.swap_pixels({ x, y }, { target_x, y });
				return;
			}
		}

		// 3. If submerged or trapped, just do standard slow mixing/pushing
		if (grid.Is_in_bounds(x + direction, y))
		{
			auto& next_tile = grid.Get_at(x + direction, y);
			auto& next_material = MaterialRegistry::Get(next_tile.id);

			if (next_tile.id == MaterialRegistry::AIR_ID ||
				(next_material.Is_fluid && tile_material.Density > next_material.Density))
			{
				grid.swap_pixels({ x, y }, { x + direction, y });
				return;
			}

			// Keep chunks awake while touching other liquids to prevent early freezing
			if (next_material.Is_fluid && next_tile.id != tile.id)
			{
				grid.set_chunk_active_at_pixel(x, y);
			}
		}
	}
}
