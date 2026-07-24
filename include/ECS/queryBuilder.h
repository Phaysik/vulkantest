/*! @file queryBuilder.h
	@brief QueryBuilder nested class definition, included inside the ECS class body.
	@details This file is not meant to be included directly. It is included by ecs.h inside the `ECS` class definition
   to split the large header into manageable pieces while preserving the nested class relationship.
	@date 07/21/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_ECS_H
	#error "queryBuilder.h must not be included directly. Include ecs.h instead."
#endif

// MARK: QueryBuilder

/*! @class QueryBuilder include/ECS/queryBuilder.h
	@brief Composable query builder that replaces the combinatorial `forEach` overload surface.
	@details Constructed via `ECS::query<...>()`. Chain `.policy()`, `.version()`, and `.commands()` to configure,
   then call `.forEach(func)` to execute. Eliminates the need for separate overloads per combination of execution
   policy, SystemVersion, and CommandBuffer.
	@tparam IsConst Whether the ECS reference is const-qualified (selects const processing helpers).
	@tparam ReqList `TypeList<...>` of required component types.
	@tparam AnyList `TypeList<...>` of alternative-match component types (default empty).
	@tparam NoneList `TypeList<...>` of excluded component types (default empty).
	@tparam ReadList `TypeList<...>` of required components exposed as const references.
	@note The builder is lightweight and designed for chaining on temporaries. It does not own any resources.
*/
template <bool IsConst, typename ReqList, typename AnyList = TypeList<>, typename NoneList = TypeList<>, typename ReadList = TypeList<>>
class QueryBuilder
{

	public:
		using SelfType = std::conditional_t<IsConst, const ECS, ECS>;

		explicit QueryBuilder(SelfType &ecs) noexcept : mECS(&ecs), mWriteMask(IsConst ? ComponentMask(0) : buildMaskFromList<ReqList>()) {}

		/*! @brief Set the execution policy for this query.
			@param[in] p Execution policy (Seq, Par, ParBatched, ParStealing).
			@return Reference to this builder for chaining.
		*/
		QueryBuilder &policy(const ExecutionPolicy execPolicy) noexcept
		{
			mPolicy = execPolicy;
			return *this;
		}

		/*! @brief Attach a SystemVersion for dirty-chunk filtering.
			@param[in,out] v SystemVersion used to skip unchanged chunks and updated after processing.
			@return Reference to this builder for chaining.
		*/
		QueryBuilder &version(SystemVersion &sysVersion) noexcept
		{
			mVersion = &sysVersion;
			return *this;
		}

		/*! @brief Specify component types for change-detection filtering.
			@tparam Ts Component types that must have changed for a chunk to be processed.
			@details Narrows the dirty-chunk filter so that only chunks where at least one of the
			   specified component types has a newer version (relative to the attached `SystemVersion`)
			   are processed. Has no effect if no `SystemVersion` is attached via `.version()`.
			@return Reference to this builder for chaining.
		*/
		template <typename... Ts>
		QueryBuilder &changed()
		{
			const ComponentMask requested{buildRequiredMask<Ts...>()};
			const ComponentMask required{buildMaskFromList<ReqList>()};
			// NOLINTNEXTLINE(readability-redundant-parentheses)
			if ((requested & required) != requested)
			{
				throw std::invalid_argument("Changed components must be required regular components");
			}
			mChangedMask = requested;
			return *this;
		}

		/*! @brief Attach a CommandBuffer for deferred command recording.
			@param[in,out] c CommandBuffer that the caller should capture by reference in their callable. Stored for potential future use
		   (e.g. thread-local command buffer distribution). Does not inject the buffer into the callback signature — capture it explicitly.
			@return Reference to this builder for chaining.
		*/
		QueryBuilder &commands(ATTR_MAYBE_UNUSED CommandBuffer &cmdBuffer) noexcept
		{
			mCmds = &cmdBuffer;
			return *this;
		}

		/*! @brief Declares required components that this query only reads.
			@tparam Ts Required regular component types exposed to the callback as const references and removed from the write set.
			@return A builder carrying the updated compile-time read access declaration.
		*/
		template <typename... Ts>
		ATTR_NODISCARD auto read()
		{
			const ComponentMask requested{buildRequiredMask<Ts...>()};
			const ComponentMask required{buildMaskFromList<ReqList>()};
			// NOLINTNEXTLINE(readability-redundant-parentheses)
			if ((requested & required) != requested)
			{
				throw std::invalid_argument("Query read access must name required regular components");
			}
			using NextReadList = typename TypeListConcat<ReadList, TypeList<Ts...>>::type;
			ComponentMask nextWriteMask{mWriteMask};
			forEachSetBit(requested, [&](const ComponentTypeID componentTypeID) { nextWriteMask.clearBit(componentTypeID); });
			return QueryBuilder<IsConst, ReqList, AnyList, NoneList, NextReadList>{*this, nextWriteMask};
		}

		/*! @brief Declares required components that this query may write.
			@tparam Ts Required regular component types exposed to the callback as mutable references and added to the write set.
			@return A builder carrying the updated compile-time write access declaration.
		*/
		template <typename... Ts>
			requires(!IsConst)
		ATTR_NODISCARD auto write()
		{
			const ComponentMask requested{buildRequiredMask<Ts...>()};
			const ComponentMask required{buildMaskFromList<ReqList>()};
			// NOLINTNEXTLINE(readability-redundant-parentheses)
			if ((requested & required) != requested)
			{
				throw std::invalid_argument("Query write access must name required regular components");
			}
			using NextReadList = typename TypeListRemove<ReadList, Ts...>::type;
			ComponentMask nextWriteMask{mWriteMask | requested};
			return QueryBuilder<IsConst, ReqList, AnyList, NoneList, NextReadList>{*this, nextWriteMask};
		}

		/*! @brief Execute the query, invoking @p func for each matching entity.
			@tparam Func Callable type compatible with the per-entity processing helpers.
			@param[in] func User callable forwarded to the appropriate dispatch implementation.
			@note If a `CommandBuffer` was attached via `.commands()`, the caller must capture it
			   by reference in their lambda — it is not injected into the callback signature.
		*/
		template <typename Func>
		void forEach(Func &&func)
		{
			const IterationGuard iterGuard{mECS->mStructuralMutex};
			constexpr bool isFilterQuery{(TypeListSize<AnyList>::value > 0) || (TypeListSize<NoneList>::value > 0)};

			if constexpr (!isFilterQuery)
			{
				[&]<typename... Comps>(TypeList<Comps...>) {
					auto accessFunction{makeAccessFunction<Comps...>(std::forward<Func>(func))};
					dispatchRaw<Comps...>(std::move(accessFunction));
				}(ReqList{});
			}
			else
			{
				[&]<typename... Comps>(TypeList<Comps...>) {
					auto accessFunction{makeAccessFunction<Comps...>(std::forward<Func>(func))};
					dispatchFiltered(std::move(accessFunction));
				}(ReqList{});
			}
		}

	private:
		using DirtyChunks = std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>>;

		template <bool, typename, typename, typename, typename>
		friend class QueryBuilder;

		template <typename OtherReadList>
		explicit QueryBuilder(const QueryBuilder<IsConst, ReqList, AnyList, NoneList, OtherReadList> &other,
						  const ComponentMask writeMask) noexcept
			: mECS(other.mECS), mPolicy(other.mPolicy), mVersion(other.mVersion), mCmds(other.mCmds), mChangedMask(other.mChangedMask),
			  mWriteMask(writeMask)
		{}

		template <typename Component, typename Arg>
		static decltype(auto) applyComponentAccess(Arg &&arg)
		{
			if constexpr (!isTagV<Component> && (IsConst || TypeListContains<Component, ReadList>::value))
			{
				return std::as_const(arg);
			}
			else
			{
				return std::forward<Arg>(arg);
			}
		}

		template <typename... Components, typename Func>
		static auto makeAccessFunction(Func &&func)
		{
			return [function = std::forward<Func>(func)](auto &&entity, auto &&...components) {
				static_assert(sizeof...(Components) == sizeof...(components));
				auto componentTuple{std::forward_as_tuple(std::forward<decltype(components)>(components)...)};
				[&]<std::size_t... Index>(std::index_sequence<Index...>) {
					function(std::forward<decltype(entity)>(entity),
							 applyComponentAccess<Components>(std::get<Index>(componentTuple))...);
				}(std::index_sequence_for<Components...>{});
			};
		}

		template <typename... Components, typename Func>
		void dispatchRaw(Func &&func)
		{
			if (mVersion == nullptr)
			{
				ECS::forEachPolicyImpl<SelfType, Components...>(*mECS, mPolicy, std::forward<Func>(func), mWriteMask);
				return;
			}

			if (!mChangedMask)
			{
				ECS::forEachPolicyVersionImpl<SelfType, Components...>(*mECS, mPolicy, *mVersion, std::forward<Func>(func), mWriteMask);
				return;
			}

			processChangedRaw<Components...>(func);
		}

		template <typename Func>
		void dispatchFiltered(Func &&func)
		{
			if (mVersion == nullptr)
			{
				ECS::forEachQueryImpl<SelfType, ReqList, AnyList, NoneList>(*mECS, mPolicy, std::forward<Func>(func), mWriteMask);
				return;
			}

			if (!mChangedMask)
			{
				ECS::forEachQueryVersionImpl<SelfType, ReqList, AnyList, NoneList>(*mECS, mPolicy, *mVersion, std::forward<Func>(func),
																				   mWriteMask);
				return;
			}

			processChangedFiltered(func);
		}

		void markProcessedWrites(Archetype *archetype, const ui chunkIndex, const bool processedAny)
		{
			if constexpr (!IsConst)
			{
				if (processedAny)
				{
					mECS->markComponentsChanged(archetype, chunkIndex, mWriteMask);
				}
			}
		}

		template <typename Func, typename Process>
		void processTrackedChunk(Archetype *archetype, const ui chunkIndex, Func &func, Process &&process)
		{
			bool processedAny{false};
			auto trackedFunction = [&](auto &&...args) {
				processedAny = true;
				func(std::forward<decltype(args)>(args)...);
			};

			try
			{
				process(trackedFunction);
			}
			catch (...)
			{
				markProcessedWrites(archetype, chunkIndex, processedAny);
				throw;
			}

			markProcessedWrites(archetype, chunkIndex, processedAny);
		}

		template <typename ProcessChunk>
		void executeDirtyChunks(const DirtyChunks &dirtyChunks, ProcessChunk &processChunk, const ComponentMask requiredMask)
		{
			auto noOp = [](const auto &) {};

			switch (mPolicy)
			{
				case ExecutionPolicy::Seq:
					forEachSeqProcessChunkAndVersion(dirtyChunks, processChunk, noOp);
					break;
				case ExecutionPolicy::Par:
					forEachParProcessChunkAndVersion(dirtyChunks, mECS->mThreadPool, processChunk, noOp);
					break;
				case ExecutionPolicy::ParBatched:
					forEachParBatchedProcessChunkAndVersion(dirtyChunks, mECS->mThreadPool, processChunk, noOp);
					break;
				case ExecutionPolicy::ParStealing:
					forEachParStealingProcessChunkAndVersion(dirtyChunks, mECS->mWorkStealingPool, processChunk, noOp);
					break;
				default:
					assert(false && "Invalid execution policy");
			}

			bulkMergeVersions(*mVersion, dirtyChunks, requiredMask);
		}

		template <typename... Components, typename Func>
		void processRawChunk(Archetype *archetype, const ui chunkIndex, Func &func)
		{
			const IterationGuard workerGuard{mECS->mStructuralMutex};
			const ui entityCount{archetype->getEntityCount(chunkIndex)};
			processTrackedChunk(archetype, chunkIndex, func, [&](auto &trackedFunction) {
				if constexpr (IsConst)
				{
					const Entity *entities{archetype->getEntityArray(chunkIndex)};
					processChunkEntitiesConst<Components...>(entities, entityCount, chunkIndex, archetype, trackedFunction);
				}
				else
				{
					Entity *entities{archetype->getEntityArray(chunkIndex)};
					processChunkEntities<Components...>(entities, entityCount, chunkIndex, archetype, trackedFunction);
				}
			});
		}

		template <typename... Components, typename Func>
		void processChangedRaw(Func &func)
		{
			constexpr ComponentMask requiredMask{buildRequiredMask<Components...>()};
			const std::vector<Archetype *> &matchingArchetypes{mECS->mQueryCache.get(requiredMask)};
			DirtyChunks dirtyChunks;
			setDirtyChunks(dirtyChunks, matchingArchetypes, *mVersion, requiredMask, mChangedMask);
			if (dirtyChunks.empty())
			{
				return;
			}

			auto processChunk
				= [&func, this](Archetype *archetype, const ui chunkIndex) { processRawChunk<Components...>(archetype, chunkIndex, func); };
			executeDirtyChunks(dirtyChunks, processChunk, requiredMask);
		}

		template <typename Func>
		void processFilteredChunk(Archetype *archetype, const ui chunkIndex, Func &func, const ComponentMask requiredTags,
								  const ComponentMask anyTags, const ComponentMask noneTags, const ComponentMask anyMask)
		{
			const IterationGuard workerGuard{mECS->mStructuralMutex};
			const ui entityCount{archetype->getEntityCount(chunkIndex)};
			const bool anyRegularMatched{static_cast<bool>(archetype->getRegularMask() & anyMask)};
			constexpr bool hasAnyClause{TypeListSize<AnyList>::value != 0};

			processTrackedChunk(archetype, chunkIndex, func, [&](auto &trackedFunction) {
				if constexpr (IsConst)
				{
					const Entity *entities{archetype->getEntityArray(chunkIndex)};
					[&]<typename... Required>(TypeList<Required...>) {
						processChunkEntitiesFilteredConst<Required...>(entities, entityCount, chunkIndex, archetype, trackedFunction,
																	   requiredTags, anyTags, noneTags, hasAnyClause, anyRegularMatched);
					}(ReqList{});
				}
				else
				{
					Entity *entities{archetype->getEntityArray(chunkIndex)};
					[&]<typename... Required>(TypeList<Required...>) {
						processChunkEntitiesFiltered<Required...>(entities, entityCount, chunkIndex, archetype, trackedFunction,
																  requiredTags, anyTags, noneTags, hasAnyClause, anyRegularMatched);
					}(ReqList{});
				}
			});
		}

		template <typename Func>
		void processChangedFiltered(Func &func)
		{
			constexpr ComponentMask requiredMask{buildMaskFromList<ReqList>()};
			constexpr ComponentMask anyMask{buildMaskFromList<AnyList>()};
			constexpr ComponentMask noneMask{buildMaskFromList<NoneList>()};
			constexpr ComponentMask requiredTags{buildTagMaskFromList<ReqList>()};
			constexpr ComponentMask anyTags{buildTagMaskFromList<AnyList>()};
			constexpr ComponentMask noneTags{buildTagMaskFromList<NoneList>()};
			const QueryKey key{
				.required = requiredMask,
				.any = anyMask,
				.none = noneMask,
				.requiredTags = requiredTags,
				.anyTags = anyTags,
				.noneTags = noneTags,
			};
			const std::vector<Archetype *> &matchingArchetypes{
				getMatchingArchetypesForQueryCalls<SelfType, AnyList, NoneList>(*mECS, key, requiredMask, anyMask, noneMask),
			};

			DirtyChunks dirtyChunks;
			setDirtyChunks(dirtyChunks, matchingArchetypes, *mVersion, requiredMask, mChangedMask);
			if (dirtyChunks.empty())
			{
				return;
			}

			auto processChunk = [this, &func, requiredTags, anyTags, noneTags, anyMask](Archetype *archetype, const ui chunkIndex) {
				processFilteredChunk(archetype, chunkIndex, func, requiredTags, anyTags, noneTags, anyMask);
			};
			executeDirtyChunks(dirtyChunks, processChunk, requiredMask);
		}

		SelfType *mECS;
		ExecutionPolicy mPolicy{ExecutionPolicy::Seq};
		SystemVersion *mVersion{nullptr};
		CommandBuffer *mCmds{nullptr};
		ComponentMask mChangedMask{0, 0};
		ComponentMask mWriteMask{0, 0};
};

// MARK: query<>() Factory Methods

/*! @brief Create a query builder for raw component types.
	@tparam Components Component types to require in the query.
	@return A `QueryBuilder` configured for the specified components.
*/
template <typename... Components>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
QueryBuilder<false, TypeList<Components...>> query()
{
	return QueryBuilder<false, TypeList<Components...>>(*this);
}

/*! @brief Const overload for raw component query builder. */
template <typename... Components>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
QueryBuilder<true, TypeList<Components...>> query() const
{
	return QueryBuilder<true, TypeList<Components...>>(*this);
}

/*! @brief Create a query builder with All + Any + None filter clauses. */
template <AllType AllF, AnyType AnyF, NoneType NoneF>
auto query()
{
	using R = PackExtractor<AllF>::type;
	using A = PackExtractor<AnyF>::type;
	using N = PackExtractor<NoneF>::type;
	return QueryBuilder<false, R, A, N>(*this);
}

/*! @brief Const overload for All + Any + None query builder. */
template <AllType AllF, AnyType AnyF, NoneType NoneF>
auto query() const
{
	using R = PackExtractor<AllF>::type;
	using A = PackExtractor<AnyF>::type;
	using N = PackExtractor<NoneF>::type;
	return QueryBuilder<true, R, A, N>(*this);
}

/*! @brief Create a query builder with All + None filter clauses. */
template <AllType AllF, NoneType NoneF>
auto query()
{
	return QueryBuilder<false, typename PackExtractor<AllF>::type, TypeList<>, typename PackExtractor<NoneF>::type>(*this);
}

/*! @brief Const overload for All + None query builder. */
template <AllType AllF, NoneType NoneF>
auto query() const
{
	return QueryBuilder<true, typename PackExtractor<AllF>::type, TypeList<>, typename PackExtractor<NoneF>::type>(*this);
}

/*! @brief Create a query builder with All + Any filter clauses. */
template <AllType AllF, AnyType AnyF>
auto query()
{
	return QueryBuilder<false, typename PackExtractor<AllF>::type, typename PackExtractor<AnyF>::type, TypeList<>>(*this);
}

/*! @brief Const overload for All + Any query builder. */
template <AllType AllF, AnyType AnyF>
auto query() const
{
	return QueryBuilder<true, typename PackExtractor<AllF>::type, typename PackExtractor<AnyF>::type, TypeList<>>(*this);
}

/*! @brief Create a query builder with Any + None filter clauses. */
template <AnyType AnyF, NoneType NoneF>
auto query()
{
	return QueryBuilder<false, TypeList<>, typename PackExtractor<AnyF>::type, typename PackExtractor<NoneF>::type>(*this);
}

/*! @brief Const overload for Any + None query builder. */
template <AnyType AnyF, NoneType NoneF>
auto query() const
{
	return QueryBuilder<true, TypeList<>, typename PackExtractor<AnyF>::type, typename PackExtractor<NoneF>::type>(*this);
}

/*! @brief Create a query builder with All filter only. */
template <AllType AllF>
auto query()
{
	return QueryBuilder<false, typename PackExtractor<AllF>::type>(*this);
}

/*! @brief Const overload for All-only query builder. */
template <AllType AllF>
auto query() const
{
	return QueryBuilder<true, typename PackExtractor<AllF>::type>(*this);
}

/*! @brief Create a query builder with Any filter only. */
template <AnyType AnyF>
auto query()
{
	return QueryBuilder<false, TypeList<>, typename PackExtractor<AnyF>::type>(*this);
}

/*! @brief Const overload for Any-only query builder. */
template <AnyType AnyF>
auto query() const
{
	return QueryBuilder<true, TypeList<>, typename PackExtractor<AnyF>::type>(*this);
}

/*! @brief Create a query builder with None filter only. */
template <NoneType NoneF>
auto query()
{
	return QueryBuilder<false, TypeList<>, TypeList<>, typename PackExtractor<NoneF>::type>(*this);
}

/*! @brief Const overload for None-only query builder. */
template <NoneType NoneF>
auto query() const
{
	return QueryBuilder<true, TypeList<>, TypeList<>, typename PackExtractor<NoneF>::type>(*this);
}
