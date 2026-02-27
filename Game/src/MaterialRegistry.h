#pragma once
#include <vector>
#include <string>
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
		std::string Name;
		bool Can_fall;
		bool Can_caascade;
		bool Is_fluid;
		float Density;


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
			m_mateials.push_back({"Air", true, false, true, 0, sf::Color(0, 0, 0), sf::Color(0, 0, 0), 1});
			m_mateials.push_back({"Sand", true, true, false, 1.5f, sf::Color(150, 150, 0), sf::Color(255, 255, 0), 8});
			m_mateials.push_back({"Stone", false, false, false, 2.5f, sf::Color(100, 100, 100), sf::Color(120, 120, 120), 4});
			m_mateials.push_back({"Water", true, true, true, 1.0f, sf::Color(0, 0, 200), sf::Color(0, 0, 250), 8});
		}

		static BlockData Get(BlockID id) 
		{
			return m_mateials.at(id);
		}

		static int Get_materials_count()
		{
			return m_mateials.size();
		}
		
	private:
		inline static std::vector<BlockData> m_mateials;
	};
}