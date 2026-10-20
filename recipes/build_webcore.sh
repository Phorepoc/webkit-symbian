#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# build_webcore.sh [ninja target] -- the page layer: WebKit's `Symbian` port (os/src/webkit/port/) = JSC as in build_jsc.sh + PAL + WebCore (curl over OpenSSL 3,
# Skia CPU, HarfBuzz/FreeType, ICU) over Open C. Inside the toolchain container:  JOBS=4 ARCH=x32|arm bash recipes/build_webcore.sh [target]
# (default target WebCore; phwebtest = the page test, phengine = the embedding behind the BrCtl facade). CONFIGURE_ONLY=1 stops after cmake. RECONF=1 re-runs cmake.
# Prereqs: build_jsc.sh once (toolchain file $TC, pure wrappers), build_icu.sh, build_webdeps.sh, build_curl_ossl.sh, build_png_nspsl.sh,
# fetch_sources.sh (apply_patches.sh copies the curl backend).
# Build hygiene: JOBS defaults to 4 (an 8 GB machine); patch scripts write only changed files.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
SRC=$WK_SRC; BLD=$V/build-wc
[ -f $TC ] || { echo "no $TC: run build_jsc.sh first"; exit 1; }
pure_wrappers || exit 1
bash $HERE/apply_patches.sh $SRC $W || exit 1
if [ ! -f $BLD/build.ninja ] || [ -n "${RECONF:-}" ]; then
  mkdir -p $BLD; cd $BLD
  cmake -G Ninja $SRC -DPORT=Symbian -DCMAKE_TOOLCHAIN_FILE=$TC -DCMAKE_BUILD_TYPE=Release \
    -DDEVELOPER_MODE=OFF -DENABLE_API_TESTS=OFF -DPHOREPOC=1 \
    -DPH_DEPS=$DEPS -DPH_MBEDTLS_SRC=$PHOREPOC_BUILD/tls/mbedtls-src -DPH_MBEDTLS_CFG=$PHOREPOC_OS_ROOT/src/mbedtls \
    -DICU_INCLUDE_DIR=$ICU/include -DICU_DATA_LIBRARY_RELEASE=$ICU/lib/libicudata.a -DICU_I18N_LIBRARY_RELEASE=$ICU/lib/libicui18n.a -DICU_UC_LIBRARY_RELEASE=$ICU/lib/libicuuc.a \
    -DCMAKE_EXE_LINKER_FLAGS="-nostdlib -nostartfiles -static -Wl,--unresolved-symbols=ignore-all -Wl,--allow-multiple-definition -Wl,-e,main -Wl,--defsym,__dso_handle=0" \
    > $LOG/wc-configure.log 2>&1; rc=$?; echo "configure exit $rc"; grep -E "CMake Error|error" -A3 $LOG/wc-configure.log | head -40
  [ $rc = 0 ] || exit 1
fi
[ -n "${CONFIGURE_ONLY:-}" ] && exit 0
T=${1:-WebCore}
cd $BLD && ninja -k 0 -j${JOBS:-4} $T > $LOG/wc-build-$T.log 2>&1; rc=$?; echo "ninja exit $rc"
echo "$(grep -c "error:" $LOG/wc-build-$T.log) errors"
exit $rc   # ninja's status (so `build_webcore.sh && package_web.sh` chains)
