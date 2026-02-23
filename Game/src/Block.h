#pragma once
#include <SFML/Graphics.hpp>



namespace PS
{
	enum BlockID
	{
		Air,
		Sand,
		Stone,
		Water
	};

	struct BlockData
	{
		sf::Color Min_color;
		sf::Color Max_color;
		uint16_t Number_of_steps;
	};

	static std::vector<BlockData> data
	{
		{sf::Color(0, 0, 0), sf::Color(0, 0, 0), 1},
		{sf::Color(150, 150, 0), sf::Color(255, 255, 0), 8},
		{sf::Color(100, 100, 100), sf::Color(120, 120, 120), 4},
		{sf::Color(0, 0, 200), sf::Color(0, 0, 250), 8},
	};

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

		static Block Create(BlockID id)
		{
			return Block(id, Random_step(data.at(id).Min_color, data.at(id).Max_color, data.at(id).Number_of_steps));
		}

		bool Is_fluid()
		{
			switch (id)
			{
			case PS::Air:	return true;
			case PS::Sand:	return false;
			case PS::Stone:	return false;
			case PS::Water:	return true;
			default:		return false;
			}
		}
		bool Can_fall()
		{
			switch (id)
			{
			case PS::Sand:	return true;
			case PS::Water:	return true;
			default:		return false;
			}
		}

		bool Is_liquid()
		{
			switch (id)
			{
			case PS::Water:	return true;
			default:		return false;
			}
		}

		bool Can_cascade()
		{
			switch (id)
			{
			case PS::Sand:	return true;
			case PS::Water:	return true;
			default:		return false;
			}
		}

		float Get_density()
		{
			switch (id)
			{
			case PS::Air:	return 0;
			case PS::Sand:	return 80;
			case PS::Stone:	return 100;
			case PS::Water:	return 30;
			default:		return 0;
			}
		}

		BlockID id;
		sf::Color color;
	};
}