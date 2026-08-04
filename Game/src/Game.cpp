#include "Game.h"

PS::Game::Game()
{
	MaterialRegistry::Create_materials();

	window.create(sf::VideoMode(Window_size.x, Window_size.y), "window", sf::Style::Default);
	Grid_offset = (Window_size - sf::Vector2f(GridSize, GridSize) * TileSize) / 2.0f;
	window.setFramerateLimit(60);
	ImGui::SFML::Init(window);

	curser_shape.setFillColor(sf::Color::Transparent);
	curser_shape.setOutlineThickness(1);
	curser_shape.setRadius(curser_radius);
	curser_shape.setOrigin(sf::Vector2f(curser_radius, curser_radius));

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
	sf::Clock clock;
	if (!paused)
	{
		for (size_t i = 0; i < updates_per_frame; i++)
			Simulation::Update_grid(grid);
	}
	update_time = clock.getElapsedTime().asSeconds() / updates_per_frame;
}

void PS::Game::render()
{
	window.clear();

	Block tile;
	for (size_t x = 0; x < GridSize; x++)
	{
		for (size_t y = 0; y < GridSize; y++)
		{
			tile = grid.Get_at(x, y);
			render_tex.setPixel(x, y, sf::Color(tile.color.r, tile.color.g, tile.color.b));
		}
	}

	tex.loadFromImage(render_tex); 
	frame.setTexture(&tex);
	window.draw(frame);

	if (show_active_chunks)
	{
		active_chunks.clear();
		grid.Get_active_chunks(active_chunks);
		for (auto chunk: active_chunks)
		{
			chunk_debug_shape.setPosition(Grid_offset + sf::Vector2f(chunk.first * CHUNK_SIZE, chunk.second * CHUNK_SIZE) * TileSize);
			window.draw(chunk_debug_shape);
		}
	}

	auto mouse_pos = sf::Mouse::getPosition(window);
	curser_shape.setPosition(sf::Vector2f(mouse_pos));
	window.draw(curser_shape);

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
			SelectedMaterial += 1;
			if (SelectedMaterial >= MaterialRegistry::Get_materials_count())
				SelectedMaterial = 0;
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
		int material = mouse_left_down ? SelectedMaterial : 0;

		for (auto point: line)
			draw_curser(point, material);

		curser_start = curser_end;
	}
	
}

void PS::Game::UI()
{
	ImGui::Begin("Material menu");
	
	for (size_t i = 0; i < MaterialRegistry::Get_materials_count(); i++)
	{
		if (ImGui::Button(MaterialRegistry::Get(i).Name.c_str()))
			SelectedMaterial = i;
	}

	ImGui::Text(MaterialRegistry::Get(SelectedMaterial).Name.c_str());
	auto mouse_pos = sf::Mouse::getPosition(window) - sf::Vector2i(Grid_offset);
	if (grid.Is_in_bounds(mouse_pos.x, mouse_pos.y))
	{
		auto tile = grid.Get_at(mouse_pos.x, mouse_pos.y);
		ImGui::Text("Pixel under mouse %s", MaterialRegistry::Get(tile.id).Name.c_str());
	}


	if (ImGui::Button("Clear"))
		grid.Clear();

	ImGui::Checkbox("Pause", &paused);
	ImGui::SliderInt("Updates per frame", &updates_per_frame, 1, 10);
	if (ImGui::Button("Reload material file"))
		MaterialRegistry::Reload_materials();

	ImGui::Checkbox("Show active chunks", &show_active_chunks);
	ImGui::Checkbox("Show benchmarks", &show_bench_marks);
	
	ImGui::End();

	if (show_bench_marks)
	{
		ImGui::Begin("BenchMarks");

		ImGui::Text(("FPS: " + std::to_string(1.0f / delta)).c_str());
		ImGui::Text(("Update time: " + std::to_string(update_time)).c_str());
		ImGui::Text(("Input time: " + std::to_string(input_time)).c_str());
		ImGui::Text(("UI time: " + std::to_string(UI_time)).c_str());
		ImGui::Text(("Render time: " + std::to_string(render_time)).c_str());
		
		ImGui::End();
	}
}

void PS::Game::run()
{
	sf::Clock UI_clock;
	sf::Clock delta_clock;
	sf::Clock bench_clock;
	while (window.isOpen())
	{	
		bench_clock.restart();
		proccessInputs();
		input_time = bench_clock.restart().asSeconds();

		ImGui::SFML::Update(window, UI_clock.restart());
		UI_time = bench_clock.restart().asSeconds();

		update();
		update_time = bench_clock.restart().asSeconds();


		render();
		render_time = bench_clock.restart().asSeconds();
		delta = delta_clock.restart().asSeconds();
	}
}

void PS::Game::draw_curser(sf::Vector2i pos, int material)
{
	for (int x = -curser_radius; x < curser_radius; x++)
	{
		for (int y = -curser_radius; y < curser_radius; y++)
		{
			int world_x = pos.x + x;
			int world_y = pos.y + y;
			
			if (!in_bound(world_x, world_y))
				continue;


			int radiusSquared = curser_radius * curser_radius;
			if (x * x + y * y < radiusSquared && 
				(material == MaterialRegistry::AIR_ID || grid.Get_at(world_x, world_y).id == MaterialRegistry::AIR_ID))
			{
					grid.Set_at(world_x, world_y, Block::Create(material));
			}
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
