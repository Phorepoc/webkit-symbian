#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# build_icu.sh -- ICU 74.2 for x32 or ARM over Open C. Inside the toolchain container, after the cxxrt repository's build.sh.
#   ARCH=x32|arm bash recipes/build_icu.sh
# 1) a native build (host tools: icupkg/genrb/pkgdata) with the data trimmed to what the browser needs (icu_trim.py: English
#    locales, no transliteration, rbnf or dictionaries), 2) a cross build with the pure-Open-C wrapper (link tests fail by
#    design -> every function "absent"; ICU has portable fallbacks).
# Output: $V/icu-$ARCH/inst/{include,lib/libicu{uc,i18n,data}.a}.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
bash "$HERE/fetch_sources.sh" icu >/dev/null || exit 1
if [ $ARCH = arm ]; then IHOST=arm-unknown-linux-gnueabi;   # mh-linux makefile rules only: the compiler is bare-metal, no __linux__
else IHOST=x86_64-pc-linux-gnu; fi
D=$HERE; J=${JOBS:-12}
pure_wrappers || exit 1
[ -d $V/icu/source ] || tar -C $V -xf "$(tarball icu)" || exit 1
S=$V/icu/source
if [ ! -f $V/icu-native/bin/icupkg ] && [ ! -f $V/icu-native/build/bin/icupkg ]; then
  mkdir -p $V/icu-native; cd $V/icu-native
  $S/runConfigureICU Linux --disable-tests --disable-samples --disable-extras --disable-layoutex > $LOG/icu-native-configure.log 2>&1 || { tail $LOG/icu-native-configure.log; exit 1; }
  make -j$J > $LOG/icu-native-make.log 2>&1 || { tail -20 $LOG/icu-native-make.log; exit 1; }
fi
# data trim: the tarball ships only the PREBUILT icudt74l.dat, so cut the items with the native icupkg
IP=$(ls $V/icu-native/bin/icupkg $V/icu-native/build/bin/icupkg 2>/dev/null | head -1)
if [ ! -f $S/data/in/.trimmed ]; then
  mkdir -p $S/data/orig
  [ -f $S/data/orig/icudt74l.dat ] || cp $S/data/in/icudt74l.dat $S/data/orig/icudt74l.dat
  LD_LIBRARY_PATH=$V/icu-native/lib $IP -l -o $V/icu-items.txt $S/data/orig/icudt74l.dat
  python3 $D/icu_trim.py $V/icu-items.txt > $V/icu-rm.txt; wc -l $V/icu-items.txt $V/icu-rm.txt
  LD_LIBRARY_PATH=$V/icu-native/lib $IP -r $V/icu-rm.txt $S/data/orig/icudt74l.dat $S/data/in/icudt74l.dat || exit 1
  ls -l $S/data/in/icudt74l.dat; touch $S/data/in/.trimmed
fi
echo native ok
mkdir -p $V/icu-$ARCH; cd $V/icu-$ARCH
if [ ! -f Makefile ]; then
  sed -i 's/^\( *\)as_fn_error \$? "Link tests are not allowed.*/\1:/' $S/configure 2>/dev/null
  CC=$W/bin/pp-gcc CXX=$W/bin/pp-g++ CFLAGS="-O2 -g0 -DU_HAVE_STD_STRING=1 -DU_HAVE_WCHAR_H=0 -DU_HAVE_WCSCPY=0" CXXFLAGS="-O2 -g0 -DU_HAVE_WCHAR_H=0 -DU_HAVE_WCSCPY=0 -std=gnu++17 $CXXINC -fexceptions -frtti" \
  LDFLAGS="-nostdlib -nostartfiles -static" \
  $S/configure --host=$IHOST --build=x86_64-linux-gnu --with-cross-build=$V/icu-native --prefix=$V/icu-$ARCH/inst \
     --enable-static --disable-shared --disable-tools --disable-tests --disable-samples --disable-extras --disable-layoutex --disable-dyload \
     --with-data-packaging=static > $LOG/icu-$ARCH-configure.log 2>&1 || { tail -30 $LOG/icu-$ARCH-configure.log; exit 1; }
fi
# Open C wchar.h typedefs mbstate_t, libstdc++ <cwchar> (no wchar support in our build) defines a struct of the same name: keep ICU off <wchar.h>
echo configured
python3 $D/icu_patch.py $S || exit 1   # Phorepoc edits (__SYMBIAN32__ only; write-on-change)
make -k -j$J > $LOG/icu-$ARCH-make.log 2>&1; echo "make exit $?"; make install > $LOG/icu-$ARCH-install.log 2>&1; grep -c "error:" $LOG/icu-$ARCH-make.log
