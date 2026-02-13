#include "Core/attributeMacros.h"
#include "ECS/deepseekTest.h"

struct Position
{
		float x, y, z;
}; // trivial

struct Velocity
{
		float dx, dy, dz;
}; // trivial

struct Health
{
		int hp;
}; // trivial

struct Mana
{
		int mp;
}; // trivial

struct Buff
{
		std::string name;
		int duration;
};

struct Buffs
{
		std::vector<Buff> list; // multiple buffs per entity
};

struct NameTag
{
		std::string tag;
}; // non‑trivial, but fine

// #include "ECS/chatgptTest.h"

// -----------------------------------------------------------------------------
//  Demo: Name + active flag, structural moves, swap-remove, query
// -----------------------------------------------------------------------------
int main()
{
	// Register components (registration happens on first call to componentId<T>)
	// Silence unused warnings with (void)
	volatile auto nameId = componentId<std::string>();
	(void) nameId;
	volatile auto activeId = componentId<bool>();
	(void) activeId;

	ECS ecs;

	Entity e1 = ecs.createEntity();
	Entity e2 = ecs.createEntity();
	Entity e3 = ecs.createEntity();

	ecs.addComponent<std::string>(e1, "Alice");
	ecs.addComponent<std::string>(e2, "Bob");
	ecs.addComponent<std::string>(e3, "Charlie");

	ecs.addComponent<bool>(e1, true);
	ecs.addComponent<bool>(e3, true);

	std::cout << "--- Active entities after setup ---\n";
	ecs.forEach<std::string, bool>([](Entity e, std::string &name, bool &active) {
		std::cout << "Entity " << e.index << ":" << e.generation << " name=" << name << " active=" << active << "\n";
	});

	ecs.removeComponent<bool>(e3);

	std::cout << "\n--- After removing Active from Charlie ---\n";
	ecs.forEach<std::string, bool>([](Entity e, std::string &name, bool &active) {
		std::cout << "Entity " << e.index << ":" << e.generation << " name=" << name << " active=" << active << "\n";
	});

	auto dummyScoreId = componentId<int>();
	(void) dummyScoreId;
	ecs.addComponent<int>(e2, 42);

	std::cout << "\n--- After adding score=42 to Bob ---\n";
	ecs.forEach<std::string, int>([](Entity e, std::string &name, int &score) {
		std::cout << "Entity " << e.index << ":" << e.generation << " name=" << name << " score=" << score << "\n";
	});

	ecs.destroyEntity(e2);
	std::cout << "\n--- After destroying Bob ---\n";
	std::cout << "Bob alive? " << ecs.alive(e2) << "\n";

	Entity e4 = ecs.createEntity();
	ecs.addComponent<std::string>(e4, "Dave");
	ecs.addComponent<bool>(e4, true);
	std::cout << "New entity: index=" << e4.index << " generation=" << e4.generation << "\n";

	std::cout << "\n--- Final state (all entities with name and active) ---\n";
	ecs.forEach<std::string, bool>([](Entity e, std::string &name, bool &active) {
		std::cout << "Entity " << e.index << ":" << e.generation << " name=" << name << " active=" << active << "\n";
	});

	Entity e = ecs.createEntity();

	ecs.addComponent<std::string>(e, "Goblin");
	ecs.addComponent<Position>(e, {10.f, 20.f, 30.f});
	ecs.addComponent<Velocity>(e, {1.f, 0.f, 0.f});
	ecs.addComponent<Health>(e, {.hp = 100});
	ecs.addComponent<Mana>(e, {.mp = 50});
	ecs.addComponent<NameTag>(e, {"Goblin"});
	ecs.addComponent<Buffs>(e, {}); // start with empty list

	auto *entityBuffs = ecs.getComponent<Buffs>(e);
	entityBuffs->list.push_back({"Speed Boost", 10});
	entityBuffs->list.push_back({"Strength Boost", 5});

	std::cout << "\n--- Created entity with multiple components ---\n";
	std::cout << "Entity " << e.index << ":" << e.generation << " name=" << ecs.getComponent<NameTag>(e)->tag << "\n";

	std::cout << "\n--- All entities ---\n";
	ecs.forEach<std::string>([](Entity entity, std::string &name) {
		std::cout << "Entity " << entity.index << ":" << entity.generation << " name=" << name << "\n";
	});

	std::cout << "\n--- Before movement ---\n";
	ecs.forEach<Position, Velocity>([](Entity entity, Position &pos, Velocity &vel) {
		std::cout << "Entity " << entity.index << ":" << entity.generation << " position=(" << pos.x << ", " << pos.y << ", " << pos.z
				  << ")" << " velocity=(" << vel.dx << ", " << vel.dy << ", " << vel.dz << ")" << "\n";
	});

	// Update positions using velocity
	ecs.forEach<Position, Velocity>([](ATTR_MAYBE_UNUSED Entity entity, Position &pos, Velocity &vel) {
		pos.x += vel.dx;
		pos.y += vel.dy;
		pos.z += vel.dz;
	});

	std::cout << "\n--- After movement ---\n";
	ecs.forEach<Position, Velocity>([](Entity entity, Position &pos, Velocity &vel) {
		std::cout << "Entity " << entity.index << ":" << entity.generation << " position=(" << pos.x << ", " << pos.y << ", " << pos.z
				  << ")" << " velocity=(" << vel.dx << ", " << vel.dy << ", " << vel.dz << ")" << "\n";
	});

	std::cout << "\n--- Buffs ---\n";
	ecs.forEach<Buffs>([](Entity entity, Buffs &buffs) {
		for (const auto &buff : buffs.list)
		{
			std::cout << "Entity " << entity.index << ":" << entity.generation << " buff=" << buff.name << ", " << buff.duration << "\n";
		}
	});
	return 0;
}