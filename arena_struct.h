#ifndef ARENA_STRUCT_H
#define ARENA_STRUCT_H

#include <stdint.h>  // size_t

typedef struct Free_block
{
	size_t size;
	struct Free_block *restrict next;
} Free_block;

struct Arena
{
	size_t capacity;
	size_t offset;
	Free_block *restrict free_list;
	unsigned char *restrict base;
};

#endif /* ARENA_STRUCT_H */
