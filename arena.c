#include <assert.h>
#include <stdint.h>  // size_t
#include <stdlib.h>  // *alloc

#include "arena.h"
#include "arena_struct.h"
#include "compiler_attributes_macros.h"

/*! @brief check if a number is a power of 2. */
#define IS_POWER2(n) (((n) & ((n) - 1)) == 0)

/*!
 * @brief return the next number divisible by `alignment`.
 *
 * @param n number to align.
 * @param alignment number to align with.
 *
 * @invariant assumes `alignment` is a power of 2.
 *
 * @returns next number greater or equal to `n` that is divisible by `alignment`.
 */
static size_t align_forward(const size_t n, const size_t alignment)
{
	assert(IS_POWER2(alignment));
	const size_t mod = n & (alignment - 1);

	return (mod ? n + (alignment - mod) : n);
}

/*!
 * @brief return a preceding number divisible by `alignment`.
 *
 * @param n number to align.
 * @param alignment number to align with.
 *
 * @invariant assumes `alignment` is a power of 2.
 *
 * @returns previous number greater or equal to `n` that is divisible by `alignment`.
 */
static size_t align_backward(const size_t n, const size_t alignment)
{
	assert(IS_POWER2(alignment));
	const size_t mod = n & (alignment - 1);

	return (n - mod);
}

/*!
 * @brief deallocate memory of an arena.
 *
 * @param arena pointer to the arena to deallocate.
 * @returns NULL always.
 */
void *arena_destroy(Arena *const restrict arena)
{
	if (!arena)
		return (NULL);

	free(arena->base);
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
Arena *arena_create(const size_t capacity)
{
	if (capacity < 1)
		return (NULL);

	Arena *const restrict arena = malloc(sizeof(Arena));

	if (!arena)
		return (NULL);

	*arena = (Arena){.capacity = capacity, .base = malloc(capacity)};
	if (!arena->base)
		return (arena_destroy(arena));

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
		arena->free_list = NULL;
	}
}

/*!
 * @brief search for a `Free_block` of atleast the given size.
 *
 * @param start address of the pointer to the first `Free_block` to start searching from.
 * @param size the size to search for.
 * @returns pointer to the first block with a size greater or equal to `size`, NULL otherwise.
 */
static Free_block *
fb_search(Free_block *restrict *const restrict start, const size_t size)
{
	Free_block **restrict prev = start;

	for (Free_block *block = *start; block; block = block->next)
	{
		if (block->size >= size)
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
void *arena_alloc(Arena *const restrict arena, size_t size, size_t alignment)
{
	if (!arena || size < 1 || alignment < size || IS_POWER2(alignment))
		return (NULL);

	/* Search the free list first. */
	size = alignment > size ? alignment : size;
	Free_block *restrict block = fb_search(&arena->free_list, size);

	if (block)
		return ((unsigned char *)block + _alignof(block->size));

	/* Otherwise, allocate from bump pointer. */
	alignment =
		_alignof(block->size) > alignment ? _alignof(block->size) : alignment;
	/* Pointer to free memory in the arena. */
	unsigned char *const restrict current = arena->base + arena->offset;
	/* An aligned pointer after `current` with enough space between it and */
	/* `current` to store the size of the new allocation. */
	unsigned char *const restrict aligned =
		align_forward(current + alignment, alignment);
	const size_t new_offset = aligned + size - (size_t)arena->base;

	if (new_offset > arena->capacity)
		return (NULL);  // out of memory

	arena->offset = new_offset;
	block = align_backward(
		(size_t)aligned - _alignof(block->size), _alignof(block->size)
	);
	*block = (Free_block){.size = size};
	return (aligned);
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

	Free_block *const restrict block = align_backward(
		(size_t)ptr - _alignof(block->size), _alignof(block->size)
	);

	block->next = arena->free_list;
	arena->free_list = block;
	return (NULL);
}

#undef IS_POWER2
