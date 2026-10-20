#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# apply_patches.sh <webkit-src> <W> -- idempotent edits of the (pristine, BSD/LGPL) WebKit 2.54.1 tree for Phorepoc, plus our new files
# (webkit/symbian/*) copied into Source/WTF/wtf/symbian/. Every edit is a marker-guarded sed/append so a re-run is a no-op.
# A re-run must not TOUCH an unchanged file either: `sed -i` rewrites it (new mtime) and BPlatform.h is included by nearly every
# JSC file -> ninja rebuilt all of JavaScriptCore on every run. Every edit goes through upd():
# applied to a temporary copy, the original replaced only when the bytes differ.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
SRC=$1; W=$2; D=$WKS/os/src/webkit
# upd <file> <command reading stdin, writing stdout>
upd() { local f=$1; shift; local t; t=$(mktemp); "$@" < "$f" > "$t" && { cmp -s "$t" "$f" || cat "$t" > "$f"; }; rm -f "$t"; }
mkdir -p $SRC/Source/WTF/wtf/symbian; for f in $D/symbian/*; do cmp -s $f $SRC/Source/WTF/wtf/symbian/$(basename $f) || cp $f $SRC/Source/WTF/wtf/symbian/; done   # (copy only changed files: a fresh mtime rebuilds all of JSC)
# 1. bmalloc: x32 (ILP32 x86-64) must not get the 16 GB Gigacage (upstream bug: no __ILP32__ exclusion like ARM64's)
upd $SRC/Source/bmalloc/bmalloc/BPlatform.h sed 's/(BCPU(X86_64) || (BCPU(ARM64)/((BCPU(X86_64) \&\& !defined(__ILP32__)) || (BCPU(ARM64)/'
# 2. libstdc++'s configure said "no" to C99 math (link tests are disabled by design); Open C's libm + libphcompat provide it
CC_H=$W/build/include/$LTRIP/bits/c++config.h
for m in _GLIBCXX_USE_C99_MATH _GLIBCXX_USE_C99_MATH_TR1 _GLIBCXX_USE_C99_STDLIB _GLIBCXX_USE_C99_INTTYPES _GLIBCXX_USE_C99_STDINT_TR1 _GLIBCXX_USE_C99_FENV_TR1 _GLIBCXX_USE_C99_CTYPE_TR1; do
  upd $CC_H sed "s|^/\* #undef $m \*/|#define $m 1|"   # (c++config.h is in every C++ TU: never rewrite it unchanged)
done
# 2a. ARM (the PinePhone): JSCOnly turns on the Capstone disassembler for ARM (JIT dumps only); the WPE tarball has no
#     Source/ThirdParty/capstone, and we need no dumps
upd $SRC/Source/cmake/OptionsJSCOnly.cmake sed 's/^if (WTF_CPU_ARM OR WTF_CPU_MIPS)$/if ((WTF_CPU_ARM OR WTF_CPU_MIPS) AND NOT PHOREPOC)/'
# 2b. keep the generic (non-Linux) memory-footprint/pressure sources: CMake says Linux, the target is not
P=$SRC/Source/WTF/wtf/PlatformJSCOnly.cmake
upd $P sed 's/^elseif (CMAKE_SYSTEM_NAME MATCHES "Linux")$/elseif (CMAKE_SYSTEM_NAME MATCHES "Linux" AND NOT PHOREPOC)/'
# 3. WTF sources: our Symbian seams replace the POSIX ones (PHOREPOC is set by the toolchain file)
seams() {
  sed '/^# ---- Phorepoc (Symbian/,$d'
  cat <<'EOT'
# ---- Phorepoc (Symbian/EKA2 over Open C): our seams in wtf/symbian/ replace the POSIX OSAllocator 
if (PHOREPOC)
    list(REMOVE_ITEM WTF_SOURCES posix/OSAllocatorPOSIX.cpp)
    list(REMOVE_ITEM WTF_SOURCES generic/MemoryFootprintGeneric.cpp)
    list(APPEND WTF_SOURCES symbian/OSAllocatorSymbian.cpp symbian/MemoryFootprintSymbian.cpp)
    list(APPEND WTF_PUBLIC_HEADERS symbian/PhSymbian.h)
endif ()
EOT
}
upd $P seams
# 4. the Symbian port : our cmake files, the curl backend the release tarball lacks (from the sparse git
# checkout of the same tag, fetch_sources.sh), our mbedTLS/Symbian sources next to it. All copy-on-change.
cpc() { cmp -s "$1" "$2" || { mkdir -p "$(dirname "$2")"; cp "$1" "$2"; }; }
cpc $D/port/OptionsSymbian.cmake $SRC/Source/cmake/OptionsSymbian.cmake
cpc $D/port/Platform-WTF.cmake $SRC/Source/WTF/wtf/PlatformSymbian.cmake
cpc $D/port/Platform-JavaScriptCore.cmake $SRC/Source/JavaScriptCore/PlatformSymbian.cmake
cpc $D/port/Platform-bmalloc.cmake $SRC/Source/bmalloc/PlatformSymbian.cmake
cpc $D/port/Platform-PAL.cmake $SRC/Source/WebCore/PAL/pal/PlatformSymbian.cmake
cpc $D/port/Platform-WebCore.cmake $SRC/Source/WebCore/PlatformSymbian.cmake
cpc $D/port/Platform-Source.cmake $SRC/Source/PlatformSymbian.cmake
for f in $D/platform-symbian/*; do cpc $f $SRC/Source/WebCore/platform/symbian/$(basename $f); done   # our platform hooks
for f in $D/phwebview/*; do cpc $f $SRC/Source/phwebview/$(basename $f); done   # our embedding (phwebtest first)
CURL_GIT=${CURL_GIT:-$DL/webkit-git/Source/WebCore/platform/network/curl}; NC=$SRC/Source/WebCore/platform/network/curl
[ -d $CURL_GIT ] || { echo "no $CURL_GIT (recipes/fetch_sources.sh fetches the curl backend of the pinned WebKit tag)"; exit 1; }
# pristine copies: a file patch.py edits is copied only while absent (re-copying would undo the edit -> a fresh mtime every run)
for f in $CURL_GIT/*; do [ -f $NC/$(basename $f) ] || cpc $f $NC/$(basename $f); done
for f in OpenSSLHelperMbedTLS.cpp CurlSSLVerifierMbedTLS.cpp CertificateInfoCurlMbedTLS.cpp CurlSSLHandleSymbian.cpp ResourceHandleSymbian.cpp; do cpc $D/webcore/$f $NC/$f; done
cpc $D/webcore/CryptoDigestMbedTLS.cpp $SRC/Source/WebCore/PAL/pal/crypto/mbedtls/CryptoDigestMbedTLS.cpp
python3 $HERE/patch.py $SRC || exit 1
exit 0
