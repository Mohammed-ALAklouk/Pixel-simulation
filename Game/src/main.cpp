#include "Game.h"

#include <exception>
#include <fstream>

int main()
{
	try
	{
		scree::Game app;
		app.run();
	}
	catch (const std::exception& e)
	{
		std::ofstream(std::string(GetApplicationDirectory()) + "scree_crash.log")
			<< "Unhandled exception: " << e.what() << '\n';
		return 1;
	}
	catch (...)
	{
		std::ofstream(std::string(GetApplicationDirectory()) + "scree_crash.log")
			<< "Unhandled non-standard exception.\n";
		return 1;
	}
}