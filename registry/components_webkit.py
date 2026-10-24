# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause

r"""components_webkit -- the webkit-symbian layer's components (merged into core's registry when this repository is built:
PHOREPOC_ADAPTATIONS=<board adaptations>:<tls>:<cxxrt>:<webkit-symbian>, see the pdk's tools/emitter/paths.py).

OS paths are relative to the merged OS tree. The engine is not in the tree: build.sh makes prebuilt archives under
$PHOREPOC_BUILD/thirdparty/{x86,arm}/cxxrt/web/ (WebCore, PAL, JavaScriptCore, WTF, bmalloc, Skia and the libraries below
them) and .../thirdparty/{x86,arm}/lib/ (libpng, libnspsl), where the "thirdparty/..." paths resolve; the components link them
through x32_archives / arm_archives. libstdc++, libgcc and libphcompat come from the cxxrt layer, and the CA bundle the ROM
stages as \resource\ssl\ca-bundle.crt (curl's roots) from the tls layer (recipes/fetch_cacert.sh)."""
import os

from paths import OS_ROOT

CA_BUNDLE = "resource\\ssl\\ca-bundle.crt=thirdparty/x86/res/ca-bundle.crt"
# the page stack on top of the compiler runtime, in link order
WEB_LIBS = ("WebCore", "PAL", "JavaScriptCore", "WTF", "bmalloc", "Skia", "freetype", "harfbuzz-icu", "harfbuzz",
            "xslt", "xml2", "sqlite3", "webpdemux", "webp", "sharpyuv", "jpeg", "curl", "ssl", "crypto", "icui18n", "icuuc", "icudata")


def _web(arch, main):
    tp = "x86" if arch == "x32" else "arm"
    return ([f"thirdparty/{tp}/cxxrt/web/lib{a}.a" for a in (main,) + WEB_LIBS]
            + [f"thirdparty/{tp}/lib/{a}.a" for a in ("libpng", "libnspsl")]
            + [f"thirdparty/{tp}/cxxrt/web/libz.a"])


COMPONENTS = {
    # The JavaScriptCore shell (WebKit 2.54.1 JSCOnly, CLoop, no JIT): the archives are built by recipes/build_jsc.sh +
    # package_jsc.sh; only the Symbian-API shim phsym.cpp is compiled here.
    "jsc": dict(mmp=os.path.join(OS_ROOT, "src/webkit/group/jsc.mmp"), allow_wsd=True, static_ctors=True,
                rtti=False, defs=["__SOFTFP="], stack=0x100000,   # flexible-model PP::MaxUserThreadStack = 1 MB (a bigger one: Create -40 KErrTooBig)
                x32_archives=[f"thirdparty/x86/cxxrt/{a}.a" for a in ("libjscmain", "libJavaScriptCore", "libWTF", "libbmalloc",
                              "libicui18n", "libicuuc", "libicudata", "libphcompat", "libstdc++", "libgcc_eh", "libgcc")]),
    # The page test: WebCore + PAL + JSC + Skia + deps, prebuilt by recipes/build_webcore.sh + package_web.sh
    "phwebtest": dict(mmp=os.path.join(OS_ROOT, "src/webkit/group/phwebtest.mmp"), allow_wsd=True, static_ctors=True,
                      rtti=False, defs=["__SOFTFP="], stack=0x100000,
                      # curl's CA bundle (CurlSSLHandleSymbian.cpp: z:/resource/ssl/ca-bundle.crt)
                      rom_data=[CA_BUNDLE],
                      x32_archives=_web("x32", "phwebmain")
                      + [f"thirdparty/x86/cxxrt/{a}.a" for a in ("libphcompat", "libstdc++", "libgcc_eh", "libgcc")]),
    # The BrCtl facade over WebKit, built to the old engine's ROM name browserengine.dll (the 2010 browser, Messaging and Help
    # load it by name; CreateBrowserControlL = ordinal 1). `ph component build phebrctl` REPLACES the old engine's image
    # (`ph component build browserengine` restores it). Engine archives: build_webcore.sh phengine + package_web.sh.
    "phebrctl": dict(mmp=os.path.join(OS_ROOT, "src/phebrctl/group/phebrctl.mmp"), rom="browserengine.dll",
                     **{"def": os.path.join(OS_ROOT, "src/phebrctl/eabi/browserengineu.def")},
                     allow_wsd=True, static_ctors=True, rtti=False, defs=["__SOFTFP="],
                     x32_archives=_web("x32", "phengine")
                     + [f"thirdparty/x86/cxxrt/{a}.a" for a in ("libphcompat", "libstdc++", "libgcc_eh", "libgcc")]),
    # The WebKit engine as a Phorepoc web-engine package: an ECom plug-in (interface 0xE7016B01, webengine repository) over the
    # same engine archives as phebrctl. The browser frontend discovers and loads it; no engine ships in the ROM by default.
    "phewebengine_webkit": dict(mmp=os.path.join(OS_ROOT, "src/phewebengine_webkit/group/phewebengine_webkit.mmp"),
                                allow_wsd=True, static_ctors=True, rtti=False, defs=["__SOFTFP="],
                                x32_archives=_web("x32", "phengine")
                                + [f"thirdparty/x86/cxxrt/{a}.a" for a in ("libphcompat", "libstdc++", "libgcc_eh", "libgcc")]),
    # The page test on ARM, JSC with the JIT (ARCH=arm bash build.sh builds every archive into thirdparty/arm/).
    "phwebtest_arm": dict(mmp=os.path.join(OS_ROOT, "src/webkit/group/phwebtest.mmp"), rom="phwebtest.exe", device=True,
                          allow_wsd=True, static_ctors=True, rtti=False, defs=["__SOFTFP="], stack=0x100000,
                          rom_data=[CA_BUNDLE],
                          arm_archives=_web("arm", "phwebmain")
                          + [f"thirdparty/arm/cxxrt/{a}.a" for a in ("libphcompat", "libstdc++")]),   # (libgcc: the device link's own -- the toolchain's v7 one has MOVW relocations)
    # The BrCtl facade on ARM. rom=browserengine.dll REPLACES the old engine in an ROM_ARCH=arm ROM; `device_rom` = it ships
    # in every ARM ROM (a plain `device` key only ships as a boot test). Carries the curl CA bundle itself (phwebtest_arm is
    # not in the ROM).
    "phebrctl_arm": dict(mmp=os.path.join(OS_ROOT, "src/phebrctl/group/phebrctl.mmp"), rom="browserengine.dll", device=True, device_rom=True,
                         **{"def": os.path.join(OS_ROOT, "src/phebrctl/eabi/browserengineu.def")},
                         allow_wsd=True, static_ctors=True, rtti=False, defs=["__SOFTFP="],
                         rom_data=[CA_BUNDLE],
                         arm_archives=_web("arm", "phengine")
                         + [f"thirdparty/arm/cxxrt/{a}.a" for a in ("libphcompat", "libstdc++")]),
    # The WebKit engine plug-in on ARM (the same ECom plug-in over the same engine archives as phebrctl_arm); `device_rom` = it
    # ships in every ARM ROM, where the browser frontend finds it. Carries the curl CA bundle (as phebrctl_arm does).
    "phewebengine_webkit_arm": dict(mmp=os.path.join(OS_ROOT, "src/phewebengine_webkit/group/phewebengine_webkit.mmp"),
                                    rom="phewebengine_webkit.dll", device=True, device_rom=True,
                                    allow_wsd=True, static_ctors=True, rtti=False, defs=["__SOFTFP="],
                                    rom_data=[CA_BUNDLE],
                                    arm_archives=_web("arm", "phengine")
                                    + [f"thirdparty/arm/cxxrt/{a}.a" for a in ("libphcompat", "libstdc++")]),
}
