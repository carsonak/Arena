#include <getopt.h>   // getopt_long
#include <stdbool.h>  // bool
#include <stdio.h>    // fprintf
#include <stdlib.h>   // rand, srand
#include <string.h>   // memset

#include "arena.h"
#include "arena_struct.h"
#include "len_type.h"

#define ARGS_ERROR(option, optarg)                                            \
	fprintf(                                                                  \
		stderr,                                                               \
		"ERROR: " option                                                      \
		" expect an unsigned integer as an argument got %s instead\n",        \
		optarg                                                                \
	)

#define REPORT_LINE(message)                                                  \
	fprintf(stderr, "%s:%d " message "\n", __FILE__, __LINE__);

typedef struct Array
{
	len_ty len;
	unsigned char *arr;
} Array;

static bool is_aligned(void *const ptr, const ulen_ty alignment)
{
	return ((ulen_ty)ptr & (alignment - 1)) == 0;
}

static void print_arena_stats(const Arena *const arena)
{
	len_ty fields = 0, arena_size = 0;

	for (Field *walk = arena->head; walk; walk = walk->next)
	{
		fields++;
		arena_size += walk->size;
	}

	printf("allocs: %" PRI_len, arena->allocs);
	printf(", frees: %" PRI_len, arena->frees);
	printf(", arena size: %" PRI_len, arena_size);
	printf(", memory in use: %" PRI_len, arena->memory_inuse);
	printf(
		", total requested memory: %" PRI_len, arena->total_memory_requested
	);
	printf(", fields: %" PRI_len, fields);
	printf(", minimum field size: %" PRI_ulen, arena->minimum_field_size);
}

int main(int argc, char *argv[])
{
	const unsigned num_arrays = 1024;
	Array arrays[1024] = {0};
	int status = 0;

	ulen_ty iterations = 1 << 20;
	unsigned seed = 0x12345;
	ulen_ty minimum_field_size = 256U * 1024;  // 256KB
	struct option options[] = {
		{"field-size", required_argument, NULL, 'f'},
		{"iterations", required_argument, NULL, 'i'},
		{"seed", required_argument, NULL, 's'},
		{0, 0, 0, 0}
	};

	for (int opt = getopt_long(argc, argv, "f:i:s:", options, NULL);
		 opt != -1;)
	{
		char *end = "";

		switch (opt)
		{
		case 'f':
			minimum_field_size = strtoul(optarg, &end, 0);
			if (*end)
				ARGS_ERROR("-f, --field-size", optarg);

			break;
		case 'i':
			iterations = strtoul(optarg, &end, 0);
			if (*end)
				ARGS_ERROR("-i, --iterations", optarg);

			break;
		case 's':
			seed = (unsigned)strtoul(optarg, &end, 0);
			if (*end)
				ARGS_ERROR("-s, --seed", optarg);

			break;
		case '?':
			return (1);
		default:
			break;
		}

		if (*end)
			return (1);

		opt = getopt_long(argc, argv, "f:i:s:", options, NULL);
	}

	Arena *const arena = arena_new();

	if (!arena)
		return (1);

	arena->minimum_field_size = minimum_field_size;
	srand(seed);
	for (ulen_ty i = 0; i < iterations; ++i)
	{
		unsigned idx = rand() % num_arrays;

		if (!arrays[idx].arr)  // Allocate memory and write to it.
		{
			arrays[idx].len = (rand() % (2 << 13)) + 1;
			len_ty align = 1 << (rand() % 11);  // 1, 2, 4 ... 1024

			if (align > arrays[idx].len)
				align = 1;

			arrays[idx].arr = arena_alloc(arena, arrays[idx].len, align);
			if (!arrays[idx].arr)
			{
				REPORT_LINE("allocation failure");
				status = 1;
				goto cleanup;
			}

			if (!is_aligned(arrays[idx].arr, align))
			{
				REPORT_LINE("pointer not aligned");
				status = 1;
				goto cleanup;
			}

			// Fill with pattern to check for overlap/corruption later
			memset(arrays[idx].arr, (idx & 0xFF), arrays[idx].len);
		}
		else  // Verify contents memory and free.
		{
			unsigned char *p = arrays[idx].arr;

			for (len_ty arr_i = 0; arr_i < arrays[idx].len; ++arr_i)
			{
				if (p[arr_i] != (idx & 0xFF))
				{
					REPORT_LINE("memory corruption detected");
					status = 1;
					goto cleanup;
				}
			}

			arena_free(arena, arrays[idx].arr);
			arrays[idx] = (Array){0};
		}
	}

	printf("iterations: %" PRI_ulen ", ", iterations);
	print_arena_stats(arena);
	putchar('\n');
cleanup:
	arena_delete(arena);
	return (status);
}
