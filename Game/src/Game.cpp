#include "Game.h"

PS::Game::Game()
{
	window.create(sf::VideoMode(Window_size.x, Window_size.y), "window", sf::Style::Default);
	Grid_offset = (Window_size - sf::Vector2f(GridSize, GridSize) * TileSize) / 2.0f;
	window.setFramerateLimit(60);
	ImGui::SFML::Init(window);

	tex.create(GridSize, GridSize);
	render_tex.create(GridSize, GridSize);
	frame.setSize(sf::Vector2f(GridSize, GridSize) * TileSize);
	frame.setPosition(Grid_offset);
	frame.setOutlineColor(sf::Color::White);
	frame.setOutlineThickness(5);

	chunk_debug_shape.setSize(sf::Vector2f(CHUNK_SIZE, CHUNK_SIZE));
	chunk_debug_shape.setFillColor(sf::Color::Transparent);
	chunk_debug_shape.setOutlineColor(sf::Color::Red);
	chunk_debug_shape.setOutlineThickness(1);

	grid.Create(GridSize, GridSize);
	
	srand(time(NULL));
}

void PS::Game::update()
{
	active_chunks.clear();
	active_chunks = grid.Get_active_chunk_positions();
	grid.update_active_chunks();
}

void PS::Game::render()
{
	window.clear();

	for (size_t x = 0; x < GridSize; x++)
	{
		for (size_t y = 0; y < GridSize; y++)
		{
			render_tex.setPixel(x, y, grid.Get_at(x, y).color);
		}
	}

	tex.loadFromImage(render_tex); 
	frame.setTexture(&tex);
	window.draw(frame);

	auto topLeft = frame.getPosition();
	for (sf::Vector2i chunk: active_chunks)
	{
		chunk_debug_shape.setPosition(topLeft + sf::Vector2f(chunk.x * CHUNK_SIZE, chunk.y * CHUNK_SIZE) * TileSize);
		window.draw(chunk_debug_shape);
	}

	UI();
	ImGui::SFML::Render(window);

	window.display();
}

void PS::Game::proccessInputs()
{
	sf::Event event;
	while (window.pollEvent(event))
	{
		ImGui::SFML::ProcessEvent(event);
		if (event.type == sf::Event::Closed)
			window.close();

		if (event.type == sf::Event::MouseWheelScrolled)
		{
			if (SelectedMaterial == Sand)
				SelectedMaterial = Stone;
			else
				SelectedMaterial = Sand;
		}
	}

	bool mouse_left_down = sf::Mouse::isButtonPressed(sf::Mouse::Left);
	bool mouse_right_down = sf::Mouse::isButtonPressed(sf::Mouse::Right);

	sf::Vector2i mouse_pos = sf::Mouse::getPosition(window) - sf::Vector2i(Grid_offset);
	int mouse_x = mouse_pos.x / TileSize, mouse_y = mouse_pos.y / TileSize;
	
	if (!mouse_left_down && !mouse_right_down) {
		is_drawing_curser = false;
	}
	
	if ((mouse_left_down || mouse_right_down) && !is_drawing_curser) {
		curser_start = mouse_pos;
		is_drawing_curser = true;
	} 

	if (is_drawing_curser)
	{
		sf::Vector2i curser_end = mouse_pos;
		auto line = GetLine(curser_start, curser_end);
		PS::BlockID material = mouse_left_down ? SelectedMaterial : Air;

		for (auto point: line)
			draw_curser(point, material);

		curser_start = curser_end;
	}
	
}

void PS::Game::UI()
{
	ImGui::Begin("debug");

	if (ImGui::Button("Air"))
		SelectedMaterial = Air;
	if (ImGui::Button("Sand"))
		SelectedMaterial = Sand;
	if (ImGui::Button("Stone"))
		SelectedMaterial = Stone;
	if (ImGui::Button("Water"))
		SelectedMaterial = Water;

	std::string mat_name;
	switch (SelectedMaterial)
	{
	case PS::Air:
		mat_name = "Air";
		break;
	case PS::Sand:
		mat_name = "Sand";
		break;
	case PS::Stone:
		mat_name = "Stone";
		break;
	case PS::Water:
		mat_name = "Water";
		break;
	default:
		mat_name = "Nothing";
		break;
	}

	ImGui::Text(mat_name.c_str());

	ImGui::End();
}

void PS::Game::run()
{
	sf::Clock deltaClock;
	while (window.isOpen())
	{
		proccessInputs();
		ImGui::SFML::Update(window, deltaClock.restart());

		update();
		render();
	}
}

void PS::Game::draw_curser(sf::Vector2i pos, BlockID material)
{
	for (int x_offset = 0; x_offset < curser_radius; x_offset++)
	{
		for (int y_offset = 0; y_offset < curser_radius; y_offset++)
		{
			int x = pos.x + x_offset - int(curser_radius / 2);
			int y = pos.y + y_offset - int(curser_radius / 2);

			if (!in_bound(x, y))
				continue;


			if (material == Air || grid.Get_at(x,y).id == Air)
				grid.Set_at(x, y, Block::Create(material));
		}

	}
}

std::vector<sf::Vector2i> PS::Game::GetLine(sf::Vector2i start, sf::Vector2i end)
{
	if (start == end) return { start };
	
	std::vector<sf::Vector2i> points;
	
	float dx = end.x - start.x;
	float dy = end.y - start.y;

	int steps = std::max(abs(dy), abs(dx));

	float Xinc = dx / steps;
	float Yinc = dy / steps;

	for (size_t i = 0; i < steps; i++)
	{
		int final_x = start.x + Xinc * i;
		int final_y = start.y + Yinc * i;
		points.push_back(sf::Vector2i(final_x, final_y));
	}


	return points;
}
