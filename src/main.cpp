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
#include "ECS/queryFilter.h"
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

	using Dimensia::ECS::All;
	using Dimensia::ECS::Any;
	using Dimensia::ECS::CommandBuffer;
	using Dimensia::ECS::Entity;
	using Dimensia::ECS::ExecutionPolicy;
	using Dimensia::ECS::None;
	using Dimensia::ECS::SystemVersion;

	std::cout << alignof(std::unordered_map<Dimensia::ECS::QueryKey, std::vector<Dimensia::ECS::Archetype *>>) << '\n';
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
	ecs.forEach<Position, Velocity>([](Entity &entity, Position &position, Velocity &velocity) {
		std::cout << "Entity " << entity.index << ":" << entity.generation << " pos=(" << position.x << "," << position.y << ","
				  << position.z << ")" << " vel=(" << velocity.dx << "," << velocity.dy << "," << velocity.dz << ")\n";
	});

	ecs.forEach<Position, Velocity>([](ATTR_MAYBE_UNUSED Entity &entity, Position &position, Velocity &velocity) {
		position.x += velocity.dx;
		position.y += velocity.dy;
		position.z += velocity.dz;
	});

	std::cout << "\n--- After movement ---\n";
	ecs.forEach<Position, Velocity>([](Entity &entity, Position &position, Velocity &velocity) {
		std::cout << "Entity " << entity.index << ":" << entity.generation << " pos=(" << position.x << "," << position.y << ","
				  << position.z << ")" << " vel=(" << velocity.dx << "," << velocity.dy << "," << velocity.dz << ")\n";
	});

	const ECS &cecs = ecs;
	std::cout << "\n--- Const query (Health) ---\n";
	cecs.forEach<Health>([](Entity &entity, const Health &health) {
		std::cout << "Entity " << entity.index << ":" << entity.generation << " HP=" << health.hp << "\n";
	});

	std::cout << "\n--- Entities with AliveTag ---\n";
	ecs.forEach<Name, AliveTag>([](Entity &entity, Name &name, AliveTag) {
		std::cout << "Entity " << entity.index << ":" << entity.generation << " name=" << name.name << "\n";
	});

	std::cout << "\n--- Buffs ---\n";
	ecs.forEach<Buffs>([](Entity &entity, Buffs &buffs) {
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
									[](ATTR_MAYBE_UNUSED Entity &entity, Position &pos, Velocity &vel) { pos.x += vel.dx; });

	// Work stealing
	ecs.forEach<Position, Velocity>(ExecutionPolicy::ParStealing,
									[](ATTR_MAYBE_UNUSED Entity &entity, Position &pos, Velocity &vel) { pos.x += vel.dx; });

	// Version-aware (only process changed chunks)
	SystemVersion physicsVersion;
	ecs.forEach<Position, Velocity>(ExecutionPolicy::Par, physicsVersion,
									[](ATTR_MAYBE_UNUSED Entity &entity, Position &pos, Velocity &vel) { pos.x += vel.dx; });

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
	constexpr int startingMana{50};

	ecs.forEach<Name>(ExecutionPolicy::Seq, cmds, [&](Entity &entity, const Name &) {
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
							[](const Entity &entity, Health &health, CommandBuffer &commandBuffer) {
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

	ecs.forEach<Name>(ExecutionPolicy::ParStealing, stressCmds, [&](Entity &entity, const Name &) {
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
	for (const Entity &entity : entities)
	{
		if (ecs.alive(entity))
		{
			++aliveCount;
		}

		if (ecs.getParent(entity) != Dimensia::ECS::NULL_ENTITY)
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

	// ------------------------------------------------------------------------
	//  Query filter examples (All, Any, None)
	// ------------------------------------------------------------------------
	std::cout << "\n=== Query filter examples (All / Any / None) ===\n";

	// Create a few entities with different component combinations for testing
	Entity queryFilterE1{ecs.createEntityWith(Name{"Entity1"}, Position{.x = 0, .y = 0, .z = 0}, Velocity{.dx = 1, .dy = 0, .dz = 0},
											  Health{startingHealth})};
	Entity queryFilterE2{
		ecs.createEntityWith(Name{"Entity2"}, Position{.x = 1, .y = 1, .z = 1}, Velocity{.dx = 0, .dy = 1, .dz = 0}, Mana{startingMana})};
	Entity queryFilterE3{ecs.createEntityWith(Name{"Entity3"}, Position{.x = 2, .y = 2, .z = 2}, Velocity{.dx = 0, .dy = 0, .dz = 1},
											  Health{startingHealth}, Mana{startingMana})};
	Entity queryFilterE4{ecs.createEntityWith(Name{"Entity4"}, Position{.x = 3, .y = 3, .z = 3}, Health{startingHealth})};
	Entity queryFilterE5{ecs.createEntityWith(Name{"Entity5"}, Velocity{.dx = 1, .dy = 2, .dz = 3}, Mana{startingMana})};
	Entity queryFilterE6{ecs.createEntityWith(Name{"Entity6"}, Health{startingHealth}, Mana{startingMana}, Buffs{})};
	Entity queryFilterE7{ecs.createEntityWith(Name{"Entity7"}, Buffs{})};

	std::cout << "\nCreated test entities:\n";
	std::cout << "queryFilterE1: Name, Pos, Vel, Health with index: " << queryFilterE1.index << "\n";
	std::cout << "queryFilterE2: Name, Pos, Vel, Mana with index: " << queryFilterE2.index << "\n";
	std::cout << "queryFilterE3: Name, Pos, Vel, Health, Mana with index: " << queryFilterE3.index << "\n";
	std::cout << "queryFilterE4: Name, Pos, Health with index: " << queryFilterE4.index << "\n";
	std::cout << "queryFilterE5: Name, Vel, Mana with index: " << queryFilterE5.index << "\n";
	std::cout << "queryFilterE6: Name, Health, Mana, Buffs with index: " << queryFilterE6.index << "\n";
	std::cout << "queryFilterE7: Name, Buffs with index: " << queryFilterE7.index << "\n";

	// Helper lambda to print matching entity indices
	auto printMatches = [&](std::string_view description, const auto &queryFunc) {
		std::cout << description << ": ";
		queryFunc();
		std::cout << "\n";
	};

	// 1. All<...> only
	printMatches("All<Position, Velocity>", [&] {
		ecs.forEach<All<Position, Velocity>>([](const Entity &entity, Position &, Velocity &) { std::cout << entity.index << " "; });
	});

	// 2. Any<...> only
	printMatches("Any<Health, Mana>",
				 [&] { ecs.forEach<All<>, Any<Health, Mana>>([](const Entity &entity) { std::cout << entity.index << " "; }); });

	// 3. None<...> only
	printMatches("None<Buffs>", [&] { ecs.forEach<All<>, None<Buffs>>([](const Entity &entity) { std::cout << entity.index << " "; }); });

	// 4. All + Any
	printMatches("All<Position> + Any<Health, Mana>", [&] {
		ecs.forEach<All<Position>, Any<Health, Mana>>([](const Entity &entity, Position &) { std::cout << entity.index << " "; });
	});

	// 5. All + None
	printMatches("All<Position> + None<Buffs>", [&] {
		ecs.forEach<All<Position>, None<Buffs>>([](const Entity &entity, Position &) { std::cout << entity.index << " "; });
	});

	// 6. Any + None
	printMatches("Any<Health, Mana> + None<Buffs>", [&] {
		ecs.forEach<All<>, Any<Health, Mana>, None<Buffs>>([](const Entity &entity) { std::cout << entity.index << " "; });
	});

	// 7. All + Any + None
	printMatches("All<Position> + Any<Health, Mana> + None<Buffs>", [&] {
		ecs.forEach<All<Position>, Any<Health, Mana>, None<Buffs>>(
			[](const Entity &entity, Position &) { std::cout << entity.index << " "; });
	});

	// 8. All<Name> (equivalent to original raw component query, but using new syntax)
	printMatches("All<Name>", [&] { ecs.forEach<All<Name>>([](const Entity &entity, Name &) { std::cout << entity.index << " "; }); });

	// 9. Any<Buffs> (entities that have Buffs)
	printMatches("Any<Buffs>", [&] { ecs.forEach<All<>, Any<Buffs>>([](const Entity &entity) { std::cout << entity.index << " "; }); });

	// 10. None<Position> (entities without Position)
	printMatches("None<Position>",
				 [&] { ecs.forEach<All<>, None<Position>>([](const Entity &entity) { std::cout << entity.index << " "; }); });

	cmds.clear();

	std::cout << "All<Position, Velocity> with command buffer: ";

	ecs.forEach<All<Position, Velocity>>(ExecutionPolicy::Seq, cmds, [&](Entity &entity, const Position & /*p*/, const Velocity & /*v*/) {
		// Do work and possibly record commands
		cmds.addComponent(entity, Health{startingHealth});
		std::cout << entity.index << ' ';
	});

	std::cout << "\n";

	cmds.apply(ecs);

	cmds.clear();

	SystemVersion queryVersion;

	std::cout << "All<Position, Velocity> with system version and command buffer: ";

	ecs.forEach<All<Position, Velocity>>(ExecutionPolicy::Seq, queryVersion, cmds,
										 [&](Entity &entity, const Position & /*p*/, const Velocity & /*v*/) {
											 // Do work and possibly record commands
											 cmds.addComponent(entity, Health{startingHealth});
											 std::cout << entity.index << ' ';
										 });

	std::cout << "\n";

	cmds.apply(ecs);

	// --- QueryBuilder API equivalents ---
	std::cout << "\n=== QueryBuilder API ===\n";

	std::cout << "query<Pos, Vel>().forEach: ";
	ecs.query<Position, Velocity>().forEach([](Entity &entity, Position &, Velocity &) { std::cout << entity.index << " "; });
	std::cout << "\n";

	std::cout << "query<All<Pos>, None<Buffs>>().policy(Par): ";
	ecs.query<All<Position>, None<Buffs>>().policy(ExecutionPolicy::Par).forEach([](const Entity &entity, Position &) {
		std::cout << entity.index << " ";
	});
	std::cout << "\n";

	cmds.clear();
	std::cout << "query<All<Pos, Vel>>().commands(cmds): ";
	ecs.query<All<Position, Velocity>>().commands(cmds).forEach([&](Entity &entity, const Position &, const Velocity &) {
		cmds.addComponent(entity, Health{startingHealth});
		std::cout << entity.index << " ";
	});
	std::cout << "\n";
	cmds.apply(ecs);

	SystemVersion builderVersion;
	std::cout << "query<Health>().version(v).policy(ParBatched): ";
	ecs.query<Health>().policy(ExecutionPolicy::ParBatched).version(builderVersion).forEach([](ATTR_MAYBE_UNUSED Entity entity, Health &) {
		std::cout << entity.index << " ";
	});
	std::cout << "\n";

	// --- View API ---
	std::cout << "\n=== View API ===\n";

	std::cout << "view<Pos, Vel>: ";
	for (auto [entity, pos, vel] : ecs.view<Position, Velocity>())
	{
		std::cout << entity.index << " ";
		(void) pos;
		(void) vel;
	}
	std::cout << "\n";

	std::cout << "view<Name> (const): ";
	const ECS &constEcs = ecs;
	for (auto [entity, name] : constEcs.view<Name>())
	{
		std::cout << entity.index << "(" << name.name << ") ";
	}
	std::cout << "\n";

	for (Entity entity : {queryFilterE1, queryFilterE2, queryFilterE3, queryFilterE4, queryFilterE5, queryFilterE6, queryFilterE7})
	{
		if (ecs.alive(entity))
		{
			ecs.destroyEntity(entity, false);
		}
	}

	ecs.compact();

	return 0;
}