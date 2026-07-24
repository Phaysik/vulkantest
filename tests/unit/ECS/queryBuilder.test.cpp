#include <catch2/catch_test_macros.hpp>

#include "ECS/ecs.h"

#include <array>
#include <cstddef>
#include <stdexcept>

#include "Components/Health/healthComponent.h"
#include "Components/Position/positionComponent.h"
#include "ECS/entity.h"
#include "ECS/systemVersion.h"

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)

SCENARIO("QueryBuilder explicit component access tracking")
{
	using Dimensia::Components::Health;
	using Dimensia::Components::Position;
	using Dimensia::ECS::ECS;
	using Dimensia::ECS::Entity;
	using Dimensia::ECS::ExecutionPolicy;
	using Dimensia::ECS::SystemVersion;

	GIVEN("independent observers for two required component columns")
	{
		ECS ecs;
		Entity target{ecs.createEntityWith(Health{10.0F}, Position{.x = 1.0F, .y = 2.0F, .z = 3.0F})};
		SystemVersion healthObserver;
		SystemVersion positionObserver;
		ecs.query<Health>().version(healthObserver).changed<Health>().read<Health>().forEach([](Entity, Health &) {});
		ecs.query<Position>().version(positionObserver).changed<Position>().read<Position>().forEach([](Entity, Position &) {});

		WHEN("Position is declared read-only and Health writable")
		{
			ecs.query<Position, Health>().read<Position>().write<Health>().forEach(
				[](Entity, Position &, Health &health) { health.hp += 5.0F; });
			std::size_t healthChanges{};
			std::size_t positionChanges{};
			ecs.query<Health>().version(healthObserver).changed<Health>().read<Health>().forEach([&](Entity, Health &) {
				++healthChanges;
			});
			ecs.query<Position>().version(positionObserver).changed<Position>().read<Position>().forEach([&](Entity, Position &) {
				++positionChanges;
			});

			THEN("only the declared writable column advances")
			{
				CHECK((healthChanges == 1));
				CHECK((positionChanges == 0));
				CHECK((static_cast<int>(ecs.getComponent<Health>(target)->hp) == 15));
			}
		}
	}

	GIVEN("a read-declared mutable query")
	{
		ECS ecs;
		static_cast<void>(ecs.createEntityWith(Health{10.0F}));
		SystemVersion observer;
		ecs.query<Health>().version(observer).changed<Health>().read<Health>().forEach([](Entity, Health &) {});

		WHEN("the query performs no write")
		{
			ecs.query<Health>().read<Health>().forEach([](Entity, Health &) {});
			std::size_t changes{};
			ecs.query<Health>().version(observer).changed<Health>().read<Health>().forEach([&](Entity, Health &) { ++changes; });

			THEN("the observer remains clean")
			{
				CHECK((changes == 0));
			}
		}
	}

	GIVEN("a writable callback that throws after modifying data")
	{
		ECS ecs;
		Entity target{ecs.createEntityWith(Health{10.0F})};
		SystemVersion observer;
		ecs.query<Health>().version(observer).changed<Health>().read<Health>().forEach([](Entity, Health &) {});

		WHEN("the callback throws")
		{
			bool exceptionObserved{false};
			try
			{
				ecs.query<Health>().write<Health>().forEach([](Entity, Health &health) {
					health.hp = 25.0F;
					throw std::runtime_error("write failed after mutation");
				});
			}
			catch (const std::runtime_error &)
			{
				exceptionObserved = true;
			}
			std::size_t changes{};
			ecs.query<Health>().version(observer).changed<Health>().read<Health>().forEach([&](Entity, Health &) { ++changes; });

			THEN("the exception propagates and the partial write remains observable")
			{
				CHECK(exceptionObserved);
				CHECK((changes == 1));
				CHECK((static_cast<int>(ecs.getComponent<Health>(target)->hp) == 25));
			}
		}
	}

	GIVEN("one writable query for each execution policy")
	{
		std::array<ExecutionPolicy, 4> policies{ExecutionPolicy::Seq, ExecutionPolicy::Par, ExecutionPolicy::ParBatched,
											ExecutionPolicy::ParStealing,};

		WHEN("each policy executes a writable query")
		{
			std::array<std::size_t, 4> changeCounts{};
			for (std::size_t policyIndex{0}; policyIndex < policies.size(); ++policyIndex)
			{
				ECS policyEcs;
				static_cast<void>(policyEcs.createEntityWith(Health{10.0F}));
				SystemVersion observer;
				policyEcs.query<Health>().version(observer).changed<Health>().read<Health>().forEach([](Entity, Health &) {});
				policyEcs.query<Health>().policy(policies.at(policyIndex)).write<Health>().forEach([](Entity, Health &health) {
					health.hp += 1.0F;
				});
				policyEcs.query<Health>().version(observer).changed<Health>().read<Health>().forEach(
					[&](Entity, Health &) { ++changeCounts.at(policyIndex); });
			}

			THEN("every execution policy stamps the written component")
			{
				for (std::size_t changes : changeCounts)
				{
					CHECK((changes == 1));
				}
			}
		}
	}

	GIVEN("two systems with independent baselines")
	{
		ECS ecs;
		Entity target{ecs.createEntityWith(Health{10.0F})};
		SystemVersion firstObserver;
		SystemVersion secondObserver;
		ecs.query<Health>().version(firstObserver).changed<Health>().read<Health>().forEach([](Entity, Health &) {});
		ecs.query<Health>().version(secondObserver).changed<Health>().read<Health>().forEach([](Entity, Health &) {});

		WHEN("the component changes and both systems process it")
		{
			ecs.addComponent(target, Health{30.0F});
			std::size_t firstChanges{};
			std::size_t secondChanges{};
			ecs.query<Health>().version(firstObserver).changed<Health>().read<Health>().forEach([&](Entity, Health &) { ++firstChanges; });
			ecs.query<Health>().version(secondObserver).changed<Health>().read<Health>().forEach([&](Entity, Health &) {
				++secondChanges;
			});

			THEN("both systems observe the same world change")
			{
				CHECK((firstChanges == 1));
				CHECK((secondChanges == 1));
			}
		}
	}

	GIVEN("a versioned system that writes its queried component")
	{
		ECS ecs;
		static_cast<void>(ecs.createEntityWith(Health{10.0F}));
		SystemVersion systemVersion;

		WHEN("the system runs twice without an external change")
		{
			std::size_t firstRun{};
			std::size_t secondRun{};
			ecs.query<Health>().version(systemVersion).changed<Health>().write<Health>().forEach([&](Entity, Health &health) {
				++firstRun;
				health.hp += 1.0F;
			});
			ecs.query<Health>().version(systemVersion).changed<Health>().write<Health>().forEach([&](Entity, Health &) { ++secondRun; });

			THEN("the successful write epoch is consumed by the system baseline")
			{
				CHECK((firstRun == 1));
				CHECK((secondRun == 0));
			}
		}
	}
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)