#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# build_jsc.sh [ninja target] -- JavaScriptCore alone (WebKit JSCOnly, static, system malloc; CLoop on x32, LLInt + Baseline JIT on ARM) over Open C +
# libstdc++ + ICU, on the "pure Open C" line (the pdk's mk_pure_wrappers.sh). Inside the toolchain container:  ARCH=x32|arm bash recipes/build_jsc.sh [target]
# (default target WTF; `jsc` is the shell, `JavaScriptCore` the library). Prereqs: the cxxrt repository's build.sh, build_icu.sh.
# Patches: apply_patches.sh.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
SRC=$WK_SRC; BLD=$V/build-jsc2
bash "$HERE/fetch_sources.sh" webkit >/dev/null || exit 1
[ -d $SRC ] || tar -C $V -xf "$(tarball webkit)" || exit 1
pure_wrappers || exit 1
bash $HERE/apply_patches.sh $SRC $W || exit 1
cat > $TC <<TC
set(CMAKE_SYSTEM_NAME Linux)   # CMake needs UNIX=true; the compiler line undefines __linux__ so WTF sees OS(UNIX)
set(CMAKE_SYSTEM_PROCESSOR $PROC)
set(CMAKE_C_COMPILER $W/bin/pp-gcc)
set(CMAKE_CXX_COMPILER $W/bin/pp-g++)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(PHOREPOC 1)
set(CMAKE_HAVE_LIBC_PTHREAD 1 CACHE BOOL "")
set(CMAKE_USE_PTHREADS_INIT 1 CACHE BOOL "")
set(CMAKE_THREAD_LIBS_INIT "" CACHE STRING "")
# Open C is not Linux: undefine the compiler's __linux__ so WTF picks OS(UNIX) (no /proc, sysinfo, ucontext, futex...)
set(CMAKE_C_FLAGS_INIT "$OSDEF -U__linux__ -U__linux -U__gnu_linux__ -Ulinux -DPHOREPOC=1")
set(CMAKE_CXX_FLAGS_INIT "$OSDEF -U__linux__ -U__linux -U__gnu_linux__ -Ulinux -DPHOREPOC=1 $CXXINC -fexceptions -frtti -fpermissive")
TC
# ARM: the LLInt + Baseline JIT for ARMv7 Thumb-2 (DFG/FTL later); x32 has no JIT backend upstream -> CLoop
if [ $ARCH = arm ]; then JITFL="-DENABLE_JIT=ON -DENABLE_C_LOOP=OFF"; else JITFL="-DENABLE_JIT=OFF -DENABLE_C_LOOP=ON"; fi
if [ ! -f $BLD/build.ninja ]; then
  mkdir -p $BLD; cd $BLD
  cmake -G Ninja $SRC -DPORT=JSCOnly -DCMAKE_TOOLCHAIN_FILE=$TC -DCMAKE_BUILD_TYPE=Release \
    $JITFL -DENABLE_DFG_JIT=OFF -DENABLE_FTL_JIT=OFF -DENABLE_WEBASSEMBLY=OFF -DENABLE_SAMPLING_PROFILER=OFF \
    -DENABLE_STATIC_JSC=ON -DUSE_SYSTEM_MALLOC=ON -DUSE_MIMALLOC=OFF -DDEVELOPER_MODE=OFF -DENABLE_API_TESTS=OFF -DUSE_LIBBACKTRACE=OFF \
    -DICU_INCLUDE_DIR=$ICU/include -DICU_DATA_LIBRARY_RELEASE=$ICU/lib/libicudata.a -DICU_I18N_LIBRARY_RELEASE=$ICU/lib/libicui18n.a -DICU_UC_LIBRARY_RELEASE=$ICU/lib/libicuuc.a \
    > $LOG/jsc-configure.log 2>&1; echo "configure exit $?"; tail -25 $LOG/jsc-configure.log
fi
# build-tree executables (LLIntSettingsExtractor, jsc) are only READ (offsets) or re-linked by the E32 emitter: link freely, unresolved = ignored
cmake -S $SRC -B $BLD -DCMAKE_EXE_LINKER_FLAGS="-nostdlib -nostartfiles -static -Wl,--unresolved-symbols=ignore-all -Wl,--allow-multiple-definition -Wl,-e,main -Wl,--defsym,__dso_handle=0" > /dev/null 2>&1
[ -n "${CONFIGURE_ONLY:-}" ] && exit 0
cd $BLD && ninja -k 0 -j${JOBS:-4} ${1:-WTF} > $LOG/jsc-build-${1:-WTF}.log 2>&1; echo "ninja exit $?"
grep -c "error:" $LOG/jsc-build-${1:-WTF}.log
