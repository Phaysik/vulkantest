/*! \file attributeMacros.h
	\brief Contains the attribute macros for creating portable C++ code
	\date 02/12/2026
	\version 0.0.1
	\since 0.0.1
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

	/*! @def ATTR_PURE
		@brief Portable macro for the C++ `[[pure]]` attribute.
		@details Expands to `[[pure]]` when supported by the compiler, otherwise to an empty token.
		The `[[pure]]` attribute indicates that a function has no side effects and its return value depends only on
		its arguments (not on global state). Use this macro to annotate library functions where this guarantee holds.
		@warning Misusing the attribute (annotating a function that reads global state or has side effects) can
		produce incorrect code generation.
		@example
		@code{.cpp}
		ATTR_PURE int compute_value(int x) { return x * 2; }
		@endcode
	*/
	#define ATTR_PURE __attribute__((pure))
#else
	#define ATTR_CONST
	#define ATTR_PURE
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
		int compute_value(ATTR_MAYBE_UNUSED int number) { return number; }
		@endcode
	*/
	#define ATTR_MAYBE_UNUSED [[maybe_unused]]
#else
	#define ATTR_MAYBE_UNUSED
#endif

#if __has_cpp_attribute(deprecated)
	/*! @def ATTR_DEPRECATED
		@brief Portable macro for the C++ `[[deprecated]]` attribute.
		@details Expands to `[[deprecated]]` when supported by the compiler, otherwise to an empty token.
		The `[[deprecated]]` attribute indicates that a function is deprecated and should not be used.
		@example
		@code{.cpp}
		ATTR_DEPRECATED int compute_value(int number) { return number; }
		@endcode
	*/
	#define ATTR_DEPRECATED [[deprecated]]
#else
	#define ATTR_DEPRECATED
#endif

#if __has_cpp_attribute(noreturn)
	/*! @def ATTR_NORETURN
		@brief Portable macro for the C++ `[[noreturn]]` attribute.
		@details Expands to `[[noreturn]]` when supported by the compiler, otherwise to an empty token.
		The `[[noreturn]]` attribute indicates that a function will not return control flow to the calling function after it finishes.
		@example
		@code{.cpp}
		ATTR_NORETURN int compute_value(int number) { return number; }
		@endcode
	*/
	#define ATTR_NORETURN [[noreturn]]
#else
	#define ATTR_NORETURN
#endif

#if __has_cpp_attribute(fallthrough)
	/*! @def ATTR_FALLTHROUGH
		@brief Portable macro for the C++ `[[fallthrough]]` attribute.
		@details Expands to `[[fallthrough]]` when supported by the compiler, otherwise to an empty token.
		The `[[fallthrough]]` attribute indicates that the fall through from the previous case label is intentional and should not be
	   diagnosed.
		@example
		@code{.cpp}
		int compute_value(int number) {
			switch (number) {
				case 1:
				case 2:
					foo();
					ATTR_FALLTHROUGH;
				case 3: // No warning on fallthrough
					bar();
				default:
					break;
			}
		}
		@endcode
	*/
	#define ATTR_FALLTHROUGH [[fallthrough]]
#else
	#define ATTR_FALLTHROUGH
#endif

#if __has_cpp_attribute(likely)
	/*! @def ATTR_LIKELY
		@brief Portable macro for the C++ `[[likely]]` attribute.
		@details Expands to `[[likely]]` when supported by the compiler, otherwise to an empty token.
		The `[[likely]]` attribute indicates that the a code path is more likely to be executed than the others.
		@example
		@code{.cpp}
		constexpr double pow(doulbe x, uint64_t n) noexcept {
			if (n > 0) ATTR_LIKELY
				return x * pow(x, n - 1);
			else ATTR_UNLIKELY
				return 1;
		}
		@endcode
	*/
	#define ATTR_LIKELY [[likely]]
#else
	#define ATTR_LIKELY
#endif

#if __has_cpp_attribute(unlikely)
	/*! @def ATTR_UNLIKELY
		@brief Portable macro for the C++ `[[unlikely]]` attribute.
		@details Expands to `[[unlikely]]` when supported by the compiler, otherwise to an empty token.
		The `[[unlikely]]` attribute indicates that the a code path is more unlikely to be executed than the others.
		@example
		@code{.cpp}
		constexpr double pow(doulbe x, uint64_t n) noexcept {
			if (n > 0) ATTR_UNLIKELY
				return x * pow(x, n - 1);
			else ATTR_UNUNLIKELY
				return 1;
		}
		@endcode
	*/
	#define ATTR_UNLIKELY [[unlikely]]
#else
	#define ATTR_UNLIKELY
#endif

#endif
