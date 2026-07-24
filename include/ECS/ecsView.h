/*! @file ecsView.h
	@brief ViewIterator and View nested class definitions, included inside the ECS class body.
	@details This file is not meant to be included directly. It is included by ecs.h inside the `ECS` class definition
   to split the large header into manageable pieces while preserving the nested class relationship.
	@date 07/21/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_ECS_H
	#error "ecsView.h must not be included directly. Include ecs.h instead."
#endif

// MARK: View

/*! @class ViewIterator include/ECS/ecsView.h
	@brief Forward iterator that walks matching archetype chunks and yields (Entity, Components&...) tuples.
	@tparam IsConst Whether component references are const-qualified.
	@tparam Components Non-tag component types to iterate.
*/
template <bool IsConst, typename... Components>
class ViewIterator
{
		static_assert((!isTagV<Components> && ...), "ECS views support regular components only; use a query for tag filtering");

	public:
		using ECSType = std::conditional_t<IsConst, const ECS, ECS>;
		using ArchVec = std::vector<Archetype *>;
		using BytePtr = std::conditional_t<IsConst, const std::byte *, std::byte *>;
		using value_type = std::tuple<Entity, std::conditional_t<IsConst, const Components &, Components &>...>;
		using difference_type = std::ptrdiff_t;

		// Sentinel (end) constructor
		ViewIterator() noexcept = default;

		// Begin constructor
		ViewIterator(ECSType *ecs, const ArchVec *archetypes, std::size_t archIdx, const ComponentMask writeMask = ComponentMask(0),
					 const ComponentMask requiredTags = ComponentMask(0), const ComponentMask anyTags = ComponentMask(0),
					 const ComponentMask noneTags = ComponentMask(0), const ComponentMask anyRegular = ComponentMask(0),
					 const bool hasAnyClause = false) noexcept
			: mECS(ecs), mArchetypes(archetypes), mWriteMask(writeMask), mRequiredTags(requiredTags), mAnyTags(anyTags),
			  mNoneTags(noneTags), mAnyRegular(anyRegular), mArchIdx(static_cast<ui>(archIdx)), mHasAnyClause(hasAnyClause)
		{
			seekMatching();
		}

		value_type operator*() const
		{
			if constexpr (!IsConst)
			{
				if (mWriteMask && (mMarkedArchIdx != mArchIdx || mMarkedChunkIdx != mChunkIdx))
				{
					// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
					mECS->markComponentsChanged((*mArchetypes)[mArchIdx], mChunkIdx, mWriteMask);
					mMarkedArchIdx = mArchIdx;
					mMarkedChunkIdx = mChunkIdx;
				}
			}

			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			return value_type{mEntityArr[mSlot],
							  (*reinterpret_cast<std::conditional_t<IsConst, const Components *, Components *>>(
								  mCompPtrs[componentID<Components>()] + (mSlot * ComponentInfos[componentID<Components>()].size)))...};
			// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		}

		ViewIterator &operator++() noexcept
		{
			++mSlot;
			seekMatching();
			return *this;
		}

		ViewIterator operator++(int) noexcept
		{
			ViewIterator tmp{*this};
			++(*this);
			return tmp;
		}

		bool operator==(const ViewIterator &other) const noexcept
		{
			if (mArchetypes == nullptr && other.mArchetypes == nullptr)
			{
				return true;
			}
			if (mArchetypes == nullptr || other.mArchetypes == nullptr)
			{
				// One is end sentinel: both must be at end
				const auto &nonNull = (mArchetypes != nullptr) ? *this : other;
				return nonNull.mArchIdx >= static_cast<ui>(nonNull.mArchetypes->size());
			}
			return mArchIdx == other.mArchIdx && mChunkIdx == other.mChunkIdx && mSlot == other.mSlot;
		}

		bool operator!=(const ViewIterator &other) const noexcept
		{
			// NOLINTNEXTLINE(readability-redundant-parentheses)
			return !(*this == other);
		}

	private:
		bool currentRowMatches(const Archetype *arch) const noexcept
		{
			const ComponentMask entityTags{arch->getTags(mChunkIdx, mSlot)};
			// NOLINTNEXTLINE(readability-redundant-parentheses)
			const bool requiredMatch{(entityTags & mRequiredTags) == mRequiredTags};
			const bool anyMatch{
				!mHasAnyClause || static_cast<bool>(arch->getRegularMask() & mAnyRegular) || static_cast<bool>(entityTags & mAnyTags),
			};
			const bool noneMatch{!static_cast<bool>(entityTags & mNoneTags)};
			return requiredMatch && anyMatch && noneMatch;
		}

		void seekMatching() noexcept
		{
			while (mArchetypes != nullptr && mArchIdx < static_cast<ui>(mArchetypes->size()))
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				Archetype *arch{(*mArchetypes)[mArchIdx]};
				const ui chunkCount{arch->getChunkCount()};

				while (mChunkIdx < chunkCount)
				{
					mEntityCount = arch->getEntityCount(mChunkIdx);
					if (mSlot == 0 && mEntityCount > 0)
					{
						mEntityArr = arch->getEntityArray(mChunkIdx);
						loadCompPtrs(arch);
					}

					while (mSlot < mEntityCount)
					{
						if (currentRowMatches(arch))
						{
							return;
						}
						++mSlot;
					}

					++mChunkIdx;
					mSlot = 0;
				}

				++mArchIdx;
				mChunkIdx = 0;
			}
		}

		void loadCompPtrs(Archetype *arch) noexcept
		{
			// Component IDs are registry-validated compile-time indices into this fixed-size array.
			// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			((mCompPtrs[componentID<Components>()] = static_cast<BytePtr>(arch->getComponentArray(mChunkIdx, componentID<Components>()))),
			 ...);
		}

		ECSType *mECS{nullptr};
		const ArchVec *mArchetypes{nullptr};
		ComponentMask mWriteMask{0};
		ComponentMask mRequiredTags{0};
		ComponentMask mAnyTags{0};
		ComponentMask mNoneTags{0};
		ComponentMask mAnyRegular{0};
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		std::array<BytePtr, MAX_COMPONENTS> mCompPtrs{};
		Entity *mEntityArr{nullptr};
		ui mArchIdx{0};
		ui mChunkIdx{0};
		ui mSlot{0};
		ui mEntityCount{0};
		bool mHasAnyClause{false};
		mutable ui mMarkedArchIdx{UINT32_MAX};
		mutable ui mMarkedChunkIdx{UINT32_MAX};
};

/*! @class View include/ECS/ecsView.h
	@brief Range object returned by `ECS::view<Components...>()` that supports range-for iteration.
	@details Provides `begin()` and `end()` iterators that walk matching archetype chunks, yielding
   `std::tuple<Entity, Components&...>` per entity. Compatible with structured bindings:
   `for (auto [e, pos, vel] : ecs.view<Position, Velocity>()) { ... }`
	@tparam IsConst Whether component references are const-qualified.
	@tparam Components Non-tag component types to iterate.
*/
template <bool IsConst, typename... Components>
class View
{
		static_assert((!isTagV<Components> && ...), "ECS views support regular components only; use a query for tag filtering");

	public:
		using iterator = ViewIterator<IsConst, Components...>;
		using ECSType = std::conditional_t<IsConst, const ECS, ECS>;

		explicit View(ECSType &ecs, const ComponentMask writeMask = IsConst ? ComponentMask(0) : buildRequiredMask<Components...>())
			: mECS(&ecs), mIterationGuard(ecs.mStructuralMutex), mWriteMask(writeMask)
		{
			constexpr ComponentMask requiredRegular{buildRequiredMask<Components...>()};
			mMatchingArchetypes = mECS->mQueryCache.get(requiredRegular);
		}

		View(const View &) = delete;
		View &operator=(const View &) = delete;
		View(View &&) noexcept = default;
		View &operator=(View &&) = delete;
		~View() = default;

		/*! @brief Construct a view from a pre-resolved archetype list (used by filtered view factories).
			@param[in] ecs ECS instance.
			@param[in] archetypes Vector of matching archetypes copied for safe lifetime management.
		*/
		template <typename AnyFilterList, typename NoneFilterList>
		explicit View(ECSType &ecs, AnyFilterList /*unused*/, NoneFilterList /*unused*/, const QueryKey &key,
					  const ComponentMask &requiredMask, const ComponentMask &anyMask, const ComponentMask &noneMask,
					  const ComponentMask requiredTags, const ComponentMask anyTags, const ComponentMask noneTags,
					  const ComponentMask writeMask = IsConst ? ComponentMask(0) : buildRequiredMask<Components...>())
			: mECS(&ecs), mIterationGuard(ecs.mStructuralMutex),
			  mMatchingArchetypes(
				  getMatchingArchetypesForQueryCalls<ECSType, AnyFilterList, NoneFilterList>(ecs, key, requiredMask, anyMask, noneMask)),
			  mRequiredTags(requiredTags), mAnyTags(anyTags), mNoneTags(noneTags), mAnyRegular(anyMask), mWriteMask(writeMask),
			  mHasAnyClause(TypeListSize<AnyFilterList>::value != 0)
		{}

		ATTR_NODISCARD iterator begin() const noexcept
		{
			return iterator(mECS, &mMatchingArchetypes, 0, mWriteMask, mRequiredTags, mAnyTags, mNoneTags, mAnyRegular, mHasAnyClause);
		}

		ATTR_NODISCARD iterator end() const noexcept
		{
			return iterator{};
		}

	private:
		ECSType *mECS;
		IterationGuard mIterationGuard;
		std::vector<Archetype *> mMatchingArchetypes{};
		ComponentMask mRequiredTags{0};
		ComponentMask mAnyTags{0};
		ComponentMask mNoneTags{0};
		ComponentMask mAnyRegular{0};
		ComponentMask mWriteMask{0};
		bool mHasAnyClause{false};
};

/*! @brief Create a view for iterating entities with the specified component types.
	@tparam Components Component types to require and iterate.
	@return A `View` object supporting range-for with structured bindings.
	@note Usage: `for (auto [e, pos, vel] : ecs.view<Position, Velocity>()) { ... }`
*/
template <typename... Components>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components> && !isTagV<Components>) && ...)
View<false, Components...> view()
{
	return View<false, Components...>(*this);
}

/*! @brief Const overload for creating a read-only view. */
template <typename... Components>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components> && !isTagV<Components>) && ...)
View<true, Components...> view() const
{
	return View<true, Components...>(*this);
}

/*! @brief Creates an explicitly read-only view from a mutable ECS.
	@tparam Components Required regular component types yielded as const references.
	@return Read-only view that never updates component change versions.
*/
template <typename... Components>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components> && !isTagV<Components>) && ...)
View<true, Components...> readView()
{
	return View<true, Components...>(static_cast<const ECS &>(*this));
}

/*! @brief Creates an explicitly writable view.
	@tparam Components Required regular component types yielded as mutable references.
	@return Writable view that stamps each dereferenced matching chunk once.
*/
template <typename... Components>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components> && !isTagV<Components>) && ...)
View<false, Components...> writeView()
{
	return View<false, Components...>(*this, buildRequiredMask<Components...>());
}

// MARK: Filtered View Factory Methods

/*! @brief Create a filtered view with All + None clauses.
	@tparam AllF `All<...>` clause listing required component types (also determines yielded components).
	@tparam NoneF `None<...>` clause listing excluded component types.
	@return A `View` supporting range-for with structured bindings over entities matching the filter.
*/
template <AllType AllF, NoneType NoneF>
auto view()
{
	using ReqList = PackExtractor<AllF>::type;
	using YieldList = RegularTypeList<ReqList>::type;
	using NoneList = PackExtractor<NoneF>::type;
	constexpr ComponentMask requiredMask{buildMaskFromList<ReqList>()};
	constexpr ComponentMask noneMask{buildMaskFromList<NoneList>()};
	constexpr ComponentMask anyMask{0, 0};
	constexpr ComponentMask requiredTags{buildTagMaskFromList<ReqList>()};
	constexpr ComponentMask anyTags{0, 0};
	constexpr ComponentMask noneTags{buildTagMaskFromList<NoneList>()};
	QueryKey key{
		.required = requiredMask,
		.any = anyMask,
		.none = noneMask,
		.requiredTags = requiredTags,
		.anyTags = anyTags,
		.noneTags = noneTags,
	};
	return [&]<typename... Comps>(TypeList<Comps...>) {
		return View<false, Comps...>(*this, TypeList<>{}, NoneList{}, key, requiredMask, anyMask, noneMask, requiredTags, anyTags,
									 noneTags);
	}(YieldList{});
}

/*! @brief Const overload for All + None filtered view. */
template <AllType AllF, NoneType NoneF>
auto view() const
{
	using ReqList = PackExtractor<AllF>::type;
	using YieldList = RegularTypeList<ReqList>::type;
	using NoneList = PackExtractor<NoneF>::type;
	constexpr ComponentMask requiredMask{buildMaskFromList<ReqList>()};
	constexpr ComponentMask noneMask{buildMaskFromList<NoneList>()};
	constexpr ComponentMask anyMask{0, 0};
	constexpr ComponentMask requiredTags{buildTagMaskFromList<ReqList>()};
	constexpr ComponentMask anyTags{0, 0};
	constexpr ComponentMask noneTags{buildTagMaskFromList<NoneList>()};
	QueryKey key{
		.required = requiredMask,
		.any = anyMask,
		.none = noneMask,
		.requiredTags = requiredTags,
		.anyTags = anyTags,
		.noneTags = noneTags,
	};
	return [&]<typename... Comps>(TypeList<Comps...>) {
		return View<true, Comps...>(*this, TypeList<>{}, NoneList{}, key, requiredMask, anyMask, noneMask, requiredTags, anyTags, noneTags);
	}(YieldList{});
}

/*! @brief Create a filtered view with All + Any + None clauses.
	@tparam AllF `All<...>` clause listing required component types (also determines yielded components).
	@tparam AnyF `Any<...>` clause listing alternative-match component types.
	@tparam NoneF `None<...>` clause listing excluded component types.
*/
template <AllType AllF, AnyType AnyF, NoneType NoneF>
auto view()
{
	using ReqList = PackExtractor<AllF>::type;
	using YieldList = RegularTypeList<ReqList>::type;
	using AnyList = PackExtractor<AnyF>::type;
	using NoneList = PackExtractor<NoneF>::type;
	constexpr ComponentMask requiredMask{buildMaskFromList<ReqList>()};
	constexpr ComponentMask anyMask{buildMaskFromList<AnyList>()};
	constexpr ComponentMask noneMask{buildMaskFromList<NoneList>()};
	constexpr ComponentMask requiredTags{buildTagMaskFromList<ReqList>()};
	constexpr ComponentMask anyTags{buildTagMaskFromList<AnyList>()};
	constexpr ComponentMask noneTags{buildTagMaskFromList<NoneList>()};
	QueryKey key{
		.required = requiredMask,
		.any = anyMask,
		.none = noneMask,
		.requiredTags = requiredTags,
		.anyTags = anyTags,
		.noneTags = noneTags,
	};
	return [&]<typename... Comps>(TypeList<Comps...>) {
		return View<false, Comps...>(*this, AnyList{}, NoneList{}, key, requiredMask, anyMask, noneMask, requiredTags, anyTags, noneTags);
	}(YieldList{});
}

/*! @brief Const overload for All + Any + None filtered view. */
template <AllType AllF, AnyType AnyF, NoneType NoneF>
auto view() const
{
	using ReqList = PackExtractor<AllF>::type;
	using YieldList = RegularTypeList<ReqList>::type;
	using AnyList = PackExtractor<AnyF>::type;
	using NoneList = PackExtractor<NoneF>::type;
	constexpr ComponentMask requiredMask{buildMaskFromList<ReqList>()};
	constexpr ComponentMask anyMask{buildMaskFromList<AnyList>()};
	constexpr ComponentMask noneMask{buildMaskFromList<NoneList>()};
	constexpr ComponentMask requiredTags{buildTagMaskFromList<ReqList>()};
	constexpr ComponentMask anyTags{buildTagMaskFromList<AnyList>()};
	constexpr ComponentMask noneTags{buildTagMaskFromList<NoneList>()};
	QueryKey key{
		.required = requiredMask,
		.any = anyMask,
		.none = noneMask,
		.requiredTags = requiredTags,
		.anyTags = anyTags,
		.noneTags = noneTags,
	};
	return [&]<typename... Comps>(TypeList<Comps...>) {
		return View<true, Comps...>(*this, AnyList{}, NoneList{}, key, requiredMask, anyMask, noneMask, requiredTags, anyTags, noneTags);
	}(YieldList{});
}
