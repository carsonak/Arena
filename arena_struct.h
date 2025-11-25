#ifndef ARENA_STRUCT_H
#define ARENA_STRUCT_H

#include <stddef.h>  // size_t

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof(*(arr)))

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
	/*! @public total memory in bytes the arena holds. */
	size_t capacity;
	/*! @private start of untouched memory in the arena. */
	unsigned char *restrict top;
	/*! @private array of linked lists of freed blocks of memory. */
	struct Free_block *restrict blocks[ARRAY_LEN(FREE_BLOCKS_SIZES) + 1];
	/*! @private start of the memory in the arena. */
	unsigned char base[];
};

#undef ARRAY_LEN

#endif /* ARENA_STRUCT_H */
