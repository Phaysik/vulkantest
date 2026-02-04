/*! \file overflowProtection.h
	\brief Contains the function declarations for creating and guarding against overflow with integral values
	\date --/--/----
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_OVERFLOWPROTECTION_H
#define INCLUDE_OVERFLOWPROTECTION_H

#include <limits>

#include "attributeMacros.h"
#include "cconcepts.h"

/*! @namespace Utility::OverflowProtection
	@brief Utilities for detecting and guarding against unsigned integer overflow.
	@details Provides small, constexpr helpers to check for multiplication overflow and to perform
	saturating multiplication when overflow would occur. These helpers are intended for use with
	unsigned integral types and are constexpr so they can be evaluated at compile time when possible.
	@note All functions are `noexcept` and return conservative values on overflow (e.g., `std::numeric_limits<Number>::max()`).
 */
namespace Utility::OverflowProtection
{

	template <Concepts::UnsignedIntegral Number>
	/*! @brief Check if multiplication of two unsigned values will overflow.
		@details Returns `true` if `num1 * num2` would be greater than
		`std::numeric_limits<Number>::max()`; otherwise returns `false`.
		@tparam Number Unsigned integral type for the operands. Must satisfy @ref Concepts::UnsignedIntegral.
		@param[in] num1 The first multiplicand.
		@param[in] num2 The second multiplicand.
		@return `true` when multiplication would overflow, `false` otherwise.
	*/
	ATTR_NODISCARD constexpr bool WillMultiplyOverflow(const Number num1, const Number num2) noexcept
	{
		if (num1 == 0 || num2 == 0)
		{
			return false;
		}
		using Limits = std::numeric_limits<Number>;
		return num1 > static_cast<Number>(Limits::max() / num2);
	}

	template <Concepts::UnsignedIntegral Number>
	/*! @brief Multiply two unsigned integers, saturating on overflow.
		@details Performs multiplication of `num1` and `num2`. If the multiplication
		would overflow the representable range of `Number`, the function returns
		`std::numeric_limits<Number>::max()` as a conservative saturated result.
		@tparam Number Unsigned integral type for the operands. Must satisfy @ref Concepts::UnsignedIntegral.
		@param[in] num1 The first multiplicand.
		@param[in] num2 The second multiplicand.
		@return The product `num1 * num2` when no overflow occurs; otherwise
		`std::numeric_limits<Number>::max()`.
		@note This function is `constexpr` and `noexcept` and uses @ref WillMultiplyOverflow() to detect overflow.
	*/
	ATTR_NODISCARD constexpr Number SafeMultiply(const Number num1, const Number num2) noexcept
	{
		return WillMultiplyOverflow<Number>(num1, num2) ? std::numeric_limits<Number>::max() : static_cast<Number>(num1 * num2);
	}
} // namespace Utility::OverflowProtection

#endif