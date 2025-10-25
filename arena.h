#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>  // size_t

#include "compiler_attributes_macros.h"

typedef struct Arena Arena;

/* allocation */

void *arena_destroy(Arena *const restrict arena);
Arena *arena_create(const size_t capacity) _malloc _malloc_free(arena_destroy);

void *arena_free(Arena *const restrict arena, void *const restrict ptr);
void *arena_alloc(
	Arena *const restrict arena, const size_t size, size_t alignment
) _malloc _alloc_size(2);
void arena_reset(Arena *const restrict arena);

#endif /* ARENA_H */
