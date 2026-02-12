#include "ECS/component.h"
#include "ECS/entity.h"

#include <benchmark/benchmark.h>

using Dimensia::ECS::Component;
using Dimensia::ECS::Entity;

struct DummyComponent : public Component
{
		DummyComponent() : Component("Dummy") {}
};

static void BM_AddComponent(benchmark::State &state)
{
	for (auto _ : state)
	{
		Entity e("bench_add");
		// measure cost of adding a single component to a fresh entity
		benchmark::DoNotOptimize(e.addComponent<DummyComponent>());
	}
}

static void BM_GetComponent(benchmark::State &state)
{
	Entity e("bench_get");
	benchmark::DoNotOptimize(e.addComponent<DummyComponent>());

	for (auto _ : state)
	{
		benchmark::DoNotOptimize(e.getComponent<DummyComponent>());
	}
}

static void BM_RemoveComponent(benchmark::State &state)
{
	for (auto _ : state)
	{
		Entity e("bench_remove");
		benchmark::DoNotOptimize(e.addComponent<DummyComponent>());
		benchmark::DoNotOptimize(e.removeComponent<DummyComponent>());
	}
}

BENCHMARK(BM_AddComponent)->Iterations(100'000);
BENCHMARK(BM_GetComponent)->Iterations(100'000);
BENCHMARK(BM_RemoveComponent)->Iterations(100'000);

BENCHMARK_MAIN();
