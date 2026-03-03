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

		inline void Create(std::uint16_t width, std::uint16_t height);
		inline void Reset_proccessed() { std::fill(m_processed.begin(), m_processed.end(), false); }

		inline Block& Get_at(std::uint16_t x, std::uint16_t y) ;
		inline int Get_width_px() const { return m_width_px; }
		inline int Get_height_px() const { return m_height_px; }

		inline bool Is_processed(int x, int y) const { return m_processed[get_global_index(x, y)]; }
		inline bool Is_chunk_active_at_pixel(int x, int y) const { return m_chunks[get_chunk_index_from_pixel(x, y)].is_active; }
		inline bool Is_in_bounds(int x, int y)			const	{	return (x >= 0 && y >= 0 && x < m_width_px && y < m_height_px);	}

		inline void Set_at(std::uint16_t x, std::uint16_t y, Block block);
		inline void swap_pixels(sf::Vector2i pos1, sf::Vector2i pos2);
		inline void set_processed(int x, int y) { m_processed[get_global_index(x, y)] = true; }
		inline void set_chunk_active_at_pixel(int x, int y);
		inline void Get_active_chunks(std::vector<sf::Vector2i>& active_chunks) const;
		inline void Mark_chunks_for_next_update()
		{
			for (auto& chunk : m_chunks)
			{
				chunk.is_active = chunk.is_active_next_frame;
				chunk.is_active_next_frame = false;
			}
		}
		
		inline void Clear()
		{
			for (auto& chunk : m_chunks)
				chunk.Reset();
			std::fill(m_processed.begin(), m_processed.end(), false);
		}

	private:
		// Helper functions
		int get_chunk_index(int x, int y)	const	{	return y * m_width_ch + x;	}
		int get_chunk_index_from_pixel(int x, int y)	const { return get_chunk_index(x >> 5, y >> 5); }
		int get_global_index(int x, int y)	const	{	return y * m_width_px + x;	}
		
		std::vector<Chunk> m_chunks;
		// Dimensions in chunks 
		int	m_width_ch = 0;
		int	m_height_ch = 0;

		std::vector<uint8_t> m_processed;

		// Dimensions in pixels
		int m_width_px = 0;
		int m_height_px = 0;
	};

	inline void Grid::Create(std::uint16_t width_px, std::uint16_t height_px)
	{
		m_width_ch = ceil(width_px / float(CHUNK_SIZE));
		m_height_ch = ceil(height_px / float(CHUNK_SIZE));

		m_width_px = m_width_ch * CHUNK_SIZE;
		m_height_px = m_height_ch * CHUNK_SIZE;

		m_chunks.resize(m_width_ch * m_height_ch);
		m_processed.resize(m_width_px * m_height_px);
	}

	inline void Grid::swap_pixels(sf::Vector2i pos1, sf::Vector2i pos2)
	{
		auto temp = Get_at(pos1.x, pos1.y);
		Set_at(pos1.x, pos1.y, Get_at(pos2.x, pos2.y));
		Set_at(pos2.x, pos2.y, temp);
		m_processed[get_global_index(pos1.x, pos1.y)] = true;
		m_processed[get_global_index(pos2.x, pos2.y)] = true;
	}

	inline void Grid::set_chunk_active_at_pixel(int x, int y)
	{
		m_chunks[get_chunk_index_from_pixel(x, y)].is_active_next_frame = true;
		int local_x = x & 31;
		int local_y = y & 31;
		int chunk_x = x >> 5;
		int chunk_y = y >> 5;

		if (local_x == 0 && chunk_x != 0) 
			m_chunks[get_chunk_index(chunk_x - 1, chunk_y)].is_active_next_frame = true;
		else if (local_x == CHUNK_SIZE - 1 && chunk_x != m_width_ch - 1) 
			m_chunks[get_chunk_index(chunk_x + 1, chunk_y)].is_active_next_frame = true;

		if (local_y == 0 && chunk_y != 0)
			m_chunks[get_chunk_index(chunk_x, chunk_y - 1)].is_active_next_frame = true;
		else if (local_y == CHUNK_SIZE - 1 && chunk_y != m_height_ch - 1)
			m_chunks[get_chunk_index(chunk_x, chunk_y + 1)].is_active_next_frame = true;
	}

	inline void Grid::Get_active_chunks(std::vector<sf::Vector2i>& active_chunks) const
	{
		for (size_t i = 0; i < m_chunks.size(); i++)
		{
			if (m_chunks[i].is_active)
			{
				int x = i % m_width_ch;
				int y = i / m_width_ch;
				active_chunks.push_back({ x, y });
			}
		}
	}

	inline void Grid::Set_at(std::uint16_t x, std::uint16_t y, Block block)
	{
		std::uint16_t chunk_index = get_chunk_index_from_pixel(x, y);
		std::uint16_t local_x = x & 31;
		std::uint16_t local_y = y & 31;
		m_chunks[chunk_index].Set_at(local_x, local_y, block);
		set_chunk_active_at_pixel(x, y);
	}

	inline Block& Grid::Get_at(std::uint16_t x, std::uint16_t y) 
	{
		std::uint16_t chunk_index = get_chunk_index_from_pixel(x, y);
		std::uint16_t local_x = x & 31;
		std::uint16_t local_y = y & 31;
		
		return m_chunks[chunk_index].Get_at(local_x, local_y);
	}
}