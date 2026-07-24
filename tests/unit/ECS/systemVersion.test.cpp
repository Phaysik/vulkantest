#include <catch2/catch_test_macros.hpp>

#include "ECS/systemVersion.h"

#include <cstddef>

#include "Components/Health/healthComponent.h"
#include "Components/Position/positionComponent.h"
#include "ECS/ecs.h"
#include "ECS/entity.h"

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)

SCENARIO("SystemVersion global change timeline")
{
	using Dimensia::Components::Health;
	using Dimensia::Components::Position;
	using Dimensia::ECS::ECS;
	using Dimensia::ECS::Entity;
	using Dimensia::ECS::SystemVersion;

	GIVEN("a frequently changed archetype and a quieter archetype")
	{
		ECS ecs;
		Entity hotEntity{ecs.createEntityWith(Health{10.0F})};
		Entity quietEntity{ecs.createEntityWith(Health{20.0F}, Position{.x = 1.0F, .y = 2.0F, .z = 3.0F})};
		for (std::size_t iteration{0}; iteration < 32; ++iteration)
		{
			ecs.addComponent(hotEntity, Health{static_cast<float>(iteration)});
		}
		SystemVersion observer;
		ecs.query<Health>().version(observer).changed<Health>().read<Health>().forEach([](Entity, const Health &) {});

		WHEN("the quieter archetype changes after the observer baseline")
		{
			ecs.addComponent(quietEntity, Health{99.0F});
			std::size_t processedCount{};
			Entity processedEntity{};
			ecs.query<Health>().version(observer).changed<Health>().read<Health>().forEach([&](Entity candidate, const Health &) {
				++processedCount;
				processedEntity = candidate;
			});

			THEN("the later world epoch is observed regardless of each chunk history")
			{
				CHECK((processedCount == 1));
				CHECK((processedEntity == quietEntity));
				CHECK(ecs.alive(hotEntity));
			}
		}
	}
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)