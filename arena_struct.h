#ifndef ARENA_STRUCT_H
#define ARENA_STRUCT_H

#include "len_type.h"

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof(*(arr)))

/*! size categories of freed blocks in an Arena. */
static const unsigned int FREE_BLOCKS_SIZES[] = {
	2 << 4,  2 << 5,  2 << 6,  2 << 7,  2 << 8,  2 << 9,  2 << 10, 2 << 11,
	2 << 12, 2 << 13, 2 << 14, 2 << 15, 2 << 16, 2 << 17, 2 << 18, 2 << 19,
};

/*!
 * @brief details of a chunk of reserved memory in an arena.
 */
typedef struct Field
{
	/*! @public usable capacity in bytes of this Field. */
	ulen_ty size;
	/*! @private pointer to the next Field in the arena. */
	struct Field *restrict next;
	/*! @private start of untouched memory in the Field. */
	unsigned char *top;
	/*! @private start of usable memory in the Field. */
	unsigned char base[];
} Field;

/*!
 * @brief node of a linked list of free blocks in an arena.
 */
typedef struct Free_block
{
	/*! size in bytes of the memory block. */
	ulen_ty size;
	/*! pointer to the next free memory block. */
	struct Free_block *restrict next;
} Free_block;

/*!
 * @brief data for an arena allocator.
 */
struct Arena
{
	/*! @private pointer to the top of the stack of fields. */
	struct Field *restrict head;
	/*! @public minimum size of a `Field` in the arena, defaults to 256MB. */
	ulen_ty minimum_field_size;
	/*! @private array of linked lists of freed blocks of memory. */
	struct Free_block *restrict blocks[ARRAY_LEN(FREE_BLOCKS_SIZES) + 1];

#ifdef ARENA_STATS
	len_ty allocs, frees, memory_inuse;
	len_ty total_memory_requested;
#endif /* ARENA_STATS */
};

#undef ARRAY_LEN

#endif /* ARENA_STRUCT_H */
