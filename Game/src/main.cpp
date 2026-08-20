#include "Game.h"

#include <exception>
#include <fstream>

int main()
{
	try
	{
#if defined(PLATFORM_WEB)
		// The loop runs in callbacks after main() returns, so the app must outlive
		// main(); leaked on purpose, reclaimed when the tab closes.
		(new scree::Game())->run();
#else
		scree::Game app;
		app.run();
#endif
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