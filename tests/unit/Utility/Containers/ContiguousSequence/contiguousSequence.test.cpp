/*! @file contiguousSequence.test.cpp
	@brief Google Test unit tests for `Containers::ContiguousSequence` utilities.
	@date 01/23/2026
	@version 0.0.1
	@since 0.0.1
	@author Matthew Moore
*/

#include "Utility/Containers/ContiguousSequence/contiguousSequence.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

using Utility::Containers::ContiguousSequence::computeContiguousSequenceSum;

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

namespace // NOSONAR
{
	class ContiguousSequenceTest : public ::testing::Test // NOSONAR
	{
		protected:
			std::vector<int> vec{1, 2, 3, 4, 5};
			std::array<int64_t, 4> arrll{10'000'000'000LL, 20'000'000'000LL, 30'000'000'000LL, 40'000'000'000LL};
	};
} // namespace

TEST_F(ContiguousSequenceTest, GivenVectorWhenSummingWholeRangeReturnsSum)
{
	std::span<const int> sequence(vec);

	EXPECT_EQ(computeContiguousSequenceSum<int>(sequence, 0, static_cast<int>(vec.size())), 15);
	// Two-arg overload should forward to the three-arg overload for full range
	EXPECT_EQ(computeContiguousSequenceSum<int>(sequence, 0), 15);
}

TEST_F(ContiguousSequenceTest, GivenVectorWhenSummingSubrangeReturnsSum)
{
	std::span<const int> sequence(vec);

	// sum of elements at indices 1,2,3 => 2 + 3 + 4 == 9
	EXPECT_EQ(computeContiguousSequenceSum<int>(sequence, 1, 3), 9);
}

TEST_F(ContiguousSequenceTest, GivenStartIndexAtOrBeyondEndReturnsZero)
{
	std::span<const int> sequence(vec);

	EXPECT_EQ(computeContiguousSequenceSum<int>(sequence, static_cast<int>(vec.size())), 0);
	EXPECT_EQ(computeContiguousSequenceSum<int>(sequence, static_cast<int>(vec.size()) + 5), 0);
}

TEST_F(ContiguousSequenceTest, GivenLengthTooLargeReturnsZero)
{
	std::span<const int> sequence(vec);

	// startIndex + length > size -> should return zero per contract
	EXPECT_EQ(computeContiguousSequenceSum<int>(sequence, 1, 10), 0);
}

TEST_F(ContiguousSequenceTest, WorksWithDifferentIntegralTypes)
{
	std::span<const int64_t> sequence(arrll);
	const int64_t expected = 10'000'000'000LL + 20'000'000'000LL + 30'000'000'000LL + 40'000'000'000LL;

	EXPECT_EQ(computeContiguousSequenceSum<int64_t>(sequence, 0), expected);
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
