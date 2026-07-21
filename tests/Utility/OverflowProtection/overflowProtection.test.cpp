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

#include <catch2/catch_test_macros.hpp>

using Dimensia::Core::ui;
using Dimensia::Core::ul;
using Dimensia::Utility::OverflowProtection::SafeMultiply;
using Dimensia::Utility::OverflowProtection::WillMultiplyOverflow;

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)

SCENARIO("OverflowProtection")
{
	GIVEN("WillMultiplyOverflow")
	{
		THEN("zero and small values do not overflow")
		{
			ui nonZero{12'345U};
			ui zero{0U};
			CHECK_FALSE(WillMultiplyOverflow<ui>(zero, nonZero));

			CHECK_FALSE(WillMultiplyOverflow<ui>(static_cast<ui>(nonZero), static_cast<ui>(zero)));

			CHECK_FALSE(WillMultiplyOverflow<ui>(1U, std::numeric_limits<ui>::max()));
		}

		THEN("exact product within range does not overflow")
		{
			// 65535 * 65535 = 4294836225 which fits in uint32_t (<= 4294967295)
			CHECK_FALSE(WillMultiplyOverflow<ui>(65'535U, 65'535U));
		}

		THEN("obvious overflow is detected")
		{
			ui max32{std::numeric_limits<ui>::max()};
			CHECK(WillMultiplyOverflow<ui>(max32, 2U));
		}

		THEN("large type overflow is detected")
		{
			ul max64{std::numeric_limits<ul>::max()};
			CHECK(WillMultiplyOverflow<ul>(max64, 2U));
		}

		THEN("multiplying by one does not overflow")
		{
			ul max64{std::numeric_limits<ul>::max()};
			CHECK_FALSE(WillMultiplyOverflow<ul>(max64, 1U));
		}
	}

	GIVEN("SafeMultiply")
	{
		THEN("zero and small values multiply correctly")
		{
			CHECK((SafeMultiply<ui>(0U, 12'345U) == 0U));
			CHECK((SafeMultiply<ui>(2U, 3U) == 6U));
		}

		THEN("exact product within range multiplies correctly")
		{
			CHECK((SafeMultiply<ui>(65'535U, 65'535U) == 4'294'836'225U));
		}

		THEN("obvious overflow returns max value")
		{
			ui max32{std::numeric_limits<ui>::max()};
			CHECK((SafeMultiply<ui>(max32, 2U) == max32));
		}

		THEN("large type overflow returns max value")
		{
			ul max64{std::numeric_limits<ul>::max()};
			CHECK((SafeMultiply<ul>(max64, 2U) == max64));
		}

		THEN("multiplying by one returns the original value")
		{
			ul max64{std::numeric_limits<ul>::max()};
			CHECK((SafeMultiply<ul>(max64, 1U) == max64));
		}
	}
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)
