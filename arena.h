#ifndef ARENA_H
#define ARENA_H

#include <inttypes.h>  // uintptr_t

#include "compiler_attributes_macros.h"

typedef struct Arena Arena;

/* allocation */

void *arena_delete(Arena *const arena);
Arena *arena_new() _malloc _malloc_free(arena_delete, 1);

void arena_reset(Arena *const arena);
void *arena_free(Arena *const restrict arena, void *const restrict ptr);
void *arena_alloc(
	Arena *const arena, uintptr_t size, const uintptr_t alignment
) _malloc _alloc_size(2);

#include "undef_compiler_attributes_macros.h"

#endif /* ARENA_H */
