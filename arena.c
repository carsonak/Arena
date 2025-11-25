#include <assert.h>
#include <stddef.h>  // size_t
#include <stdlib.h>  // *alloc
#include <string.h>  // memset
#include <sys/mman.h>  // mmap, munmap

#include "arena.h"
#include "arena_struct.h"
#include "compiler_attributes_macros.h"

/********************************** MACROS ****************************************/

#define MB256 (256U * 1024 * 1024)

/*! check if a number is a power of 2. */
#define IS_POWER2(n) (((n) & ((n) - 1)) == 0)
#define ARRAY_LEN(arr) (sizeof(arr) / sizeof(*(arr)))

/********************************** GLOBALS ***************************************/

size_t MINIMUM_FIELD_SIZE = MB256;

/********************************** TYPES ****************************************/

typedef unsigned char u8;

/*!
 * @brief details of a chunk of reserved memory in an arena.
 */
typedef struct Field
{
	/*! @public usable capacity in bytes of this Field. */
	size_t size;
	/*! @private pointer to the next Field in the arena. */
	struct Field *restrict next;
	/*! @private start of untouched memory in the Field. */
	u8 *top;
	/*! @private start of usable memory in the Field. */
	u8 base[];
} Field;

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

/****************************** STATIC FUNCTIONS **********************************/

static Free_block *fb_search(
	Arena *const arena, const size_t size, const size_t alignment
) _nonnull;
static Free_block *fb_start_address(u8 *ptr) _nonnull;
static void fb_insert(
	Arena *const restrict arena, Free_block *const restrict block
) _nonnull;

static void field_delete(Field *const field) _nonnull;
static Field *
field_new(const size_t capacity) _malloc _malloc_free(field_delete);
static void
field_push(Field *const restrict field, Arena *const restrict arena) _nonnull;

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
 * @public @memberof Free_block
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
 * @public @memberof Free_block
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
static Field *field_new(const size_t capacity)
{
	size_t size = MINIMUM_FIELD_SIZE;

	while (capacity > size / 2)
		size *= 2;

	Field *const field = mmap(
		NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0
	);

	if (field)
	{
		*field = (Field){.size = size - sizeof(*field)};
		field->top = field->base;
	}

	return (field);
}

/*!
 * @brief push a `Field` onto an `Arena`'s stack of fields.
 * @public @memberof Field
 *
 * @param field pointer to the Field to push.
 * @param arena pointer to the Arena with the stack.
 */
static void
field_push(Field *const restrict field, Arena *const restrict arena)
{
	field->next = arena->head;
	arena->head = field;
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
	if (!arena)
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
	Field *field = arena->head;

	if (!field)
	{
		field = field_new(size);
		if (!field)
		{
			arena_delete(arena);
			return (NULL);
		}

		field_push(field, arena);
	}

	size_t aligned = align_up(
		(size_t)field->top + sizeof(block->size),
		alignof_fb > alignment ? alignof_fb : alignment
	);
	/* top should always be aligned for a `Free_block` type. */
	u8 *new_top = (u8 *)align_up(aligned + size, alignof_fb);

	if (new_top > field->base + field->size)
	{
		field = field_new(size);
		if (!field)
		{
			arena_delete(arena);
			return (NULL);
		}

		field_push(field, arena);
		aligned = align_up(
			(size_t)field->top + sizeof(block->size),
			alignof_fb > alignment ? alignof_fb : alignment
		);
		new_top = (u8 *)align_up(aligned + size, alignof_fb);
	}

	/* Zero out the memory between the current position of the top and the */
	/* start of the mem to be returned. */
	block = (Free_block *)memset(field->top, 0, aligned - (size_t)field->top);
	/* The first 8 (sizeof(Free_block.size)) bytes after the field top store */
	/* the offset to the new position of the top, excluding the 8 bytes. */
	/* This position will also serve as the beginning of the `Free_block` */
	/* struct when this allocation is freed. */
	block->size = new_top - &field->top[sizeof(block->size)];
	field->top = new_top;
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
	if (!arena || !ptr)
		return (NULL);

	Free_block *const restrict block = fb_start_address(ptr);

	fb_insert(arena, block);
	return (NULL);
}

#undef MB256

#undef IS_POWER2
#undef ARRAY_LEN
