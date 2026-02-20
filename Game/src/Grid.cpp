#include "Grid.h"

void PS::Grid::Create(std::uint16_t width_px, std::uint16_t height_px)
{
	m_width_ch = ceil(width_px / float(CHUNK_SIZE));
	m_height_ch = ceil(height_px / float(CHUNK_SIZE));
	
	m_width_px = m_width_ch * CHUNK_SIZE;
	m_height_px = m_height_ch * CHUNK_SIZE;

	m_chunks.resize(m_width_ch * m_height_ch);
	m_processed.resize(m_width_px * m_height_px);
}

void PS::Grid::update_active_chunks()
{
	std::fill(m_processed.begin(), m_processed.end(), false);

	for (int y = m_height_px - 1; y >= 0; y--)
	{
		int x_increment = rand() % 2 ? 1 : -1;
		int start = x_increment == 1 ? 0 : m_width_px - 1;
		int end = x_increment == 1 ? m_width_px : -1;
		
		for (int x = start; x != end; x += x_increment)
		{
			if (!in_bound(x, y)) break;
			int chunk_x = x / CHUNK_SIZE;
			int chunk_y = y / CHUNK_SIZE;

			Chunk& chunk = m_chunks.at(get_chunk_index(chunk_x, chunk_y));
			if (!chunk.is_active)
			{
				x += (CHUNK_SIZE - 1) * x_increment;
				continue;
			}

			update_pixel(x, y);
		}
	}
}

void PS::Grid::update_pixel(int x, int y)
{
	int global_index = get_global_index(x, y);
	if (m_processed[global_index]) return;

	auto tile = Get_at(x, y);
	if (tile.id == BlockID::Air) return;

	// Falling tiles
	if (tile.Can_fall() && y != m_height_px - 1)
	{
		bool should_fall = in_bound(x, y + 1) && Get_at(x, y + 1).Is_fluid() && Get_at(x, y + 1).id != tile.id;
		if (should_fall)
		{
			tile.velocity.y += 0.5f;
			int final_y = round(y + tile.velocity.y);

			sf::Vector2i final_pos(x, y);
			for (int py = y + 1; py <= final_y; py++)
			{
				if (!in_bound(x, py) || !Get_at(x, py).Is_fluid() || tile.id == Get_at(x, py).id) {
					tile.velocity.y = 0;
					break;
				}
				final_pos.y = py;

			}

			Set_at(x, y, tile);
			swap_pixels({ x,y }, final_pos);
			return;
		}
	}

	// Sand 
	if (tile.id == Sand)
	{
		int direction = rand() % 2 ? 1 : -1;

		if (in_bound(x + direction, y + 1))
		{
			auto checked_tile = Get_at(x + direction, y + 1);
			if (checked_tile.Is_fluid() && checked_tile.Get_density() < tile.Get_density())
			{
				swap_pixels({ x,y }, { x + direction, y + 1 });
				return;
			}
		}

		if (in_bound(x - direction, y + 1))
		{
			auto checked_tile = Get_at(x - direction, y + 1);
			if (checked_tile.Is_fluid() && checked_tile.Get_density() < tile.Get_density())
			{
				swap_pixels({ x,y }, { x - direction, y + 1 });
				return;
			}
		}
	}

	// water
	if ((tile.id == Water))
	{
		int direction = rand() % 2 ? 1 : -1;

		if (in_bound(x + direction, y + 1))
		{
			auto checked_tile = Get_at(x + direction, y + 1);
			if (checked_tile.id == BlockID::Air)
			{
				swap_pixels({ x,y }, { x + direction, y + 1 });
				return;
			}
		}

		if (in_bound(x - direction, y + 1))
		{
			auto checked_tile = Get_at(x - direction, y + 1);
			if (checked_tile.id == BlockID::Air)
			{
				swap_pixels({ x,y }, { x - direction, y + 1 });
				return;
			}
		}
	}
}

void PS::Grid::update_chunk(int x_index, int y_index)
{
	Chunk& chunk = m_chunks.at(get_chunk_index(x_index, y_index));
	int world_x = x_index * CHUNK_SIZE;
	int world_y = y_index * CHUNK_SIZE;

	int x_increment = rand() % 2 ? 1 : -1;
	int start = x_increment == 1 ? 0 : CHUNK_SIZE - 1;
	int end = x_increment == 1 ? CHUNK_SIZE : -1;

	for (int local_y = CHUNK_SIZE - 1; local_y >= 0; local_y--)
	{
		int y = world_y + local_y;
		for (int local_x = start; local_x != end; local_x += x_increment)
		{
			int x = world_x + local_x;
			int global_index = get_global_index(x, y);
			if (m_processed[global_index]) continue;

			auto tile = Get_at(x, y);
			if (tile.id == BlockID::Air) continue;

			// Falling tiles
			if (tile.Can_fall() && y != m_height_px - 1)
			{
				bool should_fall = in_bound(x, y + 1) && Get_at(x, y + 1).Is_fluid() && Get_at(x, y + 1).id != tile.id;
				if (should_fall)
				{
					tile.velocity.y += 0.5f;
					int final_y = round(y + tile.velocity.y);

					sf::Vector2i final_pos(x, y);
					for (int py = y + 1; py <= final_y; py++)
					{
						if (!in_bound(x, py) || !Get_at(x, py).Is_fluid() || tile.id == Get_at(x, py).id) {
							tile.velocity.y = 0;
							break;
						}
						final_pos.y = py;

					}

					Set_at(x, y, tile);
					swap_pixels({ x,y }, final_pos);
					continue;
				}
			}

			// Sand 
			if (tile.id == Sand)
			{
				int direction = rand() % 2 ? 1 : -1;

				if (in_bound(x + direction, y + 1))
				{
					auto checked_tile = Get_at(x + direction, y + 1);
					if (checked_tile.Is_fluid() && checked_tile.Get_density() < tile.Get_density())
					{
						swap_pixels({ x,y }, { x + direction, y + 1 });
						continue;
					}
				}

				if (in_bound(x - direction, y + 1))
				{
					auto checked_tile = Get_at(x - direction, y + 1);
					if (checked_tile.Is_fluid() && checked_tile.Get_density() < tile.Get_density())
					{
						swap_pixels({ x,y }, { x - direction, y + 1 });
						continue;
					}
				}
			}

			// water
			if ((tile.id == Water))
			{
				int direction = rand() % 2 ? 1 : -1;

				if (in_bound(x + direction, y + 1))
				{
					auto checked_tile = Get_at(x + direction, y + 1);
					if (checked_tile.id == BlockID::Air)
					{
						swap_pixels({ x,y }, { x + direction, y + 1 });
						continue;
					}
				}

				if (in_bound(x - direction, y + 1))
				{
					auto checked_tile = Get_at(x - direction, y + 1);
					if (checked_tile.id == BlockID::Air)
					{
						swap_pixels({ x,y }, { x - direction, y + 1 });
						continue;
					}
				}
			}
			

		}
		
		// water sideways movement
		for (int local_x = start; local_x != end; local_x += x_increment)
		{
			int x = world_x + local_x;
			int global_index = get_global_index(x, y);
			if (m_processed[global_index]) continue;

			auto tile = Get_at(x, y);
			if (tile.id != BlockID::Water) continue;
			
			int direction = rand() % 2 ? 1 : -1;

			if (in_bound(x + direction, y))
			{
				auto checked_tile = Get_at(x + direction, y);
				if (checked_tile.id == BlockID::Air)
				{
					swap_pixels({ x,y }, { x + direction, y });
					continue;
				}
			}

			if (in_bound(x - direction, y))
			{
				auto checked_tile = Get_at(x - direction, y);
				if (checked_tile.id == BlockID::Air)
				{
					swap_pixels({ x,y }, { x - direction, y });
					continue;
				}
			}
		}
	}
}




