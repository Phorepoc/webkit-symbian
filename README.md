# webkit-symbian

The browser engine of [Phorepoc](https://github.com/Phorepoc): [WebKit](https://webkit.org) (WPE WebKit 2.54.1, pinned in
`webkit.lock`) ported to Symbian. It is a modern engine (current HTML, CSS, JavaScript, TLS 1.3 through OpenSSL 3) behind the
2010 browser control API (`CBrCtlInterface`, `phebrctl`), so the Symbian applications that embed a browser control (the 2010
browser, Messaging's HTML viewer, Help) keep working and render with it. The new browser UI is still to be designed: the 2010
browser's UI is not carried forward.

WebKit is not copied here. `recipes/` fetches the pinned releases and builds them over Open C, and `recipes/apply_patches.sh`
adds the port to the tree. This repository holds the port, the Symbian shims and the recipes.

| path | what |
|---|---|
| `os/src/webkit/port/` | the `Symbian` WebKit port's CMake files (`OptionsSymbian.cmake`, `Platform-*.cmake`): JavaScriptCore as the JSCOnly build (CLoop on x32, LLInt + Baseline JIT on ARM), WebCore + PAL on curl/OpenSSL, Skia (CPU raster), HarfBuzz, FreeType, ICU |
| `os/src/webkit/symbian/` | the WTF seams: `OSAllocator` and memory footprint over Symbian's heap, code chunks for the JIT |
| `os/src/webkit/platform-symbian/` | WebCore platform hooks: system fonts, MIME registry, pasteboard, accessibility cache, public suffixes, GL stubs |
| `os/src/webkit/webcore/` | the curl backend's Symbian side: `ResourceHandleSymbian.cpp` (loads over curl), `CurlSSLHandleSymbian.cpp` (the ROM's CA bundle), and the mbedTLS alternatives of its TLS helpers |
| `os/src/webkit/phwebview/` | the in-process embedding of WebCore: `PhEngine.cpp` (the engine thread and its C seam `PhEngineApi.h`), loader and database providers, `phwebtest.cpp` (the page test) |
| `os/src/webkit/phsym.cpp`, `group/` | the Symbian-API shim (RAM size, stack bounds, clocks, entropy, serial log) and the mmps of `jsc.exe` and `phwebtest.exe` |
| `os/src/phebrctl/` | `browserengine.dll`: the BrCtl facade (`CBrCtlInterface`) over the engine thread |
| `recipes/` | fetch and build: ICU, FreeType, HarfBuzz, libxml2, libxslt, SQLite, libwebp, libjpeg-turbo, OpenSSL, zlib, curl, libpng, libnspsl, JavaScriptCore, WebCore; packaging into archives; the patch scripts; `curl_config.h` |
| `registry/components_webkit.py` | the components: `jsc`, `phwebtest`, `phebrctl` (+ `_arm` for the two that ship on ARM) |
| `tests/` | `jsc_boot.sh` (runs `hello.js` in the JavaScriptCore shell), `web_boot.sh` (loads a page), `ppm2png.py` |
| `sources.lock`, `webkit.lock` | the pins (version, URL, SHA-256; WebKit tag and commit) |
| `build.sh` | all of it, start to end |

The engine needs the other layers of Phorepoc's build: [cxxrt](https://github.com/Phorepoc/cxxrt) (libstdc++, libphcompat),
the [pdk](https://github.com/Phorepoc/pdk) (the pure-Open-C compiler wrappers, the POSIX header overlay) and, for https, the
[tls](https://github.com/Phorepoc/tls) layer: the engine's TLS is OpenSSL 3, which the recipes build, but the CA bundle it
verifies against (Mozilla's roots, `\resource\ssl\ca-bundle.crt` in the ROM) comes from the tls layer's
`recipes/fetch_cacert.sh`. (The `*MbedTLS.cpp` files are an alternative TLS-only layer on the tls repository's mbedTLS
(`$PHOREPOC_BUILD/tls/mbedtls-src`); it is not built by default.)

## Building

This repository is a layer, like the adaptations: it adds its `os/` to the OS tree and its registry entries to core's. A full
WebKit build takes hours (WebCore's unified sources need about 1.5 GB of RAM per compiler: `JOBS=4` on an 8 GB machine). Inside the toolchain's web image (`ghcr.io/phorepoc/web`, built from
[toolchain](https://github.com/Phorepoc/toolchain)`/images/web`: CMake, Ninja, Ruby, gperf on top of the base image, which has
GCC 13 with the x32 runtime and the Arm GNU toolchain for ARM), with [core](https://github.com/Phorepoc/core), the pdk,
cxxrt and this repository checked out side by side and the include tree exported (`ph source export -a`):

```sh
docker run --rm --cpus=6 -v "$PWD/..:/src" -e JOBS=4 -e PHOREPOC_PDK=/src/pdk -e PHOREPOC_CORE=/src/core \
    -e PHOREPOC_BUILD=/src/core/build -e PHOREPOC_ADAPTATIONS=/src/adaptation-x86-64 ghcr.io/phorepoc/web \
    bash -c 'bash /src/cxxrt/build.sh && bash /src/webkit-symbian/build.sh'     # add -e ARCH=arm for ARM
```

(`PHOREPOC_ADAPTATIONS` here names the board's adaptation: the OS tree the Open C compile line is derived from. The pdk, core
and a Symbian source tree must be there, with the include tree exported.)

Output: the archives in `$PHOREPOC_BUILD/thirdparty/{x86,arm}/cxxrt/web/` and `.../lib/`; the work tree (sources, ICU, build
directories, logs) in `$PHOREPOC_BUILD/webkit-symbian-work/`. Then build the components with the layers on
`PHOREPOC_ADAPTATIONS`:

```sh
export PHOREPOC_ADAPTATIONS=<board adaptations>:<tls>:<cxxrt>:<webkit-symbian>
ph component build -b pc phwebtest phebrctl        # ARM: ph component build -b pinephone -arch arm phebrctl_arm
```

`phebrctl` builds to the ROM name `browserengine.dll` and replaces the 2010 engine in the image.

## Licence

Our files are BSD-2-Clause (see `LICENSE`), copyright The Phorepoc Authors (`AUTHORS`), the licence WebKit asks of the new
files of a port. WebKit is LGPL-2.1 and BSD-2-Clause; the engine is kept in separate DLLs and its archives are left for
relinking. The libraries the recipes build keep their own licences (`LICENSES/`); `NOTICE` has the list.

Contributions are welcome under the same terms: add yourself to `AUTHORS`.
