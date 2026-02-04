---
applyTo: '**/*{h,hpp,cpp}'
---

## Goal

When generating benchmarks, always use **Google Benchmark** to measure **runtime performance characteristics** of code under test.

Benchmarks must be:

- Correct
- Repeatable
- Isolated
- Representative of real usage
- Resistant to compiler optimizations

Benchmarks exist to **measure behavior**, not to validate correctness.

## General Rules

1. **Always use Google Benchmark**
   - Include `<benchmark/benchmark.h>`
   - Use `BENCHMARK`, `BENCHMARK_F`, or `BENCHMARK_CAPTURE`
   - Do NOT use custom timing utilities

2. **Benchmarks are not tests**
   - Do NOT use `EXPECT_*` or `ASSERT_*`
   - Do NOT test correctness
   - Do NOT rely on exceptions for control flow

3. **Measure runtime code**
   - Ensure benchmarked code executes at runtime
   - Avoid `consteval` and compile-time-only evaluation
   - Prevent dead-code elimination

4. **Do NOT constrain yourself to only 80 characters per line. You may use up to 180 characters per line to improve code clarity and maintainability.**

## File Structure

Every benchmark file MUST follow this structure:

```cpp
#include <benchmark/benchmark.h>

#include "path/to/header/under/benchmark.h"

// Optional: standard headers
#include <vector>
#include <string>
#include <random>
```

- Do NOT include test headers
- Do NOT include source (**.cpp**) files unless explicitly required
- Keep includes minimal

## Benchmark Naming

### Benchmark Function Naming

Use descriptive, stable names:

```cpp
BM_<Component>_<Operation>
```

Examples:

- **BM_Parser_ParseValidInput**
- **BM_Hash_ComputeSHA256**
- **BM_Allocator_AllocateSmallBlocks**

## Preventing Optimizer Elision

Always guard benchmarked values using:

```cpp
benchmark::DoNotOptimize(value);
benchmark::ClobberMemory();
```

Rules:

- Use **DoNotOptimize** on inputs and outputs
- Use **ClobberMemory** when memory effects matter
- Never rely on **volatile** as a substitute

## Benchmark Structure

### Basic Pattern

```cpp
static void BM_Operation(benchmark::State& state) {
    SetupData data = CreateData(state.range(0));

    for (auto _ : state) {
        auto result = Operation(data);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_Operation)->Range(1, 1 << 20);
```

## Parameterized Benchmarks

Use ranges to explore scaling behavior:

- Input size
- Data distribution
- Algorithm variants

```cpp
BENCHMARK(BM_Parse)
    ->RangeMultiplier(2)
    ->Range(64, 1 << 20);
```

Avoid:

- Magic numbers without explanation
- Excessive parameter explosion

## Setup & Teardown

- Perform expensive setup outside the measurement loop
- Use **PauseTiming()** / **ResumeTiming()** when setup must occur inside the loop

```cpp
state.PauseTiming();
PrepareData();
state.ResumeTiming();
```

## Memory & Allocation Benchmarks

- Isolate allocation costs when relevant
- Avoid measuring unrelated work
- Document what is being measured:
  - Allocation only
  - Allocation + initialization
  - Allocation + destruction

## Threading & Concurrency Benchmarks

- Explicitly specify thread counts
- Avoid implicit thread scaling
- Document synchronization assumptions
- Avoid benchmarking undefined or racy behavior

```cpp
BENCHMARK(BM_Work)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8);
```

## Input Generation

- Use deterministic input data
- Avoid randomness unless explicitly benchmarking randomness
- If randomness is required:
  - Fix the seed
  - Document it

## Measurement Discipline

- Benchmark only one logical operation at a time
- Avoid mixing hot and cold paths
- Avoid I/O in benchmarks unless explicitly measuring I/O
- Avoid logging inside benchmarks

## Reporting & Interpretation

- Benchmarks must be interpretable without reading the source
- Prefer multiple focused benchmarks over one large benchmark
- Name benchmarks to reflect intent and scope

## Common Anti-Patterns (Do NOT Do These)

- Measuring setup instead of steady-state behavior
- Benchmarking trivial inline getters/setters
- Benchmarking code that is optimized away
- Using benchmarks to justify premature micro-optimizations
- Using benchmarks as correctness tests

## How to Apply These Instructions

When I ask you to:

- “Write benchmarks for this function”
- “Benchmark this algorithm”
- “Compare performance of these implementations”

**You must**:

1. Generate Google Benchmark–compatible code

2. Ensure benchmarked code runs at runtime

3. Guard against compiler optimization

4. Use clear, descriptive benchmark names

5. Parameterize benchmarks when scaling matters

6. Make conservative assumptions when details are missing

7. Favor clarity and repeatability over clever tricks
