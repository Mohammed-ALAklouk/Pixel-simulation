#pragma once
#include "Chunk.h"
#include "Block.h"
#include "MaterialRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace scree
{
	// Blocks are stored row-major in one array, not tiled per chunk: the update sweeps a row
	// and the row under it, which are contiguous, so a falling tile's vertical neighbour is a
	// fixed +m_width_px away. m_processed is indexed the same way as the blocks.
	class Grid
	{
	public:
		Grid() = default;

		inline void Create(std::uint16_t width, std::uint16_t height, const MaterialRegistry* material_registry);
		inline void Reset_processed();

		inline Block& Get_at(std::uint16_t x, std::uint16_t y) { return m_blocks[get_global_index(x, y)]; }
		inline int Get_width_px() const { return m_width_px; }
		inline int Get_height_px() const { return m_height_px; }
		inline int Get_width_chunks() const { return m_width_ch; }
		inline int Get_height_chunks() const { return m_height_ch; }

		inline bool Is_processed(int x, int y) const { return m_processed[get_global_index(x, y)]; }
		inline bool Is_chunk_active_at_pixel(int x, int y) const { return m_chunk_active[get_chunk_index_from_pixel(x, y)] != 0; }
		inline bool Is_chunk_active(int chunk_x, int chunk_y) const { return m_chunk_active[get_chunk_index(chunk_x, chunk_y)] != 0; }
		inline bool Is_in_bounds(int x, int y)			const	{	return (x >= 0 && y >= 0 && x < m_width_px && y < m_height_px);	}
		inline bool Should_render_chunk(int chunk_x, int chunk_y) const { return m_chunks_to_render[get_chunk_index(chunk_x, chunk_y)] != 0; }

		// Which chunks hold a tile that moves against gravity. Rebuilt each downward pass (see
		// Simulation::Update_pixel) so the upward pass can skip chunks with nothing to lift.
		inline bool Chunk_has_rising(int chunk_x, int chunk_y) const { return m_chunk_rising[get_chunk_index(chunk_x, chunk_y)] != 0; }
		inline void Clear_rising() { std::fill(m_chunk_rising.begin(), m_chunk_rising.end(), std::uint8_t{ 0 }); }
		inline void mark_rising_at_pixel(int x, int y);

		// Raw row-major access for callers that walk whole rows (the renderer).
		inline const Block* Row(int y) const { return m_blocks.data() + static_cast<std::size_t>(y) * m_width_px; }

		inline void Set_at(std::uint16_t x, std::uint16_t y, Block block);
		inline void Create_at(std::uint16_t x, std::uint16_t y, MaterialID id, std::uint8_t lifespan);
		inline void swap_pixels(std::pair<int,int> pos1, std::pair<int,int> pos2);
		inline void set_processed(int x, int y) { m_processed[get_global_index(x, y)] = true; }
		inline void set_chunk_active_at_pixel(int x, int y);
		inline void Get_active_chunks(std::vector<std::pair<int,int>>& active_chunks) const;

		inline void Remap(const std::vector<MaterialID>& remap);

		inline void Mark_chunks_for_next_update()
		{
			m_chunk_active.swap(m_chunk_active_next);
			std::fill(m_chunk_active_next.begin(), m_chunk_active_next.end(), std::uint8_t{ 0 });
		}

		// Both arrays: m_chunk_active_next is the true write set and catches what the swept set
		// misses -- a write with no update behind it (brush, Remap, everything while paused) and
		// a liquid skimming into a chunk the edge halo never woke. Called at the end of an update
		// and again before the render reads the mask, so a write between the two still lands.
		inline void Mark_chunks_for_render()
		{
			for (std::size_t i = 0; i < m_chunk_active.size(); i++) {
				if (m_chunk_active[i] || m_chunk_active_next[i]) m_chunks_to_render[i] = 1;
			}
		}

		inline void Clear()
		{
			Block air = m_material_registry->CreateBlock(MaterialRegistry::AIR_ID);
			std::fill(m_blocks.begin(), m_blocks.end(), air);
			std::fill(m_chunk_active.begin(), m_chunk_active.end(), std::uint8_t{ 0 });
			std::fill(m_chunk_active_next.begin(), m_chunk_active_next.end(), std::uint8_t{ 0 });
			std::fill(m_processed.begin(), m_processed.end(), std::uint8_t{ 0 });
			Clear_rising();
			// Both activity arrays were just zeroed, so nothing else marks these dirty; without
			// this the render mask stays empty and the screen keeps showing the pre-clear grid.
			std::fill(m_chunks_to_render.begin(), m_chunks_to_render.end(), std::uint8_t{ 1 });
		}

		inline void Clear_chunks_to_render()
		{
			std::fill(m_chunks_to_render.begin(), m_chunks_to_render.end(), std::uint8_t{ 0 });
		}
	private:
		const MaterialRegistry* m_material_registry = nullptr;
		int get_chunk_index(int x, int y)	const	{	return y * m_width_ch + x;	}
		int get_chunk_index_from_pixel(int x, int y)	const { return get_chunk_index(x >> CHUNK_SHIFT, y >> CHUNK_SHIFT); }
		int get_global_index(int x, int y)	const	{	return y * m_width_px + x;	}

		std::vector<Block> m_blocks;

		// One byte per chunk, not a flag inside each: the whole map stays cache-resident, so
		// waking a chunk never evicts block data. Two arrays swapped each update, not read+write flags.
		std::vector<std::uint8_t> m_chunk_active;
		std::vector<std::uint8_t> m_chunk_active_next;
		std::vector<std::uint8_t> m_chunk_rising;
		std::vector<std::uint8_t> m_chunks_to_render;

		int	m_width_ch = 0;
		int	m_height_ch = 0;

		std::vector<uint8_t> m_processed;

		int m_width_px = 0;
		int m_height_px = 0;
	};

	inline void Grid::Create(std::uint16_t width_px, std::uint16_t height_px, const MaterialRegistry* material_registry)
	{
		m_material_registry = material_registry;
		m_width_ch = static_cast<int>(ceil(width_px / float(CHUNK_SIZE)));
		m_height_ch = static_cast<int>(ceil(height_px / float(CHUNK_SIZE)));

		m_width_px = m_width_ch * CHUNK_SIZE;
		m_height_px = m_height_ch * CHUNK_SIZE;

		m_blocks.assign(static_cast<std::size_t>(m_width_px) * m_height_px,
			material_registry->CreateBlock(MaterialRegistry::AIR_ID));
		m_chunk_active.assign(static_cast<std::size_t>(m_width_ch) * m_height_ch, 0);
		m_chunk_active_next.assign(static_cast<std::size_t>(m_width_ch) * m_height_ch, 0);
		m_chunks_to_render.assign(static_cast<std::size_t>(m_width_ch) * m_height_ch, 0);
		m_chunk_rising.assign(static_cast<std::size_t>(m_width_ch) * m_height_ch, 0);
		m_processed.assign(static_cast<std::size_t>(m_width_px) * m_height_px, 0);
	}

	// Only rows under an active chunk were touched, so clearing the rest is wasted bandwidth --
	// on a mostly-settled grid, a few kilobytes instead of a full-array memset each update.
	inline void Grid::Reset_processed()
	{
		for (int chunk_y = 0; chunk_y < m_height_ch; chunk_y++)
		{
			for (int chunk_x = 0; chunk_x < m_width_ch; chunk_x++)
			{
				if (!m_chunk_active[get_chunk_index(chunk_x, chunk_y)]) continue;

				// Widen to the whole run of active chunks in this row so one memset covers them.
				int run_end = chunk_x + 1;
				while (run_end < m_width_ch && m_chunk_active[get_chunk_index(run_end, chunk_y)]) run_end++;

				int x0 = chunk_x << CHUNK_SHIFT;
				int x1 = run_end << CHUNK_SHIFT;
				int y0 = chunk_y << CHUNK_SHIFT;
				for (int y = y0; y < y0 + CHUNK_SIZE; y++)
				{
					std::uint8_t* row = m_processed.data() + get_global_index(x0, y);
					std::fill(row, row + (x1 - x0), std::uint8_t{ 0 });
				}

				chunk_x = run_end;
			}
		}
	}

	// The most-run write in the sim. Going through Get_at/Set_at resolved each index five
	// times; here each end's index is computed once.
	inline void Grid::swap_pixels(std::pair<int,int> pos1, std::pair<int,int> pos2)
	{
		int index1 = get_global_index(pos1.first, pos1.second);
		int index2 = get_global_index(pos2.first, pos2.second);
		std::swap(m_blocks[index1], m_blocks[index2]);

		m_processed[index1] = true;
		m_processed[index2] = true;
		set_chunk_active_at_pixel(pos1.first, pos1.second);
		set_chunk_active_at_pixel(pos2.first, pos2.second);
	}

	// Edge halo, same idea as set_chunk_active_at_pixel: a tile moves at most one pixel per
	// update, so a rising tile on a chunk edge can cross into the neighbour before the upward pass.
	inline void Grid::mark_rising_at_pixel(int x, int y)
	{
		int local_x = x & CHUNK_MASK;
		int local_y = y & CHUNK_MASK;
		int chunk_x = x >> CHUNK_SHIFT;
		int chunk_y = y >> CHUNK_SHIFT;

		m_chunk_rising[get_chunk_index(chunk_x, chunk_y)] = 1;

		if (local_x == 0 && chunk_x != 0)
			m_chunk_rising[get_chunk_index(chunk_x - 1, chunk_y)] = 1;
		else if (local_x == CHUNK_SIZE - 1 && chunk_x != m_width_ch - 1)
			m_chunk_rising[get_chunk_index(chunk_x + 1, chunk_y)] = 1;

		if (local_y == 0 && chunk_y != 0)
			m_chunk_rising[get_chunk_index(chunk_x, chunk_y - 1)] = 1;
		else if (local_y == CHUNK_SIZE - 1 && chunk_y != m_height_ch - 1)
			m_chunk_rising[get_chunk_index(chunk_x, chunk_y + 1)] = 1;
	}

	inline void Grid::set_chunk_active_at_pixel(int x, int y)
	{
		int local_x = x & CHUNK_MASK;
		int local_y = y & CHUNK_MASK;
		int chunk_x = x >> CHUNK_SHIFT;
		int chunk_y = y >> CHUNK_SHIFT;

		m_chunk_active_next[get_chunk_index(chunk_x, chunk_y)] = 1;

		if (local_x == 0 && chunk_x != 0)
			m_chunk_active_next[get_chunk_index(chunk_x - 1, chunk_y)] = 1;
		else if (local_x == CHUNK_SIZE - 1 && chunk_x != m_width_ch - 1)
			m_chunk_active_next[get_chunk_index(chunk_x + 1, chunk_y)] = 1;

		if (local_y == 0 && chunk_y != 0)
			m_chunk_active_next[get_chunk_index(chunk_x, chunk_y - 1)] = 1;
		else if (local_y == CHUNK_SIZE - 1 && chunk_y != m_height_ch - 1)
			m_chunk_active_next[get_chunk_index(chunk_x, chunk_y + 1)] = 1;
	}

	inline void Grid::Get_active_chunks(std::vector<std::pair<int,int>>& active_chunks) const
	{
		for (int i = 0; i < static_cast<int>(m_chunk_active.size()); i++)
		{
			if (m_chunk_active[i])
			{
				int x = i % m_width_ch;
				int y = i / m_width_ch;
				active_chunks.push_back({ x, y });
			}
		}
	}

	inline void Grid::Remap(const std::vector<MaterialID>& remap)
	{
		for (int y = 0; y < m_height_px; y++)
		{
			for (int x = 0; x < m_width_px; x++)
			{
				auto& block = m_blocks[get_global_index(x, y)];
				if (block.id >= remap.size()) continue;

				set_chunk_active_at_pixel(x, y);
				m_material_registry->CreateBlock(block, remap[block.id], block.lifespan);
				if (m_material_registry->Get(block.id).movement.Y_direction != 1)
					mark_rising_at_pixel(x, y);
			}
		}
	}

	// Re-marks the rising set: the downward pass rebuilds it, but a reaction behind the sweep
	// or the brush writing between updates would otherwise be missed.
	inline void Grid::Set_at(std::uint16_t x, std::uint16_t y, Block block)
	{
		m_blocks[get_global_index(x, y)] = block;
		set_chunk_active_at_pixel(x, y);
		if (m_material_registry->Get(block.id).movement.Y_direction != 1)
			mark_rising_at_pixel(x, y);
	}

	inline void Grid::Create_at(std::uint16_t x, std::uint16_t y, MaterialID id, std::uint8_t lifespan)
	{
		m_material_registry->CreateBlock(m_blocks[get_global_index(x, y)], id, lifespan);
		set_chunk_active_at_pixel(x, y);
		if (m_material_registry->Get(id).movement.Y_direction != 1)
			mark_rising_at_pixel(x, y);
	}

}
