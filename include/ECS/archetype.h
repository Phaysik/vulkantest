/*! @file archetype.h
	@brief Archetype declaration: chunked storage for entities with a shared component composition.
	@details Defines the `Archetype` class which owns cache-friendly, fixed-size chunks that store entity arrays, per-entity tag bitsets,
   and tightly packed regular components. Layout and capacity are computed from `ComponentInfos` and `CHUNK_SIZE` to provide deterministic,
   aligned placement of component instances for efficient iteration and mutation by systems.
	@date 02/14/2026
	@version 0.0.3
	@since 02/14/2026
	@author Matthew Moore
*/

#ifndef INCLUDE_ECS_ARCHETYPE_H
#define INCLUDE_ECS_ARCHETYPE_H

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include "Core/attributeMacros.h"
#include "Core/typedefs.h"
#include "ECS/constants.h"

#include "chunkVersion.h"
#include "componentMask.h"
#include "componentRegistry.h"
#include "entity.h"
#include "entityRecord.h"

namespace Dimensia::ECS
{
	using Dimensia::Core::ui;
	using Dimensia::Core::ul;

	using Dimensia::Registry::ComponentInfos;
	using Dimensia::Registry::ComponentTypeID;
	using Dimensia::Registry::MAX_COMPONENTS;

	/*! @class Archetype include/ECS/archetype.h
		@brief Manages storage (chunks) for entities that share a common component composition.
		@details `Archetype` owns a collection of cache-friendly, fixed-size chunks each containing arrays of components laid out according
	   to the archetype's regular (non-tag) component set. It provides allocation, deallocation, iteration, and versioning for chunk-level
	   and component-level change tracking. The class is non-copyable and movable. Use `addEntity` / `removeEntity` to modify contents and
	   `getComponentArray` / `getEntityArray` to access underlying arrays for bulk processing.
		@note Chunk layout and capacity are computed based on component sizes and alignments defined in @ref ComponentInfos and the
	   `CHUNK_SIZE` constant.
	*/
	class Archetype
	{

		public:
			// MARK: Constructor, Assignment Operators, and Destructor

			/*! @brief Construct an archetype for the given component mask.
				@param[in] regularMask Mask describing which regular (non-tag) components this archetype stores.
				@param[in] archetypeID Stable identifier assigned by the ECS for this archetype.
			*/
			explicit Archetype(const ComponentMask &regularMask, const ui archetypeID);

			/*! @brief Deleted copy constructor.
				@details `Archetype` owns unique chunk storage (`mChunks`) and related metadata. Copying would require duplicating or
			   sharing ownership of that storage which is intentionally disallowed.
			*/
			Archetype(const Archetype &) = delete;

			/*! @brief Move constructor.
				@details Transfers ownership of internal storage and metadata from the source archetype. Left-hand-side is put into a valid,
			   unspecified state. Declared `noexcept` to allow efficient container moves.
			*/
			Archetype(Archetype &&) noexcept = default;

			/*! @brief Deleted copy assignment operator.
				@details Copy assignment is disallowed for the same reasons as the copy constructor.
			*/
			Archetype &operator=(const Archetype &) = delete;

			/*! @brief Move assignment operator.
				@details Transfers ownership of resources from the source into `*this`, releasing any existing resources held by the target.
			   Marked `noexcept` to match move constructor guarantees.
			*/
			Archetype &operator=(Archetype &&) noexcept = default;

			/*! @brief Destroy stored components in all chunks.
				@details Invokes the registered destructor for every constructed component instance in all chunks to ensure proper cleanup
			   of resources.
			*/
			~Archetype();

			// MARK: Getters

			/*! @brief Return the archetype's regular component mask.
				@return A `ComponentMask` describing the regular components stored by this archetype.
			*/
			ATTR_NODISCARD constexpr ComponentMask getRegularMask() const noexcept
			{
				return mRegularMask;
			}

			/*! @brief Return the stable archetype identifier assigned by the ECS.
				@return Archetype id.
			*/
			ATTR_NODISCARD constexpr ui getId() const noexcept
			{
				return mArchetypeID;
			}

			/*! @brief Update the archetype identifier (used during compaction when archetypes are reordered).
				@param[in] newID The new identifier to assign.
			*/
			constexpr void setId(const ui newID) noexcept
			{
				mArchetypeID = newID;
			}

			/*! @brief Number of chunks currently allocated for this archetype.
				@return Chunk count.
			*/
			ATTR_NODISCARD ui getChunkCount() const noexcept;

			/*! @brief Access the version metadata for a chunk.
				@param[in] chunkIndex Index of the chunk to query.
				@return Reference to the chunk's `ChunkVersion` used for change tracking.
			*/
			ATTR_NODISCARD const ChunkVersion &getChunkVersion(const ui chunkIndex) const;

			/*! @brief Number of entities present in a given chunk.
				@param[in] chunkIndex Index of the chunk to query.
				@return Entity count in the chunk.
			*/
			ATTR_NODISCARD ui getEntityCount(const ui chunkIndex) const;

			/*! @brief Obtain pointer to the start of the component array for a component type inside a chunk.
				@param[in] chunkIndex Index of the chunk.
				@param[in] compID Component type id to access.
				@return Pointer to the component array (type-erased), or `nullptr` if the component is not present in this archetype.
			*/
			ATTR_NODISCARD void *getComponentArray(const ui chunkIndex, const ComponentTypeID compID) const;

			/*! @brief Return pointer to the entity array stored in a chunk.
				@param[in] chunkIndex Index of the chunk.
				@return Pointer to the chunk's `Entity` array.
			*/
			ATTR_NODISCARD Entity *getEntityArray(const ui chunkIndex) const;

			/*! @brief Retrieve the tag mask for a specific entity slot in a chunk.
				@param[in] chunkIndex Chunk index.
				@param[in] slotIndex Slot (entity) index within the chunk.
				@return `ComponentMask` containing tag bits for the entity (upper half = 0).
			*/
			ATTR_NODISCARD ComponentMask getTags(const ui chunkIndex, const ui slotIndex) const;

			/*! @brief Return a pointer to the tag-bitset array for the given chunk.
				@param[in] chunkIndex Index of the chunk.
				@return Pointer to the chunk's tag bitset storage (array of `ul`).
			*/
			ATTR_NODISCARD const ul *getTagBitset(const ui chunkIndex) const;

			// MARK: Setter

			/*! @brief Set a tag bit for an entity.
				@param[in] chunkIndex Chunk index containing the entity.
				@param[in] slotIndex Slot index of the entity within the chunk.
				@param[in] tagID ComponentTypeID of the tag to set.
				@post Bumps the chunk version to indicate a change.
			*/
			void setTag(const ui chunkIndex, const ui slotIndex, const ComponentTypeID tagID);

			// MARK: Member Functions

			/*! @brief Test whether an entity has a specific tag bit set.
				@param[in] chunkIndex Chunk index containing the entity.
				@param[in] slotIndex Slot index of the entity within the chunk.
				@param[in] tagID Tag component id to test.
				@return `true` if the tag bit is set for the entity, otherwise `false`.
			*/
			ATTR_NODISCARD bool hasTag(const ui chunkIndex, const ui slotIndex, const ComponentTypeID tagID) const;

			/*! @brief Clear a tag bit for an entity and bump the chunk version.
				@param[in] chunkIndex Chunk index containing the entity.
				@param[in] slotIndex Slot index of the entity within the chunk.
				@param[in] tagID Tag component id to clear.
			*/
			void clearTag(const ui chunkIndex, const ui slotIndex, const ComponentTypeID tagID);

			/*! @brief Increment the version for a chunk to signal a structural or tag change.
				@param[in] chunkIndex Chunk index.
			*/
			void bumpChunkVersion(const ui chunkIndex);

			/*! @brief Increment the version for a specific component within a chunk.
				@param[in] chunkIndex Chunk index.
				@param[in] compID Component type id whose version to bump.
			*/
			void bumpComponentVersion(const ui chunkIndex, const ComponentTypeID compID);

			/*! @brief Add an entity with component data to this archetype.
				@param[in] entity The `Entity` value to insert.
				@param[in] copyData Array of source pointers for copy-constructible components. Null entries are ignored.
				@param[in] moveData Array of source pointers for components to move; if an entry is non-null, the component is
			   move-constructed from that pointer instead of copy-constructed.
				@param[in] tags Tag mask for the entity (lower half used for tag bits).
				@return Pair of `{chunkIndex, slotIndex}` indicating where the entity was placed.
			*/
			std::pair<ui, ui> addEntity(const Entity &entity, const std::array<const void *, MAX_COMPONENTS> &copyData,
										const std::array<void *, MAX_COMPONENTS> &moveData, const ComponentMask &tags = ComponentMask(0));

			/*! @brief Remove an entity from a chunk and compact storage.
				@param[in] chunkIndex Index of the chunk containing the entity.
				@param[in] slotIndex Slot index of the entity in the chunk.
				@return Pair `{movedEntity, slotIndex}` where `movedEntity` is the entity moved into the vacated slot (if any).
			*/
			std::pair<Entity, ui> removeEntity(const ui chunkIndex, const ui slotIndex);

			/*! @brief Compact storage by removing empty chunks and updating global entity records.
				@param[in,out] globalRecords Array of global entity records to update relocated entities' chunk indices.
			*/
			void compact(std::vector<EntityRecord> &globalRecords);

			// MARK: Template Member Function

			/*! @brief Iterate over each set component type id in the regular mask.
				@tparam F Callable invoked with a single `ComponentTypeID` argument for each set bit.
			*/
			template <typename F>
			constexpr void forEachComponent(F &&func) const noexcept
			{
				forEachSetBit(mRegularMask, std::forward<F>(func));
			}

		private:
			/*! @struct Chunk include/ECS/archetype.h
				@brief Fixed-size storage container used by an `Archetype` to hold entities and component arrays.
				@details Each `Chunk` is a contiguous, cache-friendly buffer aligned to `CHUNK_ALIGNMENT` and sized at `CHUNK_SIZE` bytes.
			   The buffer contains the entity array, tag-bitset array, and packed component arrays laid out by `computeLayout()`. The
			   `mCount` field indicates how many entity slots are currently constructed in the chunk; `mCapacity` stores the maximum slots
			   this chunk can hold (equal to the archetype's chunk capacity computed at construction).
				@note The buffer stores raw bytes; component constructors/destructors must be invoked explicitly.
			*/
			struct Chunk
			{
					alignas(CHUNK_ALIGNMENT) std::array<std::byte, CHUNK_SIZE> mBuffer{};
					ui mCount{0};
					ui mCapacity{0};
			};

			// MARK: Private Getters

			/*! @brief Return pointer to the mutable tag-bitset array for a chunk.
				@param[in] chunk Pointer to the chunk to query.
				@pre `chunk` is non-null and layout has been computed.
				@note The returned pointer is a raw, mutable pointer into the chunk buffer and does not transfer ownership; callers must
			   ensure concurrent safety externally.
				@return Pointer to the chunk's tag-bitset array (array of `ul`).
			*/
			ATTR_NODISCARD ul *getTagBitset(Chunk *chunk) const;

			/*! @overload ATTR_NODISCARD ul *getTagBitset(Chunk *chunk) const
				@brief Return pointer to the const tag-bitset array for a chunk.
				@param[in] chunk Pointer to the chunk to query.
				@return Const pointer to the chunk's tag-bitset array (array of `ul`).
			*/
			ATTR_NODISCARD const ul *getTagBitset(const Chunk *chunk) const;

			// MARK: Private Member Functions

			/*! @brief Compute the number of entities the archetype can store per chunk.
				@details Uses component sizes and alignments from @ref ComponentInfos and `CHUNK_SIZE` to determine a capacity that allows
			   all per-entity data (entities, tag bitset, components) to fit into a single chunk. The computation repeatedly reduces
			   capacity until layout fits.
				@return Capacity (number of entity slots) per chunk.
			*/
			ATTR_NODISCARD ui computeCapacity() const;

			/*! @brief Compute byte offsets and sizes for each regular component and tag/entity arrays.
				@param[in] capacity The per-chunk entity capacity used to compute offsets.
				@details Populates `mEntityArrayOffset`, `mTagBitsetOffset`, `mComponentOffsets`, and `mComponentSizes` based on component
			   alignment/size information. Must be called after `mSortedRegular` and `mChunkCapacity` are established.
			*/
			void computeLayout(ui capacity);

			/*! @brief Find or allocate a chunk with free space and return it.
				@param[out] chunk Reference to a `Chunk*` that will point to an available chunk on return.
				@param[in,out] chunkIndex Hint/index updated to the located chunk's index.
				@details Scans existing chunks for free slots, reuses entries from `mFreeChunks` when possible, and allocates a new chunk
			   when necessary. New chunks have their `capacity` set to `mChunkCapacity`.
			*/
			void acquireFreeChunk(Chunk *&chunk, std::size_t &chunkIndex);

			/*! @brief Release an entity slot by moving the last slot into the vacated slot (if any).
				@details If `slotIndex != lastSlot` the last slot's entity and component data are moved into `slotIndex` and the last slot's
			   component destructors are invoked. Does not adjust `chunk->count` (caller is responsible for decrementing it).
				@param[in,out] chunk The chunk containing the slot to release.
				@param[out] movedEntity If a move occurred, receives the entity moved into `slotIndex`.
				@param[in] slotIndex Index of the slot being released.
				@param[in] lastSlot Index of the last valid slot in the chunk prior to release.

			*/
			void releaseChunk(Chunk *&chunk, Entity &movedEntity, const ui slotIndex, const ui lastSlot);

			/*! @brief Move-construct component instances from `lastSlot` into `slotIndex` inside a chunk.
				@param[in,out] chunk The chunk containing component storage.
				@param[in] slotIndex Destination slot index.
				@param[in] lastSlot Source slot index.
				@note Uses the registered `moveConstruct` callbacks in @ref ComponentInfos for each regular component stored by this
			   archetype.
			*/
			void moveConstructChunk(Chunk *&chunk, const ui slotIndex, const ui lastSlot);

			/*! @brief Invoke destructors for components residing in `lastSlot`.
				@param[in,out] chunk The chunk containing component storage.
				@param[in] lastSlot Slot index whose components must be destroyed.
			*/
			void destructChunk(Chunk *&chunk, const ui lastSlot);

			/*! @brief Move non-empty chunks forward and update global entity records.
				@param[in,out] globalRecords Global table of `EntityRecord` entries to update relocated entities' chunk indices.
				@param[in] writeIndex Destination chunk index to receive data.
				@param[in] readIndex Source chunk index to move from.
				@details Transfers ownership of chunk storage and updates per-entity `EntityRecord::chunkIndex` when the record references
			   the moved chunk.
			*/
			void compactStorage(std::vector<EntityRecord> &globalRecords, const ui writeIndex, const ui readIndex);

		private:
			/*! @var mRegularMask
				@brief Component mask representing the regular (non-tag) components stored by this archetype.
				@details This mask is stable for the lifetime of the archetype and is used to determine layout and which component
			   `ComponentInfos` callbacks are invoked for construction, destruction, and moves. It does not contain tag bits (tags are
			   stored per-entity in chunk tag bitsets).
			*/
			ComponentMask mRegularMask;

			/*! @var mChunks
				@brief Owning container of chunk storage for this archetype.
				@details Each element is a `unique_ptr<Chunk>` owning the contiguous buffer used for a set of entity slots. Chunks may be
			   moved between indices during compaction.
			*/
			std::vector<std::unique_ptr<Chunk>> mChunks;

			/*! @var mFreeChunks
				@brief Indices of chunks that are currently empty and available for reuse.
				@details Values are indexes into `mChunks`. When a chunk becomes empty it is appended to this vector so its storage can be
			   recycled without allocating anew.
			*/
			std::vector<ui> mFreeChunks;

			/*! @var mSortedRegular
				@brief Sorted list of regular component type ids stored by this archetype.
				@details Only component types with non-zero `ComponentInfo::size` are included. The ordering is used for deterministic
			   layout and iteration when constructing/destructing component instances inside chunks.
			*/
			std::vector<ComponentTypeID> mSortedRegular;

			/*! @var mChunkVersions
				@brief Per-chunk version metadata used for change tracking.
				@details Each chunk has a corresponding `ChunkVersion` instance which records structural and per-component-version bumps to
			   allow systems to efficiently detect changes.
			*/
			std::vector<ChunkVersion> mChunkVersions;

			/*! @var mComponentOffsets
				@brief Byte offsets into a chunk's buffer for each component type.
				@details Indexed by `ComponentTypeID`; entries equal to `SIZE_MAX` indicate the component is not present in this archetype.
			   Populated by `computeLayout()`.
			*/
			std::array<std::size_t, MAX_COMPONENTS> mComponentOffsets{};

			/*! @var mComponentSizes
				@brief Per-component size in bytes for regular components stored in chunks.
				@details Indexed by `ComponentTypeID`. Populated by `computeLayout()` and used to compute per-slot offsets when
			   constructing/moving/destructing components.
			*/
			std::array<std::size_t, MAX_COMPONENTS> mComponentSizes{};

			/*! @var mEntityArrayOffset
				@brief Byte offset within a chunk's buffer where the `Entity` array begins.
				@details Computed by `computeLayout()` and used by `getEntityArray()` and chunk manipulation helpers to locate the entity
			   storage region.
			*/
			std::size_t mEntityArrayOffset{0};

			/*! @var mTagBitsetOffset
				@brief Byte offset within a chunk's buffer where the per-entity tag bitset array begins.
				@details Computed by `computeLayout()`; the tag bitset stores lower-half `ComponentMask` bits for each entity slot in the
			   chunk.
			*/
			std::size_t mTagBitsetOffset{0};

			/*! @var mArchetypeID
				@brief Stable identifier assigned by the ECS for this archetype instance.
				@details Used by global records to compare archetype identity without pointer equality.
			*/
			ui mArchetypeID;

			/*! @var mChunkCapacity
				@brief Number of entity slots per chunk for this archetype.
				@details Computed at construction time by `computeCapacity()` and assigned to newly allocated chunks' `capacity` fields. A
			   value of zero indicates no regular components or components that all have zero size.
			*/
			ui mChunkCapacity{0};
	};
} // namespace Dimensia::ECS

#endif