#pragma once
#include "rgba.h"
#include "MaterialRegistry.h"

#include <cstdint>
#include <cstdlib>

namespace PS
{
	struct Block
	{
		static RGBA Random_step(RGBA min, RGBA max, uint16_t number_of_steps)
		{
			RGBA step_color = max - min;
			float step = rand() % number_of_steps;
			int r = step_color.r * (step / number_of_steps);
			int g = step_color.g * (step / number_of_steps);
			int b = step_color.b * (step / number_of_steps);
			return min + RGBA(r, g, b);
		}

		static Block Create(int id)
		{
			auto data = MaterialRegistry::Get(id);
			return Block(id, Random_step(data.Min_color, data.Max_color, data.Number_of_steps));
		}

		int id;
		RGBA color;
	};
}