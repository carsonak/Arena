#ifndef ARENA_H
#define ARENA_H

#include "compiler_attributes_macros.h"

typedef struct Arena Arena;

/* allocation */

void *arena_destroy(Arena *const restrict arena);
Arena *arena_create(const size_t capacity) _malloc _malloc_free(arena_destroy);

void *arena_free(Arena *const restrict arena, void *const restrict ptr);
void *arena_alloc(
	Arena *const restrict arena, size_t size, size_t alignment
) _malloc _alloc_size(2) _malloc_free(arena_free);
void arena_reset(Arena *const restrict arena);

#endif /* ARENA_H */
