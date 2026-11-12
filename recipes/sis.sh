#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# sis.sh -- the pdk's package.yml runs it after the components: the engine plug-in as
# $PHOREPOC_BUILD/sis/webkit-symbian-<arch>.sis (the DLL and its ECom registration; the CA bundle is the image's).
set -eu
ARCH=${PHOREPOC_ARCH:-x32}
if [ "$ARCH" = arm ]; then KEY=phewebengine_webkit_arm; AFL=--arm; else KEY=phewebengine_webkit; AFL=; fi
# Link stubs: a name the DLL calls that no provider exports is linked to a stub that panics the caller (EUSRNIMP). The
# emitter records them in <image>.linkstubs; the release fails on any name recipes/linkstubs.allow does not list
# ("name  # reason" lines). A silent stub cost us SVG (libdom's XML binding was left out of the build).
STUBS=$PHOREPOC_BUILD/x32img/phewebengine_webkit.dll.linkstubs; [ "$ARCH" = arm ] && STUBS=$PHOREPOC_BUILD/arm/img/phewebengine_webkit.dll.linkstubs
[ -f "$STUBS" ] || { echo "sis.sh: no $STUBS (the emitter's link-stub record)"; exit 1; }
ALLOW=$(dirname "$0")/linkstubs.allow
BAD=$(grep -v '^$' "$STUBS" | while read -r n; do grep -qE "^$n([[:space:]]|$)" "$ALLOW" 2>/dev/null || echo "$n"; done)
[ -z "$BAD" ] || { echo "sis.sh: $STUBS has link stubs not in recipes/linkstubs.allow:"; echo "$BAD"; exit 1; }
# The SIS carries its release's version (PHOREPOC_PACKAGE_VERSION, set by the pdk's package.yml), so a newer release is an
# upgrade. It installs through the Catalogue; no image carries an engine.
# The licences of everything in the DLL (LGPL-2.1 for WebCore/JavaScriptCore, the libraries' own, MPL-2.0 for the CA bundle),
# NOTICE and the written source offer go with it, to \resource\phweb\licences\.
REPO=$(cd "$(dirname "$0")/.." && pwd)
LIC=
for f in "$REPO"/LICENSES/* "$REPO/LICENSE" "$REPO/NOTICE" "$REPO/SOURCE-OFFER.txt"; do
  LIC="$LIC --file $f=!:/resource/phweb/licences/$(basename "$f")"
done
python3 "$PHOREPOC_PDK/tools/make_sis.py" $KEY $AFL --unsigned --version "${PHOREPOC_PACKAGE_VERSION:-1.0.0}" \
  --name "WebKit web engine" --vendor Phorepoc $LIC \
  --out "$PHOREPOC_BUILD/sis/webkit-symbian-$ARCH.sis"
