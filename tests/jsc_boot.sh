#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# jsc_boot.sh [script.js] -- run the JavaScriptCore shell (the `jsc` component) on a board: build the component, a ROM that
# boots only `jsc z:\run.js` with the script staged as Z:\run.js, run the ROM and print the "[js]" lines (the shell's exit
# code and abort traces; its stdout goes to C:\jsout.txt).
#
# On the host, with the pdk's `ph` on PATH, after `bash build.sh` (x32, in the toolchain container):
#   PHOREPOC_ADAPTATIONS=<board adaptations>:<tls>:<cxxrt>:<webkit-symbian> PHOREPOC_RUN='<command>' bash tests/jsc_boot.sh
# PHOREPOC_RUN boots the built ROM on the board (QEMU, a virtual machine, a phone ...) and writes its serial log to stdout; it is not part of
# this repository. BOARD defaults to pc. The script is staged with the pdk's image option -O data=run.js=<file>.
set -eu
: "${PHOREPOC_RUN:?set PHOREPOC_RUN to the command that boots the ROM and prints the serial log}"
here=$(cd "$(dirname "$0")" && pwd)
BOARD=${BOARD:-pc}
JS=${1:-$here/hello.js}
ph component build -b "$BOARD" jsc
ph image rom -b "$BOARD" -s 'jsc z:\run.js' -O "data=run.js=$JS"
sh -c "$PHOREPOC_RUN" | grep -a "\[js\]" | tr -cd '\11\12\15\40-\176' | head -60
