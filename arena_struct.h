#ifndef ARENA_STRUCT_H
#define ARENA_STRUCT_H

#include <stddef.h>  // size_t

#define ENABLE_FREE_LIST
// #define ENABLE_SIZE_CLASSES
// #define ENABLE_ARENA_STATS

/*!
 * @brief node of a linked list of free blocks in an arena.
 */
typedef struct Free_block
{
	/*! @brief size in bytes of the memory block. */
	size_t size;
	/*! @brief pointer to the next free memory block. */
	struct Free_block *restrict next;
} Free_block;

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof(*(arr)))

/*! @brief size categories of freed blocks in an Arena. */
static const unsigned int ARENA_SIZE_CLASSES[] = {
	2 << 3, 2 << 4,  2 << 5,  2 << 6,  2 << 7,  2 << 8,
	2 << 9, 2 << 10, 2 << 11, 2 << 12, 2 << 13,
};

/*!
 * @brief data for an arena allocator.
 */
struct Arena
{
	/*! @brief @public total memory in bytes the arena holds. */
	size_t capacity;
	/*! @brief @private start of untouched memory in the arena. */
	size_t offset;

#ifdef ENABLE_FREE_LIST
	/*! @brief @private linked list of freed blocks of memory. */
	Free_block *restrict blocks;
#elif defined ENABLE_SIZE_CLASSES
	/*! @brief @private array of linked lists of freed blocks of memory. */
	Free_block *restrict blocks[ARRAY_LEN(ARENA_SIZE_CLASSES) + 1];
#endif /* ENABLE_FREE_LIST */

#ifdef ENABLE_ARENA_STATS
	size_t num_allocs;
	size_t num_frees;
	size_t bytes_used;
#endif /* ENABLE_ARENA_STATS */

	/*! @brief @private pointer to the start of the memory in the arena. */
	unsigned char *restrict base;
};

#endif /* ARENA_STRUCT_H */
