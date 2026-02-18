#pragma once
#include "Chunk.h"
#include "Block.h"
#include <vector>

namespace PS
{
	class Grid
	{
	public:
		Grid() = default;

		void Create(std::uint16_t width, std::uint16_t height);

		void update_active_chunks();
		
		void update_chunk(int x_index, int y_index);

		void swap_pixels(sf::Vector2i pos1, sf::Vector2i pos2)
		{
			auto temp = Get_at(pos1.x, pos1.y);
			Set_at(pos1.x, pos1.y, Get_at(pos2.x, pos2.y));
			Set_at(pos2.x, pos2.y, temp);
			m_processed[get_global_index(pos1.x, pos1.y)] = true;
			m_processed[get_global_index(pos2.x, pos2.y)] = true;
		}

		void Set_at(std::uint16_t x, std::uint16_t y, Block block)
		{
			std::uint16_t chunk_x = x / CHUNK_SIZE;
			std::uint16_t chunk_y = y / CHUNK_SIZE;
			std::uint16_t chunk_index = chunk_y * m_width_ch + chunk_x;
			std::uint16_t local_x = x % CHUNK_SIZE;
			std::uint16_t local_y = y % CHUNK_SIZE;
			m_chunks.at(chunk_index).Set_at(local_x, local_y, block);
			m_chunks.at(chunk_index).is_active = true;
		}

		Block Get_at(std::uint16_t x, std::uint16_t y) const
		{
			std::uint16_t chunk_x = x / CHUNK_SIZE;
			std::uint16_t chunk_y = y / CHUNK_SIZE;
			std::uint16_t chunk_index = chunk_y * m_width_ch + chunk_x;
			std::uint16_t local_x = x % CHUNK_SIZE;
			std::uint16_t local_y = y % CHUNK_SIZE;
			return m_chunks.at(chunk_index).Get_at(local_x, local_y);
		}

		std::vector<sf::Vector2i> Get_active_chunk_positions()
		{
			std::vector<sf::Vector2i> active_chunks;

			for (int i = 0; i < m_chunks.size(); i++)
			{
				if (m_chunks[i].is_active)
				{
					int x = i % m_width_ch;
					int y = i / m_width_ch;
					active_chunks.push_back({ x, y });
				}
			}

			return active_chunks;
		}

		// Helper functions
		bool in_bound(int x, int y)			const	{	return (x >= 0 && y >= 0 && x < m_width_px && y < m_height_px);	}
		int get_chunk_index(int x, int y)	const	{	return y * m_width_ch + x;	}
		int get_global_index(int x, int y)	const	{	return y * m_width_px + x;	}

	private:
		std::vector<Chunk> m_chunks;
		std::vector<uint8_t> m_processed;

		// Dimensions in chunks 
		int	m_width_ch = 0;
		int	m_height_ch = 0;
		// Dimensions in pixels
		int m_width_px = 0;
		int m_height_px = 0;
	};
}