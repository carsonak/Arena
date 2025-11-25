#ifndef COMMON_CALLBACK_TYPES_H
#define COMMON_CALLBACK_TYPES_H

#include <stddef.h> /* size_t */

/*!
 * @brief return a pointer to a new memory region of `size` bytes.
 *
 * @param context additional context needed by the function.
 * @param size number of bytes to allocate.
 * @returns pointer to the new memory region, NULL on error.
 */
typedef void *mem_alloc(void *context, size_t size);

/*!
 * @brief function that can "free" memory.
 *
 * @param context additional context for the function.
 * @param ptr pointer to the memory region to be freed.
 */
typedef void mem_free(void *restrict context, void *restrict ptr);

#endif /* COMMON_CALLBACK_TYPES_H */
