/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: BSD-2-Clause */

/*
 * PhSymbian.h -- The C interface of the small Symbian-API shim (phsym.cpp, compiled on the real
 * Symbian/EKA2 include line) that WTF's Symbian seams call. Everything here is implemented with euser (UserHal, RThread, RHeap).
 */
#ifndef PH_SYMBIAN_H
#define PH_SYMBIAN_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
size_t ph_total_ram_bytes(void);                    /* UserHal::MemoryInfo -> iTotalRamInBytes */
size_t ph_free_ram_bytes(void);                     /* iFreeRamInBytes */
size_t ph_heap_used_bytes(void);                    /* the process heap's committed size (memory footprint) */
int    ph_current_stack_bounds(void** origin, void** bound);   /* RThread::StackInfo of the calling thread: origin = highest address */
double ph_monotonic_seconds(void);                  /* the nanokernel tick (1 ms), 64-bit extended: Open C clock_gettime ticks in whole seconds */
double ph_wall_seconds(void);                       /* Unix wall time: the first clock_gettime(REALTIME) + monotonic elapsed (ms resolution) */
void*  ph_code_reserve(size_t bytes);               /* JIT memory: a local CODE chunk (writable + executable), committed; NULL = none */
int    ph_code_release(void* base);                 /* 1 = that was a code chunk (closed), 0 = not ours */
void   ph_cache_flush(void* start, size_t bytes);   /* User::IMB_Range over JIT-written code */
#ifdef __cplusplus
}
#endif
#endif
