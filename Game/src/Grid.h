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

		inline Block Get_at(std::uint16_t x, std::uint16_t y) const;
		inline int Get_width_px() const { return m_width_px; }
		inline int Get_height_px() const { return m_height_px; }

		inline bool Is_processed(int x, int y) const { return m_processed.at(get_global_index(x, y)); }
		inline bool Is_chunk_active_at_pixel(int x, int y) const { return m_chunks.at(get_chunk_index_from_pixel(x, y)).is_active; }
		inline bool Is_in_bounds(int x, int y)			const	{	return (x >= 0 && y >= 0 && x < m_width_px && y < m_height_px);	}

		inline void Set_at(std::uint16_t x, std::uint16_t y, Block block);
		inline void swap_pixels(sf::Vector2i pos1, sf::Vector2i pos2);

	private:
		// Helper functions
		int get_chunk_index(int x, int y)	const	{	return y * m_width_ch + x;	}
		int get_chunk_index_from_pixel(int x, int y)	const { return get_chunk_index(x / CHUNK_SIZE, y / CHUNK_SIZE); }
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

	inline void Grid::Set_at(std::uint16_t x, std::uint16_t y, Block block)
	{
		std::uint16_t chunk_index = get_chunk_index_from_pixel(x, y);
		std::uint16_t local_x = x % CHUNK_SIZE;
		std::uint16_t local_y = y % CHUNK_SIZE;
		m_chunks.at(chunk_index).Set_at(local_x, local_y, block);
		m_chunks.at(chunk_index).is_active = true;
	}

	inline Block Grid::Get_at(std::uint16_t x, std::uint16_t y) const
	{
		std::uint16_t chunk_index = get_chunk_index_from_pixel(x, y);
		std::uint16_t local_x = x % CHUNK_SIZE;
		std::uint16_t local_y = y % CHUNK_SIZE;
		return m_chunks.at(chunk_index).Get_at(local_x, local_y);
	}
}