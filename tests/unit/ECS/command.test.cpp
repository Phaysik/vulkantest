#include <catch2/catch_test_macros.hpp>

#include <utility>

#include "Components/Name/nameComponent.h"
#include "ECS/command.h"
#include "ECS/entity.h"

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)

SCENARIO("Command add creation exception contract")
{
	using Dimensia::Components::Name;
	using Dimensia::ECS::Command;
	using Dimensia::ECS::Entity;

	GIVEN("an add command for an allocating component")
	{
		Name componentValue{"name"};

		THEN("command creation permits allocation and construction exceptions to propagate")
		{
			CHECK_FALSE(noexcept(Command::makeAdd(Entity{}, std::move(componentValue))));
		}
	}
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)
