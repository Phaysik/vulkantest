/*! @file forEachPolicy.h
	@brief Policy-based forEach implementations and dispatch helpers, included inside the ECS class body.
	@details This file is not meant to be included directly. It is included by ecs.h inside the ECS class definition.
	@date 07/21/2026
	@version x.x.x
	@since x.x.x
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_ECS_H
	#error "forEachPolicy.h must not be included directly. Include ecs.h instead."
#endif

// MARK: forEach Template Member Functions

/*! @brief Convenience overload that iterates entities matching `Components...` using the sequential execution policy.
	@tparam Components Component types to include in the query.
	@tparam Func Callable type. The callable is forwarded to the underlying implementation and may have one of the supported
   signatures used by the `forEach` helpers (per-entity or per-chunk forms). See `processChunkEntities` helpers for exact
   expected callable shapes.
	@param[in] func User-provided callable that will be invoked for matching entities or chunks.
	@note This overload simply forwards to `forEach<Components...>(ExecutionPolicy::Seq, std::forward<Func>(func))`.
*/
template <typename... Components, typename Func>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
void forEach(Func &&func)
{
	forEach<Components...>(ExecutionPolicy::Seq, std::forward<Func>(func));
}

/*! @brief Const overload of the sequential `forEach` convenience wrapper.
	@tparam Components Component types to include in the query.
	@tparam Func Callable type; callable must be compatible with the const processing helpers (it will not modify ECS
   internals).
	@param[in] func User-provided callable forwarded to the underlying const implementation.
	@note Use this overload when only read access to components/entities is required.
*/
template <typename... Components, typename Func>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
void forEach(Func &&func) const
{
	forEach<Components...>(ExecutionPolicy::Seq, std::forward<Func>(func));
}

// MARK: forEachPolicyImpl

/*! @brief Sequential chunk processing implementation used by `forEach`.
	@tparam Func Callable compatible with `(Archetype*, ui, ui)`.
	@param[in] matchingArchetypes Archetypes matching the query.
	@param[in] processChunk Callable invoked for each non-empty chunk with `(arch, chunkIndex, entityCount)`.
*/
template <typename Func>
	requires InvocableWithArgs<Func, Archetype *, ui, ui>
static void forEachSeqProcessChunkOnly(const std::vector<Archetype *> &matchingArchetypes, Func &&processChunk)
{
	Func processChunkFunction{std::forward<Func>(processChunk)};

	for (Archetype *arch : matchingArchetypes)
	{
		const ui chunkCount{arch->getChunkCount()};

		for (ui chunkIndex{0}; chunkIndex < chunkCount; ++chunkIndex)
		{
			const ui entityCount{arch->getEntityCount(chunkIndex)};

			if (entityCount == 0)
			{
				continue;
			}

			processChunkFunction(arch, chunkIndex, entityCount);
		}
	}
}

/*! @brief Parallel per-chunk processing using `ThreadPool`.
	@tparam Func Callable compatible with `(Archetype*, ui, ui)`.
	@param[in] matchingArchetypes Archetypes matching the query.
	@param[in,out] threadPool Thread pool used to submit per-chunk tasks.
	@param[in] processChunk Callable invoked per-chunk; tasks are waited on before return.
*/
template <typename Func>
	requires InvocableWithArgs<Func, Archetype *, ui, ui>
static void forEachParProcessChunkOnly(const std::vector<Archetype *> &matchingArchetypes, ThreadPool &threadPool, Func &&processChunk)
{
	std::vector<std::pair<Archetype *, ui>> allChunks;

	Func processChunkFunction{std::forward<Func>(processChunk)};

	for (Archetype *arch : matchingArchetypes)
	{
		const ui chunkCount{arch->getChunkCount()};

		for (ui chunkIndex{0}; chunkIndex < chunkCount; ++chunkIndex)
		{
			if (arch->getEntityCount(chunkIndex) > 0)
			{
				allChunks.emplace_back(arch, chunkIndex);
			}
		}
	}

	if (allChunks.empty())
	{
		return;
	}

	std::latch latch(static_cast<std::ptrdiff_t>(allChunks.size()));
	std::vector<std::future<void>> futures;
	futures.reserve(allChunks.size());

	for (const auto &[arch, chunkIndex] : allChunks)
	{
		futures.push_back(threadPool.submitWithLatch(
			[arch, chunkIndex, processChunkFunction] {
				const ui entityCount{arch->getEntityCount(chunkIndex)};
				processChunkFunction(arch, chunkIndex, entityCount);
			},
			latch));
	}

	latch.wait();
	for (auto &future : futures)
	{
		future.get();
	}
}

/*! @brief Parallel batched processing: groups chunks into batches and schedules each batch.
	@tparam Components Component types used by per-entity helpers.
	@tparam Func Callable invoked per-entity/ per-chunk.
	@param[in] matchingArchetypes Archetypes matching the query.
	@param[in,out] threadPool Thread pool used to execute batches.
	@param[in] func User callable forwarded into batch tasks.
*/
template <typename Func>
	requires InvocableWithArgs<Func, Archetype *, ui, ui>
static void forEachParBatchedProcessChunkOnly(const std::vector<Archetype *> &matchingArchetypes, ThreadPool &threadPool, Func &&func)
{
	std::vector<std::pair<Archetype *, ui>> allChunks;

	for (Archetype *arch : matchingArchetypes)
	{
		const ui chunkCount{arch->getChunkCount()};
		for (ui chunkIndex{0}; chunkIndex < chunkCount; ++chunkIndex)
		{
			if (arch->getEntityCount(chunkIndex) > 0)
			{
				allChunks.emplace_back(arch, chunkIndex);
			}
		}
	}

	if (allChunks.empty())
	{
		return;
	}

	const std::size_t batchSize{getBatchSize(allChunks.size())};

	std::latch latch(static_cast<std::ptrdiff_t>((allChunks.size() + batchSize - 1) / batchSize));
	std::vector<std::future<void>> futures;
	futures.reserve((allChunks.size() + batchSize - 1) / batchSize);

	// Move the user-provided callable into a shared pointer once so it can be safely
	// captured by each batch task without forwarding/moving `func` multiple times.
	using FuncT = std::decay_t<Func>;
	const FuncT processChunkFunction{std::forward<Func>(func)};

	for (std::size_t i{0}; i < allChunks.size(); i += batchSize)
	{
		const std::size_t end{std::min(i + batchSize, allChunks.size())};
		const std::vector<std::pair<Archetype *, ui>> batch(allChunks.begin() + static_cast<std::ptrdiff_t>(i),
															allChunks.begin() + static_cast<std::ptrdiff_t>(end));

		futures.push_back(threadPool.submitWithLatch(
			[batch, processChunkFunction] mutable {
				for (const auto &[arch, chunkIndex] : batch)
				{
					const ui entityCount{arch->getEntityCount(chunkIndex)};
					processChunkFunction(arch, chunkIndex, entityCount);
				}
			},
			latch));
	}

	latch.wait();
	for (auto &future : futures)
	{
		future.get();
	}
}

/*! @brief Parallel processing using a work-stealing pool for dynamic load balancing.
	@tparam Components Component types used by per-entity helpers.
	@tparam Func Callable invoked per-entity/ per-chunk.
	@param[in] matchingArchetypes Archetypes matching the query.
	@param[in,out] workStealingPool Work-stealing pool used to execute chunk processing.
	@param[in] func User callable forwarded into worker tasks.
*/
template <typename Func>
	requires InvocableWithArgs<Func, Archetype *, ui, ui>
static void forEachParStealingProcessChunkOnly(const std::vector<Archetype *> &matchingArchetypes, WorkStealingPool &workStealingPool,
											   Func &&func)
{
	std::vector<std::pair<Archetype *, ui>> allChunks;

	for (Archetype *arch : matchingArchetypes)
	{
		ui chunkCount{arch->getChunkCount()};
		for (ui chunkIndex{0}; chunkIndex < chunkCount; ++chunkIndex)
		{
			if (arch->getEntityCount(chunkIndex) > 0)
			{
				allChunks.emplace_back(arch, chunkIndex);
			}
		}
	}

	if (allChunks.empty())
	{
		return;
	}

	const std::size_t batchSize{getBatchSize(allChunks.size())};

	std::latch latch(static_cast<std::ptrdiff_t>((allChunks.size() + batchSize - 1) / batchSize));

	using FuncT = std::decay_t<Func>;
	const FuncT processChunkFunction{std::forward<Func>(func)};

	auto futures{
		workStealingPool.submitChunks(
			allChunks,
			[processChunkFunction](Archetype *arch, ui chunkIndex) {
				const ui entityCount{arch->getEntityCount(chunkIndex)};

				processChunkFunction(arch, chunkIndex, entityCount);
			},
			latch, batchSize),
	};

	latch.wait();
	for (auto &future : futures)
	{
		future.get();
	}
}

/*! @brief Policy dispatcher for `forEach` that selects the execution strategy.
	@tparam Self The ECS type (possibly const-qualified) for method dispatch.
	@tparam Components Component types for the query.
	@tparam Func User-provided callable.
	@param[in,out] self Reference to ECS instance for accessing caches and pools.
	@param[in] policy Execution policy to use.
	@param[in] func Callable to execute for matching entities or chunks.
*/
template <class Self, typename... Components, typename Func>
static void forEachPolicyImpl(Self &self, const ExecutionPolicy &policy, Func &&func,
							  const ComponentMask writeMask
							  = std::is_const_v<std::remove_reference_t<Self>> ? ComponentMask(0) : buildRequiredMask<Components...>())
{
	const IterationGuard iterGuard{self.mStructuralMutex};

	constexpr ComponentMask requiredRegular{buildRequiredMask<Components...>()};
	const std::vector<Archetype *> &matchingArchetypes{self.mQueryCache.get(requiredRegular)};

	const Func processChunkFunction{std::forward<Func>(func)};

	auto processChunk = [&](Archetype *arch, const ui chunkIndex, const ui entityCount) {
		const IterationGuard workerGuard{self.mStructuralMutex};
		bool processedAny{false};
		auto trackedFunction = [&](auto &&...args) {
			processedAny = true;
			processChunkFunction(std::forward<decltype(args)>(args)...);
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
				processChunkEntitiesConst<Components...>(entities, entityCount, chunkIndex, arch, trackedFunction);
			}
			else
			{
				Entity *entities{arch->getEntityArray(chunkIndex)};
				processChunkEntities<Components...>(entities, entityCount, chunkIndex, arch, trackedFunction);
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

/*! @brief Iterate over entities matching `Components...` using the specified execution policy.
	@tparam Components Component types to include in the query.
	@tparam Func Callable type provided by the user. The callable will be forwarded to the policy implementation and must match
   one of the callable signatures supported by the processing helpers (per-chunk or per-entity forms).
	@param[in] policy Execution policy that controls parallelism and batching (Seq, Par, ParBatched, ParStealing).
	@param[in] func User-provided callable invoked for matching entities or chunks.
	@note This forwards to `forEachPolicyImpl` which performs dispatch based on @p policy.
*/
template <typename... Components, typename Func>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
void forEach(const ExecutionPolicy &policy, Func &&func)

{
	forEachPolicyImpl<decltype(*this), Components...>(*this, policy, std::forward<Func>(func));
}

/*! @brief Const overload: iterate over entities matching `Components...` using @p policy without mutating ECS state.
	@tparam Components Component types to include in the query.
	@tparam Func Callable type; must be compatible with the const processing helpers.
	@param[in] policy Execution policy to use.
	@param[in] func User-provided callable forwarded to the const policy implementation.
	@note Use this overload for read-only systems to enable safe parallel execution where applicable.
*/
template <typename... Components, typename Func>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
void forEach(const ExecutionPolicy &policy, Func &&func) const
{
	forEachPolicyImpl<decltype(*this), Components...>(*this, policy, std::forward<Func>(func));
}

// MARK: forEachPolicyCommandImpl

/*! @brief Dispatches command-style processing according to @p policy.
	@tparam Self The ECS type (possibly const-qualified).
	@tparam Components Component types in the query.
	@tparam Func Callable accepting `(Entity, Components...)`.
	@param[in,out] self ECS instance reference.
	@param[in] policy Execution policy to use.
	@param[in] cmds CommandBuffer available for the caller to capture by reference in the callable.
	@param[in] func User callable invoked for each entity or chunk.
	@note The `cmds` parameter is not forwarded into the callback; callers should capture it by reference in their lambda.
*/
template <class Self, typename... Components, typename Func>
static void forEachPolicyCommandImpl(Self &self, const ExecutionPolicy &policy, CommandBuffer & /*cmds*/, Func &&func,
									 const ComponentMask writeMask = std::is_const_v<std::remove_reference_t<Self>>
																	   ? ComponentMask(0)
																	   : buildRequiredMask<Components...>())
{
	const IterationGuard iterGuard{self.mStructuralMutex};

	constexpr ComponentMask requiredRegular{buildRequiredMask<Components...>()};
	const std::vector<Archetype *> &matchingArchetypes{self.mQueryCache.get(requiredRegular)};

	using FuncT = std::decay_t<Func>;
	const FuncT processChunkFunction{std::forward<Func>(func)};

	// Processing lambda – uses the appropriate chunk function based on constness
	auto processChunk = [&, processChunkFunction](Archetype *arch, ui chunkIndex, const ui entityCount) {
		const IterationGuard workerGuard{self.mStructuralMutex};
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
				processChunkEntitiesConst<Components...>(entities, entityCount, chunkIndex, arch,
														 [&](const Entity &entity, const auto &...comps) {
															 processedAny = true;
															 processChunkFunction(entity, comps...);
														 });
			}
			else
			{
				Entity *entities{arch->getEntityArray(chunkIndex)};
				processChunkEntities<Components...>(entities, entityCount, chunkIndex, arch, [&](Entity &entity, auto &&...comps) {
					processedAny = true;
					processChunkFunction(entity, comps...);
				});
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

/*! @brief Iterate over matching entities with a `CommandBuffer` provided to callbacks using @p policy.
	@tparam Components Component types to query.
	@tparam Func Callable invoked with signature `(Entity, Components..., CommandBuffer&)` or similar.
	@param[in] policy Execution policy to use.
	@param[in,out] cmds CommandBuffer forwarded to user callbacks.
	@param[in] func User callable.
*/
template <typename... Components, typename Func>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
void forEach(const ExecutionPolicy &policy, CommandBuffer &cmds, Func &&func)
{
	forEachPolicyCommandImpl<decltype(*this), Components...>(*this, policy, cmds, std::forward<Func>(func));
}

/*! @brief Const overload of the `forEach` variant that provides a `CommandBuffer` to callbacks.
 */
template <typename... Components, typename Func>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
void forEach(const ExecutionPolicy &policy, CommandBuffer &cmds, Func &&func) const
{
	forEachPolicyCommandImpl<decltype(*this), Components...>(*this, policy, cmds, std::forward<Func>(func));
}

// MARK: forEachPolicyVersionImpl

/*! @brief Sequential processing for dirty chunks with post-processing version updates.
	@tparam Func Callable invoked per chunk `(Archetype*, ui)`.
	@tparam Ver Callable used to update version information after processing each chunk.
	@param[in] dirtyChunks Vector of tuples `(arch, chunkIndex, chunkVersionPtr)` indicating work.
	@param[in] processFunc Callable executed on each dirty chunk.
	@param[in] updateVersion Callable used to update system/component versions after processing.
*/
template <typename Func, typename Ver>
static void forEachSeqProcessChunkAndVersion(const std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> &dirtyChunks,
											 Func &&processFunc, Ver &&updateVersion)
{
	const Func processChunkFunction{std::forward<Func>(processFunc)};
	const Ver updateVersionFunction{std::forward<Ver>(updateVersion)};

	for (const auto &chunk : dirtyChunks)
	{
		processChunkFunction(std::get<0>(chunk), std::get<1>(chunk));
		updateVersionFunction(chunk);
	}
}

/*! @brief Parallel processing of dirty chunks using ThreadPool; updates versions after processing.
	@tparam Func Callable invoked per chunk.
	@tparam Ver Callable to update version metadata.
	@param[in] dirtyChunks Chunks requiring processing.
	@param[in,out] threadPool ThreadPool used for parallel execution.
	@param[in] processFunc Callable executed in parallel for each dirty chunk.
	@param[in] updateVersion Callable executed once per chunk after processing to update versions.
*/
template <typename Func, typename Ver>
static void forEachParProcessChunkAndVersion(const std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> &dirtyChunks,
											 ThreadPool &threadPool, Func &&processFunc, Ver &&updateVersion)
{
	if (dirtyChunks.empty())
	{
		return;
	}

	const Func processChunkFunction{std::forward<Func>(processFunc)};
	const Ver updateVersionFunction{std::forward<Ver>(updateVersion)};

	std::latch latch(static_cast<std::ptrdiff_t>(dirtyChunks.size()));
	std::vector<std::future<void>> futures;
	futures.reserve(dirtyChunks.size());

	for (const auto &chunk : dirtyChunks)
	{
		Archetype *arch{std::get<0>(chunk)};
		ui chunkIndex{std::get<1>(chunk)};

		futures.push_back(
			threadPool.submitWithLatch([arch, chunkIndex, processChunkFunction] { processChunkFunction(arch, chunkIndex); }, latch));
	}

	latch.wait();
	for (auto &future : futures)
	{
		future.get();
	}

	for (const auto &chunk : dirtyChunks)
	{
		updateVersionFunction(chunk);
	}
}

/*! @brief Parallel batched processing for dirty chunks with post-update of versions.
	@tparam Func Callable invoked per chunk.
	@tparam Ver Callable used to update version info after processing.
	@param[in] dirtyChunks Chunks requiring processing.
	@param[in,out] threadPool ThreadPool used for batched submission.
	@param[in] processFunc Callable executed inside worker tasks.
	@param[in] updateVersion Callable applied after processing to adjust system versions.
*/
template <typename Func, typename Ver>
static void forEachParBatchedProcessChunkAndVersion(const std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> &dirtyChunks,
													ThreadPool &threadPool, Func &&processFunc, Ver &&updateVersion)
{
	// Convert to simple pairs for batching
	std::vector<std::pair<Archetype *, ui>> chunks;
	chunks.reserve(dirtyChunks.size());

	const Func processChunkFunction{std::forward<Func>(processFunc)};
	const Ver updateVersionFunction{std::forward<Ver>(updateVersion)};

	for (const auto &chunk : dirtyChunks)
	{
		chunks.emplace_back(std::get<0>(chunk), std::get<1>(chunk));
	}

	const std::size_t batchSize{getBatchSize(chunks.size())};

	std::latch latch(static_cast<std::ptrdiff_t>((chunks.size() + batchSize - 1) / batchSize));
	std::vector<std::future<void>> futures;
	futures.reserve((chunks.size() + batchSize - 1) / batchSize);

	for (std::size_t i{0}; i < chunks.size(); i += batchSize)
	{
		const std::size_t end{std::min(i + batchSize, chunks.size())};
		const std::vector<std::pair<Archetype *, ui>> batch(chunks.begin() + static_cast<std::ptrdiff_t>(i),
															chunks.begin() + static_cast<std::ptrdiff_t>(end));

		futures.push_back(threadPool.submitWithLatch(
			[batch, processChunkFunction] {
				for (const auto &[arch, chunkIndex] : batch)
				{
					processChunkFunction(arch, chunkIndex);
				}
			},
			latch));
	}

	latch.wait();
	for (auto &future : futures)
	{
		future.get();
	}

	for (const auto &chunk : dirtyChunks)
	{
		updateVersionFunction(chunk);
	}
}

/*! @brief Parallel dirty-chunk processing using work-stealing; updates versions after processing.
	@tparam Func Callable invoked per chunk.
	@tparam Ver Callable used to update version info after processing.
	@param[in] dirtyChunks Chunks requiring processing.
	@param[in,out] workStealingPool Work-stealing pool used to execute batch tasks.
	@param[in] processFunc Callable executed per chunk.
	@param[in] updateVersion Callable applied after processing to adjust system versions.
*/
template <typename Func, typename Ver>
static void forEachParStealingProcessChunkAndVersion(const std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> &dirtyChunks,
													 WorkStealingPool &workStealingPool, Func &&processFunc, Ver &&updateVersion)
{
	std::vector<std::pair<Archetype *, ui>> chunks;
	chunks.reserve(dirtyChunks.size());

	const Func processChunkFunction{std::forward<Func>(processFunc)};
	const Ver updateVersionFunction{std::forward<Ver>(updateVersion)};

	for (const auto &chunk : dirtyChunks)
	{
		chunks.emplace_back(std::get<0>(chunk), std::get<1>(chunk));
	}

	const std::size_t batchSize{getBatchSize(chunks.size())}; // same as in forEachPolicyImpl
	std::latch latch(static_cast<std::ptrdiff_t>((chunks.size() + batchSize - 1) / batchSize));

	auto futures{
		workStealingPool.submitChunks(
			chunks, [processChunkFunction](Archetype *arch, ui chunkIndex) { processChunkFunction(arch, chunkIndex); }, latch, batchSize),
	};

	// Work-stealing tasks always count down the latch, including when a callback throws.

	latch.wait();
	for (auto &future : futures)
	{
		future.get();
	}

	for (const auto &chunk : dirtyChunks)
	{
		updateVersionFunction(chunk);
	}
}

/*! @brief Dispatches processing of chunks that are dirty with respect to @p version.
	@tparam Self The ECS type (possibly const-qualified).
	@tparam Components Component types used to build the required mask.
	@tparam Func User callable invoked per-entity or per-chunk.
	@param[in,out] self ECS instance providing archetypes/caches.
	@param[in] policy Execution policy to use for processing.
	@param[in,out] version SystemVersion used to determine dirty chunks and to update after processing.
	@param[in] func Callable to execute for matching entities.
*/
template <class Self, typename... Components, typename Func>
static void forEachPolicyVersionImpl(Self &self, const ExecutionPolicy &policy, SystemVersion &version, Func &&func,
									 const ComponentMask writeMask = std::is_const_v<std::remove_reference_t<Self>>
																	   ? ComponentMask(0)
																	   : buildRequiredMask<Components...>())
{
	const IterationGuard iterGuard{self.mStructuralMutex};

	constexpr ComponentMask requiredRegular{buildRequiredMask<Components...>()};
	const std::vector<Archetype *> &matchingArchetypes{self.mQueryCache.get(requiredRegular)};

	// Collect chunks that need processing (dirty)
	std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> dirtyChunks;

	setDirtyChunks(dirtyChunks, matchingArchetypes, version, requiredRegular);

	if (dirtyChunks.empty())
	{
		return;
	}

	// Processing lambda – processes only the dirty chunks identified above
	using FuncT = std::decay_t<Func>;
	const FuncT processChunkFunction{std::forward<Func>(func)};
	auto processChunk = [&, processChunkFunction](Archetype *arch, const ui chunkIndex) {
		const IterationGuard workerGuard{self.mStructuralMutex};
		const ui count{arch->getEntityCount(chunkIndex)};
		bool processedAny{false};
		auto trackedFunction = [&](auto &&...args) {
			processedAny = true;
			processChunkFunction(std::forward<decltype(args)>(args)...);
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
				const Entity *entityArr{arch->getEntityArray(chunkIndex)};
				processChunkEntitiesConst<Components...>(entityArr, count, chunkIndex, arch, trackedFunction);
			}
			else
			{
				Entity *entityArr{arch->getEntityArray(chunkIndex)};
				processChunkEntities<Components...>(entityArr, count, chunkIndex, arch, trackedFunction);
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

	bulkMergeVersions(version, dirtyChunks, requiredRegular);
}

/*! @brief Iterate over entities matching `Components...` that are considered dirty by @p version.
	@tparam Components Component types to query.
	@tparam Func Callable invoked for matching entities.
	@param[in] policy Execution policy controlling parallelism.
	@param[in,out] version SystemVersion used to filter chunks and to update after processing.
	@param[in] func User callable.
*/
template <typename... Components, typename Func>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
void forEach(const ExecutionPolicy &policy, SystemVersion &version, Func &&func)
{
	forEachPolicyVersionImpl<decltype(*this), Components...>(*this, policy, version, std::forward<Func>(func));
}

/*! @brief Const overload of versioned `forEach`.
 */
template <typename... Components, typename Func>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
void forEach(const ExecutionPolicy &policy, SystemVersion &version, Func &&func) const
{
	forEachPolicyVersionImpl<decltype(*this), Components...>(*this, policy, version, std::forward<Func>(func));
}

// MARK: forEachPolicyVersionCommandImpl

/*! @brief Dispatches version-filtered processing with a CommandBuffer provided to callbacks.
	@tparam Self The ECS type (possibly const-qualified).
	@tparam Components Component types used to build the required mask.
	@tparam Func Callable invoked per-entity with an additional `CommandBuffer&` parameter.
	@param[in,out] self ECS instance providing archetypes/caches.
	@param[in] policy Execution policy to use for processing.
	@param[in,out] version SystemVersion used to determine dirty chunks and to update after processing.
	@param[in,out] cmds CommandBuffer forwarded to user callbacks.
	@param[in] func User callable invoked for each matching entity and supplied `cmds`.
*/
template <class Self, typename... Components, typename Func>
static void forEachPolicyVersionCommandImpl(Self &self, const ExecutionPolicy &policy, SystemVersion &version, CommandBuffer &cmds,
											Func &&func,
											const ComponentMask writeMask = std::is_const_v<std::remove_reference_t<Self>>
																			  ? ComponentMask(0)
																			  : buildRequiredMask<Components...>())
{
	const IterationGuard iterGuard{self.mStructuralMutex};

	constexpr ComponentMask requiredRegular{buildRequiredMask<Components...>()};
	const std::vector<Archetype *> &matchingArchetypes{self.mQueryCache.get(requiredRegular)};

	// Collect chunks that are dirty according to the version
	std::vector<std::tuple<Archetype *, ui, const ChunkVersion *>> dirtyChunks;

	setDirtyChunks(dirtyChunks, matchingArchetypes, version, requiredRegular);

	if (dirtyChunks.empty())
	{
		return;
	}

	using FuncT = std::decay_t<Func>;
	const FuncT processChunkFunction{std::forward<Func>(func)};

	// Processing lambda – processes only the dirty chunks identified above
	auto processChunk = [&, processChunkFunction](Archetype *arch, const ui chunkIndex) {
		const IterationGuard workerGuard{self.mStructuralMutex};
		const ui count{arch->getEntityCount(chunkIndex)};
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
				processChunkEntitiesConst<Components...>(entities, count, chunkIndex, arch,
														 [&](const Entity &entity, const auto &...comps) {
															 processedAny = true;
															 processChunkFunction(entity, comps..., cmds);
														 });
			}
			else
			{
				Entity *entityArr = arch->getEntityArray(chunkIndex);
				processChunkEntities<Components...>(entityArr, count, chunkIndex, arch, [&](const Entity &entity, auto &...comps) {
					processedAny = true;
					processChunkFunction(entity, comps..., cmds);
				});
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

	bulkMergeVersions(version, dirtyChunks, requiredRegular);
}

/*! @brief Iterate over entities matching `Components...` that are dirty with respect to @p version and provide a
   `CommandBuffer` to callbacks, using the specified execution @p policy.
	@tparam Components Component types to query.
	@tparam Func Callable type provided by the user. The callable must be compatible with the command-style processing helpers
   and may accept signatures such as `(Entity, Components..., CommandBuffer&)` (per-entity) or a per-chunk form that forwards
   `cmds` to the user's callback.
	@param[in] policy Execution policy controlling parallelism (Seq, Par, ParBatched, ParStealing).
	@param[in,out] version SystemVersion used to filter dirty chunks and updated after processing.
	@param[in,out] cmds CommandBuffer forwarded to user callbacks for recording deferred commands.
	@param[in] func User-provided callable invoked for each matching entity or chunk.
	@note This overload forwards to `forEachPolicyVersionCommandImpl` for dispatch and will update `version` after processing
   each dirty chunk.
*/
template <typename... Components, typename Func>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func)
{
	forEachPolicyVersionCommandImpl<decltype(*this), Components...>(*this, policy, version, cmds, std::forward<Func>(func));
}

/*! @brief Const overload of the versioned `forEach` that provides a `CommandBuffer` to callbacks.
	@tparam Components Component types to query (read-only).
	@tparam Func Callable type; the callable must be compatible with the const processing helpers and may accept `(Entity, const
   Components..., CommandBuffer&)` or a const per-chunk form.
	@param[in] policy Execution policy controlling parallelism.
	@param[in,out] version SystemVersion used to select dirty chunks and updated after processing.
	@param[in,out] cmds CommandBuffer forwarded to user callbacks (may be recorded to even in const systems).
	@param[in] func User-provided callable invoked for each matching entity or chunk; callbacks must not attempt to mutate ECS
   internal state.
	@note Use this overload for read-only systems that still require issuing commands via `cmds`.
*/
template <typename... Components, typename Func>
	requires((!AllType<Components> && !AnyType<Components> && !NoneType<Components>) && ...)
void forEach(ExecutionPolicy policy, SystemVersion &version, CommandBuffer &cmds, Func &&func) const
{
	forEachPolicyVersionCommandImpl<decltype(*this), Components...>(*this, policy, version, cmds, std::forward<Func>(func));
}
