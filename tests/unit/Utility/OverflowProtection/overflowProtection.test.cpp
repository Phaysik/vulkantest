/*! @file overflowProtection.test.cpp
	@brief Google Test unit tests for the OverflowProtection helpers.
	@date --/--/----
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#include "Utility/OverflowProtection/overflowProtection.h"

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

using Utility::OverflowProtection::SafeMultiply;
using Utility::OverflowProtection::WillMultiplyOverflow;

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

TEST(OverflowProtectionBasicChecks, ZeroAndSmallValues)
{
	EXPECT_FALSE(WillMultiplyOverflow<std::uint32_t>(0U, 12'345U));
	EXPECT_FALSE(WillMultiplyOverflow<std::uint32_t>(1U, std::numeric_limits<std::uint32_t>::max()));

	EXPECT_EQ(SafeMultiply<std::uint32_t>(0U, 12'345U), 0U);
	EXPECT_EQ(SafeMultiply<std::uint32_t>(2U, 3U), 6U);
}

TEST(OverflowProtectionEdgeNoOverflow, ExactProductWithinRange)
{
	// 65535 * 65535 = 4294836225 which fits in uint32_t (<= 4294967295)
	std::uint32_t num1 = 65'535U;
	std::uint32_t num2 = 65'535U;
	EXPECT_FALSE(WillMultiplyOverflow<std::uint32_t>(num1, num2));
	EXPECT_EQ(SafeMultiply<std::uint32_t>(num1, num2), 4'294'836'225U);
}

TEST(OverflowProtectionDetectOverflow32, ObviousOverflow)
{
	std::uint32_t max32 = std::numeric_limits<std::uint32_t>::max();
	EXPECT_TRUE(WillMultiplyOverflow<std::uint32_t>(max32, 2U));
	EXPECT_EQ(SafeMultiply<std::uint32_t>(max32, 2U), max32);
}

TEST(OverflowProtectionDetectOverflow64, LargeTypeOverflow)
{
	std::uint64_t max64 = std::numeric_limits<std::uint64_t>::max();
	EXPECT_TRUE(WillMultiplyOverflow<std::uint64_t>(max64, 2U));
	EXPECT_EQ(SafeMultiply<std::uint64_t>(max64, 2U), max64);
}

TEST(OverflowProtectionBorderCase, MultiplyByOne)
{
	std::uint64_t max64 = std::numeric_limits<std::uint64_t>::max();
	EXPECT_FALSE(WillMultiplyOverflow<std::uint64_t>(max64, 1U));
	EXPECT_EQ(SafeMultiply<std::uint64_t>(max64, 1U), max64);
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
