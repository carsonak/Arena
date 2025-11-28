#include <limits.h>
#include <stdbool.h>  // bool
#include <stdint.h>   // uintptr_t
#include <string.h>   // memset, memcmp
#include <tau/tau.h>

#include "arena.h"
#include "arena_struct.h"

#include "compiler_attributes_macros.h"

#define ARRAY_ISEMPTY(array)                                                  \
	(!(array) ||                                                              \
	 memcmp(                                                                  \
		 (void *)array, (unsigned char[sizeof(array)]){0}, sizeof(array)      \
	 ) == 0)

TAU_MAIN()

static bool is_aligned(void *const ptr, const ulen_ty alignment)
{
	return ((ulen_ty)ptr & (alignment - 1)) == 0;
}

struct InvalidInputs
{
	Arena *arena;
};

TEST_F_SETUP(InvalidInputs)
{
	tau->arena = arena_new();
	REQUIRE(tau->arena != NULL, "arena creation failed");
}

TEST_F_TEARDOWN(InvalidInputs) { tau->arena = arena_delete(tau->arena); }

TEST_F(InvalidInputs, InvalidInput)
{
	CHECK(
		arena_alloc(tau->arena, 0, 8) == NULL, "Size should be greater than 0"
	);

	CHECK(
		arena_alloc(tau->arena, 15, 0) == NULL,
		"Alignment should be greater than 0"
	);

	CHECK(
		arena_alloc(tau->arena, 10, 3) == NULL,
		"Alignment should be a power of 2"
	);

	CHECK(
		arena_alloc(tau->arena, 4, 8) == NULL,
		"Alignment should be less than size"
	);
}

struct ArenaTests
{
	Arena *arena;
};

TEST_F_SETUP(ArenaTests)
{
	tau->arena = arena_new();
	REQUIRE(tau->arena != NULL, "arena creation failed");
}

TEST_F_TEARDOWN(ArenaTests) { tau->arena = arena_delete(tau->arena); }

TEST_F(ArenaTests, BasicLifecycle)
{
	char *ch = arena_alloc(tau->arena, sizeof(*ch), _alignof(char));

	REQUIRE(ch != NULL, "alloc failed");
	*ch = 'w';
	CHECK(*ch == 'w', "Memory read/write failed");

	int *num = arena_alloc(tau->arena, sizeof(*num), _alignof(int));

	REQUIRE(num != NULL, "alloc failed");
	*num = INT_MAX;
	CHECK(*num == INT_MAX, "Memory read/write failed");

	intmax_t *maxint =
		arena_alloc(tau->arena, sizeof(*maxint), _alignof(intmax_t));

	REQUIRE(maxint != NULL, "alloc failed");
	*maxint = INTMAX_MIN;
	CHECK(*maxint == INTMAX_MIN, "Memory read/write failed");
}

TEST_F(ArenaTests, FieldExpansion)
{
	tau->arena->minimum_field_size = 4096;

	// Note: Overhead of Field struct and alignment padding will be involved
	void *p1 = arena_alloc(tau->arena, 2000, 1);
	REQUIRE(p1 != NULL, "Allocation failed");
	memset(p1, 'w', 2000);

	// Verify internals (White-box)
	REQUIRE(tau->arena->head != NULL, "Arena head should not be NULL");
	Field *first_field = tau->arena->head;

	// This alloc should force a new Field because 2000 + 4000 > 4096
	void *p2 = arena_alloc(tau->arena, 4000, 1);
	REQUIRE(p2 != NULL, "Allocation failed");
	memset(p2, 'w', 4000);

	CHECK(tau->arena->head != first_field, "Arena did not push a new field");
	CHECK(
		tau->arena->head->next == first_field,
		"New field is not linked to old field"
	);
}

TEST_F(ArenaTests, LargeAllocation)
{
	tau->arena->minimum_field_size = 4096;  // 4KB default
	ulen_ty size = 1024 * 10;

	// Allocate 10KB (larger than default field size)
	void *p1 = arena_alloc(tau->arena, size, 16);
	CHECK(p1 != NULL, "allocation failed");
	memset(p1, 'w', size);

	CHECK(
		tau->arena->head->size >= size,
		"Field size did not adapt to large allocation"
	);
}

TEST_F(ArenaTests, FreeListReuse)
{
	void *p1 = arena_alloc(tau->arena, 64, 8);
	void *p2 = arena_alloc(tau->arena, 64, 8);
	void *p3 = arena_alloc(tau->arena, 64, 8);

	memset(p1, 'w', 64);
	memset(p2, 'w', 64);
	memset(p3, 'w', 64);
	REQUIRE(
		ARRAY_ISEMPTY(tau->arena->blocks),
		"list of free blocks should be empty"
	);
	// Free p2. It should go to the free list.
	arena_free(tau->arena, p2);
	CHECK(
		ARRAY_ISEMPTY(tau->arena->blocks) == false,
		"list of free blocks should not be empty"
	);

	// Alloc p4. It should ideally take p2's spot.
	void *top = tau->arena->head->top;
	void *p4 = arena_alloc(tau->arena, 64, 8);

	memset(p4, 'w', 64);
	// Depending on implementation (LIFO vs FIFO free list),
	// p4 often equals p2.
	CHECK(tau->arena->head->top == top, "arena should not bump the top");
}

struct AlignedAllocations
{
	Arena *arena;
};

TEST_F_SETUP(AlignedAllocations)
{
	tau->arena = arena_new();
	REQUIRE(tau->arena != NULL, "arena creation failed");
}

TEST_F_TEARDOWN(AlignedAllocations) { tau->arena = arena_delete(tau->arena); }

TEST_F(AlignedAllocations, AllocFree1)
{
	unsigned char *ptrs[11] = {0};

	/* alloc and immediately free. */
	for (len_ty i = 0; i < 11; i++)
	{
		const len_ty size = 1 << i;

		ptrs[i] = arena_alloc(tau->arena, size, size);

		REQUIRE(ptrs[i] != NULL, "alloc failed");
		CHECK(is_aligned(ptrs[i], size), "pointer not aligned");
		memset(ptrs[i], size & 0xFF, size);

		ptrs[i] = arena_free(tau->arena, ptrs[i]);
	}

	/* alloc all at once then free later. */
	for (len_ty i = 0; i < 11; i++)
	{
		const len_ty size = 1 << i;

		ptrs[i] = arena_alloc(tau->arena, size, size);

		REQUIRE(ptrs[i] != NULL, "alloc failed");
		CHECK(is_aligned(ptrs[i], size), "pointer not aligned");
		memset(ptrs[i], size & 0xFF, size);
	}

	for (len_ty i = 0; i < 11; i++)
		ptrs[i] = arena_free(tau->arena, ptrs[i]);
}

TEST_F(AlignedAllocations, AllocFree2)
{
	unsigned char *ptrs[11] = {0};

	/* alloc all at once then free later. */
	for (len_ty i = 0; i < 11; i++)
	{
		const len_ty size = 1 << i;

		ptrs[i] = arena_alloc(tau->arena, size, size);

		REQUIRE(ptrs[i] != NULL, "alloc failed");
		CHECK(is_aligned(ptrs[i], size), "pointer not aligned");
		memset(ptrs[i], size & 0xFF, size);
	}

	for (len_ty i = 0; i < 11; i++)
		ptrs[i] = arena_free(tau->arena, ptrs[i]);

	/* alloc and immediately free. */
	for (len_ty i = 0; i < 11; i++)
	{
		const len_ty size = 1 << i;

		ptrs[i] = arena_alloc(tau->arena, size, size);

		REQUIRE(ptrs[i] != NULL, "alloc failed");
		CHECK(is_aligned(ptrs[i], size), "pointer not aligned");
		memset(ptrs[i], size & 0xFF, size);

		ptrs[i] = arena_free(tau->arena, ptrs[i]);
	}
}

struct ArenaReset
{
	Arena *arena;
};

TEST_F_SETUP(ArenaReset)
{
	tau->arena = arena_new();
	REQUIRE(tau->arena != NULL, "arena creation failed");
}

TEST_F_TEARDOWN(ArenaReset) { tau->arena = arena_delete(tau->arena); }

TEST_F(ArenaReset, Reset)
{
	void *p1 = arena_alloc(tau->arena, 100, 1);
	void *p2 = arena_alloc(tau->arena, 100, 2);
	void *p3 = arena_alloc(tau->arena, 100, 8);

	REQUIRE(p1 != NULL, "allocation failed");
	REQUIRE(p2 != NULL, "allocation failed");
	REQUIRE(p3 != NULL, "allocation failed");
	memset(p1, 'w', 100);
	memset(p2, 'w', 100);
	memset(p3, 'w', 100);

	arena_reset(tau->arena);
	CHECK(tau->arena->head != NULL, "Head should not be NULL after reset");

	// Allocating again should return a pointer close to the base
	void *p4 = arena_alloc(tau->arena, 100, 1);

	REQUIRE(p4 != NULL, "allocation failed");
	memset(p4, 'w', 100);
	// The arena should retain the largest field it has.
	CHECK(tau->arena->head->size >= tau->arena->minimum_field_size);
	// Check that free blocks were cleared
	CHECK(
		ARRAY_ISEMPTY(tau->arena->blocks),
		"list of free blocks should be empty."
	);
}

TEST_F(ArenaReset, ResetWithFree)
{
	void *p1 = arena_alloc(tau->arena, 100, 1);
	void *p2 = arena_alloc(tau->arena, 100, 2);
	void *p3 = arena_alloc(tau->arena, 100, 8);

	REQUIRE(p1 != NULL, "allocation failed");
	REQUIRE(p2 != NULL, "allocation failed");
	REQUIRE(p3 != NULL, "allocation failed");
	memset(p1, 'w', 100);
	memset(p2, 'w', 100);
	memset(p3, 'w', 100);
	arena_free(tau->arena, p2);

	arena_reset(tau->arena);

	CHECK(tau->arena->head != NULL, "Head should not be NULL after reset");
	void *p4 = arena_alloc(tau->arena, 100, 1);

	REQUIRE(p4 != NULL, "allocation failed");
	memset(p4, 'w', 100);
	CHECK(tau->arena->head->size >= tau->arena->minimum_field_size);
	CHECK(
		ARRAY_ISEMPTY(tau->arena->blocks),
		"list of free blocks should be empty."
	);
}

TEST_F(ArenaReset, ResetWithFieldExpansion)
{
	tau->arena->minimum_field_size = 4096;
	void *p1 = arena_alloc(tau->arena, 3000, 1);
	void *p2 = arena_alloc(tau->arena, 3000, 2);
	void *p3 = arena_alloc(tau->arena, 3000, 8);

	REQUIRE(p1 != NULL, "allocation failed");
	REQUIRE(p2 != NULL, "allocation failed");
	REQUIRE(p3 != NULL, "allocation failed");
	memset(p1, 'w', 3000);
	memset(p2, 'w', 3000);
	memset(p3, 'w', 3000);
	void *old_top = tau->arena->head->top;

	arena_reset(tau->arena);

	CHECK(tau->arena->head != NULL, "Head should not be NULL after reset");
	CHECK(tau->arena->head->top != old_top, "top should be reset");
	void *p4 = arena_alloc(tau->arena, 100, 1);

	REQUIRE(p4 != NULL, "allocation failed");
	memset(p4, 'w', 100);
	CHECK(tau->arena->head->size >= tau->arena->minimum_field_size);
	CHECK(
		ARRAY_ISEMPTY(tau->arena->blocks),
		"list of free blocks should be empty."
	);
}

TEST_F(ArenaReset, ResetWithFieldExpansionWithFree)
{
	tau->arena->minimum_field_size = 4096;
	void *p1 = arena_alloc(tau->arena, 3000, 1);
	void *p2 = arena_alloc(tau->arena, 3000, 2);
	void *p3 = arena_alloc(tau->arena, 3000, 8);

	REQUIRE(p1 != NULL, "allocation failed");
	REQUIRE(p2 != NULL, "allocation failed");
	REQUIRE(p3 != NULL, "allocation failed");
	memset(p1, 'w', 3000);
	memset(p2, 'w', 3000);
	memset(p3, 'w', 3000);
	arena_free(tau->arena, p2);
	void *old_top = tau->arena->head->top;

	arena_reset(tau->arena);

	CHECK(tau->arena->head != NULL, "Head should not be NULL after reset");
	CHECK(tau->arena->head->top != old_top, "top should be reset");
	void *p4 = arena_alloc(tau->arena, 100, 1);

	REQUIRE(p4 != NULL, "allocation failed");
	memset(p4, 'w', 100);
	CHECK(tau->arena->head->size >= tau->arena->minimum_field_size);
	CHECK(
		ARRAY_ISEMPTY(tau->arena->blocks),
		"list of free blocks should be empty."
	);
}
