/*! @file overflowProtection.test.cpp
	@brief Google Test unit tests for the OverflowProtection helpers.
	@date 02/12/2026
	@version 0.0.1
	@since 0.0.1
	@author Matthew Moore
*/

#include "Utility/OverflowProtection/overflowProtection.h"

#include <limits>

#include "Core/typedefs.h"

#include <gtest/gtest.h>

using Dimensia::Core::ui;
using Dimensia::Core::ul;
using Dimensia::Utility::OverflowProtection::SafeMultiply;
using Dimensia::Utility::OverflowProtection::WillMultiplyOverflow;

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

TEST(OverflowProtectionBasicChecks, ZeroAndSmallValues)
{
	EXPECT_FALSE(WillMultiplyOverflow<ui>(0U, 12'345U));
	EXPECT_FALSE(WillMultiplyOverflow<ui>(1U, std::numeric_limits<ui>::max()));

	EXPECT_EQ(SafeMultiply<ui>(0U, 12'345U), 0U);
	EXPECT_EQ(SafeMultiply<ui>(2U, 3U), 6U);
}

TEST(OverflowProtectionEdgeNoOverflow, ExactProductWithinRange)
{
	// 65535 * 65535 = 4294836225 which fits in uint32_t (<= 4294967295)
	ui num1 = 65'535U;
	ui num2 = 65'535U;
	EXPECT_FALSE(WillMultiplyOverflow<ui>(num1, num2));
	EXPECT_EQ(SafeMultiply<ui>(num1, num2), 4'294'836'225U);
}

TEST(OverflowProtectionDetectOverflow32, ObviousOverflow)
{
	ui max32 = std::numeric_limits<ui>::max();
	EXPECT_TRUE(WillMultiplyOverflow<ui>(max32, 2U));
	EXPECT_EQ(SafeMultiply<ui>(max32, 2U), max32);
}

TEST(OverflowProtectionDetectOverflow64, LargeTypeOverflow)
{
	ul max64 = std::numeric_limits<ul>::max();
	EXPECT_TRUE(WillMultiplyOverflow<ul>(max64, 2U));
	EXPECT_EQ(SafeMultiply<ul>(max64, 2U), max64);
}

TEST(OverflowProtectionBorderCase, MultiplyByOne)
{
	ul max64 = std::numeric_limits<ul>::max();
	EXPECT_FALSE(WillMultiplyOverflow<ul>(max64, 1U));
	EXPECT_EQ(SafeMultiply<ul>(max64, 1U), max64);
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
