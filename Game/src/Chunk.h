#pragma once
#define CHUNK_SIZE 32

#include "Block.h"

#include <cstddef>
#include <cstdint>

namespace scree
{
	class Chunk
	{
	public:
		void Set_at(std::uint16_t x, std::uint16_t y, scree::Block block)
		{
			data[y][x] = block;
		}

		scree::Block& Get_at(std::uint16_t x, std::uint16_t y) 
		{
			return data[y][x];
		}

		bool is_active = false;
		bool is_active_next_frame = false;
	private:
		scree::Block data[CHUNK_SIZE][CHUNK_SIZE];
	};
}