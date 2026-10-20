#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# build.sh -- the whole engine for one architecture (ARCH=x32 default, or arm): fetch the pinned sources, build ICU, the
# dependencies (FreeType, HarfBuzz, libxml2, libxslt, SQLite, libwebp, libjpeg-turbo, OpenSSL, zlib, curl, libpng, libnspsl),
# JavaScriptCore, WebCore + the embedding, and package the archives the registry entries link. It takes hours (WebCore's unified
# sources need about 1.5 GB of RAM per compiler: JOBS=4 on an 8 GB machine). Inside the toolchain container
# (ghcr.io/phorepoc/web) with PHOREPOC_PDK / PHOREPOC_CORE / PHOREPOC_BUILD set (see README.md), after the cxxrt repository's
# build.sh for the same ARCH. Each step skips what it has built; delete a build directory (or RECONF=1 for the CMake
# configuration) to rebuild. A change of the compile line (a wrapper edit) does not retrigger make/ninja: start from clean.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
export JOBS=${JOBS:-4}
r=$here/recipes
s() { echo "== $1 $(date +%T)"; }
s fetch;  bash $r/fetch_sources.sh
s icu;    bash $r/build_icu.sh | tail -2
s deps;   bash $r/build_webdeps.sh | grep "^==\|FAILED" || true
s curl;   bash $r/build_curl_ossl.sh | tail -1
s images; bash $r/build_png_nspsl.sh
s jsc;    bash $r/build_jsc.sh JavaScriptCore
s webcore; bash $r/build_webcore.sh; bash $r/build_webcore.sh phwebtest; bash $r/build_webcore.sh phengine
if [ "${ARCH:-x32}" != arm ]; then   # the JavaScriptCore shell (the `jsc` component), x32 only
  bash $r/build_jsc.sh jsc; bash $r/package_jsc.sh
fi
s package; bash $r/package_web.sh | tail -1
s done
