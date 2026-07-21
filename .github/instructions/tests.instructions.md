---
applyTo: '**/*.{h,hpp,cpp}'
---

## Goal

When I ask you to generate tests for a **file, class, or function**, always generate **Catch2**–based test code using BDD syntax targeting the file under test.

The test file MUST be located at: /tests/(unit|mocks|integrations)/<relative path to file under test>.test.cpp

The tests must be **correct, isolated, readable, deterministic**, and follow modern C++ (C++23–26) and Catch2 best practices.

## General Rules

1. **Always use Catch2 with BDD syntax**
   - Use `<catch2/catch_test_macros.hpp>`
   - Use `SCENARIO` for grouping related tests
   - Use `GIVEN` to set up preconditions
   - Use `WHEN` for actions (when possible; optional if obvious)
   - Use `THEN` for assertions
   - Do NOT use deprecated Catch2 APIs

2. **One Unit Under Test (UUT) per file**
   - Tests in a file must focus on:
     - One class **or**
     - One free function **or**
     - One logical module
   - Do not mix unrelated units in the same test file

3. **Test behavior, not implementation**
   - Assert observable behavior
   - Do NOT test private methods directly
   - Do NOT rely on internal state unless exposed by contract
   - Do NOT use **FRIEND_TEST** unless explicitly instructed

4. **Deterministic & isolated**
   - No shared global state
   - No dependency on test execution order
   - No sleep-based timing tests
   - No network, filesystem, or environment dependencies unless explicitly required
   - Do NOT use non-deterministic randomness
   - If randomness is required, fix the seed explicitly and document it

5. **Do NOT constrain yourself to only 80 characters per line. You may use up to 180 characters per line to improve code clarity and maintainability.**

## Test File Structure

Every test file MUST follow this structure:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "path/to/header/under/test.h"

// Optional: standard headers
#include <string>
#include <vector>
#include <memory>
```

**Include only what is required**

Do NOT include source (.cpp) files unless explicitly required

Use forward declarations where appropriate

## Naming Conventions

- Test files: `<original_filename>.test.cpp`

### Scenario Naming (SCENARIO)

- Use the name of the class or function under test
- **DO NOT** add underscores in the scenario name

Examples:

- **Parser**
- **HashFunction**
- **FileReaderIntegration**

### BDD Clause Naming (GIVEN/WHEN/THEN)

- Use descriptive text that reads as English prose
- **GIVEN** describes the precondition or setup
- **WHEN** describes the action (optional if action is obvious)
- **THEN** describes the expected outcome
- Use lowercase for readability

Example:

```cpp
SCENARIO("Parser")
{
  GIVEN("valid input")
  {
    WHEN("parsing")
    {
      THEN("parsing succeeds")
      {
        // assertions
      }
    }
  }
}
```

### Variable Naming

- **Do NOT** use variable names less than 3 characters long
- Variable names should be descriptive of their purpose

## Test Setup & Teardown

For setup/teardown, use:

- Inline code at the beginning of `SCENARIO` block for setup
- Capture variables by reference in nested scopes when needed
- Use RAII for resource cleanup (e.g., temporary files)
- When complex setup/teardown is required, consider a helper function or class

## Unit vs Integration vs Mock Tests

### Unit Tests (/unit/)

- Test a single class or function in isolation
- Use mocks or fakes for dependencies
- No filesystem, no network, no threads unless unavoidable

### Integration Tests (/integrations/)

- Test interactions between multiple components
- May touch filesystem or threads
  - Use temporary directories (_std::filesystem::temp_directory_path_)
  - Clean up via RAII
  - Never depend on absolute paths or repo-relative paths
- Clearly document external dependencies
- Keep scope minimal

### Mock Tests (/mocks/)

- Focus on mock behavior and expectations
- Use Catch2 matchers and custom comparators when applicable
- Verify interactions through return values and side effects
- Prefer interface-based mocking (pure virtual base classes)
- Mocks must live in the test file or a dedicated test-only header
- Avoid mocking concrete classes unless unavoidable
- Use structured assertions with meaningful error messages

## Assertions & Expectations

1. **REQUIRE** vs **CHECK** rule of thumb:

- Use **REQUIRE** for preconditions required for the rest of the test to be valid
- Use **CHECK** for independent verifications that should not stop the test
- Use **REQUIRE_THAT** with matchers for complex conditions or when failure should abort the test
- Use **CHECK_THAT** with matchers for complex conditions or better error messages

2. Prefer specific assertions

- Prefer **CHECK** for true conditions
- Prefer **CHECK_FALSE** for false conditions

3. How to use matchers

- Include `<catch2/matchers/catch_matchers_all.hpp>`
- Use matchers for complex conditions, better error messages, or when comparing against a specific value

```cpp
CHECK_THAT(result, Catch::Matchers::Equals(expected));
```

4. Floating-point comparisons

- Use **CHECK_THAT(val, Catch::Matchers::WithinAbs(expected, tolerance))**

5. Containers
   - Compare sizes first, then contents
   - Prefer element-wise assertions if order matters

6. String comparisons
   - Use **CHECK_THAT(str, Catch::Matchers::ContainsSubstring(substr))**

7. Exceptions Use:

- **CHECK_THROWS_AS(fn(), ExceptionType);**
- **CHECK_NOTHROW(fn());**

8. Comparison Operators (<, <=, >, >=, ==, !=)

- When using **CHECK** or **CHECK_FALSE** with these operators
  - Wrap the entire expression in parentheses to ensure correct evaluation

```cpp
CHECK((value > threshold));
```

## Modern C++ Guidelines

- Prefer RAII over manual setup/cleanup
- Avoid raw pointers unless required by API
- Use _std::span_, _std::string_view_, and ranges where appropriate
- Do NOT use macros for test logic

## Parameterized Tests

Use parameterized tests when:

- Testing the same logic across multiple inputs
- Edge cases vary by data, not structure
- Parameter names must describe the scenario, not the value
- Avoid numeric-only test names

Example using Catch2 test cases:

```cpp
namespace {
  struct TestCase {
    std::string input;
    std::string expected;
  };
} // namespace

SCENARIO("Parser", "[param]")
{
    auto testCases = std::vector<TestCase>{
        {"valid_input", "expected_output"},
        {"another_input", "another_output"}
    };

    for (const auto& tc : testCases) {
        GIVEN(tc.input)
        {
            THEN("parses to expected output")
            {
                CHECK_EQ(parse(tc.input), tc.expected);
            }
        }
    }
}
```

## Edge Cases & Error Conditions

- Always include tests for:
- Boundary values
- Invalid input
- Empty input
- Null or optional absence (if applicable)
- Error-returning paths
- Exception paths (if defined by contract)

## Threading & Concurrency (If Applicable)

- Clearly document concurrency assumptions
- Avoid timing-based assertions
- Prefer deterministic synchronization primitives
- Use **REQUIRE** when failure should abort the test

## Documentation in Tests

- Add brief comments only when intent is non-obvious
- Do NOT restate what the code already expresses
- Each test name should be self-documenting

## Contract & Termination Tests

- Use **CHECK_THROWS_AS** when behavior is defined to throw
- Document contract violations and error conditions
- Termination tests should be deterministic and platform-safe
- Prefer exceptions over process termination for testability

```cpp
CHECK_THROWS_AS(fn(invalid_input), std::invalid_argument);
```

## Coverage-Aware Mode

- When generating tests, prioritize runtime code coverage without compromising test correctness, clarity, or determinism.
- This means that any variables within a test case **CANNOT** be **const** or **constexpr** as doing so would prevent execution of certain code paths at runtime.

### General Coverage Rules

- Tests must execute code paths at runtime
  - Avoid consteval and compile-time–only execution in tests
  - Avoid logic that is optimized away before runtime
    - Prefer calling public APIs rather than testing via constructors alone
    - Ensure both success paths and failure/error paths are exercised

### Branch & Condition Coverage

- Where reasonable, include tests that cover:
  - Conditional branches (if, switch, pattern matching)
  - Boundary conditions
  - Error-handling paths
    - For enums or variants:
  - Ensure all meaningful alternatives are exercised

### Inlining & Optimization Awareness

- Avoid tests that rely solely on:
  - Trivial inline functions
  - Header-only constant expressions
    - If a function is inline-only:
  - Ensure it is invoked in a context that produces observable behavior

### Exception & Termination Coverage

- If a function can:
  - Throw exceptions
  - Terminate the program
  - Violate documented contracts
- Include explicit tests that exercise those paths using:
  - **CHECK_THROWS_AS** for expected exceptions
  - **CHECK_NOTHROW** for success paths
  - Document and test termination paths when part of the contract

### Test Design Guidance

- Prefer **multiple focused tests** over a single large test
- Avoid shared setup that hides which code paths are executed
- Make coverage intent obvious through test naming

### Non-Goals

- Do NOT add artificial logic solely to increase coverage
- Do NOT test unreachable or undefined behavior
- Do NOT weaken assertions to “hit lines”

### Clang-Tidy Suppression

- I wish to ignore the most common sources of warnings issued by clang-tidy
- Follow the below example to wrap all test scenarios within NOLINTBEGIN/NOLINTEND comments

```cpp
#include <vector>
#include ...

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)

SCENARIO("FizzBuzz")
{
	GIVEN("zero")
	{
		THEN("returns empty")
		{
			const auto result = fizzBuzz<ui>(0U);
			CHECK(result.empty());
		}
	}
}
...

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)

```

## How to Apply These Instructions

When I ask you to:

- “Generate unit tests for this class”
- "Write tests for this function"
- "Create integration tests for this module"
  You must:

1. Determine whether the test belongs in:

- /unit/
- /integrations/
- /mocks/

2. Create the test file at: /tests/(unit|mocks|integrations)/<relative path to file under test>.test.cpp

3. Generate complete, compilable Catch2 code using BDD syntax

4. Follow naming, structure, and best practices above

5. Make reasonable assumptions when details are missing, but remain conservative

6. Prefer correctness, clarity, and maintainability over cleverness

7. When multiple valid test designs exist, prefer the one that:

- Exercises more runtime code paths
- Produces clearer coverage results
- Remains deterministic and behavior-focused
- Uses BDD syntax to enhance readability
