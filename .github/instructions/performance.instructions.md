---
applyTo: '**/*.{h,hpp,cpp}'
---

## Goal

When generating or modifying **performance-critical code**, prioritize **predictable performance, low overhead, and scalability** while preserving correctness and clarity.

Assume the code may run in:

- Hot paths
- Tight loops
- Latency-sensitive contexts
- Memory-constrained environments

## General Performance Rules

1. **Avoid unnecessary work**
   - Do not allocate memory unless required
   - Do not copy data unnecessarily
   - Avoid hidden temporaries
   - Prefer single-pass algorithms

2. **Be explicit about performance tradeoffs**
   - If readability is sacrificed for performance, document why
   - If performance is intentionally deprioritized, document that too

3. **Prefer predictable performance**
   - Avoid unbounded operations in hot paths
   - Avoid algorithmic surprises
   - Avoid reliance on unspecified evaluation order

4. **Do NOT constrain yourself to only 80 characters per line. You may use up to 180 characters per line to improve code clarity and maintainability.**

## Algorithmic Complexity

- Always consider **time and space complexity**
- Prefer algorithms with:
  - Lower asymptotic complexity
  - Better cache locality

## Memory & Allocation

- Avoid dynamic allocation in hot paths
- Prefer:
  - Stack allocation
  - Object reuse
  - Preallocation (_reserve_, _resize_)
- Prefer _std::span_, _std::string_view_, and iterator pairs over owning containers
- Avoid _std::function_ in hot paths unless required
- Avoid reference-counted types unless ownership semantics demand them

## Data Layout & Cache Locality

- Prefer contiguous storage (_std::vector_, _std::array_)
- Avoid pointer-heavy structures in performance-sensitive code
- Consider Structure-of-Arrays (SoA) vs Array-of-Structures (AoS) tradeoffs
- Minimize object size in frequently accessed types

## Function Design

- Prefer passing small trivially copyable types by value
- Prefer passing large objects by **const&**
- Avoid returning large objects unless NRVO or move semantics are guaranteed
- Mark functions inline only when it meaningfully reduces overhead
- Use **[[nodiscard]]** for expensive computations whose result must be used

## constexpr / consteval

- Use **constexpr** to enable compile-time evaluation when beneficial
- Avoid **consteval** in runtime-critical code unless compile-time execution is required
- Clearly document compile-time vs runtime intent

## Branching & Control Flow

- Avoid deeply nested conditionals
- Prefer early exits for error handling
- Avoid unpredictable branches in hot loops
- Document assumptions about branch likelihood when relevant

## Exceptions & Error Handling

- Avoid exceptions in hot paths unless explicitly required
- Prefer value-based error handling (**std::expected**, error codes) when performance matters
- Do not use exceptions for control flow

## Concurrency & Atomics (When Applicable)

- Avoid fine-grained locking in hot paths
- Prefer lock-free or wait-free designs when feasible
- Use atomics with the weakest correct memory ordering
- Document all synchronization assumptions

## I/O & System Interaction

- Avoid synchronous I/O in performance-sensitive code
- Batch operations where possible
- Avoid logging in hot paths unless guarded or rate-limited

## Measurements & Assumptions

- Do not speculate — write code that is easy to benchmark
- Avoid premature micro-optimizations
- Prefer clear code unless profiling data justifies complexity

## Non-Goals

- Do NOT optimize for microbenchmarks alone
- Do NOT sacrifice correctness for speed
- Do NOT introduce undefined behavior for performance gains
