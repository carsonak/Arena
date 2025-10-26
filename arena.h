#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>  // size_t

#include "compiler_attributes_macros.h"

typedef struct Arena Arena;

/* allocation */

void *arena_delete(Arena *const restrict arena);
Arena *arena_new(size_t capacity) _malloc _malloc_free(arena_delete);

void *arena_free(Arena *const restrict arena, void *const restrict ptr);
void *arena_alloc(
	Arena *const restrict arena, size_t size, const size_t alignment
) _malloc _alloc_size(2);
void arena_reset(Arena *const restrict arena);

#endif /* ARENA_H */
