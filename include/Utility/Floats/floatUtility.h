/*! @file floatUtility.h
	@brief Contains the function declarations for utility functions related to floating-point numbers.
	@date --/--/----
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_FLOATUTILITY_H
#define INCLUDE_FLOATUTILITY_H

#include <algorithm>
#include <cmath>

#include "attributeMacros.h"

/*! @namespace Utility::Floats
	@brief Helper utilities and constants for robust floating-point comparisons.
	@details This namespace provides lightweight, constexpr helpers intended for safe
	comparison of floating-point values using both absolute and relative tolerances.
	The utilities are header-only, constexpr where possible, and suitable for use
	in both compile-time and runtime contexts. They are intended for domain-level
	numeric comparisons (e.g., validating algorithm results) where exact equality
	is inappropriate due to rounding error.
	@note Constants represent conservative defaults; callers may override them by passing
	explicit epsilon values to the functions.
 */
namespace Utility::Floats
{
	/*! @brief Small absolute tolerance used when comparing values near zero.
		@details Use this epsilon when the magnitudes of values under comparison are
		close to zero and relative comparisons become unstable. The value is intentionally
		tiny to avoid masking meaningful differences for typical double-precision calculations.
		@note This is a reasonable default but may be tightened or relaxed depending on the
		numerical domain. Callers may provide an explicit `absEpsilon` to `approximatelyEqualAbsRel`.
	 */
	constexpr double ABS_EPSILON{1e-12};

	/*! @brief Relative tolerance used for scale-aware comparisons of non-zero values.
		@details When two values are sufficiently far from zero, this relative epsilon is
		multiplied by the larger magnitude to produce a scale-appropriate tolerance. This
		prevents false negatives when comparing large-magnitude values where absolute
		tolerances would be ineffective.
	 */
	constexpr double REL_EPSILON{1e-8};

	/*! @brief Compare two double-precision values using combined absolute and relative tolerances.
		@details The comparison first checks whether the absolute difference between `lhs` and `rhs`
		is within `absEpsilon` to handle values near zero. If not, it falls back to a relative
		comparison of the form: |lhs - rhs| <= max(|lhs|, |rhs|) * relEpsilon, which scales the allowed
		tolerance with the magnitude of the inputs. This implementation follows Knuth's recommended
		approach for robust floating-point comparison.
		@param[in] lhs Left-hand operand to compare.
		@param[in] rhs Right-hand operand to compare.
		@param[in] absEpsilon Absolute tolerance to use for near-zero comparisons. Defaults to `ABS_EPSILON`.
		@param[in] relEpsilon Relative tolerance factor applied to the larger magnitude of the operands. Defaults to `REL_EPSILON`.
		@return `true` if the values are considered equal within the provided tolerances; otherwise `false`.
		@note The function is `constexpr` and `noexcept`, suitable for compile-time evaluation when used with constexpr values.
	*/
	ATTR_NODISCARD constexpr bool approximatelyEqualAbsRel(const double lhs, const double rhs, const double absEpsilon = ABS_EPSILON,
														   const double relEpsilon = REL_EPSILON) noexcept
	{
		// Check if the numbers are really close -- needed when comparing numbers near zero
		if (std::abs(lhs - rhs) <= absEpsilon)
		{
			return true;
		}

		// Otherwise fall back to Knuth's algorithm
		return std::abs(lhs - rhs) <= (std::max(std::abs(lhs), std::abs(rhs)) * relEpsilon);
	}
} // namespace Utility::Floats

#endif