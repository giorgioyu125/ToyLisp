/* Copyright (C) 2025 Salvatore Bertino */
/*
 * @file arena.h
 */

#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

/**
 * @defgroup arena Arena Allocator
 * @brief Fast region-based memory allocation with automatic growth.
 *
 * The arena allocator provides a simple and efficient way to manage memory
 * for objects with similar lifetimes. It allocates from a contiguous buffer
 * and can reset all allocations at once without individual frees.
 * 
 * **Key features:**
 * - Amortized O(1) allocation through exponential growth
 * - O(1) bulk deallocation via `arena_reset()`
 * - Sequential memory layout (cache-friendly)
 * - No per-allocation overhead
 * - No fragmentation
 *
 * **Typical usage pattern:**
 * @code
 * Arena* temp_arena = arena_init(4096);  // Start with 4KB
 * 
 * // Allocate many objects...
 * char* str = arena_strdup(&temp_arena, "hello");
 * int* array = arena_alloc(&temp_arena, 100 * sizeof(int));
 * 
 * // Free everything at once
 * arena_reset(temp_arena);
 * 
 * // Cleanup
 * arena_destroy(temp_arena);
 * @endcode
 *
 * @warning After `arena_reset()`, all pointers previously returned by
 *          `arena_alloc()` become invalid. Do not use them!
 *
 * @warning After `arena_destroy()`, the arena pointer itself is invalidated.
 *
 * @warning Since the arena uses realloc for growth, you must pass the arena
 *          by pointer-to-pointer to `arena_alloc()` and `arena_strdup()`, as
 *          the arena's address may change during reallocation.
 *
 * @{
 */

/** @brief Factor by which the arena grows when it runs out of space. */
#define GROWTH_FACTOR 2

/** @brief Default initial capacity if 0 is passed to arena_init(). */
#define DEFAULT_ARENA_CAPACITY 1024

/**
 * @struct Arena
 * @brief Represents a memory arena for fast allocation and bulk deallocation.
 * 
 * This structure uses a flexible array member (FAM) for the buffer, which means
 * the arena and its buffer are allocated as a single contiguous block. This
 * improves cache locality and reduces allocation overhead.
 * 
 * @warning The arena itself must be heap-allocated (via `arena_init()`). You
 *          cannot declare an Arena on the stack.
 * 
 * @warning When the arena grows via `realloc()`, the address of the Arena
 *          struct itself may change. Always use the returned pointer from
 *          allocation functions.
 */
typedef struct {
    size_t used;            ///< Number of bytes currently allocated from the buffer.
    size_t capacity;        ///< Total size of the buffer in bytes.
    unsigned char buffer[]; ///< Flexible array member: the actual memory buffer.
} Arena;

/**
 * @brief Initializes an arena with a specified initial capacity.
 *
 * Allocates the arena structure along with its initial buffer as a single
 * contiguous block. If `initial_capacity` is 0, a default size is used.
 * The arena will automatically grow as needed.
 *
 * @param[in] initial_capacity Initial size in bytes for the buffer. Use 0 for default (1KB).
 * @return Pointer to the newly allocated arena.
 *
 * @note The arena will grow exponentially (by GROWTH_FACTOR) when it runs
 *       out of space, so the initial size doesn't need to be perfect.
 *
 * @warning Exits the program on allocation failure.
 */
Arena* arena_init(size_t initial_capacity);


/**
 * @brief Allocates a block of memory from the arena.
 *
 * Returns a pointer to a memory block of at least `size` bytes. The returned
 * pointer is aligned to 8 bytes. If the arena doesn't have enough space, it
 * automatically grows by GROWTH_FACTOR (doubling its size).
 *
 * The growth strategy ensures amortized O(1) allocation time:
 * - Starting with capacity C, after n doublings we have capacity C*2^n
 * - Total bytes allocated across all reallocations: C + 2C + 4C + ... + C*2^n = C*(2^(n+1) - 1)
 * - This is O(final_capacity), so the amortized cost per byte is O(1)
 *
 * @param[in,out] arena_ptr Pointer to the arena pointer. The arena pointer may
 *                          be updated if reallocation occurs.
 * @param[in] size Number of bytes to allocate.
 * @return Pointer to the allocated memory (8-byte aligned) within the arena's buffer.
 *
 * @warning You must pass the address of your arena pointer (`&arena`), not the
 *          pointer itself, because realloc may move the arena in memory.
 *
 * @warning The returned pointer becomes invalid after `arena_reset()` or
 *          `arena_destroy()` is called on the arena.
 *
 * @warning Exits the program if reallocation fails.
 */
void* arena_alloc(Arena** arena_ptr, size_t size);

/**
 * @brief Resets the arena, making all its memory available for reuse.
 *
 * This is an O(1) operation that simply resets the `used` counter to 0.
 * The underlying buffer is not deallocated, so subsequent allocations
 * will reuse the same memory.
 *
 * @param[in,out] arena Pointer to the arena to reset.
 *
 * @warning After calling this function, **all pointers** previously returned
 *          by `arena_alloc()` from this arena become invalid. Using them
 *          results in undefined behavior!
 */
void arena_reset(Arena* arena);

/**
 * @brief Destroys the arena and frees all its memory.
 *
 * Frees the arena structure and its buffer. The arena pointer becomes invalid
 * and must not be used after this call.
 *
 * @param[in] arena Pointer to the arena to destroy. Can be NULL (no-op).
 *
 * @note It is safe to call this with NULL (it becomes a no-op).
 */
void arena_destroy(Arena* arena);

/**
 * @brief Duplicates a string into the arena.
 *
 * Allocates space for a copy of the string `s` (including null terminator)
 * and copies the string into it.
 *
 * @param[in,out] arena_ptr Pointer to the arena pointer. May be updated if
 *                          reallocation occurs.
 * @param[in] s The null-terminated string to duplicate. Can be NULL.
 * @return Pointer to the duplicated string, or NULL if `s` was NULL.
 *
 * @note The returned pointer has the same lifetime as other arena allocations.
 */
char* arena_strdup(Arena** arena_ptr, const char* s);

/**
 * @brief Prints statistics about the arena's memory usage.
 *
 * Useful for debugging and profiling memory usage.
 *
 * @param[in] arena Pointer to the arena to inspect.
 * @param[in] name A descriptive name for the arena (e.g., "Temp" or "Permanent").
 */
void arena_print_stats(const Arena* arena, const char* name);

/** @} */ // End of arena group

#endif
