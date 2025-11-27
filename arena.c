#include <assert.h>
#include <stdbool.h>  // bool
#include <stdio.h>    // perror
#include <stdlib.h>   // calloc
#include <string.h>   // memset
#include <sys/mman.h>  // mmap, munmap

#include "arena.h"
#include "arena_struct.h"
#include "compiler_attributes_macros.h"

/********************************** MACROS ****************************************/

#define MB256 (256U * 1024 * 1024)

/*! check if a number is a power of 2. */
#define IS_POWER2(n) (((n) & ((n) - 1)) == 0)
#define ARRAY_LEN(arr) (sizeof(arr) / sizeof(*(arr)))

#if defined(__has_feature)
	#if __has_feature(address_sanitizer)  // for clang
		#define __SANITIZE_ADDRESS__      // GCC already sets this
	#endif
#endif

#ifdef __SANITIZE_ADDRESS__
	#include <sanitizer/asan_interface.h>

	#define ASAN_POISON_MEMORY_REGION(addr, size)                             \
		__asan_poison_memory_region((addr), (size))
	#define ASAN_UNPOISON_MEMORY_REGION(addr, size)                           \
		__asan_unpoison_memory_region((addr), (size))
#else
	#define ASAN_POISON_MEMORY_REGION(addr, size) ((void)(addr), (void)(size))
	#define ASAN_UNPOISON_MEMORY_REGION(addr, size)                           \
		((void)(addr), (void)(size))
#endif

/********************************** TYPES ****************************************/

typedef unsigned char u8;

/****************************** STATIC FUNCTIONS **********************************/

static Free_block *fb_search(
	Arena *const arena, const ulen_ty size, const ulen_ty alignment
) _nonnull;
static Free_block *fb_start_address(u8 *ptr) _nonnull;
static void fb_insert(
	Arena *const restrict arena, Free_block *const restrict block
) _nonnull;

static void field_delete(Field *const field) _nonnull;
static Field *
field_new(const ulen_ty capacity) _malloc _malloc_free(field_delete);

static bool arena_isvalid(const Arena *const arena);
static Field *
arena_push_field(Arena *const restrict arena, const ulen_ty capacity) _nonnull;

static bool alignment_isvalid(const ulen_ty a)
{
	return (a > 0 && IS_POWER2(a));
}

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
static ulen_ty align_up(const ulen_ty n, const ulen_ty alignment)
{
	assert(alignment_isvalid(alignment));
	const ulen_ty mod = n & (alignment - 1);

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
static ulen_ty align_down(const ulen_ty n, const ulen_ty alignment)
{
	assert(alignment_isvalid(alignment));
	const ulen_ty mod = n & (alignment - 1);

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
static ulen_ty size_class_index(const ulen_ty size)
{
	ulen_ty i = 0;
	while (i < ARRAY_LEN(FREE_BLOCKS_SIZES))
	{
		if (size <= FREE_BLOCKS_SIZES[i])
			break;

		i++;
	}

	return (i);
}

/*!
 * @brief check if an Arena is in a valid state.
 *
 * @param arena pointer to the arena.
 * @returns true if valid, false otherwise.
 */
static bool arena_isvalid(const Arena *const arena)
{
	return (arena && arena->minimum_field_size > 0);
}

/*!
 * @brief push a new Field onto an Arena.
 *
 * @param arena pointer to the arena.
 * @param capacity minimum Field size to allocate.
 * @returns pointer to the pushed Field.
 */
static Field *
arena_push_field(Arena *const restrict arena, const ulen_ty capacity)
{
	assert(arena_isvalid(arena));
	assert(capacity > 0);
	ulen_ty size = arena->minimum_field_size;

	while (size / 2 < capacity)
		size *= 2;

	Field *const field = field_new(size);

	if (!field)
		return (NULL);

	field->next = arena->head;
	arena->head = field;
	return (field);
}

/*!
 * @brief search for a `Free_block` of atleast the given size.
 * @public @memberof Free_block
 *
 * @param start address of the pointer to the first `Free_block` to start searching from.
 * @param size the size to search for.
 * @returns pointer to the first block with a size greater or equal to `size`, NULL otherwise.
 */
static Free_block *
fb_search(Arena *const arena, const ulen_ty size, const ulen_ty alignment)
{
	assert(arena_isvalid(arena));
	assert(alignment_isvalid(alignment));
	for (ulen_ty i = size_class_index(size); i < ARRAY_LEN(arena->blocks); i++)
	{
		Free_block *restrict *prev = &arena->blocks[i];
		for (Free_block *block = *prev; block; block = block->next)
		{
			const ulen_ty bs = block->size;
			u8 *const mem = (u8 *)block + sizeof(block->size);
			if (bs >= size + alignment - 1 || /* clang-format off */
				(bs >= size && (ulen_ty)(&mem[block->size] - align_up((ulen_ty)mem, alignment)) >= size)) /* clang-format on */
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
 * @brief search for the true beginning of an allocation.
 * @public @memberof Free_block
 *
 * @param ptr a pointer allocated by `arena_alloc`.
 *
 * For every pointer allocated from the arena, book keeping info is stored in
 * the memory just before the pointer. The gap in between that info and the
 * pointer is always zeroed out. We can therefore find the first non-zeroed
 * memory just before a pointer in order to home in on the original position
 * where this allocation was bumped from.
 *
 * @returns pointer to a `Free_block` at the beginning of the allocation.
 */
static Free_block *fb_start_address(u8 *ptr)
{
	assert(ptr);
	do
	{
		ptr--;
	} while (*ptr == 0);

	return ((Free_block *)align_down((ulen_ty)ptr, alignof(Free_block)));
}

/*!
 * @brief insert a Free_block into the list of free blocks.
 * @public @memberof Free_block
 *
 * @param arena pointer to the Arena with the list of free blocks.
 * @param block pointer to the Free_block to insert.
 */
static void
fb_insert(Arena *const restrict arena, Free_block *const restrict block)
{
	assert(arena_isvalid(arena));
	assert(block);
	Free_block *restrict *slot = &arena->blocks[size_class_index(block->size)];

	block->next = *slot;
	*slot = block;
}

/*!
 * @brief deallocate a `Field`.
 * @public @memberof Field
 *
 * @param field pointer to the Field.
 */
static void field_delete(Field *const field)
{
	const int err = munmap(field, field->size + sizeof(*field));
	assert(err != -1 && "munmap fail");
	(void)err;
}

/*!
 * @brief return pointer to a new `Field`.
 * @public @memberof Field
 *
 * @param capacity size in bytes of the new Field.
 * @returns pointer to the new Field, NULL on error.
 */
static Field *field_new(const ulen_ty size)
{
	assert(size > 0);
	Field *const field = mmap(
		NULL, size + sizeof(*field), PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0
	);

	if (!field)
	{
		perror("mmap error");
		return (NULL);
	}

	*field = (Field){.size = size};
	field->top = field->base;
	ASAN_POISON_MEMORY_REGION(field->base, field->size);
	return (field);
}

/**********************************************************************************/

/*!
 * @brief deallocate memory of an arena.
 * @public @memberof Arena
 *
 * @param arena pointer to the arena to deallocate.
 * @returns NULL always.
 */
void *arena_delete(Arena *const arena)
{
	if (!arena)
		return (NULL);

	Field *walk = arena->head;

	while (walk)
	{
		Field *const next = walk->next;

		field_delete(walk);
		walk = next;
	}

	*arena = (Arena){0};
	free(arena);
	return (NULL);
}

/*!
 * @brief return a pointer to a new arena.
 * @public @memberof Arena
 *
 * @returns pointer to the new arena, NULL on error.
 */
Arena *arena_new()
{
	Arena *const arena = calloc(1, sizeof(*arena));

	if (arena)
		arena->minimum_field_size = MB256;

	return (arena);
}

/*!
 * @brief reset the top of the arena to the base.
 * @public @memberof Arena
 *
 * @param arena non-null pointer to the arena.
 */
void arena_reset(Arena *const arena)
{
	if (!arena_isvalid(arena))
		return;

	if (arena->head)
	{
		for (Field *next = arena->head->next; next; next = next->next)
		{
			field_delete(arena->head);
			arena->head = next;
		}

		arena->head->top = arena->head->base;
	}

	memset((void *)arena->blocks, 0, sizeof(arena->blocks));
#ifdef ARENA_STATS
	arena->frees++;
	arena->memory_inuse = 0;
#endif /* ARENA_STATS */
}

/*!
 * @brief return pointer to a memory block of a given size and alignment.
 * @public @memberof Arena
 *
 * @param arena non-null pointer to the arena.
 * @param size non-negative non-zero size in bytes to allocate.
 * @param alignment non-negative non-zero desired alignment of the pointer.
 * Must be greater than `size`.
 * @returns an aligned pointer to a memory block atleast `size` bytes,
 * NULL on error.
 */
void *arena_alloc(Arena *const arena, ulen_ty size, ulen_ty alignment)
{
	if (!arena_isvalid(arena) || size < 1 || alignment > size ||
		!alignment_isvalid(alignment))
		return (NULL);

	/* Search the free list first. */

	Free_block *restrict block = fb_search(arena, size, alignment);

	if (block)
	{
		u8 *const user_space = (u8 *)block + sizeof(block->size);
		const ulen_ty aligned = align_up((ulen_ty)user_space, alignment);

		/* Unpoison the minimum memory needed to fulfil user's request. */
		ASAN_UNPOISON_MEMORY_REGION(
			user_space, (aligned - (len_ty)user_space) + size
		);
		memset(user_space, 0, aligned - (ulen_ty)user_space);
#ifdef ARENA_STATS
		arena->allocs++;
		arena->memory_inuse += block->size;
		arena->total_memory_requested += size;
#endif /* ARENA_STATS */
		return ((void *)aligned);
	}

	/* Otherwise, allocate from the top of the Arena. */

	if (size < sizeof(Free_block) - sizeof(block->size))
		size = sizeof(Free_block) - sizeof(block->size);

	/* Align memory and leave some space before the memory to store the size. */
	Field *field = arena->head;

	if (!field)
	{
		field = arena_push_field(arena, size);
		if (!field)
			return (arena_delete(arena));
	}

	const ulen_ty alignof_fb = _alignof(Free_block);
	alignment = alignof_fb > alignment ? alignof_fb : alignment;
	/* The first few bytes of memory are reserved for internal book keeping. */
	u8 *user_space = (u8 *)field->top + sizeof(block->size);
	/* The aligned pointer to return. */
	ulen_ty aligned = align_up((ulen_ty)user_space, alignment);
	/* top should always be aligned to store a `Free_block` struct. */
	u8 *new_top = (u8 *)align_up(aligned + size, alignof_fb);

	if (new_top > field->base + field->size)
	{
		field = arena_push_field(arena, size);
		if (!field)
			return (arena_delete(arena));

		user_space = (u8 *)field->top + sizeof(block->size);
		aligned = align_up((ulen_ty)user_space, alignment);
		new_top = (u8 *)align_up(aligned + size, alignof_fb);
	}

	/* Unpoison the minimum memory needed to fulfil user's request */
	/* and store book keeping info. */
	ASAN_UNPOISON_MEMORY_REGION(
		field->top, (aligned - (len_ty)field->top) + size
	);

	/* When this allocation is freed, it will be stored in the free list. */
	/* The first few bytes after the current top will be used to store the */
	/* `Free_block` struct that contains details about the free block. */
	/* But until then we only store the total size of this allocation in */
	/* the first few bytes excluding the space needed to store this information. */
	/* The gap between the size and the pointer to be returned should be zeroed. */

	block = (Free_block *)memset(field->top, 0, aligned - (ulen_ty)field->top);
	block->size = new_top - user_space;
	field->top = new_top;
#ifdef ARENA_STATS
	arena->allocs++;
	arena->memory_inuse += block->size;
	arena->total_memory_requested += size;
#endif /* ARENA_STATS */
	return ((void *)aligned);
}

/*!
 * @brief return an allocated block of memory to an arena.
 * @public @memberof Arena
 *
 * @param arena non-null pointer to the arena.
 * @param ptr pointer to the block of memory.
 * @returns NULL always.
 */
void *arena_free(Arena *const restrict arena, void *const restrict ptr)
{
	if (!arena_isvalid(arena) || !ptr)
		return (NULL);

	Free_block *const restrict block = fb_start_address(ptr);

	ASAN_POISON_MEMORY_REGION(
		(u8 *)block + sizeof(*block),
		block->size - (sizeof(*block) - sizeof(block->size))
	);
	fb_insert(arena, block);

#ifdef ARENA_STATS
	arena->frees++;
	arena->memory_inuse -= block->size;
#endif /* ARENA_STATS */
	return (NULL);
}

#undef MB256

#undef IS_POWER2
#undef ARRAY_LEN
