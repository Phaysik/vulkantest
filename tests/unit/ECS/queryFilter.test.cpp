#include "ECS/queryFilter.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <vector>

#include "Components/Health/healthComponent.h"
#include "Components/Mana/manaComponent.h"
#include "Components/Position/positionComponent.h"
#include "ECS/commandBuffer.h"
#include "ECS/ecs.h"
#include "ECS/entity.h"
#include "ECS/systemVersion.h"
#include "Tags/Alive/aliveTag.h"
#include "Tags/Buffed/buffedTag.h"
#include "Tags/Debug/debugTag.h"

#include <catch2/catch_test_macros.hpp>

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)

SCENARIO("QueryFilter tag clause semantics")
{
	using Dimensia::Components::Health;
	using Dimensia::Components::Mana;
	using Dimensia::Components::Position;
	using Dimensia::ECS::All;
	using Dimensia::ECS::Any;
	using Dimensia::ECS::CommandBuffer;
	using Dimensia::ECS::ECS;
	using Dimensia::ECS::Entity;
	using Dimensia::ECS::ExecutionPolicy;
	using Dimensia::ECS::None;
	using Dimensia::ECS::SystemVersion;
	using Dimensia::Tags::AliveTag;
	using Dimensia::Tags::BuffedTag;
	using Dimensia::Tags::DebugTag;

	GIVEN("entities sharing regular archetypes but carrying different tags")
	{
		ECS ecs;
		Entity plain{ecs.createEntityWith(Position{.x = 1.0F, .y = 0.0F, .z = 0.0F})};
		Entity alive{ecs.createEntityWith(Position{.x = 2.0F, .y = 0.0F, .z = 0.0F}, AliveTag{})};
		Entity aliveDebug{ecs.createEntityWith(Position{.x = 3.0F, .y = 0.0F, .z = 0.0F}, AliveTag{}, DebugTag{})};
		Entity healthOnly{ecs.createEntityWith(Position{.x = 4.0F, .y = 0.0F, .z = 0.0F}, Health{40.0F})};
		Entity healthBuffed{ecs.createEntityWith(Position{.x = 5.0F, .y = 0.0F, .z = 0.0F}, Health{50.0F}, BuffedTag{})};

		WHEN("executing tag-only and mixed query clauses")
		{
			std::vector<Entity> anyAlive;
			ecs.query<Any<AliveTag>>().forEach([&](Entity entity) { anyAlive.push_back(entity); });
			std::vector<Entity> anyDebug;
			ecs.query<Any<DebugTag>>().forEach([&](Entity entity) { anyDebug.push_back(entity); });
			std::vector<Entity> noDebugTagOnly;
			ecs.query<None<DebugTag>>().forEach([&](Entity entity) { noDebugTagOnly.push_back(entity); });

			std::vector<Entity> noDebug;
			ecs.query<All<Position>, None<DebugTag>>().forEach([&](Entity entity, Position &) { noDebug.push_back(entity); });

			std::vector<Entity> mixedAny;
			ecs.query<All<Position>, Any<Health, AliveTag>>().forEach([&](Entity entity, Position &) { mixedAny.push_back(entity); });

			std::vector<Entity> combined;
			ecs.query<All<Position, AliveTag>, Any<Health, DebugTag>, None<BuffedTag>>().forEach(
				[&](Entity entity, Position &, AliveTag) { combined.push_back(entity); });

			THEN("all clauses use the entity tag row rather than only the archetype mask")
			{
				CHECK((anyAlive.size() == 2));
				CHECK((std::ranges::find(anyAlive, alive) != anyAlive.end()));
				CHECK((std::ranges::find(anyAlive, aliveDebug) != anyAlive.end()));
				REQUIRE((anyDebug.size() == 1));
				CHECK((anyDebug.front() == aliveDebug));
				CHECK((noDebugTagOnly.size() == 4));
				CHECK((std::ranges::find(noDebugTagOnly, aliveDebug) == noDebugTagOnly.end()));

				CHECK((noDebug.size() == 4));
				CHECK((std::ranges::find(noDebug, aliveDebug) == noDebug.end()));

				CHECK((mixedAny.size() == 4));
				CHECK((std::ranges::find(mixedAny, plain) == mixedAny.end()));

				REQUIRE((combined.size() == 1));
				CHECK((combined.front() == aliveDebug));
				CHECK(ecs.alive(healthOnly));
				CHECK(ecs.alive(healthBuffed));
			}
		}

		WHEN("iterating a filtered view containing required and excluded tags")
		{
			std::vector<Entity> viewed;
			for (auto [entity, position] : ecs.view<All<Position, AliveTag>, None<DebugTag>>())
			{
				static_cast<void>(position);
				viewed.push_back(entity);
			}

			THEN("the view yields only regular components for matching tag rows")
			{
				REQUIRE((viewed.size() == 1));
				CHECK((viewed.front() == alive));
			}
		}

		WHEN("executing a versioned tag-filtered builder query")
		{
			SystemVersion version;
			std::vector<Entity> processed;
			ecs.query<All<Position>, None<DebugTag>>().version(version).changed<Position>().forEach(
				[&](Entity entity, Position &) { processed.push_back(entity); });

			THEN("the changed path applies the same tag predicate")
			{
				CHECK((processed.size() == 4));
				CHECK((std::ranges::find(processed, aliveDebug) == processed.end()));
			}
		}

		WHEN("recording commands through a tag-filtered command query")
		{
			CommandBuffer commands;
			ecs.query<All<Position, AliveTag>, None<DebugTag>>().policy(ExecutionPolicy::Seq).commands(commands).forEach(
				[&](Entity entity, Position &, AliveTag) { commands.addComponent(entity, Health{99.0F}); });
			commands.apply(ecs);

			THEN("only matching rows receive commands")
			{
				CHECK(ecs.hasComponent<Health>(alive));
				CHECK(ecs.hasTag<AliveTag>(alive));
				CHECK_FALSE(ecs.hasComponent<Health>(aliveDebug));
			}
		}

		WHEN("running a tag-filtered query under every execution policy")
		{
			std::array<ExecutionPolicy, 4> policies{ExecutionPolicy::Seq, ExecutionPolicy::Par, ExecutionPolicy::ParBatched,
													ExecutionPolicy::ParStealing,};
			for (ExecutionPolicy policy : policies)
			{
				std::atomic<std::size_t> count{};
				ecs.query<Any<AliveTag>>().policy(policy).forEach([&](Entity) { count.fetch_add(1, std::memory_order_relaxed); });
				CHECK((count.load(std::memory_order_relaxed) == 2));
			}
		}

		WHEN("a new regular archetype is created after a tag query is cached")
		{
			std::size_t initialCount{};
			ecs.query<Any<AliveTag>>().forEach([&](Entity) { ++initialCount; });
			Entity manaAlive{ecs.createEntityWith(Mana{60}, AliveTag{})};
			std::vector<Entity> refreshed;
			ecs.query<Any<AliveTag>>().forEach([&](Entity entity) { refreshed.push_back(entity); });

			THEN("the incremental cache admits the new archetype candidate")
			{
				CHECK((initialCount == 2));
				CHECK((refreshed.size() == 3));
				CHECK((std::ranges::find(refreshed, manaAlive) != refreshed.end()));
			}
		}
	}
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)