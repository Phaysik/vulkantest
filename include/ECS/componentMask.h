/*! \file componentMask.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_COMPONENTMASK_H
#define INCLUDE_ECS_COMPONENTMASK_H

#include <functional>

#include "Core/typedefs.h"

#include "constants.h"

namespace Dimensia::ECS
{

	using Dimensia::Core::ul;

	struct ComponentMask
	{
			// MARK: Constructors

			explicit constexpr ComponentMask(const ul low) noexcept : mLow(low) {}

			constexpr ComponentMask(const ul low, const ul high) noexcept : mLow(low), mHigh(high) {}

			// MARK: Bitwise Operator Overloads

			constexpr ComponentMask operator&(const ComponentMask &other) const noexcept
			{
				return {mLow & other.mLow, mHigh & other.mHigh};
			}

			constexpr ComponentMask operator|(const ComponentMask &other) const noexcept
			{
				return {mLow | other.mLow, mHigh | other.mHigh};
			}

			constexpr ComponentMask operator^(const ComponentMask &other) const noexcept
			{
				return {mLow ^ other.mLow, mHigh ^ other.mHigh};
			}

			constexpr ComponentMask operator~() const noexcept
			{
				return {~mLow, ~mHigh};
			}

			constexpr ComponentMask operator<<(const ul shift) const noexcept
			{
				if (shift == 0)
				{
					return *this;
				}

				if (shift < LOWER_HALF_BIT_MASK)
				{
					return {mLow << shift, (mHigh << shift) | (mLow >> (LOWER_HALF_BIT_MASK - shift))};
				}

				if (shift < UPPER_HALF_BIT_MASK)
				{
					return {0, mLow << (shift - LOWER_HALF_BIT_MASK)};
				}

				return {0, 0};
			}

			constexpr ComponentMask operator>>(const ul shift) const noexcept
			{
				if (shift == 0)
				{
					return *this;
				}

				if (shift < LOWER_HALF_BIT_MASK)
				{
					return {(mLow >> shift) | (mHigh << (LOWER_HALF_BIT_MASK - shift)), mHigh >> shift};
				}

				if (shift < UPPER_HALF_BIT_MASK)
				{
					return {mHigh >> (shift - LOWER_HALF_BIT_MASK), 0};
				}

				return {0, 0};
			}

			constexpr ComponentMask &operator&=(const ComponentMask &other) noexcept
			{
				mLow &= other.mLow;
				mHigh &= other.mHigh;
				return *this;
			}

			constexpr ComponentMask &operator|=(const ComponentMask &other) noexcept
			{
				mLow |= other.mLow;
				mHigh |= other.mHigh;
				return *this;
			}

			constexpr ComponentMask &operator^=(const ComponentMask &other) noexcept
			{
				mLow ^= other.mLow;
				mHigh ^= other.mHigh;
				return *this;
			}

			// MARK: Comparison Operator Overloads

			constexpr bool operator==(const ComponentMask &other) const noexcept
			{
				return mLow == other.mLow && mHigh == other.mHigh;
			}

			constexpr bool operator!=(const ComponentMask &other) const noexcept
			{
				return mLow != other.mLow || mHigh != other.mHigh;
			}

			// MARK: Conversion Operator Overload

			constexpr explicit operator bool() const noexcept
			{
				return mLow != 0 || mHigh != 0;
			}

			// NOLINTBEGIN(misc-non-private-member-variables-in-classes)

			ul mLow{};
			ul mHigh{};

			// NOLINTEND(misc-non-private-member-variables-in-classes)
	};
} // namespace Dimensia::ECS

namespace std
{
	template <>
	struct hash<Dimensia::ECS::ComponentMask>
	{
			size_t operator()(const Dimensia::ECS::ComponentMask &componentMask) const noexcept;
	};
} // namespace std

#endif