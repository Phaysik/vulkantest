#include "ECS/entity.h"

#include "ECS/component.h"

#include <benchmark/benchmark.h>

using Dimensia::ECS::Component;
using Dimensia::ECS::Entity;

struct DummyComponent final : public Component
{
		DummyComponent() : Component("Dummy") {}
};

static void addComponent(benchmark::State &state)
{
	for (auto iter : state)
	{
		Entity entity("bench_add");
		// measure cost of adding a single component to a fresh entity
		benchmark::DoNotOptimize(entity.addComponent<DummyComponent>());
	}
}

static void getComponent(benchmark::State &state)
{
	Entity entity("bench_get");
	benchmark::DoNotOptimize(entity.addComponent<DummyComponent>());

	for (auto iter : state)
	{
		benchmark::DoNotOptimize(entity.getComponent<DummyComponent>());
	}
}

static void removeComponent(benchmark::State &state)
{
	for (auto iter : state)
	{
		Entity entity("bench_remove");
		benchmark::DoNotOptimize(entity.addComponent<DummyComponent>());
		benchmark::DoNotOptimize(entity.removeComponent<DummyComponent>());
	}
}

BENCHMARK(addComponent)->Iterations(100'000);
BENCHMARK(getComponent)->Iterations(100'000);
BENCHMARK(removeComponent)->Iterations(100'000);

BENCHMARK_MAIN();
