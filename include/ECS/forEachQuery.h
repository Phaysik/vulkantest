/*! @file forEachQuery.h
	@brief Query-filter forEach implementations and overloads, included inside the ECS class body.
	@details This file is not meant to be included directly. It is included by ecs.h inside the ECS class definition.
	@date 07/21/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_ECS_H
	#error "forEachQuery.h must not be included directly. Include ecs.h instead."
#endif

/*! @brief Core implementation that executes a query described by three type-lists.
	@details This function builds `ComponentMask` values from the supplied type-lists, constructs a `QueryKey`, queries the
   archetype cache to find matching archetypes, then dispatches per-chunk processing according to @p policy. The implementation
   is responsible for selecting const vs non-const per-chunk helpers and for forwarding the user callable into worker tasks when
   parallel policies are selected. The routine is intended to be instantiated at compile-time for specific type-lists and
   callables, producing zero-overhead dispatch into per-chunk processing.
	@tparam Self The `ECS` type (possibly const-qualified) used for dispatching and accessing caches/pools.
	@tparam ReqList A `TypeList<...>` containing component/tag types that are required for the query.
	@tparam AnyList A `TypeList<...>` containing component/tag types where any single match satisfies the clause.
	@tparam NoneList A `TypeList<...>` containing component/tag types that must be absent.
	@tparam Func Callable type invoked for matching entities or chunks; this callable is forwarded into the per-chunk processing
   helpers and must be compatible with the `processChunkEntities...` helpers.
	@param[in,out] self Reference to the `ECS` instance used to resolve archetypes, thread pools, and caches.
	@param[in] policy Execution policy that controls whether processing is sequential or parallel (see `ExecutionPolicy`).
	@param[in] func User-provided callable that will be invoked for matching entities or chunks.
*/
template <class Self, typename ReqList, typename AnyList, typename NoneList, typename Func>
static void forEachQueryImpl(Self &self, ExecutionPolicy &policy, Func &&func,
							 const ComponentMask writeMask
							 = std::is_const_v<std::remove_reference_t<Self>> ? ComponentMask(0) : buildMaskFromList<ReqList>())
{
	const IterationGuard iterGuard{self.mStructuralMutex};

	constexpr ComponentMask requiredMask{buildMaskFromList<ReqList>()};
	constexpr ComponentMask anyMask{buildMaskFromList<AnyList>()};
	constexpr ComponentMask noneMask{buildMaskFromList<NoneList>()};
	constexpr ComponentMask requiredTags{buildTagMaskFromList<ReqList>()};
	constexpr ComponentMask anyTags{buildTagMaskFromList<AnyList>()};
	constexpr ComponentMask noneTags{buildTagMaskFromList<NoneList>()};
	constexpr bool hasAnyClause{TypeListSize<AnyList>::value != 0};

	QueryKey const key{
		.required = requiredMask, .any = anyMask, .none = noneMask, .requiredTags = requiredTags, .anyTags = anyTags, .noneTags = noneTags,};

	const std::vector<Archetype *> &matchingArchetypes{
		getMatchingArchetypesForQueryCalls<Self, AnyList, NoneList>(self, key, requiredMask, anyMask, noneMask),};

	const Func forwardedFunction{std::forward<Func>(func)};

	// Process each matching archetype (sequential; extend to parallel as needed)
	auto processChunk = [&](Archetype *arch, const ui chunkIndex, const ui entityCount) {
		const IterationGuard workerGuard{self.mStructuralMutex};
		const bool anyRegularMatched{static_cast<bool>(arch->getRegularMask() & anyMask)};
		bool processedAny{false};
		auto trackedFunction = [&](auto &&...args) {
			processedAny = true;
			forwardedFunction(std::forward<decltype(args)>(args)...);
		};
		auto stampWrites = [&] {
			if constexpr (!std::is_const_v<std::remove_reference_t<Self>>)
			{
				if (processedAny)
				{
					self.markComponentsChanged(arch, chunkIndex, writeMask);
				}
			}
		};
		try
		{
			if constexpr (std::is_const_v<std::remove_reference_t<Self>>)
			{
				const Entity *entities{arch->getEntityArray(chunkIndex)};

				[&]<typename... Req>(TypeList<Req...>) {
					processChunkEntitiesFilteredConst<Req...>(entities, entityCount, chunkIndex, arch, trackedFunction, requiredTags,
															  anyTags, noneTags, hasAnyClause, anyRegularMatched);
				}(ReqList{});
			}
			else
			{
				Entity *entities{arch->getEntityArray(chunkIndex)};

				[&]<typename... Req>(TypeList<Req...>) {
					processChunkEntitiesFiltered<Req...>(entities, entityCount, chunkIndex, arch, trackedFunction, requiredTags, anyTags,
														 noneTags, hasAnyClause, anyRegularMatched);
				}(ReqList{});
			}
		}
		catch (...)
		{
			stampWrites();
			throw;
		}
		stampWrites();
	};

	switch (policy)
	{
		case ExecutionPolicy::Seq:
			forEachSeqProcessChunkOnly(matchingArchetypes, processChunk);
			break;
		case ExecutionPolicy::Par:
			forEachParProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
			break;
		case ExecutionPolicy::ParBatched:
			forEachParBatchedProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
			break;
		case ExecutionPolicy::ParStealing:
			forEachParStealingProcessChunkOnly(matchingArchetypes, self.mWorkStealingPool, processChunk);
			break;
		default:
			assert(false && "Invalid execution policy");
	}
}

/*! @brief Query overload that accepts query clause wrappers (`All<...>`, `Any<...>`, `None<...>`) and executes the supplied
   callable using the given `ExecutionPolicy`.
	@tparam AllFilter Type of `All<...>` clause specifying required components.
	@tparam AnyFilter Type of `Any<...>` clause specifying optional-match components.
	@tparam NoneFilter Type of `None<...>` clause specifying excluded components.
	@tparam Func Callable type invoked for matching entities or chunks.
	@param[in] policy Execution policy that controls parallelism and batching.
	@param[in] func User-provided callable forwarded to the underlying implementation.
	@note This overload transforms the clause wrappers into `TypeList` aliases via `PackExtractor` and forwards to
   `forEachQueryImpl` which performs archetype matching and dispatch.
*/
template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, Func &&func)
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
}

/*! @brief Const-qualified variant of the query overload that accepts clause wrappers and runs the callable without permitting
   modification of `ECS` internal state.
	@tparam AllFilter Type of `All<...>` clause specifying required components.
	@tparam AnyFilter Type of `Any<...>` clause specifying optional-match components.
	@tparam NoneFilter Type of `None<...>` clause specifying excluded components.
	@tparam Func Callable type invoked for matching entities or chunks; must be compatible with const processing helpers.
	@param[in] policy Execution policy that controls parallelism and batching.
	@param[in] func User-provided callable forwarded to the underlying const implementation.
	@note Use this overload for read-only systems; it forwards to `forEachQueryImpl` instantiated with a const-qualified `Self`
   so const-safe processing helpers are selected.
*/
template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, Func &&func) const
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
}

// MARK: forEachQueryImpl 2 operator overloads

/*! @brief Query overload for `All<...>` (required) together with `None<...>` (excluded) clause wrappers.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked for matching entities or chunks.
	@param[in] policy Execution policy to use.
	@param[in] func User-provided callable forwarded to `forEachQueryImpl`.
	@note This overload sets the `Any` clause to empty (`TypeList<>`) and forwards to the core query implementation.
*/
template <AllType AllFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, Func &&func)
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = TypeList<>; // empty – no "any" filter
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
}

/*! @brief Const variant of the `All` + `None` query overload.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked for matching entities or chunks; must be compatible with const processing helpers.
	@param[in] policy Execution policy to use.
	@param[in] func User-provided callable forwarded to the core const implementation.
*/
template <AllType AllFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, Func &&func) const
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = TypeList<>;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
}

/*! @brief Query overload for `All<...>` (required) and `Any<...>` (at-least-one) clause wrappers.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
	@tparam Func Callable invoked for matching entities or chunks.
	@param[in] policy Execution policy to use.
	@param[in] func User-provided callable forwarded to `forEachQueryImpl`.
	@note This overload sets the `None` clause to empty (`TypeList<>`).
*/
template <AllType AllFilter, AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, Func &&func)
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = TypeList<>; // empty – no "none" filter
	forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
}

/*! @brief Const variant of the `All` + `Any` query overload.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
	@tparam Func Callable type compatible with const processing helpers.
	@param[in] policy Execution policy to use.
	@param[in] func User-provided callable forwarded to the core const implementation.
*/
template <AllType AllFilter, AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, Func &&func) const
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = TypeList<>;
	forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
}

/*! @brief Query overload for `Any<...>` (at-least-one) together with `None<...>` (excluded) clause wrappers.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked for matching entities or chunks.
	@param[in] policy Execution policy to use.
	@param[in] func User-provided callable forwarded to `forEachQueryImpl`.
	@note This overload sets the `All` (required) clause to empty (`TypeList<>`).
*/
template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, Func &&func)
{
	using ReqList = TypeList<>; // empty – no required components
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
}

/*! @brief Const variant of the `Any` + `None` query overload.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable type compatible with const processing helpers.
	@param[in] policy Execution policy to use.
	@param[in] func User-provided callable forwarded to the core const implementation.
*/
template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, Func &&func) const
{
	using ReqList = TypeList<>;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
}

// MARK: forEachQueryImpl 1 operator overloads

/*! @brief Query overload that iterates entities matching a required-only `All<...>` clause.
	@tparam AllFilter A `All<...>` clause listing required component/tag types.
	@tparam Func Callable invoked for matching entities or chunks; the callable will be forwarded to the mutable processing
   helpers and may modify ECS state or components.
	@param[in] policy Execution policy controlling parallelism and batching (Seq, Par, ParBatched, ParStealing).
	@param[in] func User-provided callable forwarded to `forEachQueryImpl`.
	@note This overload converts `AllFilter` into a `TypeList` via `PackExtractor` and forwards to the core implementation
	with empty `Any` and `None` clauses.
*/
template <AllType AllFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, Func &&func)
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = TypeList<>;
	using NoneList = TypeList<>;
	forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
}

/*! @brief Const query overload for a required-only `All<...>` clause.
	@tparam AllFilter A `All<...>` clause listing required component/tag types.
	@tparam Func Callable invoked for matching entities or chunks; must be compatible with the const processing helpers.
	@param[in] policy Execution policy to use for processing (Seq, Par, ParBatched, ParStealing).
	@param[in] func User-provided callable forwarded to the underlying implementation.
	@note This overload converts `AllFilter` to a `TypeList` via `PackExtractor` and forwards to `forEachQueryImpl` with empty
   `Any`/`None` clauses.
*/
template <AllType AllFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, Func &&func) const
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = TypeList<>;
	using NoneList = TypeList<>;
	forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
}

/*! @brief Query overload that iterates entities matching an `Any<...>` clause (at-least-one match required).
	@tparam AnyFilter An `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
	@tparam Func Callable invoked for matching entities or chunks; the callable will be forwarded to the mutable processing
   helpers and may modify ECS state or components.
	@param[in] policy Execution policy controlling parallelism and batching (Seq, Par, ParBatched, ParStealing).
	@param[in] func User-provided callable forwarded to the underlying implementation.
	@note This overload converts `AnyFilter` into a `TypeList` via `PackExtractor` and forwards to `forEachQueryImpl` with empty
   `All` and `None` clauses.
*/
template <AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, Func &&func)
{
	using ReqList = TypeList<>;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = TypeList<>;
	forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
}

/*! @brief Const query overload for an `Any<...>` (at-least-one) clause.
	@tparam AnyFilter An `Any<...>` clause listing alternative component/tag types where any one match satisfies the clause.
	@tparam Func Callable invoked for matching entities or chunks; must be compatible with the const processing helpers.
	@param[in] policy Execution policy to use for processing.
	@param[in] func User-provided callable forwarded to the underlying implementation.
	@note This overload converts `AnyFilter` to a `TypeList` and uses empty `All`/`None` clauses.
*/
template <AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, Func &&func) const
{
	using ReqList = TypeList<>;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = TypeList<>;
	forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
}

/*! @brief Query overload that iterates entities while excluding types listed in `None<...>`.
	@tparam NoneFilter A `None<...>` clause listing component/tag types that must be absent for a match.
	@tparam Func Callable invoked for matching entities or chunks; the callable will be forwarded to the mutable
	processing helpers and may modify ECS state or components.
	@param[in] policy Execution policy controlling parallelism and batching (Seq, Par, ParBatched, ParStealing).
	@param[in] func User-provided callable forwarded to the underlying implementation.
	@note This overload converts `NoneFilter` into a `TypeList` via `PackExtractor` and forwards to `forEachQueryImpl`
	with empty `All` and `Any` clauses.
*/
template <NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, Func &&func)
{
	using ReqList = TypeList<>;
	using AnyList = TypeList<>;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
}

/*! @brief Const query overload for a `None<...>` (excluded) clause.
	@tparam NoneFilter A `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked for matching entities or chunks; must be compatible with the const processing helpers.
	@param[in] policy Execution policy to use for processing.
	@param[in] func User-provided callable forwarded to the underlying implementation.
	@note This overload converts `NoneFilter` to a `TypeList` and uses empty `All`/`Any` clauses.
*/
template <NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, Func &&func) const
{
	using ReqList = TypeList<>;
	using AnyList = TypeList<>;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, std::forward<Func>(func));
}

// MARK: forEachQueryVersionImpl

/*! @brief Dispatch and process chunks filtered by a `SystemVersion` (dirty-chunk processing).
	@details Builds masks from the compile-time type lists, finds matching archetypes, collects chunks that are "dirty" with
   respect to @p version, then processes those chunks and updates the provided `SystemVersion` afterwards. The implementation
   selects const vs. non-const processing helpers based on whether `Self` is const-qualified and supports sequential and several
   parallel execution strategies.
	@tparam Self The `ECS` type (possibly const-qualified) used for dispatching and accessing pools/caches.
	@tparam ReqList `TypeList` of required component/tag types.
	@tparam AnyList `TypeList` of alternative types where any single match satisfies the clause.
	@tparam NoneList `TypeList` of excluded component/tag types.
	@tparam Func Callable invoked per entity/chunk; forwarded into the per-chunk processing helpers.
	@param[in,out] self Reference to the `ECS` instance providing archetypes, thread pools and caches.
	@param[in] policy Execution policy controlling parallelism (Seq, Par, ParBatched, ParStealing).
	@param[in,out] version SystemVersion used to determine dirty chunks and updated after processing.
	@param[in] func User callable forwarded into per-chunk processing; must be callable with the signatures used by
	the processing helpers.
	@note Expensive setup (collecting dirty chunks) is performed before dispatch; processing measures and updates component
   versions via `forEachSetBit` to ensure version state is consistent after completion.
*/
template <class Self, typename ReqList, typename AnyList, typename NoneList, typename Func>
static void forEachQueryVersionImpl(Self &self, const ExecutionPolicy &policy, SystemVersion &version, Func &&func,
									const ComponentMask writeMask
									= std::is_const_v<std::remove_reference_t<Self>> ? ComponentMask(0) : buildMaskFromList<ReqList>())
{
	const IterationGuard iterGuard{self.mStructuralMutex};

	constexpr ComponentMask requiredMask{buildMaskFromList<ReqList>()};
	constexpr ComponentMask anyMask{buildMaskFromList<AnyList>()};
	constexpr ComponentMask noneMask{buildMaskFromList<NoneList>()};
	constexpr ComponentMask requiredTags{buildTagMaskFromList<ReqList>()};
	constexpr ComponentMask anyTags{buildTagMaskFromList<AnyList>()};
	constexpr ComponentMask noneTags{buildTagMaskFromList<NoneList>()};
	constexpr bool hasAnyClause{TypeListSize<AnyList>::value != 0};

	QueryKey const key{
		.required = requiredMask, .any = anyMask, .none = noneMask, .requiredTags = requiredTags, .anyTags = anyTags, .noneTags = noneTags,};

	const std::vector<Archetype *> &matchingArchetypes{
		getMatchingArchetypesForQueryCalls<Self, AnyList, NoneList>(self, key, requiredMask, anyMask, noneMask),};

	// Collect chunks that need processing (dirty)
	std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> dirtyChunks;

	setDirtyChunks(dirtyChunks, matchingArchetypes, version, requiredMask);

	if (dirtyChunks.empty())
	{
		return;
	}

	using FuncT = std::decay_t<Func>;
	const FuncT processChunkFunction{std::forward<Func>(func)};

	auto processChunk = [&, processChunkFunction](Archetype *arch, const ui chunkIndex) {
		const IterationGuard workerGuard{self.mStructuralMutex};
		const ui count{arch->getEntityCount(chunkIndex)};
		const bool anyRegularMatched{static_cast<bool>(arch->getRegularMask() & anyMask)};
		bool processedAny{false};
		auto stampWrites = [&] {
			if constexpr (!std::is_const_v<std::remove_reference_t<Self>>)
			{
				if (processedAny)
				{
					self.markComponentsChanged(arch, chunkIndex, writeMask);
				}
			}
		};

		try
		{
			if constexpr (std::is_const_v<std::remove_reference_t<Self>>)
			{
				const Entity *entities{arch->getEntityArray(chunkIndex)};

				[&]<typename... Req>(TypeList<Req...>) {
					processChunkEntitiesFilteredConst<Req...>(
						entities, count, chunkIndex, arch,
						[&](Entity &entity, const auto &...comps) {
							processedAny = true;
							processChunkFunction(entity, comps...);
						},
						requiredTags, anyTags, noneTags, hasAnyClause, anyRegularMatched);
				}(ReqList{});
			}
			else
			{
				Entity *entities{arch->getEntityArray(chunkIndex)};

				[&]<typename... Req>(TypeList<Req...>) {
					processChunkEntitiesFiltered<Req...>(
						entities, count, chunkIndex, arch,
						[&](Entity &entity, auto &&...comps) {
							processedAny = true;
							processChunkFunction(entity, comps...);
						},
						requiredTags, anyTags, noneTags, hasAnyClause, anyRegularMatched);
				}(ReqList{});
			}
		}
		catch (...)
		{
			stampWrites();
			throw;
		}
		stampWrites();
	};

	auto noOpVersionUpdate = [](const auto &) {};

	switch (policy)
	{
		case ExecutionPolicy::Seq:
			forEachSeqProcessChunkAndVersion(dirtyChunks, processChunk, noOpVersionUpdate);
			break;
		case ExecutionPolicy::Par:
			forEachParProcessChunkAndVersion(dirtyChunks, self.mThreadPool, processChunk, noOpVersionUpdate);
			break;
		case ExecutionPolicy::ParBatched:
			forEachParBatchedProcessChunkAndVersion(dirtyChunks, self.mThreadPool, processChunk, noOpVersionUpdate);
			break;
		case ExecutionPolicy::ParStealing:
			forEachParStealingProcessChunkAndVersion(dirtyChunks, self.mWorkStealingPool, processChunk, noOpVersionUpdate);
			break;
		default:
			assert(false && "Invalid execution policy");
	}

	bulkMergeVersions(version, dirtyChunks, requiredMask);
}

/*! @brief Iterate over entities matching the provided clause lists but only process chunks that are dirty according to @p
   version.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (at-least-one match).
	@tparam NoneFilter `None<...>` clause listing excluded component/tag types.
	@tparam Func Callable invoked for matching entities or chunks; will be forwarded into the versioned implementation.
	@param[in] policy Execution policy to use for processing (Seq, Par, ParBatched, ParStealing).
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in] func User callable forwarded to `forEachQueryVersionImpl`.
*/
template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func)
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
}

/*! @brief Const variant of the versioned `forEach` that processes only dirty chunks determined by @p version.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (at-least-one match).
	@tparam NoneFilter `None<...>` clause listing excluded component/tag types.
	@tparam Func Callable invoked for matching entities or chunks; must be compatible with const processing helpers.
	@param[in] policy Execution policy to use for processing.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in] func User callable forwarded to `forEachQueryVersionImpl`.
	@note Use this overload for read-only systems that still require version-aware processing.
*/
template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func) const
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
}

// MARK: forEachQueryVersionImpl 2 operator overloads

/*! @brief Version-aware query overload for `All<...>` + `None<...>` clauses.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked for matching entities or chunks; forwarded to the versioned implementation.
	@param[in] policy Execution policy controlling parallelism.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in] func User-provided callable forwarded to `forEachQueryVersionImpl`.
	@note This overload sets the `Any` clause to empty and forwards to the core versioned implementation.
*/
template <AllType AllFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func)
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = TypeList<>; // empty – no "any" filter
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
}

/*! @brief Const variant of the `All` + `None` versioned overload.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam NoneFilter `None<...>` clause listing excluded component/tag types.
	@tparam Func Callable invoked for matching entities or chunks; must be compatible with const processing helpers.
	@param[in] policy Execution policy controlling parallelism.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in] func User-provided callable forwarded to the core const implementation.
*/
template <AllType AllFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func) const
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = TypeList<>;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
}

/*! @brief Version-aware query overload for `All<...>` + `Any<...>` clauses.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (at-least-one match).
	@tparam Func Callable invoked for matching entities or chunks; forwarded to the versioned implementation.
	@param[in] policy Execution policy controlling parallelism.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in] func User-provided callable forwarded to `forEachQueryVersionImpl`.
	@note This overload sets the `None` clause to empty and forwards to the core versioned implementation.
*/
template <AllType AllFilter, AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func)
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = TypeList<>;
	forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
}

/*! @brief Const variant of the `All` + `Any` versioned overload.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
	@tparam Func Callable invoked for matching entities or chunks; must be compatible with const processing helpers.
	@param[in] policy Execution policy controlling parallelism.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in] func User-provided callable forwarded to the core const implementation.
*/
template <AllType AllFilter, AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func) const
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = TypeList<>;
	forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
}

/*! @brief Version-aware query overload for `Any<...>` + `None<...>` clauses.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked for matching entities or chunks; forwarded to the versioned implementation.
	@param[in] policy Execution policy controlling parallelism.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in] func User-provided callable forwarded to `forEachQueryVersionImpl`.
	@note This overload sets the `All` (required) clause to empty and forwards to the core versioned implementation.
*/
template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func)
{
	using ReqList = TypeList<>;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
}

/*! @brief Const variant of the `Any` + `None` versioned overload.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked for matching entities or chunks; must be compatible with const processing helpers.
	@param[in] policy Execution policy controlling parallelism.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in] func User-provided callable forwarded to the core const implementation.
*/
template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func) const
{
	using ReqList = TypeList<>;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
}

// MARK: forEachQueryVersionImpl 1 operator overloads

/*! @brief Version-aware `forEach` for a required-only `All<...>` clause.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam Func Callable invoked for matching entities or chunks; forwarded to the versioned implementation.
	@param[in] policy Execution policy controlling parallelism.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in] func User-provided callable forwarded to `forEachQueryVersionImpl`.
	@note Use this overload for mutable systems that need version-aware (dirty-chunk) processing.
*/
template <AllType AllFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func)
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = TypeList<>;
	using NoneList = TypeList<>;
	forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
}

/*! @brief Const variant of the required-only versioned `forEach` (`All<...>`).
	@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
	@tparam Func Callable invoked for matching entities or chunks; must be compatible with const processing helpers.
	@param[in] policy Execution policy controlling parallelism.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in] func User-provided callable forwarded to the const versioned implementation.
*/
template <AllType AllFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func) const
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = TypeList<>;
	using NoneList = TypeList<>;
	forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
}

/*! @brief Version-aware `forEach` for an `Any<...>` (at-least-one) clause.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
	@tparam Func Callable invoked for matching entities or chunks; forwarded to the versioned implementation.
	@param[in] policy Execution policy controlling parallelism.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in] func User-provided callable forwarded to `forEachQueryVersionImpl`.
	@note Use this overload when a match requires at least one of the listed component types.
*/
template <AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func)
{
	using ReqList = TypeList<>;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = TypeList<>;
	forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
}

/*! @brief Const variant of the `Any<...>` versioned `forEach`.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (read-only).
	@tparam Func Callable invoked for matching entities or chunks; must be compatible with const processing helpers.
	@param[in] policy Execution policy controlling parallelism.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in] func User-provided callable forwarded to the const versioned implementation.
*/
template <AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func) const
{
	using ReqList = TypeList<>;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = TypeList<>;
	forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
}

/*! @brief Version-aware `forEach` that excludes component types listed in `None<...>`.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent for a match.
	@tparam Func Callable invoked for matching entities or chunks; forwarded to the versioned implementation.
	@param[in] policy Execution policy controlling parallelism.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in] func User-provided callable forwarded to `forEachQueryVersionImpl`.
	@note This overload is useful for queries that only exclude specific components.
*/
template <NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func)
{
	using ReqList = TypeList<>;
	using AnyList = TypeList<>;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
}

/*! @brief Const variant of the `None<...>` versioned `forEach`.
	@tparam NoneFilter `None<...>` clause listing excluded component/tag types (read-only).
	@tparam Func Callable invoked for matching entities or chunks; must be compatible with const processing helpers.
	@param[in] policy Execution policy controlling parallelism.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in] func User-provided callable forwarded to the const versioned implementation.
*/
template <NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, Func &&func) const
{
	using ReqList = TypeList<>;
	using AnyList = TypeList<>;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryVersionImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, std::forward<Func>(func));
}

// MARK: forEachQueryCommandImpl

/*! @brief Dispatches processing with a `CommandBuffer` available for capture.
	@details Builds compile-time masks from the supplied type-lists, queries the archetype cache for matches, and then processes
   matching chunks using the selected `ExecutionPolicy`.
	@tparam Self The `ECS` type (possibly const-qualified) used for dispatch and to access thread pools/caches.
	@tparam ReqList `TypeList` of required component/tag types.
	@tparam AnyList `TypeList` of types where any single match satisfies the clause.
	@tparam NoneList `TypeList` of component/tag types that must be absent.
	@tparam Func Callable invoked per-entity or per-chunk.
	@param[in,out] self ECS instance providing archetypes, thread pools and caches.
	@param[in] policy Execution policy controlling parallelism (Seq, Par, ParBatched, ParStealing).
	@param[in] cmds `CommandBuffer` available for the caller to capture by reference in the callable.
	@param[in] func User callable forwarded into per-chunk processing; compatible with `processChunkEntities...` helpers.
	@note The `cmds` parameter is not forwarded into the callback; callers should capture it by reference in their lambda.
*/
template <class Self, typename ReqList, typename AnyList, typename NoneList, typename Func>
static void forEachQueryCommandImpl(Self &self, ExecutionPolicy &policy, CommandBuffer & /*cmds*/, Func &&func,
									const ComponentMask writeMask
									= std::is_const_v<std::remove_reference_t<Self>> ? ComponentMask(0) : buildMaskFromList<ReqList>())
{
	const IterationGuard iterGuard{self.mStructuralMutex};

	constexpr ComponentMask requiredMask{buildMaskFromList<ReqList>()};
	constexpr ComponentMask anyMask{buildMaskFromList<AnyList>()};
	constexpr ComponentMask noneMask{buildMaskFromList<NoneList>()};
	constexpr ComponentMask requiredTags{buildTagMaskFromList<ReqList>()};
	constexpr ComponentMask anyTags{buildTagMaskFromList<AnyList>()};
	constexpr ComponentMask noneTags{buildTagMaskFromList<NoneList>()};
	constexpr bool hasAnyClause{TypeListSize<AnyList>::value != 0};

	QueryKey key{
		.required = requiredMask, .any = anyMask, .none = noneMask, .requiredTags = requiredTags, .anyTags = anyTags, .noneTags = noneTags,};

	const std::vector<Archetype *> &matchingArchetypes{
		getMatchingArchetypesForQueryCalls<Self, AnyList, NoneList>(self, key, requiredMask, anyMask, noneMask),};

	using FuncT = std::decay_t<Func>;
	const FuncT processChunkFunction{std::forward<Func>(func)};

	// Process each matching archetype (sequential; extend to parallel as needed)
	auto processChunk = [&, processChunkFunction](Archetype *arch, const ui chunkIndex, const ui entityCount) {
		const IterationGuard workerGuard{self.mStructuralMutex};
		const bool anyRegularMatched{static_cast<bool>(arch->getRegularMask() & anyMask)};
		bool processedAny{false};
		auto stampWrites = [&] {
			if constexpr (!std::is_const_v<std::remove_reference_t<Self>>)
			{
				if (processedAny)
				{
					self.markComponentsChanged(arch, chunkIndex, writeMask);
				}
			}
		};
		try
		{
			if constexpr (std::is_const_v<std::remove_reference_t<Self>>)
			{
				const Entity *entities{arch->getEntityArray(chunkIndex)};

				[&]<typename... Req>(TypeList<Req...>) {
					processChunkEntitiesFilteredConst<Req...>(
						entities, entityCount, chunkIndex, arch,
						[&](const Entity &entity, const auto &...comps) {
							processedAny = true;
							processChunkFunction(entity, comps...);
						},
						requiredTags, anyTags, noneTags, hasAnyClause, anyRegularMatched);
				}(ReqList{});
			}
			else
			{
				Entity *entities{arch->getEntityArray(chunkIndex)};

				[&]<typename... Req>(TypeList<Req...>) {
					processChunkEntitiesFiltered<Req...>(
						entities, entityCount, chunkIndex, arch,
						[&](Entity &entity, auto &&...comps) {
							processedAny = true;
							processChunkFunction(entity, comps...);
						},
						requiredTags, anyTags, noneTags, hasAnyClause, anyRegularMatched);
				}(ReqList{});
			}
		}
		catch (...)
		{
			stampWrites();
			throw;
		}
		stampWrites();
	};

	switch (policy)
	{
		case ExecutionPolicy::Seq:
			forEachSeqProcessChunkOnly(matchingArchetypes, processChunk);
			break;
		case ExecutionPolicy::Par:
			forEachParProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
			break;
		case ExecutionPolicy::ParBatched:
			forEachParBatchedProcessChunkOnly(matchingArchetypes, self.mThreadPool, processChunk);
			break;
		case ExecutionPolicy::ParStealing:
			forEachParStealingProcessChunkOnly(matchingArchetypes, self.mWorkStealingPool, processChunk);
			break;
		default:
			assert(false && "Invalid execution policy");
	}
}

/*! @brief Command-style overload that accepts clause wrappers (`All`, `Any`, `None`) and forwards a `CommandBuffer`.
	@tparam AllFilter `All<...>` clause specifying required types.
	@tparam AnyFilter `Any<...>` clause specifying alternative-match types.
	@tparam NoneFilter `None<...>` clause specifying excluded types.
	@tparam Func Callable invoked per-entity or per-chunk; must accept a `CommandBuffer&` when used per-entity.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying implementation.
	@note This overload transforms clause wrappers into `TypeList` aliases via `PackExtractor` and forwards to
   `forEachQueryCommandImpl`.
*/
template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func)
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
}

/*! @brief Const-qualified command-style overload that accepts clause wrappers and forwards a `CommandBuffer`.
	@tparam AllFilter `All<...>` clause specifying required types (read-only).
	@tparam AnyFilter `Any<...>` clause specifying alternative-match types.
	@tparam NoneFilter `None<...>` clause specifying excluded types.
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
   `CommandBuffer&` when used in per-entity form.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying const implementation.
*/
template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func) const
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
}

// MARK: forEachQueryCommandImpl 2 operator overloads

/*! @brief Command-style `forEach` overload for `All<...>` + `None<...>` clauses.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying implementation.
	@note This overload sets the `Any` clause to empty (`TypeList<>`) and forwards to `forEachQueryCommandImpl`.
*/
template <AllType AllFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func)
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = TypeList<>; // empty – no "any" filter
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
}

/*! @brief Const-qualified command-style `forEach` overload for `All<...>` + `None<...>` clauses.
	@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
   `CommandBuffer&` when used per-entity.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the const implementation.
	@note This overload sets the `Any` clause to empty (`TypeList<>`) and forwards to the const `forEachQueryCommandImpl`.
*/
template <AllType AllFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func) const
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = TypeList<>;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
}

/*! @brief Command-style `forEach` overload for `All<...>` + `Any<...>` clauses.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
	@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying implementation.
	@note This overload sets the `None` clause to empty (`TypeList<>`) and forwards to `forEachQueryCommandImpl`.
*/
template <AllType AllFilter, AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func)
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = TypeList<>; // empty – no "none" filter
	forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
}

/*! @brief Const-qualified command-style `forEach` overload for `All<...>` + `Any<...>` clauses.
	@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
   `CommandBuffer&` when used per-entity.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the const implementation.
	@note This overload sets the `None` clause to empty (`TypeList<>`) and forwards to the const `forEachQueryCommandImpl`.
*/
template <AllType AllFilter, AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func) const
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = TypeList<>;
	forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
}

/*! @brief Command-style `forEach` overload for `Any<...>` + `None<...>` clauses.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying implementation.
	@note This overload sets the `All` (required) clause to empty (`TypeList<>`) and forwards to `forEachQueryCommandImpl`.
*/
template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func)
{
	using ReqList = TypeList<>; // empty – no required components
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
}

/*! @brief Const-qualified command-style `forEach` overload for `Any<...>` + `None<...>` clauses.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (read-only).
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
   `CommandBuffer&` when used per-entity.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the const implementation.
	@note This overload sets the `All` (required) clause to empty (`TypeList<>`) and forwards to the const
   `forEachQueryCommandImpl`.
*/
template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func) const
{
	using ReqList = TypeList<>;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
}

// MARK: forEachQueryCommandImpl 1 operator overloads

/*! @brief Command-style `forEach` for a required-only `All<...>` clause.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying implementation.
	@note This overload converts `AllFilter` to a `TypeList` via `PackExtractor` and forwards to `forEachQueryCommandImpl` with
   empty `Any`/`None` clauses.
*/
template <AllType AllFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func)
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = TypeList<>;
	using NoneList = TypeList<>;
	forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
}

/*! @brief Const-qualified command-style `forEach` for a required-only `All<...>` clause.
	@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
   `CommandBuffer&` when used per-entity.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the const implementation.
*/
template <AllType AllFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func) const
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = TypeList<>;
	using NoneList = TypeList<>;
	forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
}

/*! @brief Command-style `forEach` for an `Any<...>` (at-least-one) clause.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
	@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying implementation.
	@note This overload converts `AnyFilter` to a `TypeList` via `PackExtractor` and forwards to `forEachQueryCommandImpl` with
   empty `All`/`None` clauses.
*/
template <AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func)
{
	using ReqList = TypeList<>;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = TypeList<>;
	forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
}

/*! @brief Const-qualified command-style `forEach` for an `Any<...>` (at-least-one) clause.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (read-only).
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
   `CommandBuffer&` when used per-entity.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the const implementation.
*/
template <AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func) const
{
	using ReqList = TypeList<>;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = TypeList<>;
	forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
}

/*! @brief Command-style `forEach` that excludes types listed in `None<...>`.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent for a match.
	@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying implementation.
	@note This overload converts `NoneFilter` to a `TypeList` via `PackExtractor` and forwards to `forEachQueryCommandImpl` with
   empty `All`/`Any` clauses.
*/
template <NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func)
{
	using ReqList = TypeList<>;
	using AnyList = TypeList<>;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
}

/*! @brief Const-qualified command-style `forEach` that excludes types listed in `None<...>`.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent (read-only).
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
   `CommandBuffer&` when used per-entity.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the const implementation.
*/
template <NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, CommandBuffer &cmds, Func &&func) const
{
	using ReqList = TypeList<>;
	using AnyList = TypeList<>;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, cmds, std::forward<Func>(func));
}

// MARK: forEachQueryVersionCommandImpl

/*! @brief Version-aware dispatch with a `CommandBuffer` available for capture.
	@details Builds compile-time masks from the supplied type-lists, queries the archetype cache for matches, collects chunks
   that are dirty according to @p version, then processes those chunks and updates @p version afterwards.
	@tparam Self The `ECS` type (possibly const-qualified) used for dispatch and to access pools/caches.
	@tparam ReqList `TypeList` of required component/tag types.
	@tparam AnyList `TypeList` of alternative types where any single match satisfies the clause.
	@tparam NoneList `TypeList` of component/tag types that must be absent.
	@tparam Func Callable invoked per-entity or per-chunk.
	@param[in,out] self Reference to the `ECS` instance providing archetypes, thread pools and caches.
	@param[in] policy Execution policy controlling parallelism (Seq, Par, ParBatched, ParStealing).
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in] cmds `CommandBuffer` available for the caller to capture by reference in the callable.
	@param[in] func User callable forwarded into per-chunk processing; compatible with the per-chunk helpers.
	@note The `cmds` parameter is not forwarded into the callback; callers should capture it by reference in their lambda.
*/
template <class Self, typename ReqList, typename AnyList, typename NoneList, typename Func>
static void forEachQueryVersionCommandImpl(Self &self, ExecutionPolicy &policy, SystemVersion &version, CommandBuffer & /*cmds*/,
										   Func &&func,
										   const ComponentMask writeMask = std::is_const_v<std::remove_reference_t<Self>>
																			 ? ComponentMask(0)
																			 : buildMaskFromList<ReqList>())
{
	const IterationGuard iterGuard{self.mStructuralMutex};

	constexpr ComponentMask requiredMask{buildMaskFromList<ReqList>()};
	constexpr ComponentMask anyMask{buildMaskFromList<AnyList>()};
	constexpr ComponentMask noneMask{buildMaskFromList<NoneList>()};
	constexpr ComponentMask requiredTags{buildTagMaskFromList<ReqList>()};
	constexpr ComponentMask anyTags{buildTagMaskFromList<AnyList>()};
	constexpr ComponentMask noneTags{buildTagMaskFromList<NoneList>()};
	constexpr bool hasAnyClause{TypeListSize<AnyList>::value != 0};

	QueryKey key{
		.required = requiredMask, .any = anyMask, .none = noneMask, .requiredTags = requiredTags, .anyTags = anyTags, .noneTags = noneTags,};

	const std::vector<Archetype *> &matchingArchetypes{
		getMatchingArchetypesForQueryCalls<Self, AnyList, NoneList>(self, key, requiredMask, anyMask, noneMask),};

	// Collect chunks that are dirty according to the version
	std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> dirtyChunks;

	setDirtyChunks(dirtyChunks, matchingArchetypes, version, requiredMask);

	if (dirtyChunks.empty())
	{
		return;
	}

	using FuncT = std::decay_t<Func>;
	const FuncT processChunkFunction{std::forward<Func>(func)};

	auto processChunk = [&, processChunkFunction](Archetype *arch, const ui chunkIndex) {
		const IterationGuard workerGuard{self.mStructuralMutex};
		const ui count{arch->getEntityCount(chunkIndex)};
		const bool anyRegularMatched{static_cast<bool>(arch->getRegularMask() & anyMask)};
		bool processedAny{false};
		auto stampWrites = [&] {
			if constexpr (!std::is_const_v<std::remove_reference_t<Self>>)
			{
				if (processedAny)
				{
					self.markComponentsChanged(arch, chunkIndex, writeMask);
				}
			}
		};

		try
		{

			if constexpr (std::is_const_v<std::remove_reference_t<Self>>)
			{
				const Entity *entities{arch->getEntityArray(chunkIndex)};

				[&]<typename... Req>(TypeList<Req...>) {
					processChunkEntitiesFilteredConst<Req...>(
						entities, count, chunkIndex, arch,
						[&](Entity &entity, const auto &...comps) {
							processedAny = true;
							processChunkFunction(entity, comps...);
						},
						requiredTags, anyTags, noneTags, hasAnyClause, anyRegularMatched);
				}(ReqList{});
			}
			else
			{
				Entity *entities{arch->getEntityArray(chunkIndex)};

				[&]<typename... Req>(TypeList<Req...>) {
					processChunkEntitiesFiltered<Req...>(
						entities, count, chunkIndex, arch,
						[&](Entity &entity, auto &&...comps) {
							processedAny = true;
							processChunkFunction(entity, comps...);
						},
						requiredTags, anyTags, noneTags, hasAnyClause, anyRegularMatched);
				}(ReqList{});
			}
		}
		catch (...)
		{
			stampWrites();
			throw;
		}
		stampWrites();
	};

	auto noOpVersionUpdate = [](const auto &) {};

	switch (policy)
	{
		case ExecutionPolicy::Seq:
			forEachSeqProcessChunkAndVersion(dirtyChunks, processChunk, noOpVersionUpdate);
			break;
		case ExecutionPolicy::Par:
			forEachParProcessChunkAndVersion(dirtyChunks, self.mThreadPool, processChunk, noOpVersionUpdate);
			break;
		case ExecutionPolicy::ParBatched:
			forEachParBatchedProcessChunkAndVersion(dirtyChunks, self.mThreadPool, processChunk, noOpVersionUpdate);
			break;
		case ExecutionPolicy::ParStealing:
			forEachParStealingProcessChunkAndVersion(dirtyChunks, self.mWorkStealingPool, processChunk, noOpVersionUpdate);
			break;
		default:
			assert(false && "Invalid execution policy");
	}

	bulkMergeVersions(version, dirtyChunks, requiredMask);
}

/*! @brief Version-aware command-style `forEach` that accepts clause wrappers and a `CommandBuffer`.
	@tparam AllFilter `All<...>` clause specifying required types.
	@tparam AnyFilter `Any<...>` clause specifying alternative-match types.
	@tparam NoneFilter `None<...>` clause specifying excluded types.
	@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
	@param[in] policy Execution policy controlling parallelism.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying versioned implementation.
	@note Transforms clause wrappers into `TypeList` aliases via `PackExtractor` and forwards to
   `forEachQueryVersionCommandImpl` which performs dirty-chunk selection and processing.
*/
template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func)
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds, std::forward<Func>(func));
}

/*! @brief Const-qualified version-aware command-style `forEach` that accepts clause wrappers and a `CommandBuffer`.
	@tparam AllFilter `All<...>` clause specifying required types (read-only).
	@tparam AnyFilter `Any<...>` clause specifying alternative-match types.
	@tparam NoneFilter `None<...>` clause specifying excluded types.
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
   `CommandBuffer&` when used per-entity.
	@param[in] policy Execution policy controlling parallelism.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying const versioned implementation.
*/
template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds, std::forward<Func>(func));
}

// MARK: forEachQueryVersionCommandImpl 2 operator overloads

/*! @brief Version-aware command-style `forEach` for `All<...>` + `None<...>` clauses.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying versioned implementation.
	@note This overload sets the `Any` clause to empty (`TypeList<>`) and forwards to `forEachQueryVersionCommandImpl`.
*/
template <AllType AllFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func)
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = TypeList<>; // empty – no "any" filter
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds, std::forward<Func>(func));
}

/*! @brief Const-qualified version-aware command-style `forEach` for `All<...>` + `None<...>` clauses.
	@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
   `CommandBuffer&` when used per-entity.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the const versioned implementation.
*/
template <AllType AllFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = TypeList<>;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds, std::forward<Func>(func));
}

/*! @brief Version-aware command-style `forEach` for `All<...>` + `Any<...>` clauses.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
	@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying versioned implementation.
	@note This overload sets the `None` clause to empty (`TypeList<>`) and forwards to `forEachQueryVersionCommandImpl`.
*/
template <AllType AllFilter, AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func)
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = TypeList<>; // empty – no "none" filter
	forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds, std::forward<Func>(func));
}

/*! @brief Const-qualified version-aware command-style `forEach` for `All<...>` + `Any<...>` clauses.
	@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
   `CommandBuffer&` when used per-entity.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying const versioned implementation.
*/
template <AllType AllFilter, AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = TypeList<>;
	forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds, std::forward<Func>(func));
}

/*! @brief Version-aware command-style `forEach` for `Any<...>` + `None<...>` clauses.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying versioned implementation.
	@note This overload sets the `All` (required) clause to empty (`TypeList<>`) and forwards to
   `forEachQueryVersionCommandImpl`.
*/
template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func)
{
	using ReqList = TypeList<>; // empty – no required components
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds, std::forward<Func>(func));
}

/*! @brief Const-qualified version-aware command-style `forEach` for `Any<...>` + `None<...>` clauses.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (read-only).
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
   `CommandBuffer&` when used per-entity.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying const versioned implementation.
*/
template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
{
	using ReqList = TypeList<>;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds, std::forward<Func>(func));
}

// MARK: forEachQueryVersionCommandImpl 1 operator overloads

/*! @brief Version-aware command-style `forEach` for a required-only `All<...>` clause.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying versioned implementation.
	@note This overload converts `AllFilter` to a `TypeList` via `PackExtractor` and forwards to
   `forEachQueryVersionCommandImpl` with empty `Any`/`None` clauses.
*/
template <AllType AllFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func)
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = TypeList<>;
	using NoneList = TypeList<>;
	forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds, std::forward<Func>(func));
}

/*! @brief Const-qualified version-aware command-style `forEach` for a required-only `All<...>` clause.
	@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
   `CommandBuffer&` when used per-entity.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying const versioned implementation.
*/
template <AllType AllFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
{
	using ReqList = PackExtractor<AllFilter>::type;
	using AnyList = TypeList<>;
	using NoneList = TypeList<>;
	forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds, std::forward<Func>(func));
}

/*! @brief Version-aware command-style `forEach` for an `Any<...>` (at-least-one) clause.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
	@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying versioned implementation.
	@note This overload converts `AnyFilter` to a `TypeList` via `PackExtractor` and forwards to
   `forEachQueryVersionCommandImpl` with empty `All`/`None` clauses.
*/
template <AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func)
{
	using ReqList = TypeList<>;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = TypeList<>;
	forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds, std::forward<Func>(func));
}

/*! @brief Const-qualified version-aware command-style `forEach` for an `Any<...>` clause.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (read-only).
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
   `CommandBuffer&` when used per-entity.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying const versioned implementation.
*/
template <AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
{
	using ReqList = TypeList<>;
	using AnyList = PackExtractor<AnyFilter>::type;
	using NoneList = TypeList<>;
	forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds, std::forward<Func>(func));
}

/*! @brief Version-aware command-style `forEach` that excludes types listed in `None<...>`.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent for a match.
	@tparam Func Callable invoked per-entity or per-chunk; when used per-entity the callable must accept a `CommandBuffer&`.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying versioned implementation.
	@note This overload converts `NoneFilter` to a `TypeList` via `PackExtractor` and forwards to
   `forEachQueryVersionCommandImpl` with empty `All`/`Any` clauses.
*/
template <NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func)
{
	using ReqList = TypeList<>;
	using AnyList = TypeList<>;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds, std::forward<Func>(func));
}

/*! @brief Const-qualified version-aware command-style `forEach` that excludes types listed in `None<...>`.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent (read-only).
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers and accept a
   `CommandBuffer&` when used per-entity.
	@param[in] policy Execution policy controlling parallelism and batching.
	@param[in,out] version `SystemVersion` used to select dirty chunks and updated after processing.
	@param[in,out] cmds `CommandBuffer` forwarded to user callbacks for scheduling deferred operations.
	@param[in] func User-provided callable forwarded to the underlying const versioned implementation.
*/
template <NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
{
	using ReqList = TypeList<>;
	using AnyList = TypeList<>;
	using NoneList = PackExtractor<NoneFilter>::type;
	forEachQueryVersionCommandImpl<decltype(*this), ReqList, AnyList, NoneList>(*this, policy, version, cmds, std::forward<Func>(func));
}

// MARK: forEachQuery

/*! @brief Convenience `forEach` that runs the query with `ExecutionPolicy::Seq`.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked per-entity or per-chunk. Forwarded to the underlying `forEach` overload.
	@param[in] func User-provided callable forwarded to the sequential `forEach` implementation.
	@note This overload is a convenience wrapper that selects the sequential execution policy.
*/
template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(Func &&func)
{
	forEach<AllFilter, AnyFilter, NoneFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
}

/*! @brief Const-qualified convenience `forEach` that runs the query with `ExecutionPolicy::Seq`.
	@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers.
	@param[in] func User-provided callable forwarded to the sequential const `forEach` implementation.
	@note This overload is a const convenience wrapper that selects the sequential execution policy.
*/
template <AllType AllFilter, AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(Func &&func) const
{
	forEach<AllFilter, AnyFilter, NoneFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
}

// MARK: forEachQuery 2 operator overloads

/*! @brief Convenience `forEach` (required + excluded) using `ExecutionPolicy::Seq`.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked per-entity or per-chunk; forwarded to the underlying implementation.
	@param[in] func User-provided callable forwarded to the sequential `forEach` implementation.
	@note This is a convenience wrapper that selects the sequential execution policy.
*/
template <AllType AllFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(Func &&func)
{
	forEach<AllFilter, NoneFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
}

/*! @brief Const-qualified convenience `forEach` (required + excluded) using `ExecutionPolicy::Seq`.
	@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers.
	@param[in] func User-provided callable forwarded to the sequential const `forEach` implementation.
*/
template <AllType AllFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(Func &&func) const
{
	forEach<AllFilter, NoneFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
}

/*! @brief Convenience `forEach` (required + alternative) using `ExecutionPolicy::Seq`.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
	@tparam Func Callable invoked per-entity or per-chunk; forwarded to the underlying implementation.
	@param[in] func User-provided callable forwarded to the sequential `forEach` implementation.
*/
template <AllType AllFilter, AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(Func &&func)
{
	forEach<AllFilter, AnyFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
}

/*! @brief Const-qualified convenience `forEach` (required + alternative) using `ExecutionPolicy::Seq`.
	@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers.
*/
template <AllType AllFilter, AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(Func &&func) const
{
	forEach<AllFilter, AnyFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
}

/*! @brief Convenience `forEach` (alternative + excluded) using `ExecutionPolicy::Seq`.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked per-entity or per-chunk; forwarded to the underlying implementation.
*/
template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(Func &&func)
{
	forEach<AnyFilter, NoneFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
}

/*! @brief Const-qualified convenience `forEach` (alternative + excluded) using `ExecutionPolicy::Seq`.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (read-only where applicable).
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent.
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers.
*/
template <AnyType AnyFilter, NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(Func &&func) const
{
	forEach<AnyFilter, NoneFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
}

// MARK: forEachQuery 1 operator overloads

/*! @brief Convenience `forEach` for a required-only `All<...>` clause using `ExecutionPolicy::Seq`.
	@tparam AllFilter `All<...>` clause listing required component/tag types.
	@tparam Func Callable invoked per-entity or per-chunk; forwarded to the underlying implementation.
	@param[in] func User-provided callable forwarded to the sequential `forEach` implementation.
	@note This overload is a convenience wrapper that selects the sequential execution policy.
*/
template <AllType AllFilter, typename Func>
ATTR_DEPRECATED void forEach(Func &&func)
{
	forEach<AllFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
}

/*! @brief Const-qualified convenience `forEach` for a required-only `All<...>` clause using `ExecutionPolicy::Seq`.
	@tparam AllFilter `All<...>` clause listing required component/tag types (read-only).
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers.
*/
template <AllType AllFilter, typename Func>
ATTR_DEPRECATED void forEach(Func &&func) const
{
	forEach<AllFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
}

/*! @brief Convenience `forEach` for an `Any<...>` clause (at-least-one match) using `ExecutionPolicy::Seq`.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types where any single match satisfies the clause.
	@tparam Func Callable invoked per-entity or per-chunk; forwarded to the underlying implementation.
*/
template <AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(Func &&func)
{
	forEach<AnyFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
}

/*! @brief Const-qualified convenience `forEach` for an `Any<...>` clause using `ExecutionPolicy::Seq`.
	@tparam AnyFilter `Any<...>` clause listing alternative component/tag types (read-only where applicable).
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers.
*/
template <AnyType AnyFilter, typename Func>
ATTR_DEPRECATED void forEach(Func &&func) const
{
	forEach<AnyFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
}

/*! @brief Convenience `forEach` that excludes types listed in `None<...>` using `ExecutionPolicy::Seq`.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent for a match.
	@tparam Func Callable invoked per-entity or per-chunk; forwarded to the underlying implementation.
*/
template <NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(Func &&func)
{
	forEach<NoneFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
}

/*! @brief Const-qualified convenience `forEach` that excludes types listed in `None<...>` using `ExecutionPolicy::Seq`.
	@tparam NoneFilter `None<...>` clause listing component/tag types that must be absent (read-only where applicable).
	@tparam Func Callable invoked per-entity or per-chunk; must be compatible with const processing helpers.
*/
template <NoneType NoneFilter, typename Func>
ATTR_DEPRECATED void forEach(Func &&func) const
{
	forEach<NoneFilter>(ExecutionPolicy::Seq, std::forward<Func>(func));
}
