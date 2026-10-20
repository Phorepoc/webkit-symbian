/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: BSD-2-Clause */

/* PhAlloc.h -- the allocator entry points bmalloc's system-malloc mode is patched to use (libphcompat phmem.c). */
#ifndef PH_ALLOC_H
#define PH_ALLOC_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* ph_malloc(size_t);
void* ph_calloc(size_t, size_t);
void* ph_realloc(void*, size_t);
void* ph_memalign(size_t align, size_t size);
void  ph_free(void*);
#ifdef __cplusplus
}
#endif
#endif
