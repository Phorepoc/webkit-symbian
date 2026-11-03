#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# sis.sh -- the pdk's package.yml runs it after the components: the engine plug-in as
# $PHOREPOC_BUILD/sis/webkit-symbian-<arch>.sis (the DLL, its ECom registration and the CA bundle).
set -eu
ARCH=${PHOREPOC_ARCH:-x32}
if [ "$ARCH" = arm ]; then KEY=phewebengine_webkit_arm; AFL=--arm; else KEY=phewebengine_webkit; AFL=; fi
python3 "$PHOREPOC_PDK/tools/make_sis.py" $KEY $AFL --unsigned --name "WebKit web engine" --vendor Phorepoc \
  --out "$PHOREPOC_BUILD/sis/webkit-symbian-$ARCH.sis"
