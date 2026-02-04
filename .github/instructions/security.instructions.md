---
applyTo: '**/*.{h,hpp,cpp}'
---

## Goal

When generating or modifying code, prioritize **safety, robustness, and defensive design**.

Assume inputs may be:

- Malformed
- Malicious
- Unexpected
- Untrusted

Security and correctness take precedence over convenience.

## General Security Rules

1. **Fail safely**
   - Detect invalid input early
   - Reject invalid states explicitly
   - Prefer clear error signaling over silent failure

2. **Avoid undefined behavior**
   - Do not rely on unspecified or undefined behavior
   - Validate assumptions before use
   - Prefer safe standard library abstractions

3. **Trust boundaries must be explicit**
   - Clearly document where data becomes trusted
   - Validate all data crossing trust boundaries

4. **Minimize attack surface**
   - Limit exposed interfaces
   - Avoid unnecessary features or complexity
5. **Keep dependencies secure**
   - Use well-maintained libraries
   - Regularly update dependencies
   - Avoid deprecated or insecure APIs

6. **Least privilege principle**
   - Limit access rights for code modules
   - Avoid global mutable state

7. **Do NOT constrain yourself to only 80 characters per line. You may use up to 180 characters per line to improve code clarity and maintainability.**

## Input Validation

- Validate all external inputs:
  - File contents
  - Network data
  - User input
  - Environment variables
- Check:
  - Lengths
  - Ranges
  - Encodings
  - Nullability
- Reject inputs that violate documented constraints

## Integer Safety

- Guard against:
  - Integer overflow
  - Integer underflow
  - Signed/unsigned conversion errors
- Validate before arithmetic that may overflow
- Prefer explicit casts over implicit conversions
- Avoid narrowing conversions

## Memory Safety

- Avoid raw pointers unless ownership is explicit and documented
- Prefer RAII for resource management
- Avoid manual memory management where possible
- Never access memory out of bounds
- Do not assume null-termination unless guaranteed

## String & Buffer Handling

- Avoid C-style string manipulation unless required
- Prefer:
  - `std::string_view`
  - `std::span`
- Always validate buffer sizes before access
- Avoid unchecked indexing

## Error Handling

- Do not ignore error codes or return values
- Use `[[nodiscard]]` on functions returning error information
- Prefer explicit error propagation over implicit global state

## Cryptography & Sensitive Operations (If Applicable)

- Do not invent cryptographic algorithms
- Use established, well-reviewed libraries
- Avoid hard-coded secrets
- Avoid insecure comparisons; use constant-time comparison where required
- Do not log sensitive data

## Concurrency & Thread Safety

- Avoid data races
- Document thread-safety guarantees explicitly
- Protect shared mutable state
- Avoid deadlocks by consistent lock ordering

## Filesystem & OS Interaction

- Validate file paths and permissions
- Avoid TOCTOU (time-of-check/time-of-use) issues
- Handle filesystem errors explicitly
- Do not assume filesystem state remains unchanged

## Defensive Defaults

- Prefer secure defaults
- Disable optional or dangerous behavior unless explicitly enabled
- Prefer explicit configuration over implicit behavior

## Contracts & Assertions

- Use runtime validation for untrusted input
- Use assertions only for internal invariants
- Do not rely on assertions for security checks in production builds

## Logging & Diagnostics

- Avoid leaking sensitive information in logs
- Sanitize data before logging
- Avoid verbose error messages for untrusted contexts

## Non-Goals

- Do NOT weaken validation for convenience
- Do NOT silence warnings without justification
- Do NOT trade security for micro-optimizations
