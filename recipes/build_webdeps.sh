#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# build_webdeps.sh [lib...] -- WebCore's C/C++ dependencies for x32 over Open C, on the pure
# engine line (pp-gcc/pp-g++, the cmake toolchain $V/x32-openc.cmake written by build_jsc.sh).
# Sources: third_party/webdeps/ (gitignored; SOURCES.sha256 pins them). Out: $V/deps-x32/{include,lib} (static archives).
# Each lib is skipped when its archive exists (delete it, or name the lib, to rebuild). Default: all, in dependency order.
#   freetype  2.13.3  no zlib/bzip2/png/brotli/harfbuzz (WOFF2 + colour fonts later)
#   harfbuzz  10.1.0  the amalgamated harfbuzz.cc, FreeType + ICU (ours, $V/icu-x32), no glib
#   libxml2   2.13.5  no iconv/zlib/lzma/http/python (ICU does WebCore's charsets)
#   libxslt   1.1.42  ENABLE_XSLT (WPE builds with it); no python/crypto/modules/debugger
#   sqlite    3.47.2  the amalgamation; no mmap (Open C's is file-only), no WAL (needs shared memory), no extension loading
#   libwebp   1.4.0   decoder + demux; no SIMD, no threads
#   libjpeg-turbo 3.0.4  static, no SIMD (Skia's JPEG codec needs turbo's extensions: JCS_EXT_*, jpeg_skip_scanlines)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
P=$DEPS; J=${JOBS:-4}; LOG=$LOG/webdeps
mkdir -p $P/include $P/lib $LOG $V/wd
bash "$HERE/fetch_sources.sh" freetype harfbuzz libxml2 libxslt sqlite libwebp libjpeg-turbo openssl zlib >/dev/null || exit 1
pure_wrappers || exit 1
[ -f $TC ] || { echo "no $TC (run build_jsc.sh first)"; exit 1; }
CM="-G Ninja -DCMAKE_TOOLCHAIN_FILE=$TC -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$P -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=OFF"
unpack() { [ -d $V/wd/$2 ] || tar -C $V/wd -xf "$(tarball $1)" || exit 1; }
step() { echo "== $1"; }
fail() { echo "FAILED $1 (log $LOG/$1.log)"; tail -25 $LOG/$1.log; exit 1; }

b_freetype() {
  unpack freetype freetype-2.13.3
  cmake -S $V/wd/freetype-2.13.3 -B $V/wd/b-freetype $CM -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON \
    -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON > $LOG/freetype.log 2>&1 && ninja -C $V/wd/b-freetype -j$J install >> $LOG/freetype.log 2>&1
}
b_harfbuzz() {
  unpack harfbuzz harfbuzz-10.1.0
  local S=$V/wd/harfbuzz-10.1.0/src O=$V/wd/b-harfbuzz; mkdir -p $O $P/include/harfbuzz
  local HB=(-U__linux__ -U__linux -DPHOREPOC=1 -O2 -g0 -std=gnu++17 -fno-exceptions -fno-rtti $CXXINC -DHAVE_FREETYPE=1 -DHAVE_FT_GET_VAR_BLEND_COORDINATES=1
    -DHAVE_FT_SET_VAR_BLEND_COORDINATES=1 -DHAVE_FT_DONE_MM_VAR=1 -DHAVE_FT_GET_TRANSFORM=1 -DHAVE_ICU=1 -DHAVE_ICU_BUILTIN=1
    -DHB_NO_MT -DHB_NO_PRAGMA_GCC_DIAGNOSTIC_ERROR -I$P/include/freetype2 -I$ICU/include)
  $W/bin/pp-g++ "${HB[@]}" -c $S/harfbuzz.cc -o $O/harfbuzz.o > $LOG/harfbuzz.log 2>&1 || return 1
  rm -f $P/lib/libharfbuzz.a; ${CROSS}ar rcs $P/lib/libharfbuzz.a $O/harfbuzz.o
  # libharfbuzz-icu (WebKit's HarfBuzz ICU component): hb-icu.cc, which the amalgamation does not include (link census: hb_icu_script_to_script)
  $W/bin/pp-g++ "${HB[@]}" -c $S/hb-icu.cc -o $O/hb-icu.o >> $LOG/harfbuzz.log 2>&1 || return 1
  rm -f $P/lib/libharfbuzz-icu.a; ${CROSS}ar rcs $P/lib/libharfbuzz-icu.a $O/hb-icu.o
  cp $S/hb.h $S/hb-*.h $P/include/harfbuzz/; rm -f $P/include/harfbuzz/hb-*-private.h
}
b_libxml2() {
  unpack libxml2 libxml2-2.13.5
  cmake -S $V/wd/libxml2-2.13.5 -B $V/wd/b-libxml2 $CM -DLIBXML2_WITH_PYTHON=OFF -DLIBXML2_WITH_ICONV=OFF -DLIBXML2_WITH_ZLIB=OFF \
    -DLIBXML2_WITH_LZMA=OFF -DLIBXML2_WITH_HTTP=OFF -DLIBXML2_WITH_FTP=OFF -DLIBXML2_WITH_TESTS=OFF -DLIBXML2_WITH_PROGRAMS=OFF \
    -DLIBXML2_WITH_MODULES=OFF -DLIBXML2_WITH_ICU=OFF -DLIBXML2_WITH_THREADS=ON > $LOG/libxml2.log 2>&1 \
    && ninja -C $V/wd/b-libxml2 -j$J install >> $LOG/libxml2.log 2>&1
}
b_libxslt() {
  unpack libxslt libxslt-1.1.42
  cmake -S $V/wd/libxslt-1.1.42 -B $V/wd/b-libxslt $CM -DLibXml2_DIR=$P/lib/cmake/libxml2-2.13.5 -DLIBXSLT_WITH_PYTHON=OFF \
    -DLIBXSLT_WITH_CRYPTO=OFF -DLIBXSLT_WITH_MODULES=OFF -DLIBXSLT_WITH_DEBUGGER=OFF -DLIBXSLT_WITH_PROFILER=OFF \
    -DLIBXSLT_WITH_PROGRAMS=OFF -DLIBXSLT_WITH_TESTS=OFF -DLIBXSLT_WITH_THREADS=ON -DHAVE_STRXFRM_L=OFF > $LOG/libxslt.log 2>&1 \
    && ninja -C $V/wd/b-libxslt -j$J install >> $LOG/libxslt.log 2>&1
}
b_sqlite() {
  local S=$V/wd/sqlite-amalgamation-3470200
  [ -d $S ] || python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$(tarball sqlite)" $V/wd || return 1
  mkdir -p $V/wd/b-sqlite
  $W/bin/pp-gcc -U__linux__ -DPHOREPOC=1 -O2 -g0 -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION=1 -DSQLITE_MAX_MMAP_SIZE=0 \
    -DSQLITE_OMIT_WAL=1 -include $OVR/sqlite_compat.h -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_OMIT_DEPRECATED=1 -DHAVE_USLEEP=1 \
    -c $S/sqlite3.c -o $V/wd/b-sqlite/sqlite3.o > $LOG/sqlite.log 2>&1 || return 1
  rm -f $P/lib/libsqlite3.a; ${CROSS}ar rcs $P/lib/libsqlite3.a $V/wd/b-sqlite/sqlite3.o; cp $S/sqlite3.h $S/sqlite3ext.h $P/include/
}
b_libwebp() {
  unpack libwebp libwebp-1.4.0
  cmake -S $V/wd/libwebp-1.4.0 -B $V/wd/b-libwebp $CM -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF \
    -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF \
    -DWEBP_BUILD_EXTRAS=OFF -DWEBP_ENABLE_SIMD=OFF -DWEBP_USE_THREAD=OFF -DWEBP_BUILD_LIBWEBPMUX=ON > $LOG/libwebp.log 2>&1 \
    && ninja -C $V/wd/b-libwebp -j$J install >> $LOG/libwebp.log 2>&1
}
b_libjpegturbo() {
  unpack libjpeg-turbo libjpeg-turbo-3.0.4
  local B=$V/wd/b-jpegturbo S=$V/wd/libjpeg-turbo-3.0.4
  # only the static library: 3.0.4 builds its tools (djpeg/cjpeg) regardless of options, and they do not link here
  cmake -S $S -B $B $CM -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_SIMD=OFF -DWITH_TURBOJPEG=OFF > $LOG/libjpegturbo.log 2>&1 \
    && ninja -C $B -j$J jpeg-static >> $LOG/libjpegturbo.log 2>&1 || return 1
  cp $B/libjpeg.a $P/lib/libjpeg.a && cp $B/jconfig.h $S/jpeglib.h $S/jmorecfg.h $S/jerror.h $P/include/
}
# OpenSSL 3.5.4 : WebCrypto's backend (USE(OPENSSL), the PlayStation port's path) and the browser curl's TLS.
# linux-x32 target with the pure Open C compiler, no asm/threads-free features/modules/engines; entropy = the kernel CSPRNG (openssl_patch.py)
b_openssl() {
  local S=$V/wd/openssl-3.5.4
  unpack openssl openssl-3.5.4
  python3 $HERE/openssl_patch.py $S || return 1
  cd $S
  [ -f Makefile ] || CC=$W/bin/pp-gcc AR=${CROSS}ar RANLIB=${CROSS}ranlib ./Configure $OSSL_TARGET no-asm no-shared no-dso no-async no-engine no-module \
     no-tests no-apps no-docs no-afalgeng no-ui-console no-secure-memory no-uplink no-filenames -U__linux__ -U__linux -DPHOREPOC=1 -DNO_SYSLOG \
     --prefix=$P/openssl > $LOG/openssl.log 2>&1 || { cd - >/dev/null; return 1; }
  make -j$J build_libs >> $LOG/openssl.log 2>&1 || { cd - >/dev/null; return 1; }
  cp libcrypto.a libssl.a $P/lib/ && mkdir -p $P/include/openssl && cp include/openssl/*.h $P/include/openssl/; cd - >/dev/null
}
# zlib 1.3.1: Open C's zlib is 1.2.3-era (no z_const; WebCore's CompressionStream needs it). Static, the plain source list.
b_zlib() {
  local S=$V/wd/zlib-1.3.1 O=$V/wd/b-zlib f
  unpack zlib zlib-1.3.1
  rm -rf $O; mkdir -p $O; : > $LOG/zlib.log
  for f in adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate inftrees trees uncompr zutil; do
    $W/bin/pp-gcc -U__linux__ -DPHOREPOC=1 -O2 -g0 -DHAVE_UNISTD_H -c $S/$f.c -o $O/$f.o >> $LOG/zlib.log 2>&1 || return 1
  done
  rm -f $P/lib/libz.a; ${CROSS}ar rcs $P/lib/libz.a $O/*.o; cp $S/zlib.h $S/zconf.h $P/include/
}
declare -A LIBA=([freetype]=libfreetype.a [harfbuzz]=libharfbuzz.a [libxml2]=libxml2.a [sqlite]=libsqlite3.a [libwebp]=libwebp.a [libjpegturbo]=libjpeg.a [openssl]=libcrypto.a [zlib]=libz.a [libxslt]=libxslt.a)
LIBS=("$@"); [ ${#LIBS[@]} = 0 ] && LIBS=(freetype harfbuzz libxml2 libxslt sqlite libwebp libjpegturbo openssl zlib)
for l in "${LIBS[@]}"; do
  if [ $# = 0 ] && [ -f $P/lib/${LIBA[$l]} ]; then echo "== $l (have)"; continue; fi
  step $l; b_$l || fail $l
done
ls -l $P/lib/*.a
