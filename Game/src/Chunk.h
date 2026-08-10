#pragma once
#define CHUNK_SIZE 32

#include "Block.h"

#include <cstddef>
#include <cstdint>

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
					data[y][x] = PS::Block::Create(MaterialRegistry::AIR_ID);
				}
			}

			is_active = false;
			is_active_next_frame = false;
		}

		void Set_at(std::uint16_t x, std::uint16_t y, PS::Block block)
		{
			data[y][x] = block;
		}

		void Recreate_at(std::uint16_t x, std::uint16_t y, MaterialID id, std::uint8_t lifespan)
		{
			data[y][x].Recreate(id, lifespan);
		}

		void CreateAt(std::uint16_t x, std::uint16_t y, MaterialID id, std::uint8_t lifespan)
		{
			data[y][x].CreateAt(id, lifespan);
		}

		PS::Block& Get_at(std::uint16_t x, std::uint16_t y) 
		{
			return data[y][x];
		}

		bool is_active = false;
		bool is_active_next_frame = false;
	private:
		PS::Block data[CHUNK_SIZE][CHUNK_SIZE];
	};
}