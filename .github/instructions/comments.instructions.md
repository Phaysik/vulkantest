---
applyTo: '**/*.{h,hpp}'
---

## Goal

When I ask for comments for any **function, method, class, struct, enum, file, or module**, always generate **Doxygen-compatible** comments.

Use **standard Doxygen tags** and clear, concise descriptions. Assume the codebase uses Doxygen for documentation generation.

---

## General Rules

1. **Always use Doxygen format**
   - Use the `/*! ... */` block style

2. **Language-Aware**
   - For **C / C++ / C++17+**: use standard Doxygen syntax

3. **Be explicit**:
   - Document **what** the entity does
   - Document **how** to use it if not obvious
   - Document **preconditions / postconditions / side effects** when relevant
   - Avoid restating the identifier’s name as the only description

4. **Be consistent and complete**:
   - Every template parameter → `@tparam <template parameter type> <template parameter description>` line
   - Every parameter → `@param[] <parameter name> <parameter description>` line
     - Pointers or output parameters → document if they are input, output, or in/out (`@param[in]`, `@param[out]`, `@param[in,out]`)
   - Non-void returns → `@return <description>` line
   - Possible errors/exceptions → `@throws` or `@exception`
     - When relevant, document exception guarantees using standard terminology:
       - “No-throw”
       - “Strong exception guarantee”
       - “Basic exception guarantee”
   - If a function/class/variable is referencing another entity, use `@ref <related entity>` to link them

5. **Write in third person, present tense**
   - Example: “Computes the hash of the input buffer” (not “Compute” or “This function computes…” unless it improves clarity)

6. **Do NOT constrain yourself to only 80 characters per line. You may use up to 180 characters per line to improve code clarity and maintainability.**

## Doxygen Tag Usage

When generating comments, prefer the following tags:

### For Files / Modules

Place at the top of source/header files:

```cpp
/*! @file MyFileName.h
    @brief Short description of the file's purpose.
    @date The date in MM/DD/YYYY format.
    @version The current version of the file.
    @since The version or date since this file has been present.
    @author The author of the file.
 */
```

#### Other considerations for Files / Modules:

- Always include '@file', '@brief', '@date', '@version', '@since', and '@author' tags
- Use `@ingroup` for logical modules or groups
- Use `@copyright` for licensing info if needed

### For Functions / Methods

```cpp
/*! @brief Short, one-line summary in sentence form.
    @details Optional longer description that explains behavior, algorithms,
    constraints, and important usage notes.
    @pre Preconditions that must hold before calling.
    @post Postconditions guaranteed after successful return.
    @warning Important warnings about misuse or edge cases.
    @tparam T Description of the template parameter T.
    @param[in]  paramName  Description of the parameter (role, units, valid range).
    @param[out] result     Description of output parameter if any.
    @param[in,out] state   Description of in/out parameter behavior.
    @note Additional notes or remarks.
    @return Description of the return value. If the function is `void`
    @throws SomeExceptionType Description of condition(s) that cause this.
    @throws AnotherException  Optional additional thrown types.
    @ref RelatedFunction(), RelatedClass
    @date The date in MM/DD/YYYY format.
    @version The current version of the file.
    @since The version or date since this file has been present.
    @author The author of the file.
 */
```

#### Other considerations for Functions / Methods:

- Only include '@brief', '@date', '@version', '@since', and '@author' if the function represents a public API boundary
- Include `@param` for every parameter in the function signature
  - If direction is clear (like const references or pointers), tag as @param[in], @param[out], or @param[in,out].
- Include `@return` for non-void functions, even if obvious.
- If exceptions are thrown, use `@throws` / `@exception`.
- If **any** methods or functions have test within the name, include `@qualifier test` to indicate it's a test function.
- Use `@deprecated` for deprecated functions with guidance on alternatives
- Explicitly document ownership and lifetime expectations for:
  - Pointer parameters
  - Reference parameters
  - Returned pointers or references
- Clearly state whether returned references remain valid:
  - Until the next call
  - For the lifetime of the object
  - Until explicitly released
- Mention whether parameters are allowed to be **nullptr** when applicable
- When complexity is **non-obvious**, include the following within a `@note` block:
  - Time complexity (Big-O)
  - Space complexity
- **Especially important for**:
  - Template-heavy code
  - Containers
  - Algorithms
- If a function is marked **constexpr**, **noexcept**, or **[[nodiscard]]**, briefly explain why.
- If inline behavior matters for ODR or performance, mention it.

### For Classes / Structs / Enums

#### For Classes

```cpp
/*! @class ClassName HeaderFile.h Path/To/HeaderFile.h
    @brief Short, one-line summary in sentence form.
    @details Optional longer description that explains behavior, design decisions,
    constraints, and important usage notes.
    @warning Important warnings about misuse or edge cases.
    @tparam T Description of the template parameter T.
    @note Additional notes or remarks.
    @date The date in MM/DD/YYYY format.
    @version The current version of the file.
    @since The version or date since this file has been present.
    @author The author of the file.
 */
```

#### For Structs

```cpp
/*! @struct StructName HeaderFile.h Path/To/HeaderFile.h
    @brief Short, one-line summary in sentence form.
    @details Optional longer description that explains behavior, design decisions,
    constraints, and important usage notes.
    @warning Important warnings about misuse or edge cases.
    @tparam T Description of the template parameter T.
    @note Additional notes or remarks.
    @date The date in MM/DD/YYYY format.
    @version The current version of the file.
    @since The version or date since this file has been present.
    @author The author of the file.
 */
```

#### For Enums

```cpp
/*! @enum EnumName
    @showenumvalues
    @brief Short, one-line summary in sentence form.
    @details Optional longer description that explains behavior, design decisions,
    constraints, and important usage notes.
    @warning Important warnings about misuse or edge cases.
    @tparam T Description of the template parameter T.
    @note Additional notes or remarks.
    @date The date in MM/DD/YYYY format.
    @version The current version of the file.
    @since The version or date since this file has been present.
    @author The author of the file.
 */
```

#### Other considerations for Classes / Structs / Enums:

- Always include '@brief', '@date', '@version', '@since', and '@author' tags
- For the `@enum`, include `@showenumvalues` to list all enum values.
- If the type is a POD container, data carrier, or configuration struct, explicitly state that purpose in the `@brief`.
- For the member variables of each class, struct, and enum (whether private, public, or protected), use the format below to document the member variables.

#### For Namespaces

```cpp
/*! @namespace NamespaceName
    @brief Short, one-line summary in sentence form.
    @details Optional longer description that explains behavior, design decisions, constraints, and important usage notes.
    @note Additional notes or remarks.
    @date The date in MM/DD/YYYY format.
    @version The current version of the file.
    @since The version or date since this file has been present.
    @author The author of the file.
 */
 namespace NamespaceName {
     // namespace contents
 }
```

### For Member Variables

```cpp
/*! @var memberVariable
    @brief Short, one-line summary in sentence form.
    @details Optional longer description that explains behavior, constraints,
    and important usage notes.
    @showinitializer
    @note Additional notes or remarks.
 */
```

#### Other considerations for Member Variables:

- If **any** member variables have test within the name, include `@qualifier test` to indicate it's a test variable.
- If the member variable is a pointer, indicate ownership semantics (e.g., "owns the memory", "non-owning reference", "may be nullptr").
- If the variable is a C++20 concept or constraint, document the requirements it enforces.
  - Furthermore, use `@concept name` to specify it's a concept.
- If the variable is a typedef, use `@typedef <typedef declaration>` to specify it's a typedef.

### Template Parameters

- The template param must follow the form of `@tparam <TemplateParamType> <Description>`.
- Also provide a reference to the type in the description if the template param type is something that the user has created somewhere in the codebase.

Example:

```cpp
/*! @brief Generates an array representing primality for numbers up to @p maxNumber
    @note This uses the Sieve of Eratosthenes
    @pre PrimeSize > 0 && @p maxNumber < PrimeSize
    @tparam Concepts::UnsignedIntegral The type of the array which satisfies @ref Concepts::UnsignedIntegral
    @tparam std::size_t The fixed size of the returned std::bitset (must be > 0)
    @param[in] maxNumber The maximum number to mark in the bitset (must be < PrimeSize)
    @retval std::bitset<PrimeSize> A bitset where bit i is true iff i is prime (for 0 <= i <= maxNumber)
    @date 01/12/2026
    @version 0.0.3
    @since 0.0.3
    @author Matthew Moore
 */
template <Concepts::UnsignedIntegral Number, const std::size_t PrimeSize>
[[nodiscard]] constexpr std::array<Number, PrimeSize> GeneratePrimeBitset(const std::size_t maxNumber) noexcept
```

### C++26 Contracts and Semantic Preconditions

- If a function has logical preconditions or postconditions, prefer expressing them
  as **C++ contracts** (`[[expects]]`, `[[ensures]]`) when available.
- Mirror contracts in documentation:
  - `@pre` → `[[expects]]`
  - `@post` → `[[ensures]]`
- Clearly document the behavior on contract violation:
  - Undefined behavior
  - Program termination
  - Checked only in debug or contract-enabled builds

### Ranges, Views, and Borrowed Lifetimes

- For functions accepting or returning **ranges, views, or generators**:
  - Explicitly document whether evaluation is eager or lazy.
  - State whether the returned object:
    - References the input range
    - Owns its elements
    - Requires the input range to outlive it
  - Specify whether the range models `borrowed_range`.

Example:

```
@param[in] input A borrowed range that must outlive the returned view.
@return A lazy view referencing the input range.
```

### Coroutines, Async APIs, and Senders/Receivers

- For coroutine or asynchronous functions:
  - Document when execution begins (eager vs lazy).
  - Document where execution resumes (caller thread, executor, scheduler).
  - Document cancellation behavior.
  - Document exception propagation semantics.

Example:

```
@note This coroutine is lazy; execution begins on first await.
@note Resumes on the caller's execution context.
@note Cancellation propagates via std::stop_token.
```

### Modern Error Channels

- If a function returns `std::expected`:
  - Document the success invariant.
  - Document the meaning of each error type.
- If a function returns `std::optional`:
  - Document why absence is expected.
  - State whether absence represents an error or a valid state.

### constexpr and Compile-Time Intent

- If a function is `constexpr`, document whether it is intended to be evaluated:
  - At compile time
  - At runtime
  - In both contexts
- Document any runtime-only behavior guarded by `if !consteval`.

### Pattern Matching and Exhaustiveness

- For enums or variants intended to be exhaustively matched:
  - Explicitly state this expectation.
  - Document how new alternatives should be handled by callers.

Example:

```
@note All enum values must be handled exhaustively in pattern matching expressions.
```

### Layout and Address Guarantees

- If a member variable is marked `[[no_unique_address]]`:
  - Document that address identity and object layout are not guaranteed.

### Concepts

- If the variable is defined as a C++20 concept or constraint, document the requirements it enforces.
  - Furthermore, use `@concept name` to specify it's a concept.

Example

```cpp
/*! @concept IsIntegral
    @brief Checks whether a type is an integral type.
		@tparam T The type to test.
	*/
template <typename T>
concept IsIntegral = std::is_integral_v<T>;
```

### Overloads

- For overloaded functions, use `@overload <function declaration>` to link them.

Example

```cpp
/*! @brief Processes a list of integers.
    @param[in] data The data to process.
    @param[in] startIndex The starting index within the list.
 */
void processData(const std::list<int>& data, const int startIndex);

/*! @overload void processData(const std::vector<int>& data)
    @brief Processes a vector of integers.
    @param[in] data The data to process.
 */
void processData(const std::vector<int>& data);
```

### Style Guidelines

1.  **Clarity over brevity**
    - Prefer: “Computes the SHA-256 digest of the given buffer.”
    - Avoid: “Computes hash.”

2.  **Avoid redundancy**
    - Don’t just repeat the parameter name.
    - Bad: `@param` size The size.
    - Good: `@param` size Number of bytes to read from the buffer.

3.  **Mention units & ranges**
    - E.g.: `@param` timeoutMs Timeout in milliseconds. Must be >= 0.

4.  **Error behavior**
    - If the function can fail or return error codes, describe how.
    - Note: “Returns -1 on failure; check errno for details.”

5.  **Thread-safety & ownership (if relevant)**
    - Mention whether functions/classes are thread-safe, reentrant, or own resources.

6.  **Consistent terminology**
    - Use the same terms throughout the codebase for similar concepts.
    - Prefer **standard library terminology** over project-specific synonyms when applicable (e.g., range, view, iterator, sentinel, borrowed).

7.  **Keep it professional**
    - Avoid slang, jokes, or informal language.

8.  **Update comments when code changes**
    - Ensure comments remain accurate and relevant as the code evolves.

9.  **Use one of the following phrases verbatim when applicable:**
    - “Thread-safe”
    - “Not thread-safe”
    - “Thread-safe for concurrent reads, not for concurrent writes”

10. **If synchronization is required, explicitly state who is responsible (caller vs callee).**

11. **For overridden methods, briefly mention the base class behavior if it differs significantly.**

12. **Do not generate comments that restate obvious behavior for:**
    - Simple getters/setters
    - Trivial constructors/destructors
    - Operators that directly map to standard behavior (e.g., assignment, equality)
    - In such cases, use a brief `@brief` only, unless additional semantics exist.

13. **Mentioning another entity (function/class/struct/variable/etc) in the code base**
    - Do NOT use `<related entity>`
    - Use `@ref <related entity>` to link them as that will create a hyperlink in the generated documentation.

### How to Apply These Instructions

When I ask you to:

- “Add comments/documentation to this function/class/struct”
- “Generate Doxygen comments for this code”
- “Document this header in Doxygen format”

**You must:**

- **Inspect the signature and context** (parameters, return type, template params).
- **Generate a Doxygen comment block** immediately above the entity.
- Use the appropriate tags: `@file`, `@brief`, `@param`, `@return`, `@tparam`, `@throws`, `@return`, `@note`, etc.
- Ensure the comments are correct, specific, and technically meaningful, not generic placeholders.
- If information is ambiguous or missing, make reasonable assumptions but keep them realistic and conservative.
