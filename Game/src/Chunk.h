#pragma once
#define CHUNK_SIZE 32
#define USE_CHUNKS true

#include "Block.h"

namespace PS
{
	class Chunk
	{
	public:
		Chunk()
		{
			Reset();
		}

		void Reset()
		{
			for (size_t y = 0; y < CHUNK_SIZE; y++)
			{
				for (size_t x = 0; x < CHUNK_SIZE; x++)
				{
					data[y][x] = PS::Block::Create(PS::Air);
				}
			}

			is_active = false;
		}

		void Set_at(std::uint16_t x, std::uint16_t y, PS::Block block)
		{
			data[y][x] = block;
		}

		PS::Block Get_at(std::uint16_t x, std::uint16_t y) const
		{
			return data[y][x];
		}

		bool is_active = false;
		bool is_active_next_frame = false;
	private:
		PS::Block data[CHUNK_SIZE][CHUNK_SIZE];
	};

}