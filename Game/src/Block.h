#pragma once
#include "rgb.h"
#include "MaterialRegistry.h"

#include <cstdint>
#include <cstdlib>

namespace PS
{
	struct Block
	{
		static RGB Random_step(RGB min, RGB max, uint16_t number_of_steps)
		{
			RGB step_color = max - min;
			float step = rand() % number_of_steps;
			int r = step_color.r * (step / number_of_steps);
			int g = step_color.g * (step / number_of_steps);
			int b = step_color.b * (step / number_of_steps);
			return min + RGB(r, g, b);
		}

		static Block Create(int id)
		{
			auto& data = MaterialRegistry::Get(id);
			return Block(id, Random_step(data.Min_color, data.Max_color, data.Number_of_steps));
		}

		int id;
		RGB color;
		std::uint8_t lifespan = 255;
	};
}