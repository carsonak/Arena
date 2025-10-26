#include <stddef.h>  // size_t
#include <string.h>  // memcmp

#include "arena.h"         // The Arena allocator API
#include "arena_struct.h"  // Access to the internal Arena struct for checking
#include "tau/tau.h"       // The Tau testing framework

TAU_MAIN()

#define CHECK_ALIGNED(ptr, alignment)                                         \
	CHECK_EQ(((size_t)(ptr) % (alignment)), 0, "Pointer not aligned")

#define BLOCKS_ISEMPTY(blocks)                                                \
	((!blocks) ||                                                             \
	 memcmp(                                                                  \
		 (void *)(blocks), (unsigned char[sizeof(blocks)]){0}, sizeof(blocks) \
	 ) == 0)

// --- Test Suite for Arena Lifecycle ---

TEST(ArenaLifecycle, CreateAndDestroy)
{
	Arena *a = arena_new(1024);
	REQUIRE_NOT_NULL(a, "arena_new should return a valid pointer");

	a = arena_delete(a);
	CHECK_NULL(a, "arena_delete should return NULL");
}

TEST(ArenaLifecycle, CreateInvalid)
{
	Arena *a = arena_new(0);
	CHECK_NULL(a, "arena_new with 0 capacity should return NULL");
}

TEST(ArenaLifecycle, DestroyNull)
{
	void *a = arena_delete(NULL);
	CHECK_NULL(a, "arena_delete(NULL) should return NULL");
}

// --- Test Suite for Arena Allocation ---

struct ArenaAllocation
{
	Arena *restrict a;
};

TEST_F_SETUP(ArenaAllocation)
{
	tau->a = arena_new(128);
	REQUIRE_NOT_NULL(tau->a);
}

TEST_F_TEARDOWN(ArenaAllocation) { tau->a = arena_delete(tau->a); }

TEST(ArenaAllocation, SmallArena)
{
	Arena *const restrict a = arena_new(2);
	REQUIRE_NOT_NULL(a);

	CHECK_NOT_NULL(arena_alloc(a, 4, 1), "arena_alloc should succeed");

	arena_delete(a);
}

TEST_F(ArenaAllocation, Simple)
{
	void *p = arena_alloc(tau->a, 16, 16);
	CHECK_NOT_NULL(p, "arena_alloc should succeed");
	CHECK_ALIGNED(p, 16);
}

TEST_F(ArenaAllocation, InvalidParams)
{
	CHECK_NULL(arena_alloc(NULL, 16, 16), "NULL arena should fail");
	CHECK_NULL(arena_alloc(tau->a, 0, 16), "size < 1 should fail");
	CHECK_NULL(arena_alloc(tau->a, 8, 16), "alignment > size should fail");
	CHECK_NULL(
		arena_alloc(tau->a, 16, 15), "alignment not power of 2 should fail"
	);
}

TEST_F(ArenaAllocation, OutOfMemory)
{
	void *p1 = arena_alloc(tau->a, 64, 1);
	CHECK_NOT_NULL(p1, "First allocation should succeed");

	// Allocate another 64 bytes. This might succeed or fail depending on
	// overhead, but the next one must fail.
	arena_alloc(tau->a, 64, 1);

	// Try to allocate more than available.
	void *p3 = arena_alloc(tau->a, 64, 1);
	CHECK_NULL(p3, "Allocation exceeding capacity should return NULL");
}

TEST_F(ArenaAllocation, Alignment)
{
	arena_delete(tau->a);
	tau->a = arena_new(1024);
	REQUIRE_NOT_NULL(tau->a);

	void *p1 = arena_alloc(tau->a, 1, 1);
	CHECK_NOT_NULL(p1);
	CHECK_ALIGNED(p1, 1);

	void *p2 = arena_alloc(tau->a, 2, 2);
	CHECK_NOT_NULL(p2);
	CHECK_ALIGNED(p2, 2);

	void *p4 = arena_alloc(tau->a, 4, 4);
	CHECK_NOT_NULL(p4);
	CHECK_ALIGNED(p4, 4);

	void *p8 = arena_alloc(tau->a, 8, 8);
	CHECK_NOT_NULL(p8);
	CHECK_ALIGNED(p8, 8);

	void *p16 = arena_alloc(tau->a, 16, 16);
	CHECK_NOT_NULL(p16);
	CHECK_ALIGNED(p16, 16);

	void *p32 = arena_alloc(tau->a, 32, 32);
	CHECK_NOT_NULL(p32);
	CHECK_ALIGNED(p32, 32);

	void *p64 = arena_alloc(tau->a, 64, 64);
	CHECK_NOT_NULL(p64);
	CHECK_ALIGNED(p64, 64);

	void *p128 = arena_alloc(tau->a, 128, 128);
	CHECK_NOT_NULL(p128);
	CHECK_ALIGNED(p128, 128);

	// Allocate with size > alignment
	void *p28 = arena_alloc(tau->a, 4 * 7, 4);
	CHECK_NOT_NULL(p28);
	CHECK_ALIGNED(p28, 4);
}

// --- Test Suite for Arena Free List ---

struct ArenaFreeList
{
	Arena *restrict a;
};

TEST_F_SETUP(ArenaFreeList)
{
	tau->a = arena_new(1024);
	REQUIRE_NOT_NULL(tau->a);
}

TEST_F_TEARDOWN(ArenaFreeList) { tau->a = arena_delete(tau->a); }

TEST(ArenaFreeList, SmallArena)
{
	Arena *a = arena_new(2);
	REQUIRE_NOT_NULL(a, "arena_new should return a valid pointer");

	void *const restrict p1 = arena_alloc(a, 2, 1);
	REQUIRE_NOT_NULL(
		p1,
		"arena_alloc with maximum size and minimum alignment should succeed"
	);

	/* Freeing should succeed. */
	arena_free(a, p1);

	const size_t offset = a->offset;
	/* Allocating the same size again should return the same pointer */
	/* that was freed earlier. */

	arena_alloc(a, 2, 1);
	CHECK_EQ(a->offset, offset, "freed memory should be reused");

	a = arena_delete(a);
}

TEST_F(ArenaFreeList, FreeAndReuse)
{
	void *ptr_to_free = arena_alloc(tau->a, 64, 64);
	REQUIRE_NOT_NULL(ptr_to_free);
	void *p_other = arena_alloc(tau->a, 64, 64);
	REQUIRE_NOT_NULL(p_other);

	REQUIRE_TRUE(
		BLOCKS_ISEMPTY(tau->a->blocks), "Free list should initially be empty"
	);

	// Free a pointer.
	arena_free(tau->a, ptr_to_free);
	CHECK_FALSE(
		BLOCKS_ISEMPTY(tau->a->blocks),
		"Free list should have one block after first free"
	);

	const size_t offset = tau->a->offset;
	// Allocate again with the same size
	void *p_reused = arena_alloc(tau->a, 64, 64);
	CHECK_NOT_NULL(p_reused, "Allocation from free list should succeed");

	// It should return the *exact same pointer*
	CHECK_EQ(tau->a->offset, offset, "freed memory should be reused");

	// The free list should be empty again
	CHECK_TRUE(
		BLOCKS_ISEMPTY(tau->a->blocks), "Free list should be empty after reuse"
	);

	// Allocate another, should bump from the main offset
	void *p_bumped = arena_alloc(tau->a, 64, 64);
	CHECK_NOT_NULL(p_bumped);
	CHECK_GT(
		tau->a->offset, offset, "New allocation should not be the reused one"
	);
}

TEST_F(ArenaFreeList, FreeNull)
{
	// Freeing NULL should be a safe no-op
	void *p = arena_free(tau->a, NULL);
	CHECK_NULL(p, "arena_free(a, NULL) should return NULL");
	CHECK_TRUE(
		BLOCKS_ISEMPTY(tau->a->blocks), "Free list should still be empty"
	);

	// Freeing with a NULL arena should also be a no-op
	p = arena_free(NULL, (void *)0x123cdf);
	CHECK_NULL(p, "arena_free(NULL, ptr) should return NULL");
}

TEST_F(ArenaFreeList, ReuseSmaller)
{
	// Allocate a 128-byte block
	void *p128 = arena_alloc(tau->a, 128, 128);
	REQUIRE_NOT_NULL(p128);

	// Free it
	arena_free(tau->a, p128);
	REQUIRE_FALSE(BLOCKS_ISEMPTY(tau->a->blocks));

	const size_t offset = tau->a->offset;
	// Allocate a 64-byte block.
	// The current implementation finds the first fitting block and returns it,
	// without splitting the block.
	void *p64 = arena_alloc(tau->a, 64, 64);
	REQUIRE_NOT_NULL(p64);

	// Should reuse the same pointer address
	CHECK_EQ(tau->a->offset, offset, "freed memory should be reused");
	// The free list should now be empty, as the 128-byte block was "consumed"
	CHECK_TRUE(
		BLOCKS_ISEMPTY(tau->a->blocks),
		"Free list should be empty after consuming block"
	);
}

struct FreeAndReuse
{
	Arena *restrict a;
	void *restrict p1, *restrict p2;
	size_t s1, s2;
};

TEST_F_SETUP(FreeAndReuse)
{
	tau->s1 = 4 * 4;
	tau->s2 = 4 * 8;
	tau->a = arena_new(tau->s1 + tau->s2 + sizeof((Free_block){0}.size) * 2);
	REQUIRE_NOT_NULL(tau->a);

	tau->p1 = arena_alloc(tau->a, tau->s1, 4);
	REQUIRE_NOT_NULL(tau->p1);

	tau->p2 = arena_alloc(tau->a, tau->s2, 4);
	REQUIRE_NOT_NULL(tau->p2);
}

TEST_F_TEARDOWN(FreeAndReuse) { tau->a = arena_delete(tau->a); }

TEST_F(FreeAndReuse, FreeBigSmallReuseSmallBig)
{
	// Free big memory then small memory.

	arena_free(tau->a, tau->p2);
	REQUIRE_FALSE(
		BLOCKS_ISEMPTY(tau->a->blocks),
		"Free list should have one block after first free"
	);
	arena_free(tau->a, tau->p1);

	const size_t offset = tau->a->offset;
	// Allocate small memory then big memory.

	arena_alloc(tau->a, tau->s1, 4);
	CHECK_EQ(tau->a->offset, offset, "freed memory should be reused");
	arena_alloc(tau->a, tau->s2, 4);
	CHECK_EQ(tau->a->offset, offset, "freed memory should be reused");
}

TEST_F(FreeAndReuse, FreeBigSmallReuseBigSmall)
{
	// Free big memory then small memory.

	arena_free(tau->a, tau->p2);
	REQUIRE_FALSE(
		BLOCKS_ISEMPTY(tau->a->blocks),
		"Free list should have one block after first free"
	);
	arena_free(tau->a, tau->p1);

	const size_t offset = tau->a->offset;
	// Allocate big memory then small.

	arena_alloc(tau->a, tau->s2, 4);
	CHECK_EQ(tau->a->offset, offset, "freed memory should be reused");
	arena_alloc(tau->a, tau->s1, 4);
	CHECK_EQ(tau->a->offset, offset, "freed memory should be reused");
}

TEST_F(FreeAndReuse, FreeSmallBigReuseBigSmall)
{
	// Free small memory then big memory.

	arena_free(tau->a, tau->p1);
	REQUIRE_FALSE(
		BLOCKS_ISEMPTY(tau->a->blocks),
		"Free list should have one block after first free"
	);
	arena_free(tau->a, tau->p2);

	const size_t offset = tau->a->offset;
	// Allocate big memory then small.

	arena_alloc(tau->a, tau->s2, 4);
	CHECK_EQ(tau->a->offset, offset, "freed memory should be reused");
	arena_alloc(tau->a, tau->s1, 4);
	CHECK_EQ(tau->a->offset, offset, "freed memory should be reused");
}

TEST_F(FreeAndReuse, FreeSmallBigReuseSmallBig)
{
	// Free small memory then big memory.

	arena_free(tau->a, tau->p1);
	REQUIRE_FALSE(
		BLOCKS_ISEMPTY(tau->a->blocks),
		"Free list should have one block after first free"
	);
	arena_free(tau->a, tau->p2);

	const size_t offset = tau->a->offset;
	// Allocate big memory then small.

	arena_alloc(tau->a, tau->s1, 4);
	CHECK_EQ(tau->a->offset, offset, "freed memory should be reused");
	arena_alloc(tau->a, tau->s2, 4);
	CHECK_EQ(tau->a->offset, offset, "freed memory should be reused");
}

// --- Test Suite for Arena Reset ---

struct ArenaReset
{
	Arena *restrict a;
};

TEST_F_SETUP(ArenaReset)
{
	tau->a = arena_new(1024);
	REQUIRE_NOT_NULL(tau->a);
}

TEST_F_TEARDOWN(ArenaReset) { tau->a = arena_delete(tau->a); }

TEST_F(ArenaReset, Reset)
{
	// Allocate, free, and allocate again to populate offset and blocks
	void *p1 = arena_alloc(tau->a, 64, 64);
	REQUIRE_NOT_NULL(p1);
	arena_free(tau->a, p1);                    // p1 is on free list
	void *p2 = arena_alloc(tau->a, 128, 128);  // p2 is bumped
	REQUIRE_NOT_NULL(p2);

	// Check that offset is non-zero and blocks is non-null
	CHECK_NE(tau->a->offset, 0, "Offset should be non-zero before reset");
	CHECK_FALSE(
		BLOCKS_ISEMPTY(tau->a->blocks),
		"Free list should be non-null before reset"
	);

	// Reset the arena
	arena_reset(tau->a);

	// Check that offset and blocks are cleared
	CHECK_EQ(tau->a->offset, 0, "Offset should be 0 after reset");
	CHECK_TRUE(
		BLOCKS_ISEMPTY(tau->a->blocks), "Free list should be NULL after reset"
	);

	// Allocate again
	void *p3 = arena_alloc(tau->a, 64, 64);
	REQUIRE_NOT_NULL(p3);
	// The new pointer should have the same address as the *first* allocation
	CHECK_EQ(p3, p1, "First allocation after reset should match p1 address");

	// Allocate again
	void *p4 = arena_alloc(tau->a, 128, 128);
	REQUIRE_NOT_NULL(p4);
	// The new pointer should have the same address as the *second* allocation
	CHECK_EQ(p4, p2, "Second allocation after reset should match p2 address");
}

TEST(ArenaReset, ResetNull)
{
	// Resetting a NULL arena should be a safe no-op
	arena_reset(NULL);
}
