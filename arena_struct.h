#ifndef ARENA_STRUCT_H
#define ARENA_STRUCT_H

#include <stddef.h>  // size_t

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

/*!
 * @brief data for an arena allocator.
 */
struct Arena
{
	/*! @brief @public total memory in bytes the arena holds. */
	size_t capacity;
	/*! @brief @private start of untouched memory in the arena. */
	size_t offset;
	/*! @brief @private pointer to list of freed blocks of memory. */
	Free_block *restrict free_list;
	/*! @brief @private pointer to the start of the memory in the arena. */
	unsigned char *restrict base;
};

#endif /* ARENA_STRUCT_H */
