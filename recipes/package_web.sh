#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# package_web.sh -- gather the WebKit page stack's pieces into $OUT/*.a for the `phwebtest` registry
# component (x32_archives), the way package_jsc.sh does it for jsc. CMake's thin archives are re-packed into real ones; the deps
# (build_webdeps.sh, build_curl_ossl.sh, ICU) are copied. Census of what is still missing: census_web.sh.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
BLD=$V/build-wc; WB=$OUT/web; P=$DEPS/lib; ICUL=$ICU/lib
mkdir -p $WB; cd $BLD
ninja -t inputs phwebtest 2>/dev/null | grep "phwebtest.dir/.*\.o$" > /tmp/webmain.txt; wc -l < /tmp/webmain.txt
rm -f $WB/libphwebmain.a; ${CROSS}ar rcs $WB/libphwebmain.a $(cat /tmp/webmain.txt)
# the BrCtl facade's engine (phebrctl -> browserengine.dll): PhEngine.cpp + the same Ph* support sources, as its own archive
if ninja -t inputs phengine 2>/dev/null | grep "phengine.dir/.*\.o$" > /tmp/webeng.txt && [ -s /tmp/webeng.txt ]; then rm -f $WB/libphengine.a; ${CROSS}ar rcs $WB/libphengine.a $(cat /tmp/webeng.txt); fi
for l in WebCore PAL JavaScriptCore WTF bmalloc Skia; do
  f=$BLD/lib/lib$l.a; [ $WB/lib$l.a -nt $f ] && continue; rm -f $WB/lib$l.a
  (cd $BLD && ${CROSS}ar t $f | xargs ${CROSS}ar rcs $WB/lib$l.a)
done
for l in freetype harfbuzz harfbuzz-icu xml2 xslt sqlite3 webp webpdemux sharpyuv jpeg curl ssl crypto z; do cp -u $P/lib$l.a $WB/; done
for l in icui18n icuuc icudata; do cp -u $ICUL/lib$l.a $WB/; done
# native TLS cannot link into an E32 image: such sources need -femulated-tls (Platform-WebCore.cmake, at its end)
NATIVE_TLS=$(for a in $WB/*.a; do ${CROSS}readelf -rW $a 2>/dev/null | awk -v A=$(basename $a) '/^File:/{f=$2} /TPOFF|TLSGD|TLSLD/{print f; exit}'; done)
[ -z "$NATIVE_TLS" ] || { echo "native TLS relocations (add -femulated-tls for these):"; echo "$NATIVE_TLS"; exit 1; }
ls -l $WB/*.a | awk '{s+=$5; print $5, $9} END {print s, "total"}'
