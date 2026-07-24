#include <catch2/catch_test_macros.hpp>

#include "ECS/ecs.h"

#include <cstddef>

#include "Components/Health/healthComponent.h"
#include "ECS/entity.h"
#include "ECS/systemVersion.h"

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)

SCENARIO("ECS view change tracking")
{
	using Dimensia::Components::Health;
	using Dimensia::ECS::ECS;
	using Dimensia::ECS::Entity;
	using Dimensia::ECS::SystemVersion;

	GIVEN("an explicitly writable view")
	{
		ECS ecs;
		Entity target{ecs.createEntityWith(Health{10.0F})};
		SystemVersion observer;
		ecs.query<Health>().version(observer).changed<Health>().read<Health>().forEach([](Entity, Health &) {});

		WHEN("the view mutates its component")
		{
			Entity viewedEntity{};
			for (auto [candidate, health] : ecs.writeView<Health>())
			{
				viewedEntity = candidate;
				health.hp = 40.0F;
			}
			std::size_t changes{};
			ecs.query<Health>().version(observer).changed<Health>().read<Health>().forEach([&](Entity, Health &) { ++changes; });

			THEN("the dereferenced chunk advances its component version")
			{
				CHECK((viewedEntity == target));
				CHECK((changes == 1));
			}
		}
	}

	GIVEN("an explicitly read-only view")
	{
		ECS ecs;
		static_cast<void>(ecs.createEntityWith(Health{10.0F}));
		SystemVersion observer;
		ecs.query<Health>().version(observer).changed<Health>().read<Health>().forEach([](Entity, Health &) {});

		WHEN("the read-only view is exhausted")
		{
			for (auto [viewEntity, health] : ecs.readView<Health>())
			{
				static_cast<void>(viewEntity);
				static_cast<void>(health);
			}
			std::size_t changes{};
			ecs.query<Health>().version(observer).changed<Health>().read<Health>().forEach([&](Entity, Health &) { ++changes; });

			THEN("the observer remains clean")
			{
				CHECK((changes == 0));
			}
		}
	}

	GIVEN("enough entities to occupy multiple Health chunks")
	{
		ECS ecs;
		std::size_t entityCount{1'000};
		for (std::size_t index{0}; index < entityCount; ++index)
		{
			static_cast<void>(ecs.createEntityWith(Health{static_cast<float>(index)}));
		}
		SystemVersion observer;
		ecs.query<Health>().version(observer).changed<Health>().read<Health>().forEach([](Entity, Health &) {});

		WHEN("a writable view dereferences one row and exits")
		{
			for (auto [viewEntity, health] : ecs.writeView<Health>())
			{
				static_cast<void>(viewEntity);
				health.hp += 1.0F;
				break;
			}
			std::size_t changedRows{};
			ecs.query<Health>().version(observer).changed<Health>().read<Health>().forEach([&](Entity, Health &) { ++changedRows; });

			THEN("only the touched chunk is considered changed")
			{
				CHECK((changedRows > 0));
				CHECK((changedRows < entityCount));
			}
		}
	}
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)