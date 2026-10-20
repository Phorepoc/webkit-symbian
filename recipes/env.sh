# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause
#
# Sourced by the recipes. Runs inside the toolchain container (ghcr.io/phorepoc/web, for both architectures):
#   PHOREPOC_PDK     the pdk checkout (tools/openc_cflags.py, tools/toolchain/mk_pure_wrappers.sh)
#   PHOREPOC_CORE    core (default: ../core); PHOREPOC_BUILD  all generated files (default: $PHOREPOC_CORE/build)
#   PHOREPOC_OS_ROOT the OS tree (default: $PHOREPOC_CORE/os); with the tls layer merged in, src/mbedtls is its config
#   ARCH             x32 (default) or arm
# Prerequisite: the cxxrt repository's build.sh for the same ARCH (libstdc++ headers and archive, libphcompat.a), and the
# exported include tree.
#
#   OUT   the archives: $PHOREPOC_BUILD/thirdparty/{x86,arm}/cxxrt (JSC, ICU ...) and .../web (the WebKit page stack): what
#         the registry entries link
#   V     the work tree of this architecture: sources, ICU, the dependency prefix, the CMake build dirs
#   W     cxxrt's work dir (libstdc++ build tree, compiler wrappers)
WKS=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
: "${PHOREPOC_CORE:=$WKS/../core}"
: "${PHOREPOC_BUILD:=$PHOREPOC_CORE/build}"
: "${PHOREPOC_PDK:=$WKS/../pdk}"
: "${PHOREPOC_OS_ROOT:=$PHOREPOC_CORE/os}"
export PHOREPOC_CORE PHOREPOC_BUILD PHOREPOC_PDK PHOREPOC_OS_ROOT
. "$WKS/webkit.lock"
ARCH=${ARCH:-x32}
#   LTRIP  libstdc++'s target include dir name     CROSS  binutils prefix (ar/readelf/nm)     PROC  CMAKE_SYSTEM_PROCESSOR
if [ "$ARCH" = arm ]; then
    TP=arm; LTRIP=arm-none-eabi; CROSS=arm-none-eabi-; PROC=arm; OSSL_TARGET=linux-armv4
    # the bare-metal compiler defines no __unix__: WTF/bmalloc would see no OS at all (x86_64-linux gcc has it, then -U__linux__)
    OSDEF="-D__unix__=1 -D__unix=1"
else
    TP=x86; LTRIP=x86_64-unknown-elf; CROSS=; PROC=x86_64; OSSL_TARGET=linux-x32; OSDEF=""
fi
WK_WORK=$PHOREPOC_BUILD/webkit-symbian-work
DL=$WK_WORK/dl                      # the pinned source tarballs
V=$WK_WORK/$ARCH
LOG=$WK_WORK/logs/$ARCH
OUT=${OUT:-$PHOREPOC_BUILD/thirdparty/$TP/cxxrt}
TPLIB=$PHOREPOC_BUILD/thirdparty/$TP/lib       # libpng.a, libnspsl.a (and the tls layer's libmbedtls.a)
W=${W:-$PHOREPOC_BUILD/cxxrt-work/lsc-$ARCH}
GCC_SRC=$(ls -d "$PHOREPOC_BUILD"/cxxrt-work/lsc-$ARCH/gcc-* 2>/dev/null | head -1)
ICU=$V/icu-$ARCH/inst; DEPS=$V/deps-$ARCH; TC=$V/$ARCH-openc.cmake
WK_SRC=$V/wpewebkit-$WEBKIT_VERSION
mkdir -p "$DL" "$V" "$LOG" "$OUT" "$TPLIB"

# fetch_verified URL SHA256 OUTFILE -- download unless OUTFILE already has that hash; fail on a mismatch
fetch_verified() {
    local url=$1 want=$2 out=$3 got
    if [ -f "$out" ] && [ "$(sha256sum "$out" | cut -d' ' -f1)" = "$want" ]; then return 0; fi
    mkdir -p "$(dirname "$out")"
    curl -fsSL --retry 3 -o "$out.part" "$url"
    got=$(sha256sum "$out.part" | cut -d' ' -f1)
    if [ "$got" != "$want" ]; then
        rm -f "$out.part"
        echo "sha256 mismatch for $url: got $got, want $want" >&2
        return 1
    fi
    mv "$out.part" "$out"
}
# tarball NAME -- the path of a pinned source file of sources.lock (fetch_sources.sh has downloaded it)
tarball() { awk -F'\t' -v n="$1" -v d="$DL" '$1 == n { print d "/" $2 }' "$WKS/sources.lock"; }
# pure_wrappers -- the pure-Open-C compiler wrappers $W/bin/pp-gcc and pp-g++ (the pdk's tool; idempotent)
pure_wrappers() { ARCH=$ARCH W=$W bash "$PHOREPOC_PDK/tools/toolchain/mk_pure_wrappers.sh" >/dev/null; }
# the libstdc++ include dirs the engine's C++ is compiled with (cxxrt's build tree)
OVR=$PHOREPOC_PDK/tools/toolchain/openc_posix       # the pdk's POSIX header overlay
CXXINC="-I$W/build/include/$LTRIP -I$W/build/include -I$GCC_SRC/libstdc++-v3/libsupc++"
: "${PHOREPOC_SYMBIAN:=$PHOREPOC_CORE/symbian}"
OE=$PHOREPOC_SYMBIAN/symbian-2010/oss.FCL.sf.os.ossrv/genericopenlibs/openenvcore    # Open C's .def files: the census of what it exports
