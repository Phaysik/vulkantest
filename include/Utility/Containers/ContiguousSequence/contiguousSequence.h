/*! \file contiguousSequence.h
	\brief Contains the function declarations for creating helper utilities for contiguous sequence containers.
	\date --/--/----
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_UTILITY_CONTAINERS_CONTIGUOUSSEQUENCE_CONTIGUOUSSEQUENCE_H
#define INCLUDE_UTILITY_CONTAINERS_CONTIGUOUSSEQUENCE_CONTIGUOUSSEQUENCE_H

#include <span>

#include "attributeMacros.h"
#include "cconcepts.h"
#include "typedefs.h"

/*! @namespace Utility::Containers::ContiguousSequence
	@brief Utilities for working with contiguous sequence containers
	@details
	This namespace provides small, efficient helper routines that operate on
	contiguous sequence containers (for example `std::array`, `std::vector`)
	by accepting a `std::span` referencing their storage. The functions are
	intentionally simple, constexpr when possible, and suitable for use in
	performance-sensitive code paths where minimal overhead is important.
	@note All functions take `std::span<const T>` and therefore do not modify
		  the underlying sequence; callers should use `std::span` conversions
		  to pass containers or raw arrays.
	@date 01/23/2026
	@version 0.0.1
	@since 0.0.1
	@author Matthew Moore
 */
namespace Utility::Containers::ContiguousSequence
{
	/*! @brief Sum `length` elements from @p sequence starting at @p startIndex.
		@details
		Computes the sum of `length` contiguous elements beginning at
		`startIndex` within @p sequence. The function performs direct index
		access using `std::span` (bounds-checked by the caller if required).
		@tparam Concepts::Integral Integral The integer type used for indices
			   and arithmetic. Must satisfy @ref Concepts::Integral.
		@param[in] sequence A read-only span containing the elements to sum.
		@param[in] startIndex The starting index within @p sequence (0-based).
		@param[in] length The number of elements to include in the sum. The
						  caller must ensure `startIndex + length <= sequence.size()`.
		@return The sum of the specified elements as an `Integral` value if the indices are valid;
				returns zero if `startIndex >= sequence.size()` or `startIndex + length > sequence.size()`.
		@note Time complexity: O(length). Space complexity: O(1).
	*/
	template <Concepts::Integral Integral>
	ATTR_NODISCARD constexpr Integral computeContiguousSequenceSum(const std::span<const Integral> &sequence, const Integral startIndex,
																   const Integral length)
	{
		if (startIndex >= sc<Integral>(sequence.size()) || (startIndex + length) > sc<Integral>(sequence.size()))
		{
			return Integral{0};
		}

		Integral sum{0};

		for (Integral i{startIndex}; i < startIndex + length; ++i)
		{
			sum += sequence.at(sc<std::size_t>(i));
		}

		return sum;
	}

	/*! @overload
		@brief Sum elements from @p startIndex to the end of @p sequence.
		@details
		Convenience overload that forwards to the three-argument overload
		(@ref computeContiguousSequenceSum(const std::span<const Integral>&, Integral, Integral)).
		See that overload for full preconditions and complexity guarantees.
		@tparam Concepts::Integral Integral The integral type used for indices
			   and arithmetic. Must satisfy @ref Concepts::Integral.
		@param[in] sequence Read-only span of elements to sum.
		@param[in] startIndex Zero-based index at which summation begins. Defaults to 0.
		@return The sum of elements from `startIndex` to the end as an
				`Integral` value.
	*/
	template <Concepts::Integral Integral>
	ATTR_NODISCARD constexpr Integral computeContiguousSequenceSum(const std::span<const Integral> &sequence, const Integral startIndex = 0)
	{
		return computeContiguousSequenceSum<Integral>(sequence, startIndex, static_cast<Integral>(sequence.size()));
	}
} // namespace Utility::Containers::ContiguousSequence

#endif