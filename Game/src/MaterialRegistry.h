#pragma once
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include <fstream>
#include <raylib.h>

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

		static std::string Load_materials()
		{
			std::string error_message = "";
			std::string path = std::string(GetApplicationDirectory()) + "assets/materials.json";
			std::ifstream file(path);
			if (!file.is_open())
				return "[Error] Failed to load materials.json at " + path;

			nlohmann::json data = nlohmann::json::parse(file);

			int in_AIR_ID = -1, in_STONE_ID = -1, in_ACID_ID = -1, in_FIRE_ID = -1, in_WATER_ID = -1, in_STEAM_ID = -1, in_LAVA_ID = -1, in_SMOKE_ID = -1, in_HOT_STONE_ID = -1, in_DIRTY_WATER_ID = -1, in_ASH_ID = -1, in_HOT_ASH_ID = -1;
			std::vector<BlockData> in_materials;


			int index = 0;
			for (auto material: data["materials"])
			{
				try
				{
					if (material["name"] == "Air")
						in_AIR_ID = index;
					else if (material["name"] == "Stone")
						in_STONE_ID = index;
					else if (material["name"] == "Acid")
						in_ACID_ID = index;
					else if (material["name"] == "Fire")
						in_FIRE_ID = index;
					else if (material["name"] == "Water")
						in_WATER_ID = index;
					else if (material["name"] == "Steam")
						in_STEAM_ID = index;
					else if (material["name"] == "Lava")
						in_LAVA_ID = index;
					else if (material["name"] == "Smoke")
						in_SMOKE_ID = index;
					else if (material["name"] == "Hot Stone")
						in_HOT_STONE_ID = index; 
					else if (material["name"] == "Dirty Water")
						in_DIRTY_WATER_ID = index;
					else if (material["name"] == "Ash")
						in_ASH_ID = index;
					else if (material["name"] == "Hot Ash")
						in_HOT_ASH_ID = index;

					BlockData new_material = {
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
					};

					if (new_material.Number_of_steps < 1) new_material.Number_of_steps = 1;
					in_materials.push_back(new_material);
				}
				catch (const std::exception& e)
				{
					error_message += "[Error] Material number " + std::to_string(index) + " has an undefined property\n";
				}
				index++;
			}

			AIR_ID = in_AIR_ID;
			ACID_ID = in_ACID_ID;
			FIRE_ID = in_FIRE_ID;
			WATER_ID = in_WATER_ID;
			STEAM_ID = in_STEAM_ID;
			LAVA_ID = in_LAVA_ID;
			SMOKE_ID = in_SMOKE_ID;
			STONE_ID = in_STONE_ID;
			HOT_STONE_ID = in_HOT_STONE_ID;
			DIRTY_WATER_ID = in_DIRTY_WATER_ID;
			ASH_ID = in_ASH_ID;
			HOT_ASH_ID = in_HOT_ASH_ID;

			m_materials = in_materials;
			file.close();
			return error_message;
		}

		static const BlockData& Get(int id)
		{
			return m_materials[id];
		}

		static int Get_materials_count()
		{
			return m_materials.size();
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
		inline static std::vector<BlockData> m_materials;
	};
}