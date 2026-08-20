#include "Simulation.h"

void scree::Simulation::Update_grid(Grid& grid)
{
	// Order matters: Reset_processed only clears active chunks, so the active set must be the
	// one this update sweeps. Against the previous set, a tile swapped into a newly-woken chunk
	// would keep a stale processed flag and sit out an update.
	grid.Mark_chunks_for_next_update();
	grid.Reset_processed();
	// The downward pass rebuilds the rising set, so it runs first and starts empty.
	grid.Clear_rising();
	Update_grid_directional(grid, 1);
	Update_grid_directional(grid, -1);

	grid.Mark_chunks_for_render();
}

void scree::Simulation::Update_grid_directional(Grid& grid, int gravityDirection)
{
	int y_increment = gravityDirection == 1 ? -1 : 1;
	int y_start = y_increment == 1 ? 0 : grid.Get_height_px() - 1;
	int y_end = y_increment == 1 ? grid.Get_height_px() : -1;

	int chunk_count_x = grid.Get_width_chunks();

	for (int y = y_start; y != y_end; y += y_increment)
	{
		int x_increment = fast_rand() & 1 ? 1 : -1;
		int chunk_start = x_increment == 1 ? 0 : chunk_count_x - 1;
		int chunk_end = x_increment == 1 ? chunk_count_x : -1;
		int chunk_y = y >> CHUNK_SHIFT;

		// Stepping chunk by chunk, not pixel by pixel: a per-pixel loop cost a chunk
		// lookup on every pixel even to skip a sleeping chunk.
		for (int chunk_x = chunk_start; chunk_x != chunk_end; chunk_x += x_increment)
		{
			if (!grid.Is_chunk_active(chunk_x, chunk_y)) continue;
			// Nothing in this chunk moves upward, so the upward pass has no work here.
			if (gravityDirection != 1 && !grid.Chunk_has_rising(chunk_x, chunk_y)) continue;

			int x0 = chunk_x << CHUNK_SHIFT; // equivalent to chunk_x * CHUNK_SIZE, but faster
			int x = x_increment == 1 ? x0 : x0 + CHUNK_SIZE - 1;
			for (int i = 0; i < CHUNK_SIZE; i++, x += x_increment)
				Update_pixel(grid, x, y, gravityDirection);
		}
	}
}

void scree::Simulation::Update_pixel(Grid& grid, int x, int y, int gravityDirection)
{
	// Air first: it's most of the grid, and testing it here skips loading the processed array.
	// The three checks are all early-outs, so their order only changes what gets loaded.
	auto& tile = grid.Get_at(x, y);
	if (tile.id == MaterialRegistry::AIR_ID) return;

	auto&& tile_material = m_registry.Get(tile.id);
	if (tile_material.movement.Y_direction != gravityDirection)
	{
		// Seen on the downward pass; its chunk goes into the rising set the upward pass reads.
		if (gravityDirection == 1) grid.mark_rising_at_pixel(x, y);
		return;
	}

	if (grid.Is_processed(x, y)) return;

	// tile_material threaded through the three steps: none changes this tile's material without ending its update.
	if (tile_material.lifespanData.Tick)
	{
		if (update_pixel_lifespan(grid, tile, tile_material, x, y))
		{
			grid.set_processed(x, y);
			return;
		}
	}

	if (tile_material.reactionSpan.count)
	{
		if (update_pixel_reaction(grid, tile, tile_material, x, y))
		{
			grid.set_processed(x, y);
			return;
		}
	}

	update_pixel_movement(grid, tile, tile_material, x, y);
	grid.set_processed(x, y);
}

bool scree::Simulation::update_pixel_reaction(Grid& grid, Block& tile, const MaterialData& tile_material, int x, int y)
{
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
			if (!grid.Is_in_bounds(next_x, next_y)) continue;

			auto& checked_tile = grid.Get_at(next_x, next_y);
			if (!m_registry.CanReact(checked_tile.id, reaction->TargetID, reaction->targetType)) continue;
			
			int chance = reaction->Chance;
			if (reaction->targetType == Reaction::TargetType::Tag)
				chance = m_registry.Get(checked_tile.id).tagIntensity.at(reaction->TargetID);

			if (fast_rand() % 100 >= chance)	continue;

			hasReacted = true;
			// Snapshot: the target transition rewrites checked_tile in place, so self would otherwise read the new lifespan.
			const std::uint8_t checked_lifespan = checked_tile.lifespan;
			const Transition* targetTransition = m_registry.PickTransition(reaction->TargetTransitionsSpan);
			const Transition* selfTransition = m_registry.PickTransition(reaction->SelfTransitionsSpan);
			if (targetTransition && !targetTransition->noTransition) {
				std::uint8_t lifespan = checked_lifespan;
				if (targetTransition->lifespanBase == Transition::LifeSpanBase::Initial)
					lifespan = m_registry.Get(targetTransition->nextID).lifespanData.Initial;
				else if (targetTransition->lifespanBase == Transition::LifeSpanBase::Reactor)
					lifespan = tile_cpy.lifespan;

				// Only the neighbour changes, so the loop's material data is still good.
				grid.Create_at(next_x, next_y, targetTransition->nextID, lifespan);
				grid.set_processed(next_x, next_y);
			}

			if (selfTransition && !selfTransition->noTransition) {
				std::uint8_t lifespan = tile_cpy.lifespan;
				if (selfTransition->lifespanBase == Transition::LifeSpanBase::Initial)
					lifespan = m_registry.Get(selfTransition->nextID).lifespanData.Initial;
				else if (selfTransition->lifespanBase == Transition::LifeSpanBase::Reactor)
					lifespan = checked_lifespan;

				// The tile is a different material now, so everything above is stale.
				grid.Create_at(x, y, selfTransition->nextID, lifespan);
				return true;
			}

			if (reaction->sample == Reaction::Sample::FirstToReact)
				break;
		}

		if (reaction->HaltUpdate && hasReacted) return false;
	}

	return false;
}

void scree::Simulation::update_pixel_movement(Grid& grid, Block& tile, const MaterialData& tile_material, int x, int y) {
	std::int8_t gravityDirection = tile_material.movement.Y_direction;

	// Anchor tiles: each neighbour is read once and matched against every anchor tag at once.
	// The old code re-read the four neighbours per anchor tag.
	if (tile_material.anchorTagBitmask) {
		const std::array<Vec2i, 4> directions = { {{0, -gravityDirection}, {1, 0}, {-1, 0}, {0, gravityDirection}} };

		for (auto dir : directions) {
			int next_x = x + dir.x;
			int next_y = y + dir.y;
			if (grid.Is_in_bounds(next_x, next_y)) {
				auto& checked_tile = grid.Get_at(next_x, next_y);
				if (m_registry.Get(checked_tile.id).tagBitmask & tile_material.anchorTagBitmask) {
					return; // Found an anchor tile nearby, do not move
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

	// Cascading tiles
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
		// Everything below is relative to the tile's own gravity, not the screen: for a
		// rising fluid the surface is the row under it and the drop is the row above.
		bool is_on_surface = false;
		if (grid.Is_in_bounds(x, y - gravityDirection) && grid.Get_at(x, y - gravityDirection).id == MaterialRegistry::AIR_ID)
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

				// Check if there is an empty space or lighter fluid the tile could fall into
				if (grid.Is_in_bounds(check_x, y + gravityDirection))
				{
					auto& drop_tile = grid.Get_at(check_x, y + gravityDirection);
					auto& drop_material = m_registry.Get(drop_tile.id);

					if (drop_tile.id == MaterialRegistry::AIR_ID ||
						(drop_material.movement.is_fluid && tile_material.movement.density > drop_material.movement.density))
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

// The caller has already checked that this material ticks.
bool scree::Simulation::update_pixel_lifespan(Grid& grid, Block& tile, const MaterialData& tile_material, int x, int y)
{
	grid.set_chunk_active_at_pixel(x, y);

	if (m_registry.Tick(tile))
	{
		auto transition = m_registry.PickTransition(tile_material.lifespanData.OnDeathTransitionSpan);
		if (!transition || transition->noTransition) return false;

		std::uint8_t new_lifespan = m_registry.Get(transition->nextID).lifespanData.Initial;
		grid.Create_at(x, y, transition->nextID, new_lifespan);
		return true;
	}

	return false;
}
