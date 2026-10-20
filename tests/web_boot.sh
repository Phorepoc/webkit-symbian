#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# web_boot.sh [url] -- run phwebtest.exe, the WebKit page test, on a board: build the component, a ROM that boots only it (with
# networking), run the ROM and print the test's lines. phwebtest loads the page (default https://example.com/), prints the
# render tree and pixel statistics, and writes the painted page as a PPM (ppm2png.py converts it).
#
# On the host, with the pdk's `ph` on PATH, after `bash build.sh` (in the toolchain container) and with the tls layer's
# CA bundle fetched (the registry entry stages it as \resource\ssl\ca-bundle.crt):
#   PHOREPOC_ADAPTATIONS=<board adaptations>:<tls>:<cxxrt>:<webkit-symbian> PHOREPOC_RUN='<command>' bash tests/web_boot.sh
# PHOREPOC_RUN boots the built ROM (give the guest at least 1 GB of RAM) and writes its serial log to stdout; it is not part of
# this repository. BOARD defaults to pc.
set -eu
: "${PHOREPOC_RUN:?set PHOREPOC_RUN to the command that boots the ROM and prints the serial log}"
BOARD=${BOARD:-pc}
ph component build -b "$BOARD" phwebtest
ph image rom -b "$BOARD" -O net=1 -s "phwebtest${1:+ $1}"
sh -c "$PHOREPOC_RUN" | grep -a "phwebtest: \(WebKit\|painted\|done\)\|\[js\]\|Panic\|\[cxx\]\|\[pthread\]\|abort()" | tr -cd '\11\12\15\40-\176' | head -80
