#pragma once

#include "Grid.h"

#include <ImGui/imgui.h>
#include <ImGui/imgui-SFML.h>
#include <SFML/Graphics.hpp>
#include <unordered_map>

namespace PS
{

	class Game
	{
	public:
		Game();
		
		void update();
		void render();
		void proccessInputs();
		void UI();
		void run();
		void draw_curser(sf::Vector2i pos, BlockID material);

		bool in_bound(int x, int y)			const { return (x >= 0 && y >= 0 && x < GridSize && y < GridSize); }

		
		std::vector<sf::Vector2i> GetLine(sf::Vector2i start, sf::Vector2i end);

		static constexpr uint32_t GridSize = 640;
		sf::Vector2f Window_size = sf::Vector2f(1400, 900);
		sf::Vector2f Grid_offset = sf::Vector2f(0, 0);
		float TileSize = 1;

		sf::RenderWindow window;
		Grid grid;
		BlockID SelectedMaterial = Stone;

		sf::Texture tex;
		sf::Image render_tex;
		sf::RectangleShape frame;


		sf::Vector2i mouse_down_pos;

		bool is_drawing_curser = false;
		sf::Vector2i curser_start;
	};
}