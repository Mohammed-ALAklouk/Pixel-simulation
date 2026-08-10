#pragma once

#include <cstdint>

namespace scree
{
	inline int fast_rand()
	{
		static uint32_t state = 123456789;
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return static_cast<int>(state & 0x7FFFFFFF);
	}
}