#include "Core/attributeMacros.h"
#include "ECS/deepseekTest.h"

// -----------------------------------------------------------------------------
//  Example components
// -----------------------------------------------------------------------------
struct Position
{
		float x, y, z;
};

struct Velocity
{
		float dx, dy, dz;
};

struct Health
{
		int hp;
};

struct Mana
{
		int mp;
};

struct Buff
{
		std::string name;
		int duration;
};

struct Buffs
{
		std::vector<Buff> activeBuffs;
};

struct NameTag

{
		std::string tag;
};

// Zero‑size tags
struct AliveTag
{
		static constexpr bool is_tag = true;
};

struct DebugTag
{
		static constexpr bool is_tag = true;
};

struct BuffedTag
{
		static constexpr bool is_tag = true;
};

// -----------------------------------------------------------------------------
//  Demo (same as before)
// -----------------------------------------------------------------------------
int main()
{

	registerTag<AliveTag>();
	registerTag<DebugTag>();
	registerTag<BuffedTag>();

	ECS ecs;

	Entity goblin = ecs.createEntityWith(NameTag{"Goblin"}, Position{10.f, 20.f, 30.f}, Velocity{1.f, 0.f, 0.f}, Health{100}, Mana{50},
										 Buffs{{{"Haste", 5}, {"Shield", 3}}}, AliveTag{}, BuffedTag{});

	std::cout << "--- Batch created entity ---\n";
	std::cout << "Entity " << goblin.index << ":" << goblin.generation << "\n";
	std::cout << "Name: " << ecs.getComponent<NameTag>(goblin)->tag << "\n";
	std::cout << "Size of DebugTag: " << sizeof(DebugTag) << "\n";
	std::cout << "Has AliveTag: " << ecs.hasTag<AliveTag>(goblin) << "\n";
	std::cout << "Has DebugTag: " << ecs.hasTag<DebugTag>(goblin) << "\n";

	ecs.addTag<DebugTag>(goblin);
	std::cout << "After adding DebugTag: " << ecs.hasTag<DebugTag>(goblin) << "\n";

	ecs.removeTag<BuffedTag>(goblin);
	std::cout << "After removing BuffedTag: " << ecs.hasTag<BuffedTag>(goblin) << "\n";

	std::cout << "\n--- Before movement ---\n";
	ecs.forEach<Position, Velocity>([](Entity e, Position &p, Velocity &v) {
		std::cout << "Entity " << e.index << ":" << e.generation << " pos=(" << p.x << "," << p.y << "," << p.z << ")" << " vel=(" << v.dx
				  << "," << v.dy << "," << v.dz << ")\n";
	});

	ecs.forEach<Position, Velocity>([](ATTR_MAYBE_UNUSED Entity e, Position &p, Velocity &v) {
		p.x += v.dx;
		p.y += v.dy;
		p.z += v.dz;
	});

	std::cout << "\n--- After movement ---\n";
	ecs.forEach<Position, Velocity>([](Entity e, Position &p, Velocity &v) {
		std::cout << "Entity " << e.index << ":" << e.generation << " pos=(" << p.x << "," << p.y << "," << p.z << ")" << " vel=(" << v.dx
				  << "," << v.dy << "," << v.dz << ")\n";
	});

	const ECS &cecs = ecs;
	std::cout << "\n--- Const query (Health) ---\n";
	cecs.forEach<Health>(
		[](Entity e, const Health &h) { std::cout << "Entity " << e.index << ":" << e.generation << " HP=" << h.hp << "\n"; });

	std::cout << "\n--- Entities with AliveTag ---\n";
	ecs.forEach<NameTag, AliveTag>([](Entity e, NameTag &name, AliveTag) {
		std::cout << "Entity " << e.index << ":" << e.generation << " name=" << name.tag << "\n";
	});

	std::cout << "\n--- Buffs ---\n";
	ecs.forEach<Buffs>([](Entity e, Buffs &buffs) {
		for (const auto &buff : buffs.activeBuffs)
		{
			std::cout << "Entity " << e.index << ":" << e.generation << " has buff " << buff.name << " with duration " << buff.duration
					  << "\n";
		}
	});

	ecs.getComponent<Buffs>(goblin)->activeBuffs.erase(std::remove_if(ecs.getComponent<Buffs>(goblin)->activeBuffs.begin(),
																	  ecs.getComponent<Buffs>(goblin)->activeBuffs.end(),
																	  [](const Buff &buff) { return buff.name == "Haste"; }),
													   ecs.getComponent<Buffs>(goblin)->activeBuffs.end());

	std::cout << "\n--- After buff removal ---\n";
	ecs.forEach<Buffs>([](Entity entity, Buffs &buffs) {
		for (const auto &buff : buffs.activeBuffs)
		{
			std::cout << "Entity " << entity.index << ":" << entity.generation << " buff=" << buff.name << ", " << buff.duration << "\n";
		}
	});

	ecs.compact();

	return 0;
}