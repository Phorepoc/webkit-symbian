# webkit-symbian

This is a modern [WebKit](https://webkit.org) running on Symbian. It's WPE WebKit 2.54.1, with current HTML, CSS and
JavaScript and TLS 1.3, sitting behind the browser control API Symbian apps have used since 2010. Apps that show web
content, like Messaging's HTML viewer or Help, keep working unchanged and draw with the new engine.

The engine is ready to embed, but the new browser app on top of it hasn't been designed yet. We're not bringing the 2010
browser's interface forward.

We don't keep a copy of WebKit here. The recipes download the release named in `webkit.lock` and the libraries it
needs, check each one against its SHA-256, and build them for Symbian. This repository holds the port itself and the
recipes.

## Building

Be warned, a full build takes hours, and each compiler needs about 1.5 GB of RAM. On an 8 GB machine, use `JOBS=4`.

WebKit needs [cxxrt](https://github.com/Phorepoc/cxxrt) for its C++ library. For https it also needs the
[tls](https://github.com/Phorepoc/tls) repository, which supplies the certificates the engine trusts. Check out core,
the [pdk](https://github.com/Phorepoc/pdk), cxxrt, tls and this repository side by side, and run
`ph source export -a` once. Then build in the toolchain's web image:

```sh
docker run --rm --cpus=6 -v "$PWD/..:/src" -e JOBS=4 -e PHOREPOC_PDK=/src/pdk -e PHOREPOC_CORE=/src/core \
    -e PHOREPOC_BUILD=/src/core/build ghcr.io/phorepoc/web \
    bash -c 'bash /src/cxxrt/build.sh && bash /src/webkit-symbian/build.sh'
```

Add `-e ARCH=arm` for an ARM device. Once that's done, build the Symbian components:

```sh
export PHOREPOC_ADAPTATIONS=<device repositories>:<tls>:<cxxrt>:<webkit-symbian>
ph component build -b myboard phwebtest phebrctl
```

For an ARM board, that's `ph component build -b myboard -arch arm phebrctl_arm`. `phebrctl` becomes
`browserengine.dll` in the image and replaces the 2010 engine.

## What's in here

- `os/src/webkit/port/` is the Symbian port's build setup. JavaScriptCore runs with a JIT on ARM and an interpreter on
  the PC, and pages are drawn by Skia on the CPU.
- `os/src/webkit/symbian/` and `platform-symbian/` connect WebKit to Symbian's memory, fonts, file types and clipboard.
- `os/src/webkit/webcore/` loads pages over curl, using the certificates in the ROM.
- `os/src/webkit/phwebview/` runs the engine in its own thread inside the app, and `os/src/phebrctl/` puts the 2010
  browser control API in front of it.
- `recipes/` downloads and builds WebKit and the libraries it needs, including ICU, FreeType, HarfBuzz, SQLite,
  OpenSSL and curl.
- `tests/` has two boot tests. One runs a script in the JavaScript shell, and the other loads a web page.

## License

Our files are BSD-2-Clause, the license WebKit asks for in new port files. WebKit itself is LGPL-2.1 and BSD-2-Clause,
and it's kept in its own libraries so you can relink it. The other libraries keep their own licenses, which `NOTICE`
lists.

Contributions are welcome under the same terms. Add your name to `AUTHORS` in your first pull request.
