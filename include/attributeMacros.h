/*! \file attributeMacros.h
	\brief Contains the attribute macros for creating portable C++ code
	\date --/--/----
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ATTRIBUTEMACROS_H
#define INCLUDE_ATTRIBUTEMACROS_H

#ifdef __clang__
	/*! @def ATTR_CLANG
		@brief Defined when compiling with the Clang/LLVM compiler.
		@details This macro is set to `1` when the compiler predefined macro `__clang__` is present.
		Use it to conditionally enable Clang-specific features or attributes.
		@note This macro is internal to the attribute macros helper and is not intended as a stable public API.
	 */
	#define ATTR_CLANG
#elifdef __GNUC__
	/*! @def ATTR_GCC
		@brief Defined when compiling with GCC.
		@details This macro is set to `1` when the compiler predefined macro `__GNUC__` is present.
		Use it to enable GCC-specific attributes or workarounds.
		@note Do not assume exact GCC version from this macro; check `__GNUC__`/`__GNUC_MINOR__` when needed.
	 */
	#define ATTR_GCC
#elifdef _MSC_VER
	/*! @def ATTR_MSVC
		@brief Defined when compiling with Microsoft Visual C++.
		@details This macro is set to `1` when `_MSC_VER` is defined by the compiler.
		Use it to guard MSVC-specific pragmas or attribute equivalents.
	 */
	#define ATTR_MSVC
#else
	/*! @def ATTR_UNKNOWN_COMPILER
		@brief Defined when the compiler could not be identified by known macros.
		@details Acts as a fallback indicator for unknown or unsupported compilers. When this macro
		is set, attribute macros should resolve to safe defaults (typically empty macros).
	 */
	#define ATTR_UNKNOWN_COMPILER
#endif

// Portable attribute macro: use GCC/Clang attribute when available, otherwise empty.

#if defined(ATTR_CLANG) || defined(ATTR_GCC)
	/*! @def ATTR_CONST
		@brief Portable macro for the compiler `const` attribute.
		@details Expands to `__attribute__((const))` on Clang and GCC, and to an empty token on other compilers.
		The `const` attribute conveys that a function has no side effects and its return value depends only on
		its arguments (not on global state). Use this macro to annotate library functions where this guarantee holds.
		@warning Misusing the attribute (annotating a function that reads global state or has side effects) can
		produce incorrect code generation.
		@example
		@code{.cpp}
		ATTR_CONST int pure_function(int x) { return x * 2; }
		@endcode
	*/
	#define ATTR_CONST __attribute__((const))
#else
	#define ATTR_CONST
#endif

#if __has_cpp_attribute(nodiscard)
	/*! @def ATTR_NODISCARD
		@brief Portable macro for the C++ `[[nodiscard]]` attribute.
		@details Expands to `[[nodiscard]]` when supported by the compiler, otherwise to an empty token.
		The `[[nodiscard]]` attribute indicates that the return value of a function should not be ignored.
		Use this macro to annotate functions where ignoring the return value is likely a bug.
		@example
		@code{.cpp}
		ATTR_NODISCARD int compute_value() { return 42; }
		@endcode
	*/
	#define ATTR_NODISCARD [[nodiscard]]
#else
	#define ATTR_NODISCARD
#endif

#if __has_cpp_attribute(maybe_unused)
	/*! @def ATTR_MAYBE_UNUSED
		@brief Portable macro for the C++ `[[maybe_unused]]` attribute.
		@details Expands to `[[maybe_unused]]` when supported by the compiler, otherwise to an empty token.
		The `[[maybe_unused]]` attribute indicates that the a parameter may be unused.
		@example
		@code{.cpp}
		int compute_value(ATTR_MAYBE_UNUSED number) { return 42; }
		@endcode
	*/
	#define ATTR_MAYBE_UNUSED [[maybe_unused]]
#else
	#define ATTR_MAYBE_UNUSED
#endif

#endif
