#include <assert.h>
#include <stddef.h>  // size_t
#include <stdlib.h>  // *alloc
#include <string.h>  // memset

#include "arena.h"
#include "arena_struct.h"
#include "compiler_attributes_macros.h"

typedef unsigned char u8;

/*! @brief check if a number is a power of 2. */
#define IS_POWER2(n) (((n) & ((n) - 1)) == 0)

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
 * @brief deallocate memory of an arena.
 *
 * @param arena pointer to the arena to deallocate.
 * @returns NULL always.
 */
void *arena_delete(Arena *const restrict arena)
{
	if (!arena)
		return (NULL);

	*arena = (Arena){0};
	free(arena);
	return (NULL);
}

/*!
 * @brief allocate an arena of the given capacity.
 *
 * @param capacity positive non-zero capacity in bytes of the arena to allocate.
 * @returns pointer to the new arena, NULL on error.
 */
Arena *arena_new(size_t capacity)
{
	if (capacity < 1)
		return (NULL);

	/* The `Arena` should be able to store an array of atleast one `Free_block`. */
	/* This ensures a new `Arena` can fulfil a request for a memory block of the */
	/* size of the original initialising capacity. */
	capacity = align_up(capacity, _alignof(Free_block));
	if (capacity < sizeof(Free_block))
		capacity = sizeof(Free_block);

	Arena *const restrict arena = malloc(sizeof(Arena) + capacity);
	if (arena)
	{
		*arena = (Arena){
			.capacity = capacity,
			.base = (u8 *)arena + sizeof(*arena),
		};
	}

	return (arena);
}

/*!
 * @brief reset the bump pointer of the arena to 0.
 *
 * @param arena non-null pointer to the arena.
 */
void arena_reset(Arena *const restrict arena)
{
	if (arena)
	{
		arena->offset = 0;
#if defined ENABLE_FREE_LIST || defined ENABLE_SIZE_CLASSES
		memset((void *)arena->blocks, 0, sizeof(arena->blocks));
#endif /* defined ENABLE_FREE_LIST || defined ENABLE_SIZE_CLASSES */
#if defined(ENABLE_ARENA_STATS)
		arena->num_allocs = 0;
		arena->num_frees = 0;
		arena->bytes_used = 0;
#endif
	}
}

#ifdef ENABLE_SIZE_CLASSES
static Free_block *restrict *
size_class_index(Arena *const restrict arena, const size_t size)
{
	size_t i = 0;
	while (i < ARRAY_LEN(ARENA_SIZE_CLASSES))
	{
		if (size > ARENA_SIZE_CLASSES[i] && arena->blocks[i])
			break;

		i++;
	}

	return (&arena->blocks[i]);
}
#endif /* ENABLE_SIZE_CLASSES */

/*!
 * @brief search for a `Free_block` of atleast the given size.
 *
 * @param start address of the pointer to the first `Free_block` to start searching from.
 * @param size the size to search for.
 * @returns pointer to the first block with a size greater or equal to `size`, NULL otherwise.
 */
static Free_block *fb_search(
	Arena *const restrict arena, const size_t size, const size_t alignment
)
{
	assert(IS_POWER2(alignment));
#if defined ENABLE_FREE_LIST
	Free_block *restrict *restrict prev = &arena->blocks;
#elif defined ENABLE_SIZE_CLASSES
	Free_block *restrict *restrict prev = size_class_index(arena, size);
#endif /* ENABLE_FREE_LIST */

	for (Free_block *block = *prev; block; block = block->next)
	{
		const size_t bs = block->size;
		u8 *const restrict mem = (u8 *)block + sizeof(block->size);
		if (bs >= size + alignment - 1 ||
			(bs >= size && (size_t)(&mem[block->size] -
									align_up((size_t)mem, alignment)) >= size))
		{
			*prev = block->next;
			return (block);
		}

		prev = &block->next;
	}

	return (NULL);
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
void *
arena_alloc(Arena *const restrict arena, size_t size, const size_t alignment)
{
	if (!arena || size < 1 || alignment > size || !IS_POWER2(alignment))
		return (NULL);

	const size_t alignof_fb = _alignof(Free_block);
	/* Search the free list first. */

	Free_block *restrict block = fb_search(arena, size, alignment);

	if (block)
	{
#if defined(ENABLE_ARENA_STATS)
		arena->num_allocs++;
#endif
		return (
			(void *)align_up((size_t)block + sizeof(block->size), alignment)
		);
	}

	/* Otherwise, allocate from bump pointer. */

	/* Only allocate in Aligned chunks of `Free_block`. */
	size = align_up(size, alignof_fb);
	if (size < sizeof(block))
		size = sizeof(block);

	u8 *const restrict current = arena->base + arena->offset;
	/* Align memory and leave some space before the memory to store the size. */
	u8 *const restrict aligned = (u8 *)align_up(
		(size_t)current + alignof_fb,
		alignof_fb > alignment ? alignof_fb : alignment
	);
	const size_t new_offset = (size_t)aligned + size - (size_t)arena->base;

	if (new_offset > arena->capacity)
		return (NULL);  // out of memory

	/* Zero out the memory between the current position of the bump pointer */
	/* and the start of the mem to be returned. */
	block = (Free_block *)memset(current, 0, aligned - current);
	/* The first 8 (sizeof(size_t)) bytes after the bump pointer store the */
	/* offset to the new position of the bump pointer, excluding the 8 bytes. */
	/* This position will also double as space for the `Free_block` struct */
	/* when this allocation is freed. */
	block->size = new_offset - (arena->offset + sizeof(block->size));
	arena->offset = new_offset;
#if defined(ENABLE_ARENA_STATS)
	arena->num_allocs++;
	arena->bytes_used += block->size;
#endif
	return ((void *)aligned);
}

/*!
 * @brief search for the true beginning of the memory block.
 *
 * @param ptr where to start searching from.
 *
 * The first 8 bytes (sizeof(size_t)) of the block should have the size of
 * the block, every byte in between there and `ptr` should be zeroed.
 *
 * @returns pointer to the start of the `Free_block`.
 */
static Free_block *fb_start_address(u8 *restrict ptr)
{
	do
	{
		ptr--;
	} while (*ptr == 0);

	return ((Free_block *)align_down((size_t)ptr, sizeof(size_t)));
}

static void
fb_insert(Arena *const restrict arena, Free_block *const restrict block)
{
#ifdef ENABLE_FREE_LIST
	Free_block *restrict *restrict slot = &arena->blocks;
#elif defined ENABLE_SIZE_CLASSES
	Free_block *restrict *restrict slot = size_class_index(arena, block->size);
#endif /* ENABLE_FREE_LIST */

	block->next = *slot;
	*slot = block;
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
#if defined(ENABLE_ARENA_STATS)
	arena->num_frees++;
#endif
	return (NULL);
}

#undef IS_POWER2
