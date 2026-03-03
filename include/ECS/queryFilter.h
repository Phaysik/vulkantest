/*! \file queryFilter.h
	\brief Contains the function declarations for creating a Detailed file description
	\date 03/02/2026
	\version x.x.x
	\since x.x.x
	\author Matthew Moore
*/

#ifndef INCLUDE_ECS_QUERYFILTER_H
#define INCLUDE_ECS_QUERYFILTER_H

#include <compare>
#include <cstddef>
#include <type_traits>

#include "componentMask.h"
#include "processChunkHelpers.h"

namespace Dimensia::ECS
{
	template <typename...>
	struct TypeList
	{};
	template <typename>
	struct TypeListSize;

	template <typename... Ts>
	struct TypeListSize<TypeList<Ts...>> : std::integral_constant<std::size_t, sizeof...(Ts)>
	{};

	template <typename...>
	struct All
	{};

	template <typename...>
	struct Any
	{};

	template <typename...>
	struct None
	{};

	template <typename>
	struct isAll : std::false_type
	{};

	template <typename... Ts>
	struct isAll<All<Ts...>> : std::true_type
	{};

	template <typename>
	struct isAny : std::false_type
	{};

	template <typename... Ts>
	struct isAny<Any<Ts...>> : std::true_type
	{};

	template <typename>
	struct isNone : std::false_type
	{};

	template <typename... Ts>
	struct isNone<None<Ts...>> : std::true_type
	{};

	template <typename T>
	concept AllType = isAll<T>::value;

	template <typename T>
	concept AnyType = isAny<T>::value;

	template <typename T>
	concept NoneType = isNone<T>::value;

	template <typename>
	struct PackExtractor;

	template <typename... Ts>
	struct PackExtractor<All<Ts...>>
	{
			using type = TypeList<Ts...>;
	};

	template <typename... Ts>
	struct PackExtractor<Any<Ts...>>
	{
			using type = TypeList<Ts...>;
	};

	template <typename... Ts>
	struct PackExtractor<None<Ts...>>
	{
			using type = TypeList<Ts...>;
	};

	template <typename... Components>
	constexpr ComponentMask buildComponentMask() noexcept
	{
		ComponentMask mask{0, 0};
		(([&] {
			 if constexpr (!isTagV<Components>)
			 { // tags are ignored in the mask
				 ComponentTypeID compID{componentID<Components>()};
				 if (compID < LOWER_HALF_BIT_MASK)
				 {
					 mask.mLow |= (1U << compID);
				 }
				 else
				 {
					 mask.mHigh |= (1U << (compID - LOWER_HALF_BIT_MASK));
				 }
			 }
		 }()),
		 ...);
		return mask;
	}

	template <typename List>
	struct MaskBuilder;

	template <typename... Ts>
	struct MaskBuilder<TypeList<Ts...>>
	{
			static constexpr ComponentMask value = buildComponentMask<Ts...>();
	};

	template <typename List>
	constexpr ComponentMask buildMaskFromList()
	{
		return MaskBuilder<List>::value;
	}

	struct QueryKey
	{
		public:
			ComponentMask required;
			ComponentMask any;
			ComponentMask none;
			std::strong_ordering operator<=>(const QueryKey &other) const noexcept = default;
	};
} // namespace Dimensia::ECS

namespace std
{
	template <>
	struct hash<Dimensia::ECS::QueryKey>
	{
			std::size_t operator()(const Dimensia::ECS::QueryKey &key) const noexcept
			{
				const std::size_t required{hash<Dimensia::ECS::ComponentMask>{}(key.required)};
				const std::size_t any{hash<Dimensia::ECS::ComponentMask>{}(key.any)};
				const std::size_t none{hash<Dimensia::ECS::ComponentMask>{}(key.none)};
				return required ^ (any << 1U) ^ (none << 2U);
			}
	};
} // namespace std

#endif