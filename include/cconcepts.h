/*! @file cconcepts.h
	@brief Contains the declarations of common concepts that might be used in multiple files.
	@date --/--/----
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef BASE_INCLUDE_CCONCEPTS_H
#define BASE_INCLUDE_CCONCEPTS_H

#include <string>
#include <type_traits>

/*! @namespace Concepts
	@brief Collection of common compile-time type concepts used across the codebase.
	@details This namespace provides small, expressive concepts built on top of the standard
	type-traits library. Use these concepts to constrain template parameters for integral,
	unsigned, signed, floating-point, rational (integral or floating), and std::string-like types.
	@note All concepts are compile-time predicates with no runtime cost.
 */
namespace Concepts
{
	/*! @concept Integral
		@brief Tests whether a type is an integral type.
		@tparam T The type to test. Typical examples: `int`, `long`, `char`.
	*/
	template <typename T>
	concept Integral = std::is_integral_v<T>;

	/*! @concept UnsignedIntegral
		@brief Tests whether a type is an unsigned integral type.
		@tparam T The type to test. Typical examples: `unsigned int`, `std::uint32_t`.
	*/
	template <typename T>
	concept UnsignedIntegral = std::is_unsigned_v<T>;

	/*! @concept SignedIntegral
		@brief Tests whether a type is a signed integral type.
		@tparam T The type to test. Typical examples: `int`, `long`.
	*/
	template <typename T>
	concept SignedIntegral = std::is_signed_v<T>;

	/*! @concept FloatingPoint
		@brief Tests whether a type is a floating-point type.
		@tparam T The type to test. Typical examples: `float`, `double`, `long double`.
	*/
	template <typename T>
	concept FloatingPoint = std::is_floating_point_v<T>;

	/*! @concept RationalNumber
		@brief Tests whether a type represents a rational number (integral or floating-point).
		@details This concept is satisfied if `T` is either an `Integral` or a `FloatingPoint`.
		Use this to constrain templates that accept any numeric type that models rational numbers.
		@tparam T The type to test. Accepts both integral and floating-point types.
	*/
	template <typename T>
	concept RationalNumber = Integral<T> || FloatingPoint<T>;

	/*! @concept String
		@brief Tests whether a type is `std::string` (ignores cv/ref qualifiers).
		@details This concept compares the decayed, cv-ref-removed form of `T` with `std::string`.
		It is satisfied for `std::string`, `const std::string&`, `std::string&&`, etc.
		@tparam T The type to test. Use this to constrain templates that require `std::string` specifically.
		@note This concept does not accept C-style strings (e.g., `const char*`).
	*/
	template <typename T>
	concept String = std::is_same_v<std::string, std::remove_cvref_t<T>>;
} // namespace Concepts

#endif