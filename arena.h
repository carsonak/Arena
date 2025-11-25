#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>  // size_t

#include "common_callback_types.h"
#include "compiler_attributes_macros.h"

typedef struct Arena Arena;

/* allocation */

void *arena_delete(
	mem_free *const dealloc, void *const restrict dealloc_context,
	Arena *const restrict arena
);
Arena *arena_new(
	mem_alloc *const alloc, void *const alloc_context, size_t capacity
) _malloc _malloc_free(arena_delete, 3);

Arena *arena_new_at(unsigned char *const mem, const size_t size);
Arena *arena_nest(Arena *const arena, size_t capacity) _malloc;

void *arena_free(Arena *const restrict arena, void *const restrict ptr);
void *arena_alloc(
	Arena *const arena, size_t size, const size_t alignment
) _malloc _alloc_size(2);
void arena_reset(Arena *const arena);

#endif /* ARENA_H */
