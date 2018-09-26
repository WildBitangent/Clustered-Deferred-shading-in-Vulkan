#include <cstdlib>
#include <iostream>

#include "BaseApp.h"


int main() 
{
	try 
	{
		BaseApp::getInstance().run();
	}
	catch (const std::runtime_error& e) 
	{
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
