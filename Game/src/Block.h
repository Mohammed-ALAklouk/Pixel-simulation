#pragma once
#include "rgb.h"
#include "MaterialData.h"
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

		static Block Create(MaterialID id)
		{
			auto& data = MaterialRegistry::Get(id);
			return Block(id, Random_step(data.minColor, data.maxColor, data.numberOfSteps));
		}

		bool Tick()
		{
			auto& data = MaterialRegistry::Get(id);
			if (lifespan < data.lifespanData.Tick) {
				lifespan = 0;
				return true;
			}
			
			lifespan -= data.lifespanData.Tick;
			if (data.interpolateColor)
				color = data.minColor + (data.maxColor - data.minColor) * (static_cast<float>(lifespan) / data.lifespanData.Initial);
		
			return lifespan == 0;
		}

		void Recreate(MaterialID id, std::uint8_t lifespan)
		{
			auto& data = MaterialRegistry::Get(id);
			if (this->id != id) {
				this->color = Random_step(data.minColor, data.maxColor, data.numberOfSteps);
			}

			this->id = id;
			this->lifespan = lifespan;
		}

		void CreateAt(MaterialID id, std::uint8_t lifespan)
		{
			auto& data = MaterialRegistry::Get(id);
			this->id = id;
			this->lifespan = lifespan;
			if (data.interpolateColor)
				this->color = data.minColor + (data.maxColor - data.minColor) * (static_cast<float>(lifespan) / data.lifespanData.Initial);
			else
				this->color = Random_step(data.minColor, data.maxColor, data.numberOfSteps);
		}


		MaterialID id;
		RGB color;
		std::uint8_t lifespan = 255;
	};
}