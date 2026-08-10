#include "Simulation.h"

void scree::Simulation::Update_grid(Grid& grid)
{
	grid.Reset_processed();
	grid.Mark_chunks_for_next_update();
	Update_grid_directional(grid, 1);
	Update_grid_directional(grid, -1);
}

void scree::Simulation::Update_grid_directional(Grid& grid, int gravityDirection)
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

void scree::Simulation::Update_pixel(Grid& grid, int x, int y, int gravityDirection)
{
	if (grid.Is_processed(x, y)) return;

	auto& tile = grid.Get_at(x, y);

	auto&& tile_material = m_registry.Get(tile.id);
	if (tile.id == MaterialRegistry::AIR_ID) return;
	if (tile_material.movement.Y_direction != gravityDirection) return;

	bool shouldDie = update_pixel_lifespan(grid, tile, x, y);
	if (shouldDie)
	{
		grid.set_processed(x, y);
		return;
	}

	update_pixel_reaction(grid, tile, x, y);
	update_pixel_movement(grid, tile, x, y);
	grid.set_processed(x, y);
}

void scree::Simulation::update_pixel_reaction(Grid& grid, Block& tile, int x, int y)
{
	auto&& tile_material = m_registry.Get(tile.id);
	int start_index = tile_material.reactionSpan.start;
	int count = tile_material.reactionSpan.count;
	auto tile_cpy = tile; // Copy of the tile to avoid issues with self-reactions

	for (int i = start_index; i < start_index + count; i++)
	{
		const auto& reaction = m_registry.GetReaction(i);
		std::int8_t direction = fast_rand() & 1 ? 1 : -1;
		std::int8_t gravityDirection = tile_material.movement.Y_direction;
		std::array<Vec2i, 4> directions = { {{0, gravityDirection}, {direction, 0}, {-direction, 0}, {0, -gravityDirection}} };
		bool hasReacted = false;

		for (auto dir : directions)
		{
			int next_x = x + dir.x;
			int next_y = y + dir.y;
			if (grid.Is_in_bounds(next_x, next_y))
			{
				auto& checked_tile = grid.Get_at(next_x, next_y);
				if (m_registry.CanReact(checked_tile.id, reaction->TargetID, reaction->targetType)) {
					int chance = reaction->Chance;
					if (reaction->targetType == Reaction::TargetType::Tag) 
						chance = m_registry.Get(checked_tile.id).tagData.at(reaction->TargetID).intensity;

					if (fast_rand() % 101 >= chance)	continue;
					hasReacted = true;
					const Transition* targetTransition = m_registry.PickTransition(reaction->TargetTransitionsSpan);
					const Transition* selfTransition = m_registry.PickTransition(reaction->SelfTransitionsSpan);
					if (targetTransition && !targetTransition->noTransition) {
						std::uint8_t lifespan = checked_tile.lifespan;
						if (targetTransition->lifespanBase == Transition::LifeSpanBase::Initial)
							lifespan = m_registry.Get(targetTransition->nextID).lifespanData.Initial;
						else if (targetTransition->lifespanBase == Transition::LifeSpanBase::Reactor)
							lifespan = tile_cpy.lifespan;

						grid.Create_at(next_x, next_y, targetTransition->nextID, lifespan);
						grid.set_processed(next_x, next_y);
					}

					if (selfTransition && !selfTransition->noTransition) {
						std::uint8_t lifespan = tile.lifespan;
						if (selfTransition->lifespanBase == Transition::LifeSpanBase::Initial)
							lifespan = m_registry.Get(selfTransition->nextID).lifespanData.Initial;
						else if (selfTransition->lifespanBase == Transition::LifeSpanBase::Reactor)
							lifespan = checked_tile.lifespan;

						grid.Create_at(x, y, selfTransition->nextID, lifespan);
					}

					if (reaction->sample == Reaction::Sample::FirstToReact)
						break;
				}
			}
		}

		if (reaction->HaltUpdate && hasReacted) return;
	}
}

void scree::Simulation::update_pixel_movement(Grid& grid, Block& tile, int x, int y) {
	auto&& tile_material = m_registry.Get(tile.id);
	std::int8_t gravityDirection = tile_material.movement.Y_direction;
	
	// Anchor tiles
	for (int i = 0; i < MAX_TAGS; i++) {
		if (tile_material.tagData.at(i).isAnchor) {
			std::array<Vec2i, 4> directions = { {{0, -gravityDirection}, {1, 0}, {-1, 0}, {0, gravityDirection}} };

			for (auto dir : directions) {
				int next_x = x + dir.x;
				int next_y = y + dir.y;
				if (grid.Is_in_bounds(next_x, next_y)) {
					auto& checked_tile = grid.Get_at(next_x, next_y);
					auto& checked_tile_material = m_registry.Get(checked_tile.id);
					if (checked_tile_material.tagData.at(i).intensity) {
						return; // Found an anchor tile nearby, do not move
					}
				}
			}
		}
	}

	// Falling tiles
	if (tile_material.movement.can_fall && grid.Is_in_bounds(x, y + gravityDirection))
	{
		if (fast_rand() % 100 < tile_material.movement.scatter_chance)
		{
			int direction = fast_rand() & 1 ? 1 : -1;
			int directions[2] = { direction, -direction };
			for (size_t i = 0; i < 2; i++)
			{
				if (grid.Is_in_bounds(x + directions[i], y + gravityDirection))
				{
					auto& checked_tile = grid.Get_at(x + directions[i], y + gravityDirection);
					auto& checked_tile_material = m_registry.Get(checked_tile.id);
					if (checked_tile_material.movement.is_fluid && checked_tile_material.movement.density < tile_material.movement.density)
					{
						grid.swap_pixels({ x,y }, { x + directions[i], y + gravityDirection });
						return;
					}
				}
			}
		}

		auto& next_tile = grid.Get_at(x, y + gravityDirection);
		auto& next_tile_material = m_registry.Get(next_tile.id);
		if (tile_material.movement.density > next_tile_material.movement.density)
		{
			grid.swap_pixels({ x,y }, { x, y + gravityDirection });
			return;
		}
	}

	// Cacading tiles 
	if (tile_material.movement.can_cascade)
	{
		if (fast_rand() & 1)
		{
			int direction = fast_rand() & 1 ? 1 : -1;


			if (grid.Is_in_bounds(x + direction, y + gravityDirection))
			{
				auto& checked_tile = grid.Get_at(x + direction, y + gravityDirection);
				auto& checked_tile_material = m_registry.Get(checked_tile.id);
				if (checked_tile_material.movement.is_fluid && checked_tile_material.movement.density < tile_material.movement.density)
				{
					grid.swap_pixels({ x,y }, { x + direction, y + gravityDirection });
					return;
				}
			}

			if (grid.Is_in_bounds(x - direction, y + gravityDirection))
			{
				auto& checked_tile = grid.Get_at(x - direction, y + gravityDirection);
				auto& checked_tile_material = m_registry.Get(checked_tile.id);
				if (checked_tile_material.movement.is_fluid && checked_tile_material.movement.density < tile_material.movement.density)
				{
					grid.swap_pixels({ x,y }, { x - direction, y + gravityDirection });
					return;
				}
			}
		}
	}

	// Liquid tiles
	if (tile_material.movement.is_liquid)
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
				auto& side_material = m_registry.Get(side_tile.id);

				if (side_tile.id != MaterialRegistry::AIR_ID &&
					(!side_material.movement.is_fluid || side_material.movement.density >= tile_material.movement.density))
				{
					break;
				}

				target_x = check_x;

				// Check if there is an empty space or lighter fluid directly below this new spot
				if (grid.Is_in_bounds(check_x, y + 1))
				{
					auto& below_tile = grid.Get_at(check_x, y + 1);
					auto& below_material = m_registry.Get(below_tile.id);

					if (below_tile.id == MaterialRegistry::AIR_ID ||
						(below_material.movement.is_fluid && tile_material.movement.density > below_material.movement.density))
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
			auto& next_material = m_registry.Get(next_tile.id);

			if (next_tile.id == MaterialRegistry::AIR_ID ||
				(next_material.movement.is_fluid && tile_material.movement.density > next_material.movement.density))
			{
				grid.swap_pixels({ x, y }, { x + direction, y });
				return;
			}

			// Keep chunks awake while touching other liquids to prevent early freezing
			if (next_material.movement.is_fluid && next_tile.id != tile.id)
			{
				grid.set_chunk_active_at_pixel(x, y);
			}
		}
	}
}

bool scree::Simulation::update_pixel_lifespan(Grid& grid, Block& tile, int x, int y)
{
	auto&& tile_material = m_registry.Get(tile.id);
	if (tile_material.lifespanData.Tick)
	{
		grid.set_chunk_active_at_pixel(x, y);

		if (m_registry.Tick(tile))
		{
			auto transition = m_registry.PickTransition(tile_material.lifespanData.OnDeathTransitionSpan);
			if (!transition || transition->noTransition) return false;

			std::uint8_t new_lifespan = m_registry.Get(transition->nextID).lifespanData.Initial;
			grid.Recreate_at(x, y, transition->nextID, new_lifespan);
			return true;
		}
	}

	return false;
}






