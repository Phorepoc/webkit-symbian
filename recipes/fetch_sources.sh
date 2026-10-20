#!/usr/bin/env bash
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# fetch_sources.sh [name...] -- every source release of sources.lock (SHA-256 checked) into
# $PHOREPOC_BUILD/webkit-symbian-work/dl/, and WebCore's curl network backend (which the WPE release tarball lacks) from
# the pinned tag of WebKit's git repository (webkit.lock; the commit is checked) into $DL/webkit-git/.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"
while IFS=$'\t' read -r name file sha url; do
    case "$name" in ''|'#'*) continue ;; esac
    if [ $# -gt 0 ] && ! printf '%s\n' "$@" | grep -qx "$name"; then continue; fi
    fetch_verified "$url" "$sha" "$DL/$file"
    echo "$name: $DL/$file"
done < "$WKS/sources.lock"
if [ $# -gt 0 ] && ! printf '%s\n' "$@" | grep -qx webkit; then exit 0; fi
G=$DL/webkit-git
if [ ! -d "$G/Source/WebCore/platform/network/curl" ] || [ "$(git -C "$G" rev-parse HEAD 2>/dev/null)" != "$WEBKIT_COMMIT" ]; then
    rm -rf "$G"
    git clone -q --depth 1 --filter=blob:none --no-checkout --branch "$WEBKIT_TAG" "$WEBKIT_GIT" "$G"
    git -C "$G" sparse-checkout set Source/WebCore/platform/network/curl
    git -C "$G" checkout -q
fi
[ "$(git -C "$G" rev-parse HEAD)" = "$WEBKIT_COMMIT" ] || { echo "WebKit tag $WEBKIT_TAG is not commit $WEBKIT_COMMIT" >&2; exit 1; }
echo "WebKit $WEBKIT_TAG ($WEBKIT_COMMIT): curl backend in $G"
