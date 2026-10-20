#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# package_jsc.sh -- gather the JSC build's pieces into $OUT/*.a for the `jsc` registry component (x32_archives), and census
# the symbols the link will NOT find (-> $LOG/jsc-undefined.txt): not in our archives, not exported by Open C libc/libm/libpthread, not in euser.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
BLD=$V/build-jsc2; ICUL=$ICU/lib
cd $BLD || exit 1
ninja -t inputs jsc 2>/dev/null | grep "jsc.dir/.*\.o$" > /tmp/jscmain.txt; wc -l < /tmp/jscmain.txt
rm -f $OUT/libjscmain.a; ${CROSS}ar rcs $OUT/libjscmain.a $(cat /tmp/jscmain.txt)
for l in JavaScriptCore WTF bmalloc; do   # CMake makes THIN archives (members by path): re-pack them into real ones
  f=$(find $BLD -name lib$l.a | head -1); [ -n "$f" ] || continue; rm -f $OUT/lib$l.a
  (cd $(dirname $f) && ${CROSS}ar t $f | xargs ${CROSS}ar rcs $OUT/lib$l.a)
done
for l in icui18n icuuc icudata; do cp $ICUL/lib$l.a $OUT/; done
ls -l $OUT/libjscmain.a $OUT/libJavaScriptCore.a $OUT/libWTF.a $OUT/libicu*.a $OUT/libphcompat.a 2>&1 | awk '{print $5, $9}'
# --- undefined-symbol census
cd $OUT; A="libjscmain.a libJavaScriptCore.a libWTF.a libbmalloc.a libicui18n.a libicuuc.a libicudata.a libphcompat.a libstdc++.a libgcc_eh.a libgcc.a"
${CROSS}nm -A --defined-only $A 2>/dev/null | awk '{print $NF}' | sort -u > /tmp/def.txt
${CROSS}nm -u $A 2>/dev/null | grep -v ":$" | awk '{print $NF}' | sort -u > /tmp/und.txt
cat $OE/libc/eabi/libcu.def $OE/libm/eabi/libmu.def $OE/libpthread/eabi/libpthreadu.def | awk '$1!="EXPORTS" && NF>=2 {print $1}' | sort -u > /tmp/oc.txt
comm -23 /tmp/und.txt /tmp/def.txt | comm -23 - /tmp/oc.txt > $LOG/jsc-undefined.txt
wc -l /tmp/und.txt /tmp/oc.txt $LOG/jsc-undefined.txt
