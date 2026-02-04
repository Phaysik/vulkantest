#include <cstdlib>
#include <exception>
#include <iostream>

#include "attributeMacros.h"
#include "triangleApplication.h"

ATTR_CONST int main()
{
	HelloTriangleApplication app;

	try
	{
		app.run();
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}