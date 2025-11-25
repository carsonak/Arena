#include <assert.h>
#include <stddef.h>  // size_t
#include <stdlib.h>  // *alloc
#include <string.h>  // memset

#include "arena.h"
#include "arena_struct.h"
#include "compiler_attributes_macros.h"

/*************************************** TYPES ****************************************/

typedef unsigned char u8;

/*!
 * @brief node of a linked list of free blocks in an arena.
 */
typedef struct Free_block
{
	/*! size in bytes of the memory block. */
	size_t size;
	/*! pointer to the next free memory block. */
	struct Free_block *restrict next;
} Free_block;

/*************************************** MACROS ****************************************/

/*! check if a number is a power of 2. */
#define IS_POWER2(n) (((n) & ((n) - 1)) == 0)
#define ARRAY_LEN(arr) (sizeof(arr) / sizeof(*(arr)))

/*********************************** STATIC FUNCTIONS **********************************/

static Free_block *fb_search(
	Arena *const arena, const size_t size, const size_t alignment
) _nonnull;
static Free_block *fb_start_address(u8 *ptr) _nonnull;
static void fb_insert(
	Arena *const restrict arena, Free_block *const restrict block
) _nonnull;

/*!
 * @brief return the next multiple of the given number starting from a given number.
 *
 * @param n starting number.
 * @param alignment base multiple.
 *
 * @invariant assumes `alignment` is a power of 2.
 *
 * @returns next multiple of `alignment` greater than `n`.
 */
static size_t align_up(const size_t n, const size_t alignment)
{
	assert(IS_POWER2(alignment));
	const size_t mod = n & (alignment - 1);

	if (mod == 0)
		return (n);

	return (n | (alignment - 1)) + 1;
}

/*!
 * @brief return a preceding multiple of a number starting from a given number.
 *
 * @param n starting number.
 * @param alignment base multiple.
 *
 * @invariant assumes `alignment` is a power of 2.
 *
 * @returns previous multiple of `alignment` starting from `n`.
 */
static size_t align_down(const size_t n, const size_t alignment)
{
	assert(IS_POWER2(alignment));
	const size_t mod = n & (alignment - 1);

	if (mod == 0)
		return (n);

	return (n & ~(alignment - 1));
}

/*!
 * @brief match a given size to an index in the array of size classes.
 *
 * @param size the size to classify.
 * @returns index of the size class.
 */
static size_t size_class_index(const size_t size)
{
	size_t i = 0;
	while (i < ARRAY_LEN(FREE_BLOCKS_SIZES))
	{
		if (size <= FREE_BLOCKS_SIZES[i])
			break;

		i++;
	}

	return (i);
}

/*!
 * @brief search for a `Free_block` of atleast the given size.
 *
 * @param start address of the pointer to the first `Free_block` to start searching from.
 * @param size the size to search for.
 * @returns pointer to the first block with a size greater or equal to `size`, NULL otherwise.
 */
static Free_block *
fb_search(Arena *const arena, const size_t size, const size_t alignment)
{
	assert(IS_POWER2(alignment));
	for (size_t i = size_class_index(size); i < ARRAY_LEN(arena->blocks); i++)
	{
		Free_block *restrict *prev = &arena->blocks[i];
		for (Free_block *block = *prev; block; block = block->next)
		{
			const size_t bs = block->size;
			u8 *const mem = (u8 *)block + sizeof(block->size);
			if (bs >= size + alignment - 1 ||
				(bs >= size &&
				 (size_t)(&mem[block->size] -
						  align_up((size_t)mem, alignment)) >= size))
			{
				*prev = block->next;
				return (block);
			}

			prev = &block->next;
		}
	}

	return (NULL);
}

/*!
 * @brief search for the true beginning of the memory block.
 *
 * @param ptr where to start searching from.
 *
 * The first 8 bytes (sizeof(Free_block.size)) of the block should have the size of
 * the block, every byte in between there and `ptr` should be zeroed.
 *
 * @returns pointer to the start of the `Free_block`.
 */
static Free_block *fb_start_address(u8 *ptr)
{
	do
	{
		ptr--;
	} while (*ptr == 0);

	return ((Free_block *)align_down((size_t)ptr, alignof(Free_block)));
}

static void
fb_insert(Arena *const restrict arena, Free_block *const restrict block)
{
	Free_block *restrict *slot = &arena->blocks[size_class_index(block->size)];

	block->next = *slot;
	*slot = block;
}

/***************************************************************************************/

/*!
 * @brief deallocate memory of an arena.
 *
 * @param dealloc pointer to a function that can deallocate memory.
 * @param dealloc_context additional context for the deallocation function.
 * @param arena pointer to the arena to deallocate.
 * @returns NULL always.
 */
void *arena_delete(
	mem_free *const dealloc, void *const restrict dealloc_context,
	Arena *const restrict arena
)
{
	if (!arena)
		return (NULL);

	*arena = (Arena){0};
	if (dealloc)
		dealloc(dealloc_context, arena);
	else
		free(arena);

	return (NULL);
}

/*!
 * @brief allocate an arena of the given capacity.
 *
 * @param alloc pointer to a function that can allocate memory.
 * @param alloc_context additional context for the allocation function.
 * @param capacity positive non-zero capacity in bytes of the arena to allocate.
 * @returns pointer to the new arena, NULL on error.
 */
Arena *
arena_new(mem_alloc *const alloc, void *const alloc_context, size_t capacity)
{
	if (capacity < 1)
		return (NULL);

	/* The `Arena` should be able to store an array of atleast one `Free_block`. */
	/* This ensures a new `Arena` can fulfil a request for a memory block of the */
	/* size of the original initialising capacity. */
	capacity += sizeof((Free_block){0}.size);
	if (capacity < sizeof(Free_block) - sizeof((Free_block){0}.size))
		capacity = sizeof(Free_block) - sizeof((Free_block){0}.size);

	capacity = align_up(capacity + sizeof(Arena), _alignof(Free_block));
	Arena *arena = NULL;

	if (alloc)
		arena = arena_new_at(alloc(alloc_context, capacity), capacity);
	else
		arena = arena_new_at(malloc(capacity), capacity);

	return (arena);
}

/*!
 * @brief initialise a new Arena inside a given memory block.
 *
 * @param mem non-null pointer to a the memory block to use.
 * @param size size of the memory block, must be large enough to hold an
 * `Arena` and a `Free_block` struct.
 * @returns pointer to the new arena.
 */
Arena *arena_new_at(unsigned char *const mem, const size_t size)
{
	if (!mem || size <= sizeof(Arena))
		return (NULL);

	Arena *const arena = (Arena *)mem;

	if (size - sizeof(*arena) < sizeof(Free_block))
		return (NULL);

	*arena = (Arena){.capacity = size - sizeof(*arena)};
	arena->top = arena->base;
	return (arena);
}

/*!
 * @brief reset the top of the arena to the base.
 *
 * @param arena non-null pointer to the arena.
 */
void arena_reset(Arena *const arena)
{
	if (arena)
	{
		arena->top = arena->base;
		memset((void *)arena->blocks, 0, sizeof(arena->blocks));
	}
}

/*!
 * @brief return pointer to a memory block of a given size and alignment.
 *
 * @param arena non-null pointer to the arena.
 * @param size non-negative non-zero size in bytes to allocate.
 * @param alignment non-negative non-zero desired alignment of the pointer.
 * Must be greater than `size`.
 * @returns an aligned pointer to a memory block atleast `size` bytes,
 * NULL on error.
 */
void *arena_alloc(Arena *const arena, size_t size, const size_t alignment)
{
	if (!arena || size < 1 || alignment > size || !IS_POWER2(alignment))
		return (NULL);

	const size_t alignof_fb = _alignof(Free_block);
	/* Search the free list first. */

	Free_block *restrict block = fb_search(arena, size, alignment);

	if (block)
	{
		return (
			(void *)align_up((size_t)block + sizeof(block->size), alignment)
		);
	}

	/* Otherwise, allocate from the top of the Arena. */

	if (size < sizeof(Free_block) - sizeof(block->size))
		size = sizeof(Free_block) - sizeof(block->size);

	/* Align memory and leave some space before the memory to store the size. */
	const size_t aligned = align_up(
		(size_t)arena->top + sizeof(block->size),
		alignof_fb > alignment ? alignof_fb : alignment
	);
	/* top should always be aligned for a `Free_block` type. */
	u8 *const new_top = (u8 *)align_up(aligned + size, alignof_fb);

	if (new_top > &arena->base[arena->capacity])
		return (NULL);  // out of memory

	/* Zero out the memory between the current position of the top and the */
	/* start of the mem to be returned. */
	block = (Free_block *)memset(arena->top, 0, aligned - (size_t)arena->top);
	/* The first 8 (sizeof(Free_block.size)) bytes after the arena top store */
	/* the offset to the new position of the top, excluding the 8 bytes. */
	/* This position will also serve as the beginning of the `Free_block` */
	/* struct when this allocation is freed. */
	block->size = new_top - &arena->top[sizeof(block->size)];
	arena->top = new_top;
	return ((void *)aligned);
}

/*!
 * @brief allocate an arena nested within a new arena.
 *
 * @param arena pointer to the parent arena.
 * @param capacity positive non-zero capacity in bytes of the new arena.
 * @returns pointer to the new arena, NULL on error.
 */
Arena *arena_nest(Arena *const arena, size_t capacity)
{
	if (!arena || capacity < 1)
		return (NULL);

	capacity += sizeof(*arena) + sizeof((Free_block){0}.size);
	capacity = align_up(capacity, _alignof(Free_block));
	return (
		arena_new_at(arena_alloc(arena, capacity, _alignof(Arena)), capacity)
	);
}

/*!
 * @brief return an allocated block of memory to an arena.
 *
 * @param arena non-null pointer to the arena.
 * @param ptr pointer to the block of memory.
 * @returns NULL always.
 */
void *arena_free(Arena *const restrict arena, void *const restrict ptr)
{
	if (!arena || !ptr)
		return (NULL);

	Free_block *const restrict block = fb_start_address(ptr);

	fb_insert(arena, block);
	return (NULL);
}

#undef IS_POWER2
