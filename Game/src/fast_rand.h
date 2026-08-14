#pragma once

#include <cstdint>

namespace scree
{
	namespace detail { inline std::uint32_t& rand_state() { static std::uint32_t state = 123456789; return state; } }

	// Resetting the state makes a run reproducible: the simulation takes every one of
	// its coin flips from here, so the same seed over the same starting grid replays
	// the same frames. The benchmark reseeds before each timed run so that repeats
	// measure the same work rather than drifting apart.
	inline void fast_rand_seed(std::uint32_t seed)
	{
		detail::rand_state() = seed ? seed : 123456789;	// zero is a fixed point of xorshift
	}

	inline int fast_rand()
	{
		std::uint32_t& state = detail::rand_state();
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return static_cast<int>(state & 0x7FFFFFFF);
	}
}
