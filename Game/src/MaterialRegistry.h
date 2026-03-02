#pragma once
#include <vector>
#include <string>
#include <SFML/Graphics.hpp>
#include "nlohmann/json.hpp"
#include <fstream>

namespace PS
{
	struct BlockData
	{
		std::string Name;
		bool Can_fall;
		bool Can_caascade;
		bool Is_fluid;
		bool Is_liquid;
		float Density;
		int Gravity_direction;
		int Decay_rate;
		int Corrosion_chance;
		int Burn_chance;


		sf::Color Min_color;
		sf::Color Max_color;
		uint16_t Number_of_steps;
	};


	class MaterialRegistry
	{
	public:
		MaterialRegistry() = default;

		static void Create_materials()
		{
			std::ifstream file("materials.json");
			if (!file.is_open())
			{
				printf("Failed to open materials.json file\n");
				exit(1);
			}

			nlohmann::json data = nlohmann::json::parse(file);
			
			int index = 0;
			for (auto material: data["materials"])
			{
				if (material["name"] == "Air")
					AIR_ID = index;
				else if (material["name"] == "Acid")
					ACID_ID = index; 
				else if (material["name"] == "Fire")
					FIRE_ID = index;

				m_mateials.push_back({
					material["name"],
					material["can_fall"],
					material["can_cascade"],
					material["is_fluid"],
					material["is_liquid"],
					material["density"],
					material["gravity"],
					material["decay_chance"],
					material["corrosion_chance"],
					material["burn_chance"],

					sf::Color(material["color_min"][0], material["color_min"][1], material["color_min"][2]),
					sf::Color(material["color_max"][0], material["color_max"][1], material["color_max"][2]),
					material["steps"],
				});
				index++;
			}

			file.close();
		}

		static void Reload_materials()
		{
			m_mateials.clear();
			Create_materials();
		}

		static const BlockData& Get(int id)
		{
			return m_mateials[id];
		}

		static int Get_materials_count()
		{
			return m_mateials.size();
		}
		
		inline static int AIR_ID;
		inline static int ACID_ID;
		inline static int FIRE_ID;

	private:
		inline static std::vector<BlockData> m_mateials;
	};
}