/* Copyright (C) 2025 Salvatore Bertino */
/**
 * @file arena.c
 * @brief A simple arena allocator with dynamic growth.
 */

#include "arena.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

Arena* arena_init(size_t initial_capacity) {
    if (initial_capacity == 0) {
        initial_capacity = DEFAULT_ARENA_CAPACITY;
    }

    Arena* arena = (Arena*)malloc(sizeof(Arena) + initial_capacity);
    if (!arena) {
        fprintf(stderr, "FATAL: Cannot allocate initial arena of %zu bytes\n", 
                initial_capacity);
        exit(EXIT_FAILURE);
    }

    arena->used = 0;
    arena->capacity = initial_capacity;

    return arena;
}

void* arena_alloc(Arena** arena_ptr, size_t size) {
    Arena* arena = *arena_ptr;

    size_t aligned_size = (size + 7) & ~7;

    if (arena->used + aligned_size > arena->capacity) {
        size_t needed_capacity = arena->used + aligned_size;
        size_t grown_capacity = arena->capacity * GROWTH_FACTOR;
        size_t new_capacity = (grown_capacity > needed_capacity) 
                             ? grown_capacity 
                             : needed_capacity;

        Arena* new_arena = (Arena*)realloc(arena, sizeof(Arena) + new_capacity);
        if (!new_arena) {
            fprintf(stderr, "FATAL: Arena reallocation failed\n");
            fprintf(stderr, "       Tried to grow from %zu to %zu bytes\n", 
                    arena->capacity, new_capacity);
            fprintf(stderr, "       Already used: %zu bytes\n", arena->used);
            fprintf(stderr, "       Requested: %zu bytes\n", size);
            exit(EXIT_FAILURE);
        }

        *arena_ptr = new_arena;
        arena = new_arena;
        arena->capacity = new_capacity;

        #ifdef ARENA_DEBUG
        fprintf(stderr, "[ARENA] Grew to %zu bytes (was %zu)\n", 
                new_capacity, arena->capacity / GROWTH_FACTOR);
        #endif
    }

    void* ptr = arena->buffer + arena->used;
    arena->used += aligned_size;
    return ptr;
}

void arena_reset(Arena* arena) {
    arena->used = 0;
}

void arena_destroy(Arena* arena) {
    if (arena) free(arena);
}

char* arena_strdup(Arena** arena_ptr, const char* s) {
    if (!s) return NULL;

    size_t len = strlen(s) + 1;
    char* new_str = (char*)arena_alloc(arena_ptr, len);
    memcpy(new_str, s, len);
    return new_str;
}

void arena_print_stats(const Arena* arena, const char* name) {
    printf("[%s Arena] Used: %zu / %zu bytes (%.1f%% full)\n",
           name,
           arena->used,
           arena->capacity,
           100.0 * (double)arena->used / (double)arena->capacity);
}
