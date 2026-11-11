#pragma once
#include <stddef.h>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_SPIRAM 2
static inline size_t heap_caps_get_free_size(int c) { (void)c; return 0; }
static inline size_t heap_caps_get_largest_free_block(int c) { (void)c; return 0; }
