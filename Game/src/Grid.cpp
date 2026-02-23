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

			Chunk& chunk = m_chunks.at(get_chunk_index_from_pixel(x,y));
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
	if (tile.Can_fall())
	{
		if (in_bound(x, y + 1) && Get_at(x, y + 1).Is_fluid() && Get_at(x, y + 1).id != tile.id)
		{
			swap_pixels({ x,y }, {x, y + 1});
			return;
		}
	}

	// Cacading tiles 
	if (tile.Can_cascade())
	{
		if (rand() % 100 <= 50)
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
				if (checked_tile.Is_fluid() && checked_tile.Get_density() < tile.Get_density() )
				{
					swap_pixels({ x,y }, { x - direction, y + 1 });
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
		if (in_bound(x + direction, y))
		{
			auto checked_tile = Get_at(x + direction, y);
			if (checked_tile.id == BlockID::Air)
			{
				swap_pixels({ x,y }, { x + direction, y });
				return;
			}
		}

		if (in_bound(x - direction, y))
		{
			auto checked_tile = Get_at(x - direction, y);
			if (checked_tile.id == BlockID::Air)
			{
				swap_pixels({ x,y }, { x - direction, y });
				return;
			}
		}
	}
}