/*! @file cconcepts.test.cpp
	@brief C++ file for creating tests for validating custom concepts.
	@date 02/12/2026
	@version 0.0.1
	@since 0.0.1
	@author Matthew Moore
*/

#include "Core/cconcepts.h"

#include <string>
#include <string_view>

#include "Core/typedefs.h"

#include <catch2/catch_test_macros.hpp>

using Dimensia::Core::FloatingPoint;
using Dimensia::Core::Integral;
using Dimensia::Core::RationalNumber;
using Dimensia::Core::sb;
using Dimensia::Core::si;
using Dimensia::Core::SignedIntegral;
using Dimensia::Core::sl;
using Dimensia::Core::String;
using Dimensia::Core::ub;
using Dimensia::Core::ui;
using Dimensia::Core::ul;
using Dimensia::Core::UnsignedIntegral;

// Compile-time sanity checks (will fail to compile if concepts change unexpectedly)
static_assert(Integral<si>);
static_assert(Integral<bool>);
static_assert(FloatingPoint<double>);
static_assert(!FloatingPoint<si>);
static_assert(RationalNumber<si>);
static_assert(RationalNumber<double>);
static_assert(String<std::string>);
static_assert(String<const std::string &>);
static_assert(!String<const char *>);

// NOLINTBEGIN(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)

SCENARIO("Concepts")
{
	GIVEN("Integral")
	{
		GIVEN("Basic integral types")
		{
			THEN("They should satisfy the Integral concept")
			{
				REQUIRE(Integral<si>);
				REQUIRE(Integral<ul>);
				REQUIRE(Integral<bool>);
			}
		}

		GIVEN("Non-integral types")
		{
			THEN("They should not satisfy the Integral concept")
			{
				REQUIRE_FALSE(Integral<double>);
				REQUIRE_FALSE(Integral<std::string>);
			}
		}
	}

	GIVEN("Signed Integrals")
	{
		GIVEN("Signed integral types")
		{
			THEN("They should satisfy the appropriate concepts")
			{
				REQUIRE(SignedIntegral<sl>);
				REQUIRE(SignedIntegral<si>);
				REQUIRE(SignedIntegral<sb>);
			}
		}

		GIVEN("Unsigned integral types")
		{
			THEN("They should not satisfy the SignedIntegral concept")
			{
				REQUIRE_FALSE(SignedIntegral<ul>);
				REQUIRE_FALSE(SignedIntegral<ui>);
				REQUIRE_FALSE(SignedIntegral<ub>);
			}
		}

		GIVEN("Non-Integral types")
		{
			THEN("They should not satisfy the UnsignedIntegral concept")
			{
				REQUIRE_FALSE(UnsignedIntegral<double>);
				REQUIRE_FALSE(UnsignedIntegral<std::string>);
			}
		}
	}

	GIVEN("Unsigned Integrals")
	{
		GIVEN("Unsigned integral types")
		{
			THEN("They should satisfy the appropriate concepts")
			{
				REQUIRE(UnsignedIntegral<ul>);
				REQUIRE(UnsignedIntegral<ui>);
				REQUIRE(UnsignedIntegral<ub>);
			}
		}

		GIVEN("Signed integral types")
		{
			THEN("They should not satisfy the UnsignedIntegral concept")
			{
				REQUIRE_FALSE(UnsignedIntegral<sl>);
				REQUIRE_FALSE(UnsignedIntegral<si>);
				REQUIRE_FALSE(UnsignedIntegral<sb>);
			}
		}

		GIVEN("Non-Integral types")
		{
			THEN("They should not satisfy the UnsignedIntegral concept")
			{
				REQUIRE_FALSE(UnsignedIntegral<double>);
				REQUIRE_FALSE(UnsignedIntegral<std::string>);
			}
		}
	}

	GIVEN("Floating Point")
	{
		GIVEN("Floating-point types")
		{
			THEN("They should satisfy the FloatingPoint concept")
			{
				REQUIRE(FloatingPoint<float>);
				REQUIRE(FloatingPoint<double>);
			}
		}

		GIVEN("Non-floating-point types")
		{
			THEN("They should not satisfy the FloatingPoint concept")
			{
				REQUIRE_FALSE(FloatingPoint<si>);
				REQUIRE_FALSE(FloatingPoint<std::string>);
			}
		}
	}

	GIVEN("Rational Number")
	{
		GIVEN("Integral and floating-point types")
		{
			THEN("They should satisfy the RationalNumber concept")
			{
				REQUIRE(RationalNumber<si>);
				REQUIRE(RationalNumber<double>);
				REQUIRE(RationalNumber<bool>);
			}
		}

		GIVEN("Non-integral and non-floating-point types")
		{
			THEN("They should not satisfy the RationalNumber concept")
			{
				REQUIRE_FALSE(RationalNumber<std::string>);
				REQUIRE_FALSE(RationalNumber<std::string_view>);
			}
		}
	}

	// NOLINTBEGIN(hicpp-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

	GIVEN("String")
	{
		GIVEN("std::string and its cv-ref variants")
		{
			THEN("They should satisfy the String concept")
			{
				REQUIRE(String<std::string>);
				REQUIRE(String<const std::string &>);
				REQUIRE(String<std::string &&>);
			}
		}

		GIVEN("Non-std::string types")
		{
			THEN("They should not satisfy the String concept")
			{
				REQUIRE_FALSE(String<std::string_view>);
				REQUIRE_FALSE(String<const char *>);
				REQUIRE_FALSE(String<char[6]>);
			}
		}
	}

	// NOLINTEND(hicpp-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
}

// NOLINTEND(misc-const-correctness,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-function-cognitive-complexity)