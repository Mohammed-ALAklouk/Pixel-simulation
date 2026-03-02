#pragma once

#include "Grid.h"
#include "Simulation.h"

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
		void draw_curser(sf::Vector2i pos, int material);

		bool in_bound(int x, int y)			const { return (x >= 0 && y >= 0 && x < GridSize && y < GridSize); }

		
		std::vector<sf::Vector2i> GetLine(sf::Vector2i start, sf::Vector2i end);

		static constexpr uint32_t GridSize = 640;
		sf::Vector2f Window_size = sf::Vector2f(1400, 900);
		sf::Vector2f Grid_offset = sf::Vector2f(0, 0);
		float TileSize = 1;

		sf::RenderWindow window;
		Grid grid;
		int SelectedMaterial = 0;

		sf::Texture tex;
		sf::Image render_tex;
		sf::RectangleShape frame;


		sf::Vector2i mouse_down_pos;

		bool is_drawing_curser = false;
		sf::Vector2i curser_start;
		int curser_radius = 15;

		sf::RectangleShape chunk_debug_shape;
		std::vector<sf::Vector2i> active_chunks;

		int  updates_per_frame = 3;

		float input_time = 0;
		float UI_time = 0;
		float update_time = 0;
		float render_time = 0;
		float delta = 0;

		bool show_active_chunks = false;
		bool show_bench_marks = false;
	};
}