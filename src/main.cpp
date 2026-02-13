#include <cstdlib>
#include <exception>
#include <iostream>

#include "Components/Transform/transformComponent.h"
#include "Core/configCat.h"
#include "ECS/entity.h"

int main()
{
	try
	{
		Dimensia::Core::ConfigCat::setSDKKey("");

		using Dimensia::Components::TransformComponent;
		Dimensia::ECS::Entity entity("MyEntity");
		const TransformComponent added{*entity.addComponent<TransformComponent>(TransformComponent())};
		const bool removed{entity.removeComponent<TransformComponent>()};

		if (removed)
		{
			const TransformComponent added2{*entity.addComponent<TransformComponent>(TransformComponent())};
		}
		else
		{
			std::cout << "Failed to remove component\n";
		}

		TransformComponent comp{*entity.getComponent<TransformComponent>()};

		comp.setPosition({0.0F, 0.0F, 0.0F});

		Dimensia::Core::ConfigCat::closeClient();
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}