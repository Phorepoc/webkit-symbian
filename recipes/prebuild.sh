#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# prebuild.sh -- the pdk's package.yml runs it in the toolchain container before the components: cxxrt's archives,
# then the whole engine (build.sh) for PHOREPOC_ARCH.
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
export ARCH=${PHOREPOC_ARCH:-x32} JOBS=${JOBS:-$(nproc)}
IFS=: read -r -a repos <<< "${PHOREPOC_ADAPTATIONS:-}"
IFS=: read -r -a names <<< "${PHOREPOC_ADAPTATION_NAMES:-}"
cxxrt=
for i in "${!repos[@]}"; do [ "${names[i]:-}" = cxxrt ] && cxxrt=${repos[i]}; done
[ -n "$cxxrt" ] || { echo "the engine needs cxxrt in PHOREPOC_ADAPTATIONS"; exit 1; }
bash "$cxxrt/build.sh"
exec bash "$HERE/build.sh"
