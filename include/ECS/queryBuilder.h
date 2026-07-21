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
	@note The builder is lightweight (4 members) and designed for chaining on temporaries. It does not own any resources.
*/
template <bool IsConst, typename ReqList, typename AnyList = TypeList<>, typename NoneList = TypeList<>>
class QueryBuilder
{

	public:
		using SelfType = std::conditional_t<IsConst, const ECS, ECS>;

		explicit QueryBuilder(SelfType &ecs) noexcept : mECS(&ecs) {}

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

		/*! @brief Attach a CommandBuffer for deferred command recording.
			@param[in,out] c CommandBuffer available for capture by reference in the callable.
			@return Reference to this builder for chaining.
		*/
		QueryBuilder &commands(CommandBuffer &cmdBuffer) noexcept
		{
			mCmds = &cmdBuffer;
			return *this;
		}

		/*! @brief Execute the query, invoking @p func for each matching entity.
			@tparam Func Callable type compatible with the per-entity processing helpers.
			@param[in] func User callable forwarded to the appropriate dispatch implementation.
		*/
		template <typename Func>
		void forEach(Func &&func)
		{
			constexpr bool isFilterQuery = (TypeListSize<AnyList>::value > 0) || (TypeListSize<NoneList>::value > 0);

			if constexpr (!isFilterQuery)
			{
				// Raw component query path: unpack TypeList into Components... parameter pack
				[&]<typename... Comps>(TypeList<Comps...>) {
					if (mVersion != nullptr)
					{
						ECS::forEachPolicyVersionImpl<SelfType, Comps...>(*mECS, mPolicy, *mVersion, std::forward<Func>(func));
					}
					else
					{
						ECS::forEachPolicyImpl<SelfType, Comps...>(*mECS, mPolicy, std::forward<Func>(func));
					}
				}(ReqList{});
			}
			else
			{
				// Query-filter path: pass TypeLists directly
				if (mVersion != nullptr)
				{
					ECS::forEachQueryVersionImpl<SelfType, ReqList, AnyList, NoneList>(*mECS, mPolicy, *mVersion, std::forward<Func>(func));
				}
				else
				{
					ECS::forEachQueryImpl<SelfType, ReqList, AnyList, NoneList>(*mECS, mPolicy, std::forward<Func>(func));
				}
			}
		}

	private:
		SelfType *mECS;
		ExecutionPolicy mPolicy{ExecutionPolicy::Seq};
		SystemVersion *mVersion{nullptr};
		CommandBuffer *mCmds{nullptr};
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
