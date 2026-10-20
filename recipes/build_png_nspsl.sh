#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# build_png_nspsl.sh -- libpng (PNG decoder) and libnspsl (NetSurf's public suffix list library, the list compiled in) for the engine, on the
# pure Open C line. Inside the toolchain container, after the cxxrt repository's build.sh:  ARCH=x32|arm bash recipes/build_png_nspsl.sh
# Out: $PHOREPOC_BUILD/thirdparty/{x86,arm}/lib/{libpng,libnspsl}.a, the headers png.h pngconf.h pnglibconf.h nspsl.h in $DEPS/include.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
bash "$HERE/fetch_sources.sh" libpng libnspsl >/dev/null || exit 1
pure_wrappers || exit 1
T=$V/imglibs; mkdir -p $T $DEPS/include
PNG=$T/libpng-1.6.44; NSPSL=$T/libnspsl-0.1.7
[ -d $PNG ] || tar -C $T -xzf "$(tarball libpng)" || exit 1
[ -d $NSPSL ] || tar -C $T -xzf "$(tarball libnspsl)" || exit 1
cp "$PNG/scripts/pnglibconf.h.prebuilt" "$PNG/pnglibconf.h"
O=$T/obj; rm -rf $O; mkdir -p $O/png $O/nspsl
# libpng: the library sources of its own Makefile (no SIMD: PNG_*_OPT=0 -> plain C filters). -U__DLL__: Symbian's __DLL__
# from the Open C line is also Borland's "building a DLL", and libpng then wants PNG_DLL_EXPORT; this is a static archive.
for s in png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset pngtrans pngwio pngwrite pngwtran pngwutil; do
    (cd $PNG && $W/bin/pp-gcc -O2 -g0 -U__DLL__ -DPNG_INTEL_SSE_OPT=0 -DPNG_ARM_NEON_OPT=0 -DPNG_MIPS_MSA_OPT=0 \
        -DPNG_POWERPC_VSX_OPT=0 -DPNG_LOONGARCH_LSX_OPT=0 -iquote $PNG -c $s.c -o $O/png/$s.o) || exit 1
done
rm -f $TPLIB/libpng.a; ${CROSS}ar rcs $TPLIB/libpng.a $O/png/*.o
cp $PNG/png.h $PNG/pngconf.h $PNG/pnglibconf.h $DEPS/include/
# libnspsl: nspsl.c includes the generated suffix table src/psl.inc (shipped in the release tarball)
(cd $NSPSL && $W/bin/pp-gcc -O2 -g0 -std=gnu99 -D_GNU_SOURCE -I include -I src -c src/nspsl.c -o $O/nspsl/nspsl.o) || exit 1
rm -f $TPLIB/libnspsl.a; ${CROSS}ar rcs $TPLIB/libnspsl.a $O/nspsl/nspsl.o
cp $NSPSL/include/nspsl.h $DEPS/include/
ls -l $TPLIB/libpng.a $TPLIB/libnspsl.a
