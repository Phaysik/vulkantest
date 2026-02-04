---
applyTo: '**/*{h,hpp,cpp}'
---

## Goal

When generating benchmarks that measure **memory usage, allocation behavior,
or lifetime characteristics**, prioritize **allocator visibility, determinism,
and isolation**.

Memory benchmarks must clearly answer at least one of:

- How many allocations occur?
- How much memory is allocated?
- How long allocations live?
- How allocation patterns scale?

## Scope

This instruction file applies **only** when:

- Benchmark intent involves memory usage
- Allocation behavior is performance-critical
- Comparing allocation strategies or data layouts

It supplements, but does not override, `benchmarks.instructions.md`.

## General Rules

1. **Make allocations explicit**
   - Clearly identify what allocates
   - Avoid hidden allocations
   - Prefer explicit construction/destruction points

2. **Isolate allocation cost**
   - Do not mix allocation with unrelated computation
   - Separate allocation, use, and deallocation phases when possible

3. **Prefer steady-state measurement**
   - Measure behavior after warm-up
   - Avoid one-time initialization unless explicitly measuring it

4. **Do NOT constrain yourself to only 80 characters per line. You may use up to 180 characters per line to improve code clarity and maintainability.**

## Allocation Control

### Allocator Awareness

- Prefer allocator-aware containers where applicable
- Use custom allocators explicitly when comparing strategies
- Do not rely on the default global allocator unless that is the subject

```cpp
std::vector<int, MyAllocator<int>> vec;
```

## Preventing Allocation Elision

- Ensure allocated memory is actually used
- Guard allocated objects with benchmark::DoNotOptimize
- Avoid compiler-visible deallocation before measurement ends

## Measuring Allocation Patterns

### Allocation Count

- Structure benchmarks so allocation count is stable per iteration
- Avoid allocation in setup unless measuring setup
- Document expected allocation count per iteration

## Allocation Size

- Use fixed, documented sizes
- Avoid implicit resizing
- Prefer **reserve** to control growth behavior

## Allocation Lifetime

- Be explicit about object lifetime:
  - Per-iteration
  - Per-benchmark
  - Pooled / reused

## Memory Reuse & Pools

- Prefer reuse when benchmarking steady-state systems
- Document pool size and reuse strategy
- Avoid benchmarking empty or trivially small pools

## Fragmentation & Layout

- When relevant, benchmark:
  - Small vs large allocations
  - Mixed-size allocation patterns
  - Contiguous vs fragmented layouts
- Avoid unrealistic allocation distributions

## Thread-Local & Concurrent Allocation

- Explicitly document allocator behavior under concurrency
- Avoid implicit sharing of allocators unless intentional
- Control thread count explicitly in benchmarks

## Input Determinism

- Avoid randomized allocation patterns unless required
- Fix seeds when randomness is used
- Document distribution characteristics

## Instrumentation & Tool Compatibility

- Benchmarks must be compatible with:
  - AddressSanitizer
  - LeakSanitizer
  - heaptrack
  - massif
  - valgrind (where practical)
- Do not suppress allocator hooks or instrumentation

## Reporting & Interpretation

- Clearly state what is being measured:
  - Allocation throughput
  - Peak memory usage
  - Retained memory
  - Fragmentation effects
- Prefer multiple focused benchmarks over aggregate metrics

## Common Anti-Patterns (Do NOT Do These)

- Benchmarking allocation + unrelated computation together
- Measuring memory usage without controlling allocation lifetime
- Relying on OS-level allocation behavior without documentation
- Treating reduced allocation count as automatically “better”

## How to Apply These Instructions

When I ask you to:

- “Benchmark memory usage of this container”
- “Compare allocation strategies”
- “Measure allocator performance”

**You must**:

1. Make allocation behavior explicit

2. Isolate allocation cost

3. Prevent compiler and allocator elision

4. Document allocation assumptions

5. Produce repeatable, tool-friendly benchmarks
