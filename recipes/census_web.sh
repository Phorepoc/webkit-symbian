#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# census_web.sh -- the undefined-symbol census of the WebKit page stack (phwebtest = WebCore + PAL + JSC + WTF +
# bmalloc + Skia + the deps), the way package_jsc.sh does it for jsc: symbols no archive defines and Open C (libc/libm/libpthread .def)
# does not export. Out: $LOG/web-undefined.txt (mangled) + web-undefined-demangled.txt, grouped counts on stdout. This list is the
# remaining platform work . CMake's thin archives are read in place.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
BLD=$V/build-wc; P=$DEPS/lib; N=$TPLIB; ICUL=$ICU/lib
cd $BLD || exit 1
OBJ=$(ninja -t inputs phwebtest 2>/dev/null | grep "phwebtest.dir/.*\.o$")
A="$(ls $BLD/lib/lib{WebCore,PAL,JavaScriptCore,WTF,bmalloc,Skia}.a) $P/libfreetype.a $P/libharfbuzz.a $P/libharfbuzz-icu.a $P/libxml2.a $P/libsqlite3.a $P/libwebp.a \
   $P/libwebpdemux.a $P/libsharpyuv.a $P/libjpeg.a $N/libpng.a $N/libnspsl.a $P/libcurl.a $P/libssl.a $P/libcrypto.a $P/libz.a $ICUL/libicui18n.a $ICUL/libicuuc.a \
   $ICUL/libicudata.a $OUT/libphcompat.a $OUT/libstdc++.a"
${CROSS}nm -A --defined-only $OBJ $A 2>/dev/null | awk 'NF>=3 && $(NF-1)!="U" {print $NF}' | sort -u > /tmp/def.txt
${CROSS}nm -u $OBJ $A 2>/dev/null | grep -v ":$" | awk '{print $NF}' | sort -u > /tmp/und.txt
cat $OE/libc/eabi/libcu.def $OE/libm/eabi/libmu.def $OE/libpthread/eabi/libpthreadu.def | awk '$1!="EXPORTS" && NF>=2 {print $1}' | sort -u > /tmp/oc.txt
comm -23 /tmp/und.txt /tmp/def.txt | comm -23 - /tmp/oc.txt > $LOG/web-undefined.txt
c++filt < $LOG/web-undefined.txt > $LOG/web-undefined-demangled.txt
wc -l /tmp/und.txt /tmp/def.txt $LOG/web-undefined.txt
# grouped: C symbols, then C++ by namespace::class
echo "== C (libc-level) symbols:"; grep -v "::" $LOG/web-undefined-demangled.txt | head -80 | tr '\n' ' '; echo
echo "== C++ by class:"; grep "::" $LOG/web-undefined-demangled.txt | sed 's/(.*//' | awk -F'::' '{ if (NF>=3) print $1"::"$2; else print $1 }' | sort | uniq -c | sort -rn | head -60
