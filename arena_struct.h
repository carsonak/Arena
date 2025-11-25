#ifndef ARENA_STRUCT_H
#define ARENA_STRUCT_H

#include <stddef.h>  // size_t

#include "common_callback_types.h"

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof(*(arr)))

/*! Minimum size of a `Field` in the arena, defaults to 256MB. */
extern size_t MINIMUM_FIELD_SIZE;

/*! size categories of freed blocks in an Arena. */
static const unsigned int FREE_BLOCKS_SIZES[] = {
	2 << 4,  2 << 5,  2 << 6,  2 << 7,  2 << 8,  2 << 9,  2 << 10, 2 << 11,
	2 << 12, 2 << 13, 2 << 14, 2 << 15, 2 << 16, 2 << 17, 2 << 18, 2 << 19,
};

/*!
 * @brief data for an arena allocator.
 */
struct Arena
{
	/*! @private pointer to the top of the stack of fields. */
	struct Field *restrict head;
	/*! @private array of linked lists of freed blocks of memory. */
	struct Free_block *restrict blocks[ARRAY_LEN(FREE_BLOCKS_SIZES) + 1];
};

#undef ARRAY_LEN

#endif /* ARENA_STRUCT_H */
