/*! @file queryFilter.h
	@brief Declares compile-time query filter helpers for ECS component matching.
	@details Provides lightweight type wrappers (`All`, `Any`, `None`) and traits/concepts used to express query semantics,
	compile-time utilities to extract and transform type packs into `ComponentMask` values, and the `QueryKey` runtime key used
	to cache and compare query signatures.
	@date 03/02/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
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
	/*! @brief Variadic type-list used as an internal compile-time container.
		@tparam Types Parameter pack of contained types.
		@date 03/03/2026
		@version x.x.x
		@since x.x.x
		@author Matthew Moore
	*/
	template <typename...>
	struct TypeList
	{};

	/*! @brief Computes the number of types stored in a @ref TypeList.
		@tparam List A `TypeList<...>` instantiation.
		@note Exposes a `value` member via `std::integral_constant`.
		@date 03/03/2026
		@version x.x.x
		@since x.x.x
		@author Matthew Moore
	*/
	template <typename>
	struct TypeListSize;

	template <typename... Ts>
	struct TypeListSize<TypeList<Ts...>> : std::integral_constant<std::size_t, sizeof...(Ts)>
	{};

	template <typename T, typename List>
	struct TypeListPrepend;

	template <typename T, typename... Ts>
	struct TypeListPrepend<T, TypeList<Ts...>>
	{
		using type = TypeList<T, Ts...>;
	};

	template <typename List>
	struct RegularTypeList;

	template <>
	struct RegularTypeList<TypeList<>>
	{
		using type = TypeList<>;
	};

	template <typename T, typename... Ts>
	struct RegularTypeList<TypeList<T, Ts...>>
	{
		using tail = RegularTypeList<TypeList<Ts...>>::type;
		using type = std::conditional_t<isTagV<T>, tail, typename TypeListPrepend<T, tail>::type>;
	};

	/*! @brief Marker wrapper expressing a query clause where all listed component types are required.
		@tparam Types Component/tag types that must all be present.
		@date 03/03/2026
		@version x.x.x
		@since x.x.x
		@author Matthew Moore
	*/
	template <typename...>
	struct All
	{};

	/*! @brief Marker wrapper expressing a query clause where at least one listed component type is required.
		@tparam Types Component/tag types where any single match satisfies the clause.
		@date 03/03/2026
		@version x.x.x
		@since x.x.x
		@author Matthew Moore
	*/
	template <typename...>
	struct Any
	{};

	/*! @brief Marker wrapper expressing a query clause where listed component types must be absent.
		@tparam Types Component/tag types that must not be present.
		@date 03/03/2026
		@version x.x.x
		@since x.x.x
		@author Matthew Moore
	*/
	template <typename...>
	struct None
	{};

	/*! @brief Marker wrapper expressing a change-detection filter clause.
		@tparam Types Component types whose per-component version must have advanced since the last system run.
		@details Used with `SystemVersion` to narrow dirty-chunk filtering to only chunks where the specified
	   component types have actually been modified, rather than any component in the query.
		@note Requires a `SystemVersion` to be attached; without one, the filter has no effect.
	*/
	template <typename...>
	struct Changed
	{};

	/*! @brief Type trait that identifies whether a type is an `All<...>` query clause.
		@tparam T Type to inspect.
	*/
	template <typename>
	struct isAll : std::false_type
	{};

	template <typename... Ts>
	struct isAll<All<Ts...>> : std::true_type
	{};

	/*! @brief Type trait that identifies whether a type is an `Any<...>` query clause.
		@tparam T Type to inspect.
	*/
	template <typename>
	struct isAny : std::false_type
	{};

	template <typename... Ts>
	struct isAny<Any<Ts...>> : std::true_type
	{};

	/*! @brief Type trait that identifies whether a type is a `None<...>` query clause.
		@tparam T Type to inspect.
	*/
	template <typename>
	struct isNone : std::false_type
	{};

	template <typename... Ts>
	struct isNone<None<Ts...>> : std::true_type
	{};

	/*! @brief Concept satisfied when `T` is an `All<...>` clause wrapper.
		@tparam T Type to evaluate.
	*/
	template <typename T>
	concept AllType = isAll<T>::value;

	/*! @brief Concept satisfied when `T` is an `Any<...>` clause wrapper.
		@tparam T Type to evaluate.
	*/
	template <typename T>
	concept AnyType = isAny<T>::value;

	/*! @brief Concept satisfied when `T` is a `None<...>` clause wrapper.
		@tparam T Type to evaluate.
	*/
	template <typename T>
	concept NoneType = isNone<T>::value;

	/*! @brief Type trait that identifies whether a type is a `Changed<...>` query clause.
		@tparam T Type to inspect.
	*/
	template <typename>
	struct isChanged : std::false_type
	{};

	template <typename... Ts>
	struct isChanged<Changed<Ts...>> : std::true_type
	{};

	/*! @brief Concept satisfied when `T` is a `Changed<...>` clause wrapper.
		@tparam T Type to evaluate.
	*/
	template <typename T>
	concept ChangedType = isChanged<T>::value;

	/*! @brief Extracts a `TypeList<...>` from a query clause wrapper.
		@tparam Clause Clause wrapper (`All<...>`, `Any<...>`, or `None<...>`).
		@note The extracted list is exposed as nested alias `type`.
	*/
	template <typename>
	struct PackExtractor;

	template <typename... Ts>
	struct PackExtractor<All<Ts...>>
	{
		public:
			using type = TypeList<Ts...>;
	};

	template <typename... Ts>
	struct PackExtractor<Any<Ts...>>
	{
		public:
			using type = TypeList<Ts...>;
	};

	template <typename... Ts>
	struct PackExtractor<None<Ts...>>
	{
		public:
			using type = TypeList<Ts...>;
	};

	template <typename... Ts>
	struct PackExtractor<Changed<Ts...>>
	{
		public:
			using type = TypeList<Ts...>;
	};

	/*! @brief Builds a `ComponentMask` for all types contained in a `TypeList`.
		@tparam List The type list to convert into a mask.
	*/
	template <typename List>
	struct MaskBuilder;

	template <typename... Ts>
	struct MaskBuilder<TypeList<Ts...>>
	{
		public:
			static constexpr ComponentMask value = buildRequiredMask<Ts...>();
	};

	/*! @brief Computes a component mask from a `TypeList` type.
		@tparam List A `TypeList<...>` containing component/tag types.
		@return The compile-time `ComponentMask` with bits set for the listed types.
		@note This function is `constexpr` so it can be used in compile-time query construction.
	*/
	template <typename List>
	constexpr ComponentMask buildMaskFromList()
	{
		return MaskBuilder<List>::value;
	}

	/*! @brief Computes a tag-only component mask from a `TypeList`.
		@tparam List A `TypeList<...>` containing component and tag types.
		@return Mask containing only tag type identifiers from @p List.
	*/
	template <typename List>
	struct TagMaskBuilder;

	template <typename... Ts>
	struct TagMaskBuilder<TypeList<Ts...>>
	{
		static constexpr ComponentMask value = buildTagMask<Ts...>();
	};

	template <typename List>
	constexpr ComponentMask buildTagMaskFromList()
	{
		return TagMaskBuilder<List>::value;
	}

	/*! @struct QueryKey include/ECS/queryKey.h
		@brief Canonical runtime key representing a compiled ECS query signature.
		@details `required` stores components that must be present, `any` stores optional-match components where at least one bit must
	   match, and `none` stores components that must be absent. Keys are comparable and hashable to support associative caches.
		@date 03/03/2026
		@version x.x.x
		@since x.x.x
		@author Matthew Moore
	*/
	struct QueryKey
	{
		public:
			/*! @brief Performs default lexicographic three-way comparison of query masks.
				@param[in] other Query key compared against this key.
				@return Ordering result over `required`, `any`, and `none`.
			*/
			std::strong_ordering operator<=>(const QueryKey &other) const noexcept = default;

			// NOLINTBEGIN(misc-non-private-member-variables-in-classes)

			/*! @var required
				@brief Mask of components/tags that must all be present.
			*/
			ComponentMask required;

			/*! @var any
				@brief Mask of components/tags where at least one bit must be present.
			*/
			ComponentMask any;

			/*! @var none
				@brief Mask of components/tags that must be absent.
			*/
			ComponentMask none;

			/*! @var requiredTags
				@brief Per-row tag bits that must all be present.
			*/
			ComponentMask requiredTags{0};

			/*! @var anyTags
				@brief Per-row tag bits where at least one may satisfy the Any clause.
			*/
			ComponentMask anyTags{0};

			/*! @var noneTags
				@brief Per-row tag bits that must all be absent.
			*/
			ComponentMask noneTags{0};

			// NOLINTEND(misc-non-private-member-variables-in-classes)
	};
} // namespace Dimensia::ECS

namespace std
{
	/*! @brief Hash specialization for `Dimensia::ECS::QueryKey`.
		@details Combines the hash values of `required`, `any`, and `none` masks using shift/xor mixing suitable for hash-table keying.
	*/
	template <>
	struct hash<Dimensia::ECS::QueryKey>
	{
			/*! @brief Computes a hash code for a query key.
				@param[in] key Query key to hash.
				@return Combined hash value derived from the three component masks.
			*/
			std::size_t operator()(const Dimensia::ECS::QueryKey &key) const noexcept
			{
				constexpr unsigned int ANY_SHIFT{1U};
				constexpr unsigned int NONE_SHIFT{2U};
				constexpr unsigned int REQUIRED_TAGS_SHIFT{3U};
				constexpr unsigned int ANY_TAGS_SHIFT{4U};
				constexpr unsigned int NONE_TAGS_SHIFT{5U};
				const std::size_t required{hash<Dimensia::ECS::ComponentMask>{}(key.required)};
				const std::size_t any{hash<Dimensia::ECS::ComponentMask>{}(key.any)};
				const std::size_t none{hash<Dimensia::ECS::ComponentMask>{}(key.none)};
				const std::size_t requiredTags{hash<Dimensia::ECS::ComponentMask>{}(key.requiredTags)};
				const std::size_t anyTags{hash<Dimensia::ECS::ComponentMask>{}(key.anyTags)};
				const std::size_t noneTags{hash<Dimensia::ECS::ComponentMask>{}(key.noneTags)};

				return required ^ (any << ANY_SHIFT) ^ (none << NONE_SHIFT) ^ (requiredTags << REQUIRED_TAGS_SHIFT)
					   ^ (anyTags << ANY_TAGS_SHIFT) ^ (noneTags << NONE_TAGS_SHIFT);
			}
	};
} // namespace std

#endif