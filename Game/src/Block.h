#pragma once
#include <SFML/Graphics.hpp>
#include "MaterialRegistry.h"




namespace PS
{
	struct Block
	{
		static sf::Color Random_step(sf::Color min, sf::Color max, uint16_t number_of_steps)
		{
			sf::Color step_color = max - min;
			float step = rand() % number_of_steps;
			int r = step_color.r * (step / number_of_steps);
			int g = step_color.g * (step / number_of_steps);
			int b = step_color.b * (step / number_of_steps);
			return min + sf::Color(r, g, b);
		}

		static Block Create(int id)
		{
			auto data = MaterialRegistry::Get(id);
			return Block(id, Random_step(data.Min_color, data.Max_color, data.Number_of_steps));
		}

		int id;
		sf::Color color;
	};
}