/*! \file componentMask.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 02/14/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_COMPONENTMASK_H
#define INCLUDE_ECS_COMPONENTMASK_H

#include <cstdint>
#include <functional>

struct ComponentMask
{
		uint64_t low;
		uint64_t high;

		constexpr ComponentMask() : low(0), high(0) {}

		constexpr ComponentMask(uint64_t l) : low(l), high(0) {}

		constexpr ComponentMask(uint64_t l, uint64_t h) : low(l), high(h) {}

		// All operators now defined inline in the header
		constexpr ComponentMask operator&(const ComponentMask &other) const
		{
			return {low & other.low, high & other.high};
		}

		constexpr ComponentMask operator|(const ComponentMask &other) const
		{
			return {low | other.low, high | other.high};
		}

		constexpr ComponentMask operator^(const ComponentMask &other) const
		{
			return {low ^ other.low, high ^ other.high};
		}

		constexpr ComponentMask operator~() const
		{
			return {~low, ~high};
		}

		constexpr ComponentMask operator<<(int shift) const
		{
			if (shift < 64)
			{
				return {low << shift, (high << shift) | (low >> (64 - shift))};
			}
			else if (shift < 128)
			{
				return {0, low << (shift - 64)};
			}
			else
			{
				return {0, 0};
			}
		}

		constexpr ComponentMask operator>>(int shift) const
		{
			if (shift < 64)
			{
				return {(low >> shift) | (high << (64 - shift)), high >> shift};
			}
			else if (shift < 128)
			{
				return {high >> (shift - 64), 0};
			}
			else
			{
				return {0, 0};
			}
		}

		constexpr ComponentMask &operator&=(const ComponentMask &other)
		{
			low &= other.low;
			high &= other.high;
			return *this;
		}

		constexpr ComponentMask &operator|=(const ComponentMask &other)
		{
			low |= other.low;
			high |= other.high;
			return *this;
		}

		constexpr ComponentMask &operator^=(const ComponentMask &other)
		{
			low ^= other.low;
			high ^= other.high;
			return *this;
		}

		constexpr bool operator==(const ComponentMask &other) const
		{
			return low == other.low && high == other.high;
		}

		constexpr bool operator!=(const ComponentMask &other) const
		{
			return low != other.low || high != other.high;
		}

		constexpr explicit operator bool() const
		{
			return low != 0 || high != 0;
		}
};

namespace std
{
	template <>
	struct hash<ComponentMask>
	{
			size_t operator()(const ComponentMask &m) const noexcept;
	};
} // namespace std

#endif