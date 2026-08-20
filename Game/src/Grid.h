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
	// fixed +m_widthPx away. m_processed is indexed the same way as the blocks.
	class Grid
	{
	public:
		Grid() = default;

		inline void Create(std::uint16_t width, std::uint16_t height, const MaterialRegistry* material_registry);
		inline void ResetProcessed();

		inline Block& GetAt(std::uint16_t x, std::uint16_t y) { return m_blocks[GetGlobalIndex(x, y)]; }
		inline int GetWidthPx() const { return m_widthPx; }
		inline int GetHeightPx() const { return m_heightPx; }
		inline int GetWidthChunks() const { return m_widthCh; }
		inline int GetHeightChunks() const { return m_heightCh; }

		inline bool IsProcessed(int x, int y) const { return m_processed[GetGlobalIndex(x, y)]; }
		inline bool IsChunkActiveAtPixel(int x, int y) const { return m_chunkActive[GetChunkIndexFromPixel(x, y)] != 0; }
		inline bool IsChunkActive(int chunk_x, int chunk_y) const { return m_chunkActive[GetChunkIndex(chunk_x, chunk_y)] != 0; }
		inline bool IsInBounds(int x, int y)			const	{	return (x >= 0 && y >= 0 && x < m_widthPx && y < m_heightPx);	}
		inline bool ShouldRenderChunk(int chunk_x, int chunk_y) const { return m_chunksToRender[GetChunkIndex(chunk_x, chunk_y)] != 0; }

		// Which chunks hold a tile that moves against gravity. Rebuilt each downward pass (see
		// Simulation::UpdatePixel) so the upward pass can skip chunks with nothing to lift.
		inline bool ChunkHasRising(int chunk_x, int chunk_y) const { return m_chunkRising[GetChunkIndex(chunk_x, chunk_y)] != 0; }
		inline void ClearRising() { std::fill(m_chunkRising.begin(), m_chunkRising.end(), std::uint8_t{ 0 }); }
		inline void MarkRisingAtPixel(int x, int y);

		// Raw row-major access for callers that walk whole rows (the renderer).
		inline const Block* Row(int y) const { return m_blocks.data() + static_cast<std::size_t>(y) * m_widthPx; }

		inline void SetAt(std::uint16_t x, std::uint16_t y, Block block);
		inline void CreateAt(std::uint16_t x, std::uint16_t y, MaterialID id, std::uint8_t lifespan);
		inline void SwapPixels(std::pair<int,int> pos1, std::pair<int,int> pos2);
		inline void SetProcessed(int x, int y) { m_processed[GetGlobalIndex(x, y)] = true; }
		inline void SetChunkActiveAtPixel(int x, int y);
		inline void GetActiveChunks(std::vector<std::pair<int,int>>& active_chunks) const;

		inline void Remap(const std::vector<MaterialID>& remap);

		inline void MarkChunksForNextUpdate()
		{
			m_chunkActive.swap(m_chunkActiveNext);
			std::fill(m_chunkActiveNext.begin(), m_chunkActiveNext.end(), std::uint8_t{ 0 });
		}

		// Both arrays: m_chunkActiveNext is the true write set and catches what the swept set
		// misses -- a write with no update behind it (brush, Remap, everything while paused) and
		// a liquid skimming into a chunk the edge halo never woke. Called at the end of an update
		// and again before the render reads the mask, so a write between the two still lands.
		inline void MarkChunksForRender()
		{
			for (std::size_t i = 0; i < m_chunkActive.size(); i++) {
				if (m_chunkActive[i] || m_chunkActiveNext[i]) m_chunksToRender[i] = 1;
			}
		}

		inline void Clear()
		{
			Block air = m_materialRegistry->CreateBlock(MaterialRegistry::AIR_ID);
			std::fill(m_blocks.begin(), m_blocks.end(), air);
			std::fill(m_chunkActive.begin(), m_chunkActive.end(), std::uint8_t{ 0 });
			std::fill(m_chunkActiveNext.begin(), m_chunkActiveNext.end(), std::uint8_t{ 0 });
			std::fill(m_processed.begin(), m_processed.end(), std::uint8_t{ 0 });
			ClearRising();
			// Both activity arrays were just zeroed, so nothing else marks these dirty; without
			// this the render mask stays empty and the screen keeps showing the pre-clear grid.
			std::fill(m_chunksToRender.begin(), m_chunksToRender.end(), std::uint8_t{ 1 });
		}

		inline void ClearChunksToRender()
		{
			std::fill(m_chunksToRender.begin(), m_chunksToRender.end(), std::uint8_t{ 0 });
		}
	private:
		const MaterialRegistry* m_materialRegistry = nullptr;
		int GetChunkIndex(int x, int y)	const	{	return y * m_widthCh + x;	}
		int GetChunkIndexFromPixel(int x, int y)	const { return GetChunkIndex(x >> CHUNK_SHIFT, y >> CHUNK_SHIFT); }
		int GetGlobalIndex(int x, int y)	const	{	return y * m_widthPx + x;	}

		std::vector<Block> m_blocks;

		// One byte per chunk, not a flag inside each: the whole map stays cache-resident, so
		// waking a chunk never evicts block data. Two arrays swapped each update, not read+write flags.
		std::vector<std::uint8_t> m_chunkActive;
		std::vector<std::uint8_t> m_chunkActiveNext;
		std::vector<std::uint8_t> m_chunkRising;
		std::vector<std::uint8_t> m_chunksToRender;

		int	m_widthCh = 0;
		int	m_heightCh = 0;

		std::vector<uint8_t> m_processed;

		int m_widthPx = 0;
		int m_heightPx = 0;
	};

	inline void Grid::Create(std::uint16_t width_px, std::uint16_t height_px, const MaterialRegistry* material_registry)
	{
		m_materialRegistry = material_registry;
		m_widthCh = static_cast<int>(ceil(width_px / float(CHUNK_SIZE)));
		m_heightCh = static_cast<int>(ceil(height_px / float(CHUNK_SIZE)));

		m_widthPx = m_widthCh * CHUNK_SIZE;
		m_heightPx = m_heightCh * CHUNK_SIZE;

		m_blocks.assign(static_cast<std::size_t>(m_widthPx) * m_heightPx,
			material_registry->CreateBlock(MaterialRegistry::AIR_ID));
		m_chunkActive.assign(static_cast<std::size_t>(m_widthCh) * m_heightCh, 0);
		m_chunkActiveNext.assign(static_cast<std::size_t>(m_widthCh) * m_heightCh, 0);
		m_chunksToRender.assign(static_cast<std::size_t>(m_widthCh) * m_heightCh, 0);
		m_chunkRising.assign(static_cast<std::size_t>(m_widthCh) * m_heightCh, 0);
		m_processed.assign(static_cast<std::size_t>(m_widthPx) * m_heightPx, 0);
	}

	// Only rows under an active chunk were touched, so clearing the rest is wasted bandwidth --
	// on a mostly-settled grid, a few kilobytes instead of a full-array memset each update.
	inline void Grid::ResetProcessed()
	{
		for (int chunk_y = 0; chunk_y < m_heightCh; chunk_y++)
		{
			for (int chunk_x = 0; chunk_x < m_widthCh; chunk_x++)
			{
				if (!m_chunkActive[GetChunkIndex(chunk_x, chunk_y)]) continue;

				// Widen to the whole run of active chunks in this row so one memset covers them.
				int run_end = chunk_x + 1;
				while (run_end < m_widthCh && m_chunkActive[GetChunkIndex(run_end, chunk_y)]) run_end++;

				int x0 = chunk_x << CHUNK_SHIFT;
				int x1 = run_end << CHUNK_SHIFT;
				int y0 = chunk_y << CHUNK_SHIFT;
				for (int y = y0; y < y0 + CHUNK_SIZE; y++)
				{
					std::uint8_t* row = m_processed.data() + GetGlobalIndex(x0, y);
					std::fill(row, row + (x1 - x0), std::uint8_t{ 0 });
				}

				chunk_x = run_end;
			}
		}
	}

	// The most-run write in the sim. Going through GetAt/SetAt resolved each index five
	// times; here each end's index is computed once.
	inline void Grid::SwapPixels(std::pair<int,int> pos1, std::pair<int,int> pos2)
	{
		int index1 = GetGlobalIndex(pos1.first, pos1.second);
		int index2 = GetGlobalIndex(pos2.first, pos2.second);
		std::swap(m_blocks[index1], m_blocks[index2]);

		m_processed[index1] = true;
		m_processed[index2] = true;
		SetChunkActiveAtPixel(pos1.first, pos1.second);
		SetChunkActiveAtPixel(pos2.first, pos2.second);
	}

	// Edge halo, same idea as SetChunkActiveAtPixel: a tile moves at most one pixel per
	// update, so a rising tile on a chunk edge can cross into the neighbour before the upward pass.
	inline void Grid::MarkRisingAtPixel(int x, int y)
	{
		int local_x = x & CHUNK_MASK;
		int local_y = y & CHUNK_MASK;
		int chunk_x = x >> CHUNK_SHIFT;
		int chunk_y = y >> CHUNK_SHIFT;

		m_chunkRising[GetChunkIndex(chunk_x, chunk_y)] = 1;

		if (local_x == 0 && chunk_x != 0)
			m_chunkRising[GetChunkIndex(chunk_x - 1, chunk_y)] = 1;
		else if (local_x == CHUNK_SIZE - 1 && chunk_x != m_widthCh - 1)
			m_chunkRising[GetChunkIndex(chunk_x + 1, chunk_y)] = 1;

		if (local_y == 0 && chunk_y != 0)
			m_chunkRising[GetChunkIndex(chunk_x, chunk_y - 1)] = 1;
		else if (local_y == CHUNK_SIZE - 1 && chunk_y != m_heightCh - 1)
			m_chunkRising[GetChunkIndex(chunk_x, chunk_y + 1)] = 1;
	}

	inline void Grid::SetChunkActiveAtPixel(int x, int y)
	{
		int local_x = x & CHUNK_MASK;
		int local_y = y & CHUNK_MASK;
		int chunk_x = x >> CHUNK_SHIFT;
		int chunk_y = y >> CHUNK_SHIFT;

		m_chunkActiveNext[GetChunkIndex(chunk_x, chunk_y)] = 1;

		if (local_x == 0 && chunk_x != 0)
			m_chunkActiveNext[GetChunkIndex(chunk_x - 1, chunk_y)] = 1;
		else if (local_x == CHUNK_SIZE - 1 && chunk_x != m_widthCh - 1)
			m_chunkActiveNext[GetChunkIndex(chunk_x + 1, chunk_y)] = 1;

		if (local_y == 0 && chunk_y != 0)
			m_chunkActiveNext[GetChunkIndex(chunk_x, chunk_y - 1)] = 1;
		else if (local_y == CHUNK_SIZE - 1 && chunk_y != m_heightCh - 1)
			m_chunkActiveNext[GetChunkIndex(chunk_x, chunk_y + 1)] = 1;
	}

	inline void Grid::GetActiveChunks(std::vector<std::pair<int,int>>& active_chunks) const
	{
		for (int i = 0; i < static_cast<int>(m_chunkActive.size()); i++)
		{
			if (m_chunkActive[i])
			{
				int x = i % m_widthCh;
				int y = i / m_widthCh;
				active_chunks.push_back({ x, y });
			}
		}
	}

	inline void Grid::Remap(const std::vector<MaterialID>& remap)
	{
		for (int y = 0; y < m_heightPx; y++)
		{
			for (int x = 0; x < m_widthPx; x++)
			{
				auto& block = m_blocks[GetGlobalIndex(x, y)];
				if (block.id >= remap.size()) continue;

				SetChunkActiveAtPixel(x, y);
				m_materialRegistry->CreateBlock(block, remap[block.id], block.lifespan);
				if (m_materialRegistry->Get(block.id).movement.yDirection != 1)
					MarkRisingAtPixel(x, y);
			}
		}
	}

	// Re-marks the rising set: the downward pass rebuilds it, but a reaction behind the sweep
	// or the brush writing between updates would otherwise be missed.
	inline void Grid::SetAt(std::uint16_t x, std::uint16_t y, Block block)
	{
		m_blocks[GetGlobalIndex(x, y)] = block;
		SetChunkActiveAtPixel(x, y);
		if (m_materialRegistry->Get(block.id).movement.yDirection != 1)
			MarkRisingAtPixel(x, y);
	}

	inline void Grid::CreateAt(std::uint16_t x, std::uint16_t y, MaterialID id, std::uint8_t lifespan)
	{
		m_materialRegistry->CreateBlock(m_blocks[GetGlobalIndex(x, y)], id, lifespan);
		SetChunkActiveAtPixel(x, y);
		if (m_materialRegistry->Get(id).movement.yDirection != 1)
			MarkRisingAtPixel(x, y);
	}

}
