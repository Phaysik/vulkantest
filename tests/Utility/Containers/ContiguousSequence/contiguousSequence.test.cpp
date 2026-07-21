/*! @file contiguousSequence.test.cpp
	@brief Google Test unit tests for `Containers::ContiguousSequence` utilities.
	@date 01/23/2026
	@version 0.0.1
	@since 0.0.1
	@author Matthew Moore
*/

#include "Utility/Containers/ContiguousSequence/contiguousSequence.h"

#include <array>
#include <span>
#include <vector>

#include "Core/typedefs.h"

#include <catch2/catch_test_macros.hpp>

using Dimensia::Core::si;
using Dimensia::Core::sl;
using Dimensia::Utility::Containers::ContiguousSequence::computeContiguousSequenceSum;

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)

SCENARIO("ContiguousSequence")
{
	GIVEN("computerContiguousSequenceSum")
	{
		std::vector<si> vec{1, 2, 3, 4, 5};
		std::array<sl, 4> arrll{10'000'000'000LL, 20'000'000'000LL, 30'000'000'000LL, 40'000'000'000LL};

		GIVEN("A vector of integers")
		{
			std::span<const si> sequence(vec);

			THEN("summing the whole range returns the total sum")
			{
				REQUIRE((computeContiguousSequenceSum<si>(sequence, 0, static_cast<si>(vec.size())) == 15));
				// Two-arg overload should forward to the three-arg overload for full range
				CHECK((computeContiguousSequenceSum<si>(sequence, 0) == 15));
			}

			THEN("summing a subrange returns the correct sum")
			{
				// sum of elements at indices 1,2,3 => 2 + 3 + 4 == 9
				CHECK((computeContiguousSequenceSum<si>(sequence, 1, 3) == 9));
			}

			THEN("start index at or beyond end returns zero")
			{
				CHECK((computeContiguousSequenceSum<si>(sequence, static_cast<si>(vec.size())) == 0));
				CHECK((computeContiguousSequenceSum<si>(sequence, static_cast<si>(vec.size()) + 5) == 0));
			}

			THEN("length too large returns zero")
			{
				// startIndex + length > size -> should return zero per contract
				CHECK((computeContiguousSequenceSum<si>(sequence, 1, 10) == 0));
			}
		}

		GIVEN("A vector of integers with a large sum that exceeds 32-bit limits")
		{
			std::span<const sl> sequence(arrll);

			THEN("summing the whole range returns the correct total sum without overflow")
			{
				sl expected{10'000'000'000LL + 20'000'000'000LL + 30'000'000'000LL + 40'000'000'000LL};

				REQUIRE((computeContiguousSequenceSum<sl>(sequence, 0, static_cast<si>(arrll.size())) == expected));

				// Two-arg overload should forward to the three-arg overload for full range
				CHECK((computeContiguousSequenceSum<sl>(sequence, 0) == expected));
			}
		}
	}
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)
