#pragma once
#include <vector>
#include <string>
#include <SFML/Graphics.hpp>
#include <json.hpp>
#include <fstream>

#include "Block.h"

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
		int Scatter_chance;


		RGBA Min_color;
		RGBA Max_color;
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
				else if (material["name"] == "Stone")
					STONE_ID = index;
				else if (material["name"] == "Acid")
					ACID_ID = index; 
				else if (material["name"] == "Fire")
					FIRE_ID = index;
				else if (material["name"] == "Water")
					WATER_ID = index;
				else if (material["name"] == "Steam")
					STEAM_ID = index;
				else if (material["name"] == "Lava")
					LAVA_ID = index;
				else if (material["name"] == "Smoke")
					SMOKE_ID = index;
				else if (material["name"] == "Hot Stone")
					HOT_STONE_ID = index; 
				else if (material["name"] == "Dirty Water")
					DIRTY_WATER_ID = index;
				else if (material["name"] == "Ash")
					ASH_ID = index;
				else if (material["name"] == "Hot Ash")
					HOT_ASH_ID = index;

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
					material["scatter_chance"],

					RGBA(material["color_min"][0], material["color_min"][1], material["color_min"][2]),
					RGBA(material["color_max"][0], material["color_max"][1], material["color_max"][2]),
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
		inline static int WATER_ID;
		inline static int STEAM_ID;
		inline static int LAVA_ID;
		inline static int STONE_ID;
		inline static int SMOKE_ID;
		inline static int HOT_STONE_ID;
		inline static int DIRTY_WATER_ID;
		inline static int ASH_ID;
		inline static int HOT_ASH_ID;
		

	private:
		inline static std::vector<BlockData> m_mateials;
	};
}