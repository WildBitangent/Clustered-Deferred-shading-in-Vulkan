/**
 * @file 'main.cpp'
 * @brief Entry point
 * @copyright The MIT license 
 * @author Matej Karas
 */

#include <cstdlib>
#include <iostream>

#include "BaseApp.h"

#include "Model.h"
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
