/*! \file attributeMacros.h
	\brief Contains the attribute macros for creating portable C++ code
	\date 02/12/2026
	\version 0.0.1
	\since 0.0.1
	\author Matthew Moore
*/

#ifndef INCLUDE_ATTRIBUTEMACROS_H
#define INCLUDE_ATTRIBUTEMACROS_H

#ifdef __GNUC__
	/*! @def ATTR_GCC
		@brief Defined when compiling with GCC.
		@details This macro defined when the compiler predefined macro `__GNUC__` is present.
		Use it to enable GCC-specific attributes or workarounds.
		@note Do not assume exact GCC version from this macro; check `__GNUC__`/`__GNUC_MINOR__` when needed.
	*/
	#define ATTR_GCC
#endif

#ifdef __clang__
	/*! @def ATTR_CLANG
		@brief Defined when compiling with the Clang/LLVM compiler.
		@details This macro defined when the compiler predefined macro `__clang__` is present. Use it to conditionally enable Clang-specific
	   features or attributes.
		@note This macro is internal to the attribute macros helper and is not intended as a stable public API.
	*/
	#define ATTR_CLANG
#endif

#ifdef _MSC_VER
	/*! @def ATTR_MSVC
		@brief Defined when compiling with Microsoft Visual C++.
		@details This macro defined when `_MSC_VER` is defined by the compiler.
		Use it to guard MSVC-specific pragmas or attribute equivalents.
	*/
	#define ATTR_MSVC
#endif

// Portable attribute macro: use GCC/Clang attribute when available, otherwise empty.

#if defined(ATTR_GCC) || defined(ATTR_CLANG)
	#if __has_attribute(const)
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

	#if __has_attribute(pure)
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
		#define ATTR_PURE
	#endif

	#if __has_attribute(returns_nonnull)

		/*! @def ATTR_RETURNS_NONNULL
			@brief Portable macro for the compiler `returns_nonnull` attribute.
			@details Expands to `__attribute__((returns_nonnull))` on Clang and GCC, and to an empty token on other compilers. The
		   `returns_nonnull` attribute indicates that a function never returns a null pointer. Use this macro to annotate functions that
		   guarantee a non-null return value.
			@warning Misusing the attribute (annotating a function that may return null) can produce undefined behavior.
			@example
			@code{.cpp}
			ATTR_RETURNS_NONNULL int* get_value() { static int x = 42; return &x; }
			@endcode
		*/
		#define ATTR_RETURNS_NONNULL __attribute__((returns_nonnull))
	#else
		#define ATTR_RETURNS_NONNULL
	#endif

	#if __has_attribute(flag_enum)
		/*! @def ATTR_FLAG_ENUM
			@brief Portable macro for the compiler `flag_enum` attribute.
			@details Expands to `__attribute__((flag_enum))` on Clang and GCC, and to an empty token on other compilers. The `flag_enum`
		   attribute indicates that an enumeration is intended to be used as a bitmask (i.e., its values are powers of two). Use this
		   macro to annotate enum types that represent flags.
			@warning Misusing the attribute (annotating an enum that is not a bitmask) can produce incorrect code generation.
			@example
			@code{.cpp}
			enum class ATTR_FLAG_ENUM MyFlags {
				FlagA,
				FlagB,
				FlagC
			};
			@endcode
		*/
		#define ATTR_FLAG_ENUM __attribute__((flag_enum))
	#else
		#define ATTR_FLAG_ENUM
	#endif

	#if __has_attribute(always_inline)
		/*! @def ATTR_ALWAYS_INLINE
			@brief Portable macro for the compiler `always_inline` attribute.
			@details Expands to `__attribute__((always_inline))` on Clang and GCC, and to an empty token on other compilers.
			The `always_inline` attribute requests that the compiler inline a function at all call sites where it is used. It is
			a strong hint to the optimizer and may be ignored by some toolchains or in builds where inlining is inhibited. Use this
			macro for small wrapper or forwarding functions where guaranteeing inlining improves performance or eliminates overhead.
			@warning Overuse can increase code size and may prevent useful compiler optimizations. Do not apply to large functions
			or when the function's address is taken, unless inlining is truly required.
			@example
			@code{.cpp}
			ATTR_ALWAYS_INLINE static inline int fast_mul2(int x) { return x * 2; }
			@endcode
		*/
		#define ATTR_ALWAYS_INLINE __attribute__((always_inline))
	#else
		#define ATTR_ALWAYS_INLINE
	#endif

	#if __has_attribute(artificial)
		/*! @def ATTR_ARTIFICIAL
			@brief Portable macro for the compiler `artificial` attribute.
			@details Expands to `__attribute__((artificial))` on Clang and GCC, and to an empty token on other compilers.
			The `artificial` attribute marks a function or variable as being compiler- or tool-generated ("artificial"). When present,
			debuggers and diagnostic tools may treat the entity as non-user code (for example, by suppressing certain warnings,
			omitting frames from stack traces, or adjusting debug information).
			Use this macro for thin wrapper functions, thunks, or compiler-generated helpers where treating the symbol as artificial
			improves debug clarity.
			@warning Behavior and debugger handling are implementation-defined. Do not rely on this attribute for program semantics
			or to silence real errors; use it only to improve diagnostics and debug experience.
			@example
			@code{.cpp}
			ATTR_ARTIFICIAL static inline void wrapper_for_debug() { helper(); }
			@endcode
		*/
		#define ATTR_ARTIFICIAL __attribute__((artificial))
	#else
		#define ATTR_ARTIFICIAL
	#endif

	#if __has_attribute(assume_aligned)
		/*! @def ATTR_ASSUME_ALIGNED
			@brief Portable macro for supplying an `assume_aligned` annotation.
			@details When supported by the compiler, use `ATTR_ASSUME_ALIGNED(alignment)` to indicate that a pointer or object
			is assumed to be aligned to `alignment` bytes. When an explicit byte `offset` is required, the helper macro
			`ATTR_ASSUME_ALIGNED_EX(alignment, offset)` is provided. On compilers that support the attribute this expands to
			`__attribute__((assume_aligned(alignment, offset)))` (or the single-argument form when using `ATTR_ASSUME_ALIGNED`).
			This annotation allows the optimizer to assume stronger alignment, which can enable vectorized loads/stores and other
			optimizations.
			@warning Incorrect alignment annotations can lead to undefined behavior if the actual pointer/object is less aligned
			than claimed. Only apply when you can guarantee the runtime alignment.
			@example
			@code{.cpp}
			int * ATTR_ASSUME_ALIGNED(16) ptr = get_aligned_ptr(); // assume ptr is 16-byte aligned
			// when an offset is required:
			int * ATTR_ASSUME_ALIGNED_EX(16, 8) ptr2 = ...; // assume ptr2 + 8 is 16-byte aligned
			@endcode
		*/
		#define ATTR_ASSUME_ALIGNED_EX(alignment, offset) __attribute__((assume_aligned(alignment, offset)))
		#define ATTR_ASSUME_ALIGNED(alignment)			  ATTR_ASSUME_ALIGNED_EX(alignment, 0)
	#else
		#define ATTR_ASSUME_ALIGNED_EX(alignment, offset)
		#define ATTR_ASSUME_ALIGNED(alignment)
	#endif

	#if __has_attribute(cold)
		/*! @def ATTR_COLD
			@brief Portable macro for the compiler `cold` attribute.
			@details Expands to `__attribute__((cold))` on Clang and GCC, and to an empty token on other compilers.
			The `cold` attribute indicates that a function or code path is unlikely to be executed. Compilers may use this hint to
			optimize for code size and locality of hot paths (for example, placing cold functions out-of-line or reducing inlining
			pressure in hot paths). Use this macro for error handlers, rare cleanup paths, diagnostics, and other code that is
			rarely executed.
			@warning Marking hot or performance-sensitive functions as cold can degrade performance. Use only when the rarity of
			execution is well-understood.
			@example
			@code{.cpp}
			ATTR_COLD void handle_error(int code) { // infrequent error path  }
			@endcode
		*/
		#define ATTR_COLD __attribute__((cold))
	#else
		#define ATTR_COLD
	#endif

	#if __has_attribute(hot)
		/*! @def ATTR_HOT
			@brief Portable macro for the compiler `hot` attribute.
			@details Expands to `__attribute__((hot))` on Clang and GCC, and to an empty token on other compilers.
			The `hot` attribute indicates that a function or code path is expected to be executed frequently. Compilers may use this
			hint to optimize for throughput and locality (for example, favoring inlining or placing the function in a hot section).
			Use this macro for small, performance-critical functions or tight inner loops where the execution frequency is high and
			profiling shows benefit.
			@warning Overusing `ATTR_HOT` can increase code size and hurt instruction-cache behavior. Apply only to proven hot
			paths backed by profiling data.
			@example
			@code{.cpp}
			ATTR_HOT inline int inner_compute(int x) { return x * 2; }
			@endcode
		*/
		#define ATTR_HOT __attribute__((hot))
	#else
		#define ATTR_HOT
	#endif

	#if __has_attribute(nonnull)
		/*! @def ATTR_NONNULL
			@brief Portable macro for the compiler `nonnull` attribute.
			@details Expands to `__attribute__((nonnull))` or `__attribute__((nonnull(N1, N2, ...)))` on Clang and GCC, and to an empty
			token on compilers that do not support the attribute. The `nonnull` attribute indicates that one or more function
			pointer parameters must not be null; using it lets the compiler emit warnings or perform optimizations.
			@warning Misusing the attribute (annotating parameters that may be null) can lead to incorrect diagnostics or
			unsound optimizations.
			@example
			@code{.cpp}
			// Both parameters must be non-null (indices are 1-based):
			int copy_strings(const char *src, char *dst) ATTR_NONNULL(1, 2);
			@endcode
		*/
		#define ATTR_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
	#else
		#define ATTR_NONNULL(...)
	#endif

	#if __has_attribute(noinline)
		/*! @def ATTR_NOINLINE
			@brief Portable macro for the compiler `noinline` attribute.
			@details Expands to `__attribute__((noinline))` on Clang and GCC, and to an empty token on other compilers.
			The `noinline` attribute prevents the compiler from inlining a function at its call sites. This is useful when inlining
			a function would cause excessive code-size growth (e.g. exceeding `--param inline-unit-growth`) or when the function
			should remain a discrete call for profiling or debugging purposes. The attribute is compatible with `constexpr`;
			compile-time evaluation is unaffected.
			@warning Preventing inlining of small, hot functions can degrade performance. Apply only when inlining is known to
			cause problems (e.g. build failures due to inline-unit-growth limits or measurable code-size bloat).
			@example
			@code{.cpp}
			ATTR_NOINLINE void expensive_path() { // large function body }
			@endcode
		*/
		#define ATTR_NOINLINE __attribute__((noinline))
	#else
		#define ATTR_NOINLINE
	#endif
#endif

#ifdef ATTR_CLANG
	#if __has_attribute(enum_extensibility)
		/*! @def ATTR_ENUM_EXTENSIBILITY_OPEN
			@brief Portable macro for the Clang `enum_extensibility(open)` attribute.
			@details Expands to `__attribute__((enum_extensibility(open)))` on Clang, and to an empty token on other compilers. The
		   `enum_extensibility(open)` attribute indicates that an enumeration can take any values allowed by the standard and instructs
		   clang to be more lenient when issuing warnings.
			@warning Misusing the attribute (annotating an enum that is not intended to be extensible) can produce incorrect code
		   generation.
			@example
			@code{.cpp}
			enum ATTR_ENUM_EXTENSIBILITY_OPEN MyEnum {
				ValueA,
				ValueB,
				ValueC
			};

			enum ATTR_ENUM_EXTENSIBILITY_OPEN ATTR_FLAG_ENUM OpenFlagEnum {
				D0 = 1 << 0, D1 = 1 << 1
			};

			enum OpenEnum oe;
			enum OpenFlagEnum ofe;

			oe = B1;           // no warnings
			oe = 100;          // no warnings

			ofe = D0 | D1;     // no warnings
			ofe = D0 | D1 | 4; // no warnings
			@endcode
		*/
		#define ATTR_ENUM_EXTENSIBILITY_OPEN __attribute__((enum_extensibility(open)))
	#else
		#define ATTR_ENUM_EXTENSIBILITY_OPEN
	#endif

	#if __has_attribute(enum_extensibility)
		/*! @def ATTR_ENUM_EXTENSIBILITY_CLOSED
			@brief Portable macro for the Clang `enum_extensibility(closed)` attribute.
			@details Expands to `__attribute__((enum_extensibility(closed)))` on Clang, and to an empty token on other compilers. The
		   `enum_extensibility(closed)` attribute indicates that an enumeration takes a value that corresponds to one of the enumerators
		   listed in the enum definition, or when the enum is also annotated with @ref ATTR_FLAG_ENUM, a value that can be constructed
		   using values corresponding to the enumerators.
			@warning Misusing the attribute (annotating an enum that is not intended to be extensible) can produce incorrect code
		   generation.
			@example
			@code{.cpp}
			enum ATTR_ENUM_EXTENSIBILITY_CLOSED MyEnum {
				ValueA,
				ValueB,
				ValueC
			};

			enum ATTR_ENUM_EXTENSIBILITY_CLOSED ATTR_FLAG_ENUM OpenFlagEnum {
				C0 = 1 << 0, C1 = 1 << 1
			};

			enum ClosedEnum oe;
			enum ClosedFlagEnum cfe;

			oe = B1;           // no warnings
			oe = 100;          // no warnings

			cfe = C0 | C1;     // no warnings
			cfe = C0 | C1 | 4; // warning issued
			@endcode
		*/
		#define ATTR_ENUM_EXTENSIBILITY_CLOSED __attribute__((enum_extensibility(closed)))
	#else
		#define ATTR_ENUM_EXTENSIBILITY_CLOSED
	#endif

	#if __has_attribute(using_if_exists)
		/*! @def ATTR_USING_IF_EXISTS
			@brief Portable macro for the Clang `using_if_exists` attribute.
			@details Expands to `__attribute__((using_if_exists))` on Clang, and to an empty token on other compilers. The `using_if_exists`
		   attribute allows a using declaration to refer to a name that may not exist without causing an error. Use this macro to
		   conditionally import names that may not be present in all versions of a library or API.
			@warning Misusing the attribute (annotating a using declaration that must refer to an existing name) can produce incorrect code
		   generation.
			@example
			@code{.cpp}
			namespace LibraryV1 {
				struct TypeA {};
			}

			namespace LibraryV2 {
				struct TypeA {};
				struct TypeB {};
			}

			using namespace LibraryV1;
			using namespace LibraryV2;

			using TypeB ATTR_USING_IF_EXISTS; // no error if TypeB doesn't exist

			TypeA a; // OK
			TypeB b; // OK if TypeB exists, otherwise compile error
			@endcode
		*/
		#define ATTR_USING_IF_EXISTS __attribute__((using_if_exists))
	#else
		#define ATTR_USING_IF_EXISTS
	#endif

	#if __has_attribute(noderef)
		/*! @def ATTR_NODEREF
			@brief Portable macro for the Clang `noderef` attribute.
			@details Expands to `__attribute__((noderef))` on Clang, and to an empty token on other compilers. The `noderef`
			attribute is used to mark pointer-typed declarations whose dereference should be diagnosed by the compiler. When a
			pointer type is annotated with `noderef`, attempts to dereference that pointer (directly or indirectly) will typically
			produce a diagnostic warning indicating the dereference is unsafe or not intended. This attribute is intended for use
			with special pointer-like types or tools that need to flag unsafe dereferences; it does not change runtime semantics.
			@warning Use `ATTR_NODEREF` only on pointer types where dereferencing is intentionally unsafe or should be diagnosed.
			Incorrectly annotating ordinary pointers may produce noisy diagnostics.
			@example
			@code{.cpp}
			int __attribute__((noderef)) *p;
			int x = *p;  // warning

			int __attribute__((noderef)) **p2;
			x = **p2;  // warning

			int * __attribute__((noderef)) *p3;
			p = *p3;  // warning

			struct S {
			int a;
			};
			struct S __attribute__((noderef)) *s;
			x = s->a;    // warning
			x = (*s).a;  // warning
			@endcode
		*/
		#define ATTR_NODEREF __attribute__((noderef))
	#else
		#define ATTR_NODEREF
	#endif

	#if __has_attribute(called_once)
		/*! @def ATTR_CALLED_ONCE
			@brief Portable macro for the Clang `called_once` attribute.
			@details Expands to `__attribute__((called_once))` on Clang, and to an empty token on other compilers. The
			`called_once` attribute applies to parameters with function-like types (function pointers or Objective-C/C blocks)
			and indicates that the callable is expected to be invoked exactly once on every execution path. Clang performs
			static checks for violations such as: the parameter is not called, is called more than once, or is not called on some
			execution paths. This attribute is analysis-only and does not affect runtime behavior.
			@warning Use `ATTR_CALLED_ONCE` only for callbacks that must be invoked exactly once; incorrect use can generate
			noisy or spurious diagnostics from static analysis.T
			@example
			@code{.cpp}
			void fooWithCallback(void (^callback)(void) __attribute__((called_once))) {
			if (somePredicate()) {
				...
				callback();
			} else {
				callback(); // OK: callback is called on every path
			}
			}

			void barWithCallback(void (^callback)(void) __attribute__((called_once))) {
			if (somePredicate()) {
				...
				callback(); // note: previous call is here
			}
			callback(); // warning: callback is called twice
			}

			void foobarWithCallback(void (^callback)(void) __attribute__((called_once))) {
			if (somePredicate()) {  // warning: callback is not called when condition is false
				...
				callback();
			}
			}
			@endcode
		*/
		#define ATTR_CALLED_ONCE __attribute__((called_once))
	#else
		#define ATTR_CALLED_ONCE
	#endif

	#if __has_cpp_attribute(clang::no_specializations)
		/*! @def ATTR_NO_SPECIALIZATIONS
			@brief Portable macro for the Clang `[[clang::no_specializations]]` attribute.
			@details Expands to `[[clang::no_specializations]]` on Clang, and to an empty token on other compilers. The
		   `[[clang::no_specializations]]` attribute indicates that a function template should not be instantiated with any template
		   arguments. Use this macro to annotate function templates that are intended to be used only with explicit specializations.
			@warning Misusing the attribute (annotating a function template that is intended to be instantiated) can produce incorrect code
		   generation.
			@example
			@code{.cpp}
			template <typename T>
			ATTR_NO_SPECIALIZATIONS void foo(T) {
				// implementation
			}

			template <>
			void foo<int>(int) {
				// specialization for int
			}

			int main() {
				foo(42); // error: no matching function for call to 'foo'
				return 0;
			}
			@endcode
		*/
		#define ATTR_NO_SPECIALIZATIONS [[clang::no_specializations]]
	#else
		#define ATTR_NO_SPECIALIZATIONS
	#endif

	#if __has_cpp_attribute(clang::preferred_type)
		/*! @def ATTR_PREFERRED_TYPE
			@brief Portable macro for the Clang `[[clang::preferred_type(T)]]` attribute.
			@details Expands to `[[clang::preferred_type(T)]]` on Clang, and to an empty token on other compilers. The
		   `[[clang::preferred_type(T)]]` attribute allows adjusting the tpye of a bit-field in debug information. This can be helpful when
		   a bit-field is intended to store an enumeration value, but has to be specified as having the enumeration's underlying type.
			@warning Misusing the attribute (annotating a function template that is intended to be instantiated) can produce incorrect code
		   generation.
			@example
			@code{.cpp}
			enum class Colors {
				Red,
				Green,
				Blue
			};

			struct S {
				ATTR_PREFERRED_TYPE(Colors) unsigned ColorVal : 2;
				ATTR_PREFERRED_TYPE(bool) unsigned UseAlternateColorSpace : 1;
			} s = { Green, false };
			@endcode
		*/
		#define ATTR_PREFERRED_TYPE(param) [[clang::preferred_type(param)]]
	#else
		#define ATTR_PREFERRED_TYPE(param)
	#endif

	#if __has_cpp_attribute(clang::reinitializes)
		/*! @def ATTR_REINITIALIZES
			@brief Portable macro for the Clang `[[clang::reinitializes]]` attribute.
			@details Expands to `[[clang::reinitializes]]` on Clang, and to an empty token on other compilers.
			The `[[clang::reinitializes]]` attribute indicates that a non-static, non-const C++ member
			function reinitializes the entire object to a known, valid state regardless of its
			prior state. Static analysis tools can use this information to treat moved-from
			objects as reinitialized after the call. This attribute is for analysis only and does
			not affect code generation.
			@warning Do not annotate member functions that only partially reinitialize an object;
			misuse may cause static analysis to miss real issues or produce misleading results.
			@example
			@code{.cpp}
			class Container {
				public:
					// Clear() restores the container to a valid empty state even if it was moved-from.
					ATTR_REINITIALIZES void Clear();
			};
			@endcode
		*/
		#define ATTR_REINITIALIZES [[clang::reinitializes]]
	#else
		#define ATTR_REINITIALIZES
	#endif
#else
	#define ATTR_ENUM_EXTENSIBILITY_OPEN
	#define ATTR_ENUM_EXTENSIBILITY_CLOSED
	#define ATTR_USING_IF_EXISTS
	#define ATTR_NODEREF
	#define ATTR_CALLED_ONCE
	#define ATTR_NO_SPECIALIZATIONS
	#define ATTR_PREFERRED_TYPE(param)
	#define ATTR_REINITIALIZES
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
		The `[[noreturn]]` attribute indicates that a function will not return control flow to the calling function after it
	   finishes.
		@example
		@code{.cpp}
		ATTR_NORETURN void terminate_program() { exit(1); }
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
		The `[[fallthrough]]` attribute indicates that the fall through from the previous case label is intentional and should not
	   be diagnosed.
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
