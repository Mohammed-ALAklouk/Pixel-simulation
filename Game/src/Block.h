#pragma once
#include "rgb.h"
#include "MaterialData.h"
#include "MaterialRegistry.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace scree
{
	struct Block
	{

		// The sign of (max - min) carries the direction, so a channel where min > max
		// descends. RGB's own operators can't be used here -- they saturate at 0 and would
		// flatten such a channel instead. The result is clamped rather than left to RGB's
		// uint8_t constructor, which wraps modularly: a t above 1 would turn 310 into 54.
		static RGB lerp(RGB min, RGB max, float t)
		{
			t = std::clamp(t, 0.0f, 1.0f);
			int r = int(min.r) + static_cast<int>((int(max.r) - int(min.r)) * t);
			int g = int(min.g) + static_cast<int>((int(max.g) - int(min.g)) * t);
			int b = int(min.b) + static_cast<int>((int(max.b) - int(min.b)) * t);
			return RGB(static_cast<std::uint8_t>(std::clamp(r, 0, 255)),
					   static_cast<std::uint8_t>(std::clamp(g, 0, 255)),
					   static_cast<std::uint8_t>(std::clamp(b, 0, 255)));
		}

		// Divides by steps-1 so the last step lands exactly on max; dividing by steps
		// leaves max unreachable. One step means one shade, and that shade is min.
		static RGB Random_step(RGB min, RGB max, uint16_t number_of_steps)
		{
			if (number_of_steps <= 1) return min;

			int step = fast_rand() % number_of_steps;
			return lerp(min, max, static_cast<float>(step) / (number_of_steps - 1));
		}

		// An Initial of 0 is accepted at load and would divide here, so guard it.
		static float lifespan_ratio(std::uint8_t lifespan, std::uint8_t initial)
		{
			return initial ? static_cast<float>(lifespan) / initial : 0.0f;
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
				color = lerp(data.minColor, data.maxColor,
					lifespan_ratio(lifespan, data.lifespanData.Initial));

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
				this->color = lerp(data.minColor, data.maxColor,
					lifespan_ratio(lifespan, data.lifespanData.Initial));
			else
				this->color = Random_step(data.minColor, data.maxColor, data.numberOfSteps);
		}


		MaterialID id;
		RGB color;
		std::uint8_t lifespan = 255;
	};
}