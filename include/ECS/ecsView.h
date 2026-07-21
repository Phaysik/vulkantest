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
	public:
		using ECSType = std::conditional_t<IsConst, const ECS, ECS>;
		using ArchVec = std::vector<Archetype *>;
		using BytePtr = std::conditional_t<IsConst, const std::byte *, std::byte *>;
		using value_type = std::tuple<Entity, std::conditional_t<IsConst, const Components &, Components &>...>;
		using difference_type = std::ptrdiff_t;

		// Sentinel (end) constructor
		ViewIterator() noexcept = default;

		// Begin constructor
		ViewIterator(const ArchVec *archetypes, std::size_t archIdx) noexcept : mArchetypes(archetypes), mArchIdx(static_cast<ui>(archIdx))
		{
			seekNonEmpty();
		}

		value_type operator*() const noexcept
		{
			// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
			return value_type{mEntityArr[mSlot],
							  (*reinterpret_cast<std::conditional_t<IsConst, const Components *, Components *>>(
								  mCompPtrs[componentID<Components>()] + (mSlot * ComponentInfos[componentID<Components>()].size)))...};
			// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
		}

		ViewIterator &operator++() noexcept
		{
			++mSlot;
			if (mSlot >= mEntityCount)
			{
				++mChunkIdx;
				advanceChunk();
			}
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
			return !(*this == other);
		}

	private:
		void seekNonEmpty() noexcept
		{
			mChunkIdx = 0;
			advanceChunk();
		}

		void advanceChunk() noexcept
		{
			while (mArchetypes != nullptr && mArchIdx < static_cast<ui>(mArchetypes->size()))
			{
				// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
				Archetype *arch{(*mArchetypes)[mArchIdx]};
				const ui chunkCount{arch->getChunkCount()};

				while (mChunkIdx < chunkCount)
				{
					mEntityCount = arch->getEntityCount(mChunkIdx);
					if (mEntityCount > 0)
					{
						mEntityArr = arch->getEntityArray(mChunkIdx);
						loadCompPtrs(arch);
						mSlot = 0;
						return;
					}
					++mChunkIdx;
				}

				++mArchIdx;
				mChunkIdx = 0;
			}
		}

		void loadCompPtrs(Archetype *arch) noexcept
		{
			((mCompPtrs[componentID<Components>()] = static_cast<BytePtr>(arch->getComponentArray(mChunkIdx, componentID<Components>()))),
			 ...);
		}

		const ArchVec *mArchetypes{nullptr};
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		std::array<BytePtr, MAX_COMPONENTS> mCompPtrs{};
		Entity *mEntityArr{nullptr};
		ui mArchIdx{0};
		ui mChunkIdx{0};
		ui mSlot{0};
		ui mEntityCount{0};
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
	public:
		using iterator = ViewIterator<IsConst, Components...>;
		using ECSType = std::conditional_t<IsConst, const ECS, ECS>;

		explicit View(ECSType &ecs) noexcept : mECS(&ecs)
		{
			constexpr ComponentMask requiredRegular{buildRequiredMask<Components...>()};
			mMatchingArchetypes = &mECS->mQueryCache.get(requiredRegular);
		}

		ATTR_NODISCARD iterator begin() const noexcept
		{
			return iterator(mMatchingArchetypes, 0);
		}

		ATTR_NODISCARD iterator end() const noexcept
		{
			return iterator{};
		}

	private:
		ECSType *mECS;
		const std::vector<Archetype *> *mMatchingArchetypes{nullptr};
};

/*! @brief Create a view for iterating entities with the specified component types.
	@tparam Components Component types to require and iterate.
	@return A `View` object supporting range-for with structured bindings.
	@note Usage: `for (auto [e, pos, vel] : ecs.view<Position, Velocity>()) { ... }`
*/
template <typename... Components>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
View<false, Components...> view()
{
	return View<false, Components...>(*this);
}

/*! @brief Const overload for creating a read-only view. */
template <typename... Components>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
View<true, Components...> view() const
{
	return View<true, Components...>(*this);
}
