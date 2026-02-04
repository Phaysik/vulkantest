---
applyTo: '**/*.{h,hpp,cpp}'
---

## Goal

When I ask you to generate tests for a **file, class, or function**, always generate **Google Test (gtest)**–based test code targeting the file under test.

The test file MUST be located at: /tests/(unit|mocks|integrations)/<relative path to file under test>.test.cpp

The tests must be **correct, isolated, readable, deterministic**, and follow modern C++ (C++23–26) and Google Test best practices.

## General Rules

1. **Always use Google Test**
   - Use `<gtest/gtest.h>`
   - Prefer `TEST_F` or `TEST_P` over `TEST` when state or reuse is involved
   - Do NOT use deprecated gtest APIs

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
#include <gtest/gtest.h>

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

### Test Suite Naming

- Use the name of the class or function under test
- Append _Test_ for test fixtures
- **DO NOT** add underscores in the test suite name

Examples:

- **ParserTest**
- **HashFunctionTest**
- **FileReaderIntegrationTest**

### Test Case Naming

- Use **GivenWhenThen** or **BehaviorConditionResult** style:
  - TEST_F(ParserTest, GivenValidInputWhenParsingThenSucceeds)
- **DO NOT** add underscores in the test case name

### Variable Naming

- **Do NOT** use variable names less than 3 characters long
- Variable names should be descriptive of their purpose

## Test Fixtures

Use test fixtures when:

- Shared setup/teardown is required
- Common test data exists
- Dependency injection or mocks are involved

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
- Use Google Mock (<gmock/gmock.h>) when applicable
- Verify interactions (**EXPECT_CALL**, **Times**, etc.)
- Prefer interface-based mocking (pure virtual base classes)
- Mocks must live in the test file or a dedicated test-only header
- Avoid mocking concrete classes unless unavoidable
- Use **NiceMock** or **StrictMock** explicitly when intent matters

```cpp
using ::testing::NiceMock;
using ::testing::StrictMock;
```

## Assertions & Expectations

1. **ASSERT\_\*** vs **EXPECT\_\*** rule of thumb:

- Use **ASSERT\_\*** for preconditions required for the rest of the test
- Use **EXPECT\_\*** for independent verifications

2. Prefer **EXPECT_THAT** with matchers for complex assertions

- Use matchers such as:
  - **ElementsAre**
  - **Contains**
  - **HasSubstr**
  - **Optional**
  - **VariantWith**

```cpp
EXPECT_THAT(result, testing::ElementsAre(1, 2, 3));
```

3. Prefer specific assertions

- Prefer **EXPECT_EQ**, **EXPECT_TRUE**, **EXPECT_FALSE**
- Avoid generic **EXPECT_TRUE(condition)**

4. Floating-point comparisons

- Use **EXPECT_NEAR** with explicit tolerance

5. Containers
   - Compare sizes first, then contents
   - Prefer element-wise assertions if order matters

6. Exceptions Use:

- **EXPECT_THROW(fn(), ExceptionType);**
- **EXPECT_NO_THROW(fn());**

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

Example:

```cpp
class ParserParamTest
    : public ::testing::TestWithParam<TestCase> {};

TEST_P(ParserParamTest, ParsesCorrectly) {
    const auto& tc = GetParam();
    EXPECT_EQ(parse(tc.input), tc.expected);
}

INSTANTIATE_TEST_SUITE_P(
    ValidInputs,
    ParserParamTest,
    ::testing::Values(
        TestCase{...},
        TestCase{...}
    )
);
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
- Use **ASSERT\_\*** when failure should abort the test

## Documentation in Tests

- Add brief comments only when intent is non-obvious
- Do NOT restate what the code already expresses
- Each test name should be self-documenting

## Contract & Death Tests

- Use **EXPECT_DEATH** or **ASSERT_DEATH** when behavior is defined as:
  - Program termination
  - Contract violation

- Death tests must be deterministic and platform-safe

```cpp
EXPECT_DEATH(fn(invalid_input), ".*");
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
  - **EXPECT_THROW**
  - **EXPECT_NO_THROW**
  - **EXPECT_DEATH / ASSERT_DEATH (when termination is specified)**

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
- Follow the below example to wrap all test cases within NOLINTBEGIN/NOLINTEND comments

```cpp
#include <vector>
#include ...

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

TEST(FizzBuzzTest, GivenZeroReturnsEmpty)
{
	const auto result = fizzBuzz<ui>(0U);
	EXPECT_TRUE(result.empty());
}
...

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

```

## How to Apply These Instructions

When I ask you to:

- “Generate unit tests for this class”
- “Write Google Tests for this function”
- “Create integration tests for this module”
  You must:

1. Determine whether the test belongs in:

- /unit/
- /integrations/
- /mocks/

2. Create the test file at: /tests/(unit|mocks|integrations)/<relative path to file under test>.test.cpp

3. Generate complete, compilable Google Test code

4. Follow naming, structure, and best practices above

5. Make reasonable assumptions when details are missing, but remain conservative

6. Prefer correctness, clarity, and maintainability over cleverness

7. When multiple valid test designs exist, prefer the one that:

- Exercises more runtime code paths
- Produces clearer coverage results
- Remains deterministic and behavior-focused
