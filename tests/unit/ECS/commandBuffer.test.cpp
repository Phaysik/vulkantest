#include "ECS/commandBuffer.h"

#include <atomic>
#include <cstddef>
#include <latch>
#include <thread>
#include <vector>

#include "Components/Health/healthComponent.h"
#include "Components/Position/positionComponent.h"
#include "ECS/ecs.h"
#include "ECS/entity.h"
#include "ECS/queryFilter.h"
#include "ECS/systemVersion.h"
#include "Tags/Alive/aliveTag.h"

#include <catch2/catch_test_macros.hpp>

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)

SCENARIO("CommandBuffer concurrent recording and playback")
{
	using Dimensia::Components::Health;
	using Dimensia::ECS::CommandBuffer;
	using Dimensia::ECS::ECS;
	using Dimensia::ECS::Entity;

	GIVEN("multiple registered producer buffers and a concurrent consumer")
	{
		std::size_t producerCount{4};
		std::size_t commandsPerProducer{128};
		std::size_t entityCount{producerCount * commandsPerProducer};
		ECS ecs;
		CommandBuffer commands;
		std::vector<Entity> entities;
		entities.reserve(entityCount);
		for (std::size_t entityIndex{0}; entityIndex < entityCount; ++entityIndex)
		{
			entities.push_back(ecs.createEntity());
		}

		std::latch producersReady{static_cast<std::ptrdiff_t>(producerCount)};
		std::latch consumerReady{1};
		std::latch start{1};
		std::atomic<std::size_t> producersRemaining{producerCount};
		std::vector<std::jthread> producers;
		producers.reserve(producerCount);

		for (std::size_t producerIndex{0}; producerIndex < producerCount; ++producerIndex)
		{
			producers.emplace_back([&, producerIndex] {
				std::size_t firstEntityIndex{producerIndex * commandsPerProducer};
				commands.addComponent(entities.at(firstEntityIndex), Health{static_cast<float>(firstEntityIndex)});
				producersReady.count_down();
				start.wait();

				for (std::size_t offset{1}; offset < commandsPerProducer; ++offset)
				{
					std::size_t entityIndex{firstEntityIndex + offset};
					commands.addComponent(entities.at(entityIndex), Health{static_cast<float>(entityIndex)});
				}

				producersRemaining.fetch_sub(1, std::memory_order_release);
			});
		}

		std::jthread consumer([&] {
			consumerReady.count_down();
			start.wait();
			while (producersRemaining.load(std::memory_order_acquire) != 0)
			{
				commands.apply(ecs);
				std::this_thread::yield();
			}
			commands.apply(ecs);
		});

		producersReady.wait();
		consumerReady.wait();

		WHEN("recording and playback proceed concurrently")
		{
			start.count_down();
			producers.clear();
			consumer.join();
			commands.apply(ecs);

			THEN("every command is applied exactly to its intended entity")
			{
				for (std::size_t entityIndex{0}; entityIndex < entities.size(); ++entityIndex)
				{
					const Health *health{ecs.getComponent<Health>(entities.at(entityIndex))};
					REQUIRE((health != nullptr));
					CHECK((static_cast<std::size_t>(health->hp) == entityIndex));
				}
			}
		}
	}
}

SCENARIO("CommandBuffer concurrent recording and clearing")
{
	using Dimensia::Components::Health;
	using Dimensia::ECS::CommandBuffer;
	using Dimensia::ECS::ECS;
	using Dimensia::ECS::Entity;

	GIVEN("a producer recording while another thread repeatedly clears snapshots")
	{
		std::size_t entityCount{256};
		ECS ecs;
		CommandBuffer commands;
		std::vector<Entity> entities;
		entities.reserve(entityCount + 1);
		for (std::size_t entityIndex{0}; entityIndex <= entityCount; ++entityIndex)
		{
			entities.push_back(ecs.createEntity());
		}

		std::latch producerReady{1};
		std::latch start{1};
		std::atomic<bool> producerFinished{false};
		std::jthread producer([&] {
			commands.addComponent(entities.front(), Health{0.0F});
			producerReady.count_down();
			start.wait();
			for (std::size_t entityIndex{1}; entityIndex < entityCount; ++entityIndex)
			{
				commands.addComponent(entities.at(entityIndex), Health{static_cast<float>(entityIndex)});
			}
			producerFinished.store(true, std::memory_order_release);
		});

		std::jthread clearer([&] {
			start.wait();
			while (!producerFinished.load(std::memory_order_acquire))
			{
				commands.clear();
				std::this_thread::yield();
			}
			commands.clear();
		});

		producerReady.wait();

		WHEN("recording and clearing proceed concurrently")
		{
			start.count_down();
			producer.join();
			clearer.join();

			Entity markerEntity{entities.back()};
			commands.addComponent(markerEntity, Health{999.0F});
			commands.apply(ecs);

			THEN("the buffer remains usable for commands recorded after clearing")
			{
				const Health *health{ecs.getComponent<Health>(markerEntity)};
				REQUIRE((health != nullptr));
				CHECK((static_cast<int>(health->hp) == 999));
			}
		}
	}
}

SCENARIO("CommandBuffer deterministic playback ordering")
{
	using Dimensia::Components::Health;
	using Dimensia::ECS::CommandBuffer;
	using Dimensia::ECS::ECS;
	using Dimensia::ECS::Entity;

	GIVEN("multiple adds to the same component")
	{
		ECS ecs;
		CommandBuffer commands;
		Entity entity{ecs.createEntity()};
		commands.addComponent(entity, Health{10.0F});
		commands.addComponent(entity, Health{20.0F});

		WHEN("the commands are applied")
		{
			commands.apply(ecs);

			THEN("adds execute in recording order and the latest value wins")
			{
				const Health *health{ecs.getComponent<Health>(entity)};
				REQUIRE((health != nullptr));
				CHECK((static_cast<int>(health->hp) == 20));
			}
		}
	}

	GIVEN("a remove recorded before an add for the same component")
	{
		ECS ecs;
		CommandBuffer commands;
		Entity entity{ecs.createEntityWith(Health{5.0F})};
		commands.removeComponent<Health>(entity);
		commands.addComponent(entity, Health{30.0F});

		WHEN("the commands are applied")
		{
			commands.apply(ecs);

			THEN("phase ordering makes remove win over add")
			{
				CHECK_FALSE(ecs.hasComponent<Health>(entity));
			}
		}
	}

	GIVEN("two producer threads with an explicitly ordered recording point")
	{
		ECS ecs;
		CommandBuffer commands;
		Entity entity{ecs.createEntity()};
		std::latch firstRecorded{1};
		std::jthread firstProducer([&] {
			commands.addComponent(entity, Health{40.0F});
			firstRecorded.count_down();
		});
		std::jthread secondProducer([&] {
			firstRecorded.wait();
			commands.addComponent(entity, Health{50.0F});
		});

		WHEN("both thread buffers are merged")
		{
			firstProducer.join();
			secondProducer.join();
			commands.apply(ecs);

			THEN("the global recording sequence determines the result")
			{
				const Health *health{ecs.getComponent<Health>(entity)};
				REQUIRE((health != nullptr));
				CHECK((static_cast<int>(health->hp) == 50));
			}
		}
	}

	GIVEN("component and parent commands targeting an entity that is also destroyed")
	{
		ECS ecs;
		CommandBuffer commands;
		Entity parent{ecs.createEntity()};
		Entity child{ecs.createEntity()};
		commands.setParent(child, parent);
		commands.addComponent(child, Health{70.0F});
		commands.destroy(child);

		WHEN("the phased commands are applied")
		{
			commands.apply(ecs);

			THEN("destroy wins and the later parent phase safely ignores the stale child")
			{
				CHECK_FALSE(ecs.alive(child));
				CHECK(ecs.getChildren(parent).empty());
			}
		}
	}
}

SCENARIO("CommandBuffer world timeline integration")
{
	using Dimensia::Components::Health;
	using Dimensia::Components::Position;
	using Dimensia::ECS::All;
	using Dimensia::ECS::CommandBuffer;
	using Dimensia::ECS::ECS;
	using Dimensia::ECS::Entity;
	using Dimensia::ECS::SystemVersion;
	using Dimensia::Tags::AliveTag;

	GIVEN("deferred add and replacement commands")
	{
		ECS ecs;
		Entity target{ecs.createEntityWith(Position{.x = 1.0F, .y = 2.0F, .z = 3.0F})};
		SystemVersion observer;
		CommandBuffer commands;

		WHEN("the component is added and then replaced through playback")
		{
			commands.addComponent(target, Health{10.0F});
			commands.apply(ecs);
			std::size_t added{};
			ecs.query<Health>().version(observer).changed<Health>().read<Health>().forEach([&](Entity, const Health &) { ++added; });
			commands.addComponent(target, Health{20.0F});
			commands.apply(ecs);
			std::size_t replaced{};
			ecs.query<Health>().version(observer).changed<Health>().read<Health>().forEach([&](Entity, const Health &) { ++replaced; });

			THEN("both deferred writes advance the shared timeline")
			{
				CHECK((added == 1));
				CHECK((replaced == 1));
				CHECK((static_cast<int>(ecs.getComponent<Health>(target)->hp) == 20));
			}
		}
	}

	GIVEN("deferred remove, tag, and destroy commands")
	{
		ECS ecs;
		Entity removedTarget{ecs.createEntityWith(Position{.x = 1.0F, .y = 0.0F, .z = 0.0F}, Health{10.0F})};
		Entity taggedTarget{ecs.createEntityWith(Position{.x = 2.0F, .y = 0.0F, .z = 0.0F})};
		Entity destroyedTarget{ecs.createEntityWith(Position{.x = 3.0F, .y = 0.0F, .z = 0.0F})};
		Entity survivor{ecs.createEntityWith(Position{.x = 4.0F, .y = 0.0F, .z = 0.0F})};
		SystemVersion removeObserver;
		SystemVersion tagObserver;
		SystemVersion destroyObserver;
		ecs.query<Position>().version(removeObserver).changed<Position>().read<Position>().forEach([](Entity, const Position &) {});
		ecs.query<Position>().version(tagObserver).changed<Position>().read<Position>().forEach([](Entity, const Position &) {});
		ecs.query<Position>().version(destroyObserver).changed<Position>().read<Position>().forEach([](Entity, const Position &) {});
		CommandBuffer commands;

		WHEN("the commands are applied in separate playback batches")
		{
			commands.removeComponent<Health>(removedTarget);
			commands.apply(ecs);
			std::size_t removeRows{};
			ecs.query<Position>().version(removeObserver).changed<Position>().read<Position>().forEach([&](Entity, const Position &) {
				++removeRows;
			});

			commands.addComponent(taggedTarget, AliveTag{});
			commands.apply(ecs);
			std::size_t tagRows{};
			ecs.query<Position>().version(tagObserver).changed<Position>().read<Position>().forEach([&](Entity, const Position &) {
				++tagRows;
			});

			commands.destroy(destroyedTarget);
			commands.apply(ecs);
			std::size_t destroyRows{};
			ecs.query<Position>().version(destroyObserver).changed<Position>().read<Position>().forEach([&](Entity, const Position &) {
				++destroyRows;
			});

			std::size_t taggedRows{};
			ecs.query<All<Position, AliveTag>>().read<Position>().forEach([&](Entity, const Position &, AliveTag) { ++taggedRows; });

			THEN("each structural command is visible and world state remains coherent")
			{
				CHECK((removeRows > 0));
				CHECK((tagRows > 0));
				CHECK((destroyRows > 0));
				CHECK_FALSE(ecs.hasComponent<Health>(removedTarget));
				CHECK((taggedRows == 1));
				CHECK_FALSE(ecs.alive(destroyedTarget));
				CHECK(ecs.alive(survivor));
			}
		}
	}
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)