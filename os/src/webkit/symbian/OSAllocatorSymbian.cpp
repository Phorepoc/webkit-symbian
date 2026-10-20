// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause

/*
 * OSAllocatorSymbian.cpp -- WTF:OSAllocator for Symbian/EKA2 over Open C.
 *
 * Open C's mmap is file-only (no MAP_ANON), and a Symbian process's address space is its RHeap (an RChunk the kernel commits on demand):
 * "reserve" is therefore just a zeroed block from that heap, "commit"/"decommit"/"protect" are no-ops (the heap pages are committed
 * and RWX-agnostic for data), and "release" is free(). EXECUTABLE requests (JSC's JIT pool, ARM) are a Symbian local code chunk instead
 * (phsym.cpp ph_code_reserve): heap pages are never executable. A later, RChunk-direct implementation only matters if
 * decommit must return pages to the system. Guard pages are not added (the callers pass the count back to release, which we ignore).
 *
 * Interface and the semantics documented in wtf/OSAllocator.h are from WebKit (BSD-2-Clause, (C) Apple Inc.); this file is new code.
 */
#include "config.h"
#include <wtf/OSAllocator.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <wtf/Assertions.h>
#include <wtf/symbian/PhSymbian.h>

namespace WTF {

void* OSAllocator::tryReserveUncommitted(size_t bytes, Usage, void*, bool, bool executable, bool, unsigned)
{
    if (executable)
        return ph_code_reserve(bytes);
    return calloc(1, bytes);
}

void* OSAllocator::reserveUncommitted(size_t bytes, Usage usage, void* address, bool writable, bool executable, bool jitCageEnabled, unsigned guards)
{
    void* result = tryReserveUncommitted(bytes, usage, address, writable, executable, jitCageEnabled, guards);
    RELEASE_ASSERT_WITH_MESSAGE(result, "OSAllocator: out of memory (%zu bytes)", bytes);
    return result;
}

void* OSAllocator::tryReserveUncommittedAligned(size_t bytes, size_t alignment, Usage, void*, bool, bool executable, bool, unsigned)
{
    if (executable)
        return ph_code_reserve(bytes);   // a chunk base is page (4 KB) aligned; JSC's JIT pool asks for no more
    // The slack in front is never given back ("cannot be released", see OSAllocator.h).
    char* raw = static_cast<char*>(calloc(1, bytes + alignment));
    if (!raw)
        return nullptr;
    uintptr_t p = (reinterpret_cast<uintptr_t>(raw) + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
    return reinterpret_cast<void*>(p);
}

void* OSAllocator::tryReserveAndCommit(size_t bytes, Usage usage, void* address, bool writable, bool executable, bool jitCageEnabled, unsigned guards)
{
    return tryReserveUncommitted(bytes, usage, address, writable, executable, jitCageEnabled, guards);
}

void* OSAllocator::reserveAndCommit(size_t bytes, Usage usage, void* address, bool writable, bool executable, bool jitCageEnabled, unsigned guards)
{
    return reserveUncommitted(bytes, usage, address, writable, executable, jitCageEnabled, guards);
}

void* OSAllocator::tryReserveAndCommitImpl(size_t bytes, Usage usage, void* address, bool writable, bool executable, bool jitCageEnabled, unsigned guards, bool)
{
    return tryReserveUncommitted(bytes, usage, address, writable, executable, jitCageEnabled, guards);
}

void OSAllocator::commit(void*, size_t, bool, bool) { }
void OSAllocator::decommit(void*, size_t) { }
void OSAllocator::hintMemoryNotNeededSoon(void*, size_t) { }
void OSAllocator::protect(void*, size_t, bool, bool) { }
bool OSAllocator::tryProtect(void*, size_t, bool, bool) { return true; }

void OSAllocator::releaseDecommitted(void* base, size_t, unsigned)
{
    if (ph_code_release(base))
        return;
    free(base);
}

void OSAllocator::zeroFill(void* base, size_t bytes)
{
    memset(base, 0, bytes);
}

} // namespace WTF
