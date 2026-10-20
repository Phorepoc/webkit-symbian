// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause

// phsym.cpp -- The Symbian-API shim of the JSC port: compiled on the REAL Symbian include
// line (everything else of the engine is on the pure Open C line, where e32 headers must not appear), called through
// wtf/symbian/PhSymbian.h. All euser: UserHal (RAM size), RThread::StackInfo (stack bounds), User::AllocSize (heap footprint).
#include <e32std.h>
#include <e32hal.h>
#include <e32math.h>
#include <e32svr.h>
#include <u32hal.h>
#include <stddef.h>

extern "C" {

size_t ph_total_ram_bytes(void)
	{
	TMemoryInfoV1Buf buf;
	if (UserHal::MemoryInfo(buf) != KErrNone)
		return 128u << 20;
	return (size_t)buf().iTotalRamInBytes;
	}

size_t ph_free_ram_bytes(void)
	{
	TMemoryInfoV1Buf buf;
	if (UserHal::MemoryInfo(buf) != KErrNone)
		return 64u << 20;
	return (size_t)buf().iFreeRamInBytes;
	}

size_t ph_heap_used_bytes(void)
	{
	TInt total = 0;
	User::AllocSize(total);
	return (size_t)total;
	}

// Clocks: Open C's clock_gettime(CLOCK_REALTIME) ticks in whole seconds here, and WTF's generic MonotonicTime::now
// is built on it -- timers, performance.now(), Date.now() all moved in 1 s steps (a setTimeout(1000) fired after 0-2 s;
// WPT relevant-mutations' 1 s window flaked). The nanokernel tick instead: User::NTickCount() x its period, extended to
// 64 bits across the 32-bit wrap. The wrap bookkeeping is under a lock (several WebKit threads read the clock).
double ph_monotonic_seconds(void)
	{
	static TInt sPeriodUs = 0;		// the nanokernel tick period
	if (!sPeriodUs)
		{
		TInt p = 0;
		if (UserSvr::HalFunction(EHalGroupKernel, EKernelHalNTickPeriod, &p, 0) != KErrNone || p <= 0)
			p = 1000;
		sPeriodUs = p;
		}
	static TUint32 sLast = 0;
	static TUint64 sHigh = 0;
	static TInt sLock = 0;
	while (User::LockedInc(sLock) != 0)		// euser's atomics: an ARMv5TE device compile has no LDREX (__atomic_* = a libatomic call)
		User::LockedDec(sLock);
	TUint32 now = User::NTickCount();
	if (now < sLast)
		sHigh += (TUint64)1 << 32;
	sLast = now;
	TUint64 ticks = sHigh | now;
	User::LockedDec(sLock);
	return (double)ticks * sPeriodUs / 1e6;
	}

double ph_wall_seconds(void)
	{
	static double sBase = -1;		// wall time at the first call, minus the monotonic time then
	if (sBase < 0)
		{
		TTime now;
		now.UniversalTime();
		const TTime epoch(MAKE_TINT64(0x00dcddb3, 0x0f2f8000));	// 1970-01-01 in TTime microseconds
		sBase = (double)(now.Int64() - epoch.Int64()) / 1e6 - ph_monotonic_seconds();
		}
	return sBase + ph_monotonic_seconds();
	}

// JIT memory. A process's heap and data chunks are not
// executable; a LOCAL CODE chunk (RChunk::CreateLocalCode) is user-writable AND executable -- the shape EKA2 gives run-time code
// generators. Committed whole up front (JSC reserves one fixed pool). Up to 8 live chunks; release finds its chunk by base.
static RChunk sCodeChunks[8];
static void* sCodeBases[8];

void* ph_code_reserve(size_t aBytes)
	{
	for (TInt i = 0; i < 8; i++)
		{
		if (sCodeBases[i])
			continue;
		const TInt r = sCodeChunks[i].CreateLocalCode((TInt)aBytes, (TInt)aBytes);
		RDebug::Printf("PH: js code chunk %u bytes -> %d", (TUint)aBytes, r);		// the JIT pool (a failure = JSC runs without JIT)
		if (r != KErrNone)
			return NULL;
		sCodeBases[i] = sCodeChunks[i].Base();
		return sCodeBases[i];
		}
	return NULL;
	}

int ph_code_release(void* aBase)
	{
	for (TInt i = 0; i < 8; i++)
		if (sCodeBases[i] && sCodeBases[i] == aBase)
			{
			sCodeChunks[i].Close();
			sCodeBases[i] = NULL;
			return 1;
			}
	return 0;
	}

// Instruction-memory barrier over freshly written code: clean the D-cache, invalidate the I-cache + branch predictor (the
// ARM kernel's IMB_Range exec call) -- what JSC's cacheFlush does with the cacheflush syscall on Linux.
void ph_cache_flush(void* aStart, size_t aBytes)
	{
	User::IMB_Range(aStart, (TUint8*)aStart + aBytes);
	}

int ph_current_stack_bounds(void** aOrigin, void** aBound)
	{
	TThreadStackInfo si;
	if (RThread().StackInfo(si) != KErrNone)
		return -1;
	*aOrigin = (void*)si.iBase;      // highest address
	*aBound = (void*)si.iLimit;      // lowest address
	return 0;
	}

}

extern "C" unsigned ph_random32(void)
	{
	return (unsigned)Math::Random();
	}

// ICU's uprv_timezone (cxxrt/icu_patch.py): Open C's localtime() returns NULL; the system UTC offset in seconds
extern "C" int ph_utc_offset_seconds(void)
	{
	return User::UTCOffset().Int();
	}

// bulk bytes from the kernel's cryptographically strong RNG (us_exec.cpp: SHA-256 based, 256-bit strength) -- WTF's
// RandomDevice (Open C has no /dev/urandom: open "succeeds", every read fails -> crashUnableToReadFromURandom)
extern "C" void ph_random_bytes(void* aBuf, unsigned aLen)
	{
	TUint8* p = static_cast<TUint8*>(aBuf);
	while (aLen)
		{
		const TInt n = aLen > 0x4000 ? 0x4000 : (TInt)aLen;
		TPtr8 d(p, n, n);
		Math::Random(d);
		p += n;
		aLen -= n;
		}
	}

// Output: a ROM-started exe (bootui) has no console, so Open C's stdout/stderr go nowhere. Redirect both to C:\jsout.txt
// (unbuffered; the test harness reads it off the disk image) -- PH_JSOUT build only; harmless without a C: drive (freopen fails).
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <e32debug.h>
static void ph_atexit() { RDebug::Printf("[js] atexit"); }
// stdout+stderr -> one file on the first writable drive (D: = the RAM drive, then C:); exit() replays it to the serial log
// as "[js] | <line>" -- the test boots (QEMU, the phone) read the log, not a disk image (no C: in the bootui ROM)
static const char* gPhOut = 0;
static void ph_redirect_stdio() __attribute__((constructor(101)));
static void ph_redirect_stdio()
	{
	TBuf<256> cl; User::CommandLine(cl); RDebug::Printf("[js] ctor, cmdline=%S", &cl);
	atexit(ph_atexit);
	static const char* const KOut[] = { "d:/jsout.txt", "c:/jsout.txt" };
	for (unsigned i = 0; i < sizeof(KOut) / sizeof(KOut[0]) && !gPhOut; i++)
		if (freopen(KOut[i], "w", stdout))
			{
			gPhOut = KOut[i];
			setvbuf(stdout, 0, _IONBF, 0);
			if (freopen(KOut[i], "a", stderr))
				setvbuf(stderr, 0, _IONBF, 0);
			}
	RDebug::Printf("[js] stdout -> %s", gPhOut ? gPhOut : "(nowhere: no writable drive)");
	}

// PH_HEAPCHECK: walk the whole heap (RHeap::Check panics on a damaged cell) -- phwebtest calls it after every WPT test to
// find the first test that corrupts memory (the WPT list's FILE buffer turned to garbage after ~90 tests)
extern "C" void ph_heap_check(const char* aWhen)
	{
	User::Heap().Check();
	TInt biggest = 0, allocSize = 0, cells = User::AllocSize(allocSize); (void)User::Heap().Available(biggest);
	RDebug::Printf("[js] heap ok after %s: %d cells %d bytes, size %d", aWhen, cells, allocSize, User::Heap().Size());
	}

// one line straight to the serial log (stdout reaches it only at exit/abort; a panic loses it): phwebtest's WPT test markers
// RDebug truncates near 256 bytes: longer lines go out in 200-byte pieces, each after the first marked "[js] >+ " (phweb_iter.sh
// joins them). No file I/O -- the per-test stdout replay (fopen/fseek of jsout.txt ~200 times) went with corrupt Open C file
// state: the WPT list FILE read back garbage, a font pread faulted in CFileDesc::LSeek.
extern "C" void ph_serial(const char* aLine)
	{
	TInt len = strlen(aLine);
	TInt off = 0;
	char piece[201];
	do	{
		TInt n = len - off > 200 ? 200 : len - off;
		memcpy(piece, aLine + off, n);
		piece[n] = 0;
		RDebug::Printf(off ? "[js] >+ %s" : "[js] > %s", piece);
		off += n;
		} while (off < len);
	}

extern "C" void ph_serialf(const char* aFmt, ...)
	{
	static char buf[2048];
	va_list ap;
	va_start(ap, aFmt);
	vsnprintf(buf, sizeof(buf), aFmt, ap);
	va_end(ap);
	ph_serial(buf);
	}

extern "C" void ph_replay_stdout()   // also ph_throw.cpp (a C++ throw ends the process)
	{
	if (!gPhOut)
		return;
	fflush(stdout);
	fflush(stderr);
	FILE* f = fopen(gPhOut, "r");
	if (!f)
		return;
	char line[240];
	int n = 0;
	while (fgets(line, sizeof(line), f) && n++ < 200000)
		{
		char* nl = strchr(line, '\n');
		if (nl)
			*nl = 0;
		RDebug::Printf("[js] | %s", line);
		}
	fclose(f);
	}

// Diagnostics (serial log): who ends the process. exit()/abort() would otherwise vanish into User::Exit without a trace.
extern "C" void exit(int aCode) __attribute__((noreturn));
extern "C" void exit(int aCode)
	{
	ph_replay_stdout();
	RDebug::Printf("[js] exit(%d)", aCode);
	User::Exit(aCode);
	}
// the code addresses worth printing from a stack: +-aSpan around this image's code, clamped at 0. At run time: as a
// constant, &abort - span folded into a relocation that wraps below 0 (bfd truncated it silently, lld rejects it)
static void ph_code_range(TUint aSpan, TUint& aLo, TUint& aHi)
	{
	volatile TUint base = (TUint)&ph_code_range;
	aLo = base > aSpan ? base - aSpan : 0;
	aHi = base + aSpan;
	}
extern "C" void abort(void) __attribute__((noreturn));
extern "C" void abort(void)
	{
	ph_replay_stdout();   // std::terminate's "terminate called after throwing ..." line went to stderr
	RDebug::Printf("[js] abort() ra=%x", (unsigned)(TUint)__builtin_return_address(0));
	// which limit ran out (JSC MemoryExhaustion): this heap's committed size + max vs the system's free RAM
	{
	TInt biggest = 0, allocSize = 0, cells = User::AllocSize(allocSize); (void)User::Heap().Available(biggest);
	TMemoryInfoV1Buf mi; UserHal::MemoryInfo(mi);
	RDebug::Printf("[js] heap: %d cells %d bytes, size %d, max %d; free RAM %d of %d", cells, allocSize, User::Heap().Size(),
		User::Heap().MaxLength(), mi().iFreeRamInBytes, mi().iTotalRamInBytes);
	}
	TUint* sp = (TUint*)__builtin_frame_address(0); TUint lo, hi; int n = 0; ph_code_range(0x1600000, lo, hi);
	// only up to the top of this thread's stack (the unbounded scan faulted past it: KERN-EXEC 3 hid the abort)
	TThreadStackInfo info; TUint top = (TUint)sp + 4096 * 4;
	if (RThread().StackInfo(info) == KErrNone && info.iBase > (TLinAddr)sp) top = info.iBase;
	for (int i = 0; (TUint)&sp[i] < top && n < 40; i++) if (sp[i] >= lo && sp[i] < hi) { RDebug::Printf("[js] st %d: %x", i, sp[i]); n++; }
	User::Exit(134);
	}
// libgcc_eh's unwinder asks glibc's _dl_find_object for the pc's frame tables (an EUSRNIMP stub earlier: a C++ throw on
// a phwebtest worker thread). "Not found" (-1) is the answer for an E32 image; the first call also names the throw site --
// the code addresses on the stack, for symbolication.
extern "C" int _dl_find_object(void* aPc, void*)
	{
	static TBool dumped = EFalse;
	if (!dumped)
		{
		dumped = ETrue;
		RDebug::Printf("[js] _dl_find_object pc=%x", (unsigned)(TUint)aPc);
		TUint* sp = (TUint*)__builtin_frame_address(0); TUint lo, hi; int n = 0; ph_code_range(0x5000000, lo, hi);
		// only up to the top of THIS thread's stack (a worker's is small: the unbounded scan faulted, KERN-EXEC 3)
		TThreadStackInfo info; TUint top = (TUint)sp + 4096 * 4;
		if (RThread().StackInfo(info) == KErrNone && info.iBase > (TLinAddr)sp) top = info.iBase;
		for (int i = 0; (TUint)&sp[i] < top && n < 60; i++) if (sp[i] >= lo && sp[i] < hi) { RDebug::Printf("[js] st %d: %x", i, sp[i]); n++; }
		}
	return -1;
	}
// libgcc's gthr "is libpthread linked" probe (a weak glibc symbol, another link stub)
extern "C" int pthread_key_create(unsigned*, void (*)(void*));
extern "C" int __pthread_key_create(unsigned* aKey, void (*aDtor)(void*)) { return pthread_key_create(aKey, aDtor); }
