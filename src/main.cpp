#include <algorithm>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <ratio>
#include <vector>

#include "Components/Buff/buffComponent.h"
#include "Components/Buffs/buffsComponent.h"
#include "Components/Health/healthComponent.h"
#include "Components/Mana/manaComponent.h"
#include "Components/Name/nameComponent.h"
#include "Components/Position/positionComponent.h"
#include "Components/Velocity/velocityComponent.h"
#include "Core/attributeMacros.h"
#include "ECS/commandBuffer.h"
#include "ECS/ecs.h"
#include "ECS/entity.h"
#include "ECS/systemVersion.h"
#include "Tags/Alive/aliveTag.h"
#include "Tags/Buffed/buffedTag.h"
#include "Tags/Debug/debugTag.h"
#include "Utility/Clock/timer.h"

int main()
{
	using Dimensia::ECS::ECS;

	using Dimensia::Components::Buff;
	using Dimensia::Components::Buffs;
	using Dimensia::Components::Health;
	using Dimensia::Components::Mana;
	using Dimensia::Components::Name;
	using Dimensia::Components::Position;
	using Dimensia::Components::Velocity;

	using Dimensia::Tags::AliveTag;
	using Dimensia::Tags::BuffedTag;
	using Dimensia::Tags::DebugTag;

	using Dimensia::ECS::CommandBuffer;
	using Dimensia::ECS::Entity;
	using Dimensia::ECS::ExecutionPolicy;
	using Dimensia::ECS::SystemVersion;

	std::cout << alignof(void *) << '\n';
	std::cout << alignof(void (*)(void *)) << '\n';
	std::cout << alignof(Dimensia::ECS::ComponentTypeID) << '\n';
	// std::cout << alignof(bool) << '\n';
	// std::cout << alignof(std::atomic<bool>) << '\n';

	ECS ecs;

	const Entity goblin = ecs.createEntityWith(
		Name{"Goblin"}, Position{.x = 10.F, .y = 20.F, .z = 30.F}, Velocity{.dx = 1.F, .dy = 0.F, .dz = 0.F}, Health{100}, Mana{50},
		Buffs{{{.name = "Haste", .duration = 5}, {.name = "Shield", .duration = 3}}}, AliveTag{}, BuffedTag{});

	std::cout << "--- Batch created entity ---\n";
	std::cout << "Entity " << goblin.index << ":" << goblin.generation << "\n";
	std::cout << "Name: " << ecs.getComponent<Name>(goblin)->name << "\n";
	std::cout << "Size of DebugTag: " << sizeof(DebugTag) << "\n";
	std::cout << "Has AliveTag: " << ecs.hasTag<AliveTag>(goblin) << "\n";
	std::cout << "Has DebugTag: " << ecs.hasTag<DebugTag>(goblin) << "\n";

	ecs.addTag<DebugTag>(goblin);
	std::cout << "After adding DebugTag: " << ecs.hasTag<DebugTag>(goblin) << "\n";

	ecs.removeTag<BuffedTag>(goblin);
	std::cout << "After removing BuffedTag: " << ecs.hasTag<BuffedTag>(goblin) << "\n";

	std::cout << "\n--- Before movement ---\n";
	ecs.forEach<Position, Velocity>([](Entity entity, Position &position, Velocity &velocity) {
		std::cout << "Entity " << entity.index << ":" << entity.generation << " pos=(" << position.x << "," << position.y << ","
				  << position.z << ")" << " vel=(" << velocity.dx << "," << velocity.dy << "," << velocity.dz << ")\n";
	});

	ecs.forEach<Position, Velocity>([](ATTR_MAYBE_UNUSED Entity entity, Position &position, Velocity &velocity) {
		position.x += velocity.dx;
		position.y += velocity.dy;
		position.z += velocity.dz;
	});

	std::cout << "\n--- After movement ---\n";
	ecs.forEach<Position, Velocity>([](Entity entity, Position &position, Velocity &velocity) {
		std::cout << "Entity " << entity.index << ":" << entity.generation << " pos=(" << position.x << "," << position.y << ","
				  << position.z << ")" << " vel=(" << velocity.dx << "," << velocity.dy << "," << velocity.dz << ")\n";
	});

	const ECS &cecs = ecs;
	std::cout << "\n--- Const query (Health) ---\n";
	cecs.forEach<Health>([](Entity entity, const Health &health) {
		std::cout << "Entity " << entity.index << ":" << entity.generation << " HP=" << health.hp << "\n";
	});

	std::cout << "\n--- Entities with AliveTag ---\n";
	ecs.forEach<Name, AliveTag>([](Entity entity, Name &name, AliveTag) {
		std::cout << "Entity " << entity.index << ":" << entity.generation << " name=" << name.name << "\n";
	});

	std::cout << "\n--- Buffs ---\n";
	ecs.forEach<Buffs>([](Entity entity, Buffs &buffs) {
		for (const auto &buff : buffs.activeBuffs)
		{
			std::cout << "Entity " << entity.index << ":" << entity.generation << " has buff " << buff.name << " with duration "
					  << buff.duration << "\n";
		}
	});

	ecs.getComponent<Buffs>(goblin)->activeBuffs.erase(std::remove_if(ecs.getComponent<Buffs>(goblin)->activeBuffs.begin(),
																	  ecs.getComponent<Buffs>(goblin)->activeBuffs.end(),
																	  [](const Buff &buff) { return buff.name == "Haste"; }),
													   ecs.getComponent<Buffs>(goblin)->activeBuffs.end());

	std::cout << "\n--- After buff removal ---\n";
	ecs.forEach<Buffs>(ExecutionPolicy::Par, [](Entity entity, Buffs &buffs) {
		for (const auto &buff : buffs.activeBuffs)
		{
			std::cout << "Entity " << entity.index << ":" << entity.generation << " buff=" << buff.name << ", " << buff.duration << "\n";
		}
	});

	// Batch processing
	ecs.forEach<Position, Velocity>(ExecutionPolicy::ParBatched,
									[](ATTR_MAYBE_UNUSED Entity entity, Position &pos, Velocity &vel) { pos.x += vel.dx; });

	// Work stealing
	ecs.forEach<Position, Velocity>(ExecutionPolicy::ParStealing,
									[](ATTR_MAYBE_UNUSED Entity entity, Position &pos, Velocity &vel) { pos.x += vel.dx; });

	// Version-aware (only process changed chunks)
	SystemVersion physicsVersion;
	ecs.forEach<Position, Velocity>(ExecutionPolicy::Par, physicsVersion,
									[](ATTR_MAYBE_UNUSED Entity entity, Position &pos, Velocity &vel) { pos.x += vel.dx; });

	// --- Hierarchy example ---
	std::cout << "\n--- Hierarchy example ---\n";
	Entity parent{ecs.createEntityWith(Name{"Parent"})};
	const Entity child{ecs.createEntityWith(Name{"Child"})};
	ecs.setParent(child, parent);
	std::cout << "Child's parent: " << ecs.getParent(child).index << "\n";
	const std::vector<Entity> children{ecs.getChildren(parent)};
	std::cout << "Parent's children count: " << children.size() << "\n";

	// --- Hierarchical destruction with children ---
	std::cout << "\n--- Destroy parent with children ---\n";
	// ecs.destroyEntity(parent, true); // destroy parent and child
	ecs.destroyEntity(parent); // destroy parent and child
	std::cout << "Parent alive: " << ecs.alive(parent) << "\n";
	std::cout << "Child alive: " << ecs.alive(child) << "\n";

	// --- Command buffer example ---
	std::cout << "\n--- Command buffer example ---\n";
	CommandBuffer cmds;
	constexpr float startingHealth{200.F};

	ecs.forEach<Name>(ExecutionPolicy::Seq, cmds, [&](Entity entity, Name &) {
		cmds.addComponent(entity, Health{startingHealth}); // defer adding Health
	});
	cmds.apply(ecs);
	std::cout << "Goblin HP after command buffer: " << ecs.getComponent<Health>(goblin)->hp << "\n";

	ecs.compact();

	// ------------------------------------------------------------------------
	//  Combined SystemVersion + CommandBuffer example
	// ------------------------------------------------------------------------
	std::cout << "\n=== Combined SystemVersion + CommandBuffer ===\n";

	// Create a SystemVersion to track changes for the <Health> query.
	SystemVersion healthVersion;

	// First run: all chunks are dirty because the version is new.
	{
		CommandBuffer commands;
		ecs.forEach<Health>(ExecutionPolicy::ParBatched, healthVersion, commands,
							[](Entity entity, Health &health, CommandBuffer &commandBuffer) {
								// Simulate work: increment health
								health.hp += 1.0F;
								// Record a command to add a Name component to this entity
								commandBuffer.addComponent(entity, Name{"AutoNamed"});
							});

		// Apply the recorded commands (adds Name components)
		commands.apply(ecs);

		std::cout << "First run processed all Health components.\n";
	}

	// After the first run, the SystemVersion has been updated to reflect
	// the latest chunk versions.

	// Second run: no components have been modified, so needsUpdate() returns
	// false for all chunks → nothing is processed.
	{
		CommandBuffer commands;
		bool processedAny{false};

		ecs.forEach<Health>(ExecutionPolicy::ParBatched, healthVersion, commands, [&](Entity, Health &, CommandBuffer &) {
			processedAny = true; // This should NOT be called
		});

		if (!processedAny)
		{
			std::cout << "Second run correctly skipped all chunks (no changes).\n";
		}
		else
		{
			std::cout << "Warning: Second run processed chunks unexpectedly.\n";
		}
	}

	if (ecs.alive(goblin))
	{
		Health health{*ecs.getComponent<Health>(goblin)}; // copy current value
		health.hp += 1.0F;								  // modify
		ecs.addComponent(goblin, health);				  // reassign (bumps version)
	}

	// Third run: the chunk containing that entity is now dirty, so it will be processed.
	{
		CommandBuffer commands;
		std::size_t processedCount{0};

		ecs.forEach<Health>(ExecutionPolicy::ParBatched, healthVersion, commands,
							[&](Entity, Health &, CommandBuffer &) { ++processedCount; });

		std::cout << "Third run processed " << processedCount << " chunk(s) (the one that was modified).\n";
	}

	// ------------------------------------------------------------------------
	//  Stress test: forEach iteration speed (raw overhead)
	// ------------------------------------------------------------------------
	std::cout << "\n=== Stress test: forEach iteration speed ===\n";

	constexpr int ITERATIONS{1'000};
	constexpr int WORKLOAD{5'000};
	std::vector<Entity> iterEntities;
	iterEntities.reserve(ITERATIONS);

	// Create many entities with a Name component
	for (int i = 0; i < ITERATIONS; ++i)
	{
		iterEntities.emplace_back(ecs.createEntityWith(Name{"dummy"}));
	}

	// Sequential iteration with trivial work
	{
		std::size_t counter = 0;
		Dimensia::Utility::Clock::Timer::start();
		ecs.forEach<Name>([&](Entity, Name &) {
			++counter; // extremely cheap operation
		});
		auto time = Dimensia::Utility::Clock::Timer::stop<std::milli>();
		std::cout << "Sequential forEach (counter=" << counter << ") took " << time << " ms.\n";
	}

	// Parallel (Par) iteration with trivial work
	{
		std::atomic<std::size_t> counter = 0;
		Dimensia::Utility::Clock::Timer::start();
		ecs.forEach<Name>(ExecutionPolicy::Par, [&](Entity, Name &) { ++counter; });
		auto time = Dimensia::Utility::Clock::Timer::stop<std::milli>();
		std::cout << "Parallel (Par) forEach (counter=" << counter.load() << ") took " << time << " ms.\n";
	}

	// Parallel batched (ParBatched) iteration
	{
		std::atomic<std::size_t> counter = 0;
		Dimensia::Utility::Clock::Timer::start();
		ecs.forEach<Name>(ExecutionPolicy::ParBatched, [&](Entity, Name &) { ++counter; });
		auto time = Dimensia::Utility::Clock::Timer::stop<std::milli>();
		std::cout << "Parallel (ParBatched) forEach (counter=" << counter.load() << ") took " << time << " ms.\n";
	}

	// Work‑stealing (ParStealing) iteration
	{
		std::atomic<std::size_t> counter = 0;
		Dimensia::Utility::Clock::Timer::start();
		ecs.forEach<Name>(ExecutionPolicy::ParStealing, [&](Entity, Name &) { ++counter; });
		auto time = Dimensia::Utility::Clock::Timer::stop<std::milli>();
		std::cout << "Parallel (ParStealing) forEach (counter=" << counter.load() << ") took " << time << " ms.\n";
	}

	// Cleanup
	for (Entity &entity : iterEntities)
	{
		if (ecs.alive(entity))
		{
			ecs.destroyEntity(entity, false);
		}
	}

	// ------------------------------------------------------------------------
	//  Stress test: forEach with moderate workload (parallel should shine)
	// ------------------------------------------------------------------------
	std::cout << "\n=== Stress test: forEach with moderate workload ===\n";

	std::vector<Entity> modEntities;
	modEntities.reserve(ITERATIONS);

	// Create entities with Health component
	for (int i = 0; i < ITERATIONS; ++i)
	{
		modEntities.emplace_back(ecs.createEntityWith(Health{static_cast<float>(i)}));
	}

	const float mult{1.000001F};
	const float adder{0.000001F};

	// Sequential execution
	{
		Dimensia::Utility::Clock::Timer::start();
		ecs.forEach<Health>([&](Entity, Health &health) {
			volatile float dummy = health.hp;
			for (int iter = 0; iter < WORKLOAD; ++iter)
			{
				dummy = (dummy * mult) + adder; // cheap but non‑trivial math
			}
			health.hp = dummy; // write back to prevent complete elimination
		});
		auto time = Dimensia::Utility::Clock::Timer::stop<std::milli>();
		std::cout << "Sequential forEach took " << time << " ms.\n";
	}

	// Parallel (Par) – one task per chunk
	{
		Dimensia::Utility::Clock::Timer::start();
		ecs.forEach<Health>(ExecutionPolicy::Par, [&](Entity, Health &health) {
			volatile float dummy = health.hp;
			for (int iter = 0; iter < WORKLOAD; ++iter)
			{
				dummy = (dummy * mult) + adder;
			}
			health.hp = dummy;
		});
		auto time = Dimensia::Utility::Clock::Timer::stop<std::milli>();
		std::cout << "Parallel (Par) forEach took " << time << " ms.\n";
	}

	// Parallel batched (ParBatched) – adaptive batching
	{
		Dimensia::Utility::Clock::Timer::start();
		ecs.forEach<Health>(ExecutionPolicy::ParBatched, [&](Entity, Health &health) {
			volatile float dummy = health.hp;
			for (int iter = 0; iter < WORKLOAD; ++iter)
			{
				dummy = (dummy * mult) + adder;
			}
			health.hp = dummy;
		});
		auto time = Dimensia::Utility::Clock::Timer::stop<std::milli>();
		std::cout << "Parallel (ParBatched) forEach took " << time << " ms.\n";
	}

	// Work‑stealing (ParStealing)
	{
		Dimensia::Utility::Clock::Timer::start();
		ecs.forEach<Health>(ExecutionPolicy::ParStealing, [&](Entity, Health &health) {
			volatile float dummy = health.hp;
			for (int iter = 0; iter < WORKLOAD; ++iter)
			{
				dummy = (dummy * mult) + adder;
			}
			health.hp = dummy;
		});
		auto time = Dimensia::Utility::Clock::Timer::stop<std::milli>();
		std::cout << "Parallel (ParStealing) forEach took " << time << " ms.\n";
	}

	// Cleanup
	for (Entity &entity : modEntities)
	{
		if (ecs.alive(entity))
		{
			ecs.destroyEntity(entity, false);
		}
	}

	// ------------------------------------------------------------------------
	//  Stress test: command buffer with parallel forEach (modified to ensure one operation per entity)
	// ------------------------------------------------------------------------
	std::cout << "\n=== Stress test: command buffer ===\n";

	std::vector<Entity> entities;
	entities.reserve(ITERATIONS);

	// Create many entities with only a Name (or any component, just to have data)
	for (int i = 0; i < ITERATIONS; ++i)
	{
		entities.emplace_back(ecs.createEntityWith(Name{"dummy"}));
	}

	CommandBuffer stressCmds;

	Dimensia::Utility::Clock::Timer::start();

	ecs.forEach<Name>(ExecutionPolicy::ParStealing, stressCmds, [&](Entity entity, Name &) {
		volatile float dummy = 1.0;
		for (int iter = 0; iter < WORKLOAD; ++iter)
		{
			dummy = (dummy * mult) + adder; // some meaningless math
		}
		(void) dummy;									// prevent optimization
		const int eID = static_cast<int>(entity.index); // use entity index as unique ID

		// Assign each entity exactly one operation based on id % 5
		const int operation = eID % 5;

		switch (operation)
		{
			case 0: // add Health
				stressCmds.addComponent(entity, Health{static_cast<float>(eID) * 2});
				break;
			case 2: // destroy
				stressCmds.destroy(entity);
				break;
			case 3: // set parent (link to entity id/2, if still alive)
				if (eID != 0)
				{
					const int parentIdx = eID / 2;
					if (parentIdx < ITERATIONS && ecs.alive(entities.at(static_cast<std::size_t>(parentIdx))))
					{
						stressCmds.setParent(entity, entities.at(static_cast<std::size_t>(parentIdx)));
					}
				}
				break;
			default:
				break;
		}
	});

	auto time{Dimensia::Utility::Clock::Timer::stop<std::milli>()};
	std::cout << "ParStealing forEach " << ITERATIONS << " queued commands with a workload of " << WORKLOAD << " in " << time << " ms.\n";

	Dimensia::Utility::Clock::Timer::start();
	stressCmds.apply(ecs);
	time = Dimensia::Utility::Clock::Timer::stop<std::milli>();
	std::cout << "apply() executed commands in " << time << " ms.\n";

	// ------------------------------------------------------------------------
	//  Verification
	// ------------------------------------------------------------------------
	int healthCount = 0;
	int nameCount = 0;
	int aliveCount = 0;
	int parentCount = 0;

	ecs.forEach<Health>([&](Entity, Health &) { ++healthCount; });
	ecs.forEach<Name>([&](Entity, Name &) { ++nameCount; });
	for (const Entity entity : entities)
	{
		if (ecs.alive(entity))
		{
			++aliveCount;
		}

		if (ecs.getParent(entity).index != 0)
		{
			++parentCount;
		}
	}

	// Expected approximate counts (using modulo 5 distribution):
	// - case 0: add Health → 20% of entities
	// - case 1: remove Name → 20% (name removed)
	// - case 2: destroy → 20%
	// - case 3: set parent → 20% (excluding id=0, so ~19.98%)
	// - case 4: no op → 20% (name remains)
	const int expectedAlive = ITERATIONS - (ITERATIONS / 5); // destroyed 20%
	const int expectedHealth = ITERATIONS / 5;				 // case 0
	const int expectedName
		= ITERATIONS
		- (ITERATIONS / 5); // not removed (case 1 removed) + no op (case 4) = 40% remain? Wait, case 1 removes, case 4 keeps. So total name
							// = case 4 (20%) + case 0? case 0 adds Health but does not remove Name, so name remains. Also case 2 destroys,
							// so they are gone. So name count = case 0 (20%) + case 3 (20%) + case 4 (20%) = 60%? But careful: case 3 sets
							// parent but does not remove Name, so name remains. So total name = entities not in case 1 and not destroyed.
							// Destroyed (case 2) are 20%, case 1 removes 20%, so remaining 60% should have Name. Also entities with Health
							// (case 0) are a subset of those (20%). So expectedName = ITERATIONS * 3/5 = 6000 for 10000.
	const int expectedParent = (ITERATIONS / 5) - 1; // case 3, exclude id 0 (approximately)

	std::cout << "\n--- Results ---\n";
	std::cout << "Alive      : " << aliveCount << " (expected ~" << expectedAlive << ")\n";
	std::cout << "Health     : " << healthCount << " (expected ~" << expectedHealth << ")\n";
	std::cout << "Name       : " << nameCount << " (expected ~" << expectedName << ")\n";
	std::cout << "Has parent : " << parentCount << " (expected ~" << expectedParent << ")\n";

	// Cleanup remaining entities
	Dimensia::Utility::Clock::Timer::start();
	for (Entity &entity : entities)
	{
		if (ecs.alive(entity))
		{
			ecs.destroyEntity(entity, false);
		}
	}
	time = Dimensia::Utility::Clock::Timer::stop<std::milli>();
	std::cout << "Cleanup took " << time << " ms.\n";

	ecs.compact();

	return 0;
}