#!/usr/bin/env python3
# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause

"""patch.py <webkit-src> -- idempotent text edits of the WebKit 2.54.1 tree for Phorepoc (our; the tree is BSD/LGPL, unmodified elsewhere).
Each edit: (file, marker, old, new). `marker` (a string in `new`) makes a re-run a no-op."""
import sys, re, os
SRC = sys.argv[1] + "/Source/"
E = []
def edit(f, old, new):
    E.append((f, old, new))

edit("WTF/wtf/AvailableMemory.cpp", '#elif OS(UNIX) || OS(HAIKU)\n    long pages = sysconf(_SC_PHYS_PAGES);',
     '#elif defined(PHOREPOC)\n    return ph_total_ram_bytes();\n#elif OS(UNIX) || OS(HAIKU)\n    long pages = sysconf(_SC_PHYS_PAGES);')
edit("WTF/wtf/AvailableMemory.cpp", '#include "config.h"\n', '#include "config.h"\n#if defined(PHOREPOC)\n#include <wtf/symbian/PhSymbian.h>\n#endif\n')
edit("WTF/wtf/StackBounds.cpp", '#else // !OS(OPENBSD)\n',
     '#elif defined(PHOREPOC)\n\n// Phorepoc: Open C has no pthread_getattr_np; the calling thread\'s stack comes from RThread::StackInfo (phsym.cpp).\n'
     'StackBounds StackBounds::newThreadStackBounds(PlatformThreadHandle)\n{\n    void* origin = nullptr;\n    void* bound = nullptr;\n    ph_current_stack_bounds(&origin, &bound);\n    return StackBounds { origin, bound };\n}\n\n'
     '#else // !OS(OPENBSD)\n')
edit("WTF/wtf/StackBounds.cpp", 'StackBounds StackBounds::currentThreadStackBoundsInternal()\n{\n    auto ret = newThreadStackBounds(pthread_self());',
     'StackBounds StackBounds::currentThreadStackBoundsInternal()\n{\n#if defined(PHOREPOC)\n    return newThreadStackBounds(pthread_self());\n#endif\n    auto ret = newThreadStackBounds(pthread_self());')
edit("WTF/wtf/StackBounds.cpp", '#include "config.h"\n', '#include "config.h"\n#if defined(PHOREPOC)\n#include <wtf/symbian/PhSymbian.h>\n#endif\n')

edit("WTF/wtf/StackPointer.h", '    __asm__ volatile ("movq %%rsp, %0" : "=r"(stackPointer) ::);',
     '#if defined(__ILP32__)   // x32: pointers are 32 bits, rsp is 64\n    uint64_t stackPointerRegister = 0;\n    __asm__ volatile ("movq %%rsp, %0" : "=r"(stackPointerRegister) ::);\n    stackPointer = reinterpret_cast<void*>(static_cast<uintptr_t>(stackPointerRegister));\n#else\n    __asm__ volatile ("movq %%rsp, %0" : "=r"(stackPointer) ::);\n#endif')
edit("JavaScriptCore/heap/BlockDirectory.cpp", '#if OS(UNIX) && !PLATFORM(PLAYSTATION) && !OS(QNX) && !OS(HAIKU)\n    size_t pageSize = WTF::pageSize();',
     '#if OS(UNIX) && !PLATFORM(PLAYSTATION) && !OS(QNX) && !OS(HAIKU) && !defined(PHOREPOC)   // no mincore(): every heap page is resident\n    size_t pageSize = WTF::pageSize();')
edit("JavaScriptCore/runtime/JSCBytecodeCacheVersion.cpp", '#if OS(UNIX)\n#include <dlfcn.h>', '#if OS(UNIX) && !defined(PHOREPOC)\n#include <dlfcn.h>')
edit("JavaScriptCore/runtime/JSCBytecodeCacheVersion.cpp", '#elif OS(UNIX) && !PLATFORM(PLAYSTATION) && !OS(HAIKU) && !OS(QNX)\n        auto result', '#elif OS(UNIX) && !PLATFORM(PLAYSTATION) && !OS(HAIKU) && !OS(QNX) && !defined(PHOREPOC)   // no ELF build-id on E32 images\n        auto result')
edit("JavaScriptCore/runtime/JSBigInt.cpp", '#if CPU(X86_64)\n    Digit quotient;\n    Digit rem;\n    __asm__("divq', '#if CPU(X86_64) && !defined(__ILP32__)   // x32: Digit is 32 bits, divq wants rax/rdx-sized operands -> portable code below\n    Digit quotient;\n    Digit rem;\n    __asm__("divq')
# bmalloc system-malloc mode -> our allocator (aligned + free()-able; libphcompat phmem.c). Replacements are file-wide.
SUBS = [("::malloc(", "::ph_malloc("), ("::calloc(", "::ph_calloc("), ("::realloc(", "::ph_realloc("), ("::free(", "::ph_free("),
        ("::aligned_alloc(", "::ph_memalign(")]
for f in ("bmalloc/bmalloc/bmalloc.h", "bmalloc/bmalloc/bmalloc.cpp", "bmalloc/bmalloc/SystemHeap.cpp"):
    E.append((f, "@SUBS@", SUBS))
edit("WTF/wtf/posix/ThreadingPOSIX.cpp", '    case StackAllocationSpecification::Kind::Default:\n        break;',
     '    case StackAllocationSpecification::Kind::Default:\n#if defined(PHOREPOC)\n        pthread_attr_setstacksize(&attr, 512 * 1024);   // Open C\'s default thread stack is tiny\n#endif\n        break;')
edit("WTF/wtf/posix/ThreadingPOSIX.cpp", '    bool signalIsInstalled = attemptToSetSignal(g_wtfConfig.sigThreadSuspendResume);\n    RELEASE_ASSERT(signalIsInstalled);',
     '#if defined(PHOREPOC)   // Open C sigaction() rejects SIGUSR1; no signal is ever delivered cross-thread here (Thread::suspend is unsupported)\n    (void)attemptToSetSignal;\n#else\n    bool signalIsInstalled = attemptToSetSignal(g_wtfConfig.sigThreadSuspendResume);\n    RELEASE_ASSERT(signalIsInstalled);\n#endif')
# Clocks: Open C's clock_gettime ticks in whole seconds; WTF's generic MonotonicTime::now and the wall clock are built on it ->
# timers / performance.now / Date.now in 1 s steps (WPT relevant-mutations flaked). The nanokernel tick instead (phsym.cpp).
edit("WTF/wtf/CurrentTime.cpp", '#include "config.h"\n', '#include "config.h"\n#if defined(PHOREPOC)\n#include <wtf/symbian/PhSymbian.h>\n#endif\n')
edit("WTF/wtf/CurrentTime.cpp", 'static inline double currentTime()\n{\n    struct timespec ts { };\n    clock_gettime(CLOCK_REALTIME, &ts);',
     'static inline double currentTime()\n{\n#if defined(PHOREPOC)\n    return ph_wall_seconds();\n#endif\n    struct timespec ts { };\n    clock_gettime(CLOCK_REALTIME, &ts);')
# ARM LLInt (the PinePhone): offlineasm's `globaladdr` exists for Darwin (movw/movt + non-lazy pointer) and Linux (GOT-relative,
# PIC) only. E32 images are not PIC and relocate absolute 32-bit words only (no MOVW/MOVT relocations): a literal word holding the
# symbol's address, emitted at the next label like Linux's GOT words, loaded pc-relative
edit("JavaScriptCore/offlineasm/arm.rb", '            # Throw a compiler error everywhere else.\n            $asm.putStr("#else")\n            $asm.putStr("#error Missing globaladdr implementation")',
     '            $asm.putStr("#elif defined(PHOREPOC)")\n            $asm.puts "ldr #{dest.armOperand}, #{gotLabel}"\n\n            # Throw a compiler error everywhere else.\n            $asm.putStr("#else")\n            $asm.putStr("#error Missing globaladdr implementation")')
edit("JavaScriptCore/offlineasm/arm.rb", '                $asm.puts ".word #{labelRef.asmLabel}(GOT)"\n',
     '                $asm.puts ".word #{labelRef.asmLabel}(GOT)"\n                $asm.putStr("#elif defined(PHOREPOC)")\n                $asm.puts ".p2align 2"\n                $asm.puts "#{gotLabel}:"\n                $asm.puts ".word #{labelRef.asmLabel}"\n')
# (the .p2align: the E32 loader relocates whole aligned words only -- Thumb code leaves the label at any halfword: 382 refused
# relocations once the JIT build switched the LLInt from the C loop to offlineasm; the pad is a Thumb nop)
# ARM LLInt: the ELF local-label / hidden-symbol syntax is keyed on an OS list without a plain OS(UNIX); ours is ELF + GNU as
edit("WTF/wtf/InlineASM.h", '    || OS(QNX)                 \\\n    || OS(WINDOWS)\n',
     '    || OS(QNX)                 \\\n    || OS(WINDOWS)             \\\n    || defined(PHOREPOC)\n')
edit("WTF/wtf/InlineASM.h", '    || OS(OPENBSD)             \\\n    || OS(QNX)\n    // ELF platform',
     '    || OS(OPENBSD)             \\\n    || OS(QNX)                 \\\n    || defined(PHOREPOC)\n    // ELF platform')
# ARM LLInt: each opcode gets a `.size name, . - name` (offlineasm, ELF) but the `name:` label itself only under OS(LINUX)
edit("JavaScriptCore/llint/LowLevelInterpreter.cpp", '#if OS(LINUX)\n#define OFFLINE_ASM_OPCODE_DEBUG_LABEL(label)  #label ":\\n"',
     '#if OS(LINUX) || defined(PHOREPOC)\n#define OFFLINE_ASM_OPCODE_DEBUG_LABEL(label)  #label ":\\n"')
# libpas (bmalloc headers, ARM32 branch): PAS_ASSERT_IF exists only for Darwin/Linux (upstream); the plain assert says the same
edit("bmalloc/libpas/src/libpas/pas_utils.h", '#elif PAS_ARM32\n    PAS_ASSERT_IF(true, !"Should not be reached");',
     '#elif PAS_ARM32\n#ifndef PAS_ASSERT_IF   /* PHOREPOC: the first ARM32 use; the later ones follow */\n#define PAS_ASSERT_IF(cond, exp, ...) PAS_ASSERT(exp)\n#endif\n    PAS_ASSERT_IF(true, !"Should not be reached");')
# ARM JIT (the PinePhone): JSC flushes freshly written code with Linux's cacheflush syscall; ours is the IMB_Range exec call (phsym.cpp)
edit("JavaScriptCore/assembler/ARMv7Assembler.h", '#elif OS(LINUX)\n        size_t page = pageSize();',
     '#elif defined(PHOREPOC)\n        ph_cache_flush(code, size);\n#elif OS(LINUX)\n        size_t page = pageSize();')
edit("JavaScriptCore/assembler/ARMv7Assembler.h", '#include <wtf/Vector.h>\n',
     '#include <wtf/Vector.h>\n#if defined(PHOREPOC)\n#include <wtf/symbian/PhSymbian.h>\n#endif\n')
edit("WTF/wtf/CurrentTime.cpp", '#else\n    static double lastTime = 0;\n    double currentTimeNow = currentTime();',
     '#elif defined(PHOREPOC)\n    return fromRawSeconds(ph_monotonic_seconds());\n#else\n    static double lastTime = 0;\n    double currentTimeNow = currentTime();')

# RandomDevice: Open C has no /dev/urandom (open "succeeds", every read fails -> crashUnableToReadFromURandom); the kernel's
# CSPRNG through arc4random_buf (compat/phmisc.c -> phsym.cpp ph_random_bytes -> Math::Random(TDes8&))
edit("WTF/wtf/RandomDevice.cpp", '#if OS(FUCHSIA)\n#include <zircon/syscalls.h>\n#endif\n',
     '#if OS(FUCHSIA)\n#include <zircon/syscalls.h>\n#endif\n\n#if defined(PHOREPOC)\nextern "C" void arc4random_buf(void*, size_t);   // libphcompat (compat/phmisc.c)\n#endif\n')
edit("WTF/wtf/RandomDevice.cpp", '    int ret = 0;\n    do {\n        ret = open("/dev/urandom", O_RDONLY, 0);',
     '#if defined(PHOREPOC)\n    m_fd = -1;   // Phorepoc: no /dev/urandom; cryptographicallyRandomValues uses arc4random_buf (the kernel CSPRNG)\n    return;\n#endif\n    int ret = 0;\n    do {\n        ret = open("/dev/urandom", O_RDONLY, 0);')
edit("WTF/wtf/RandomDevice.cpp", '#elif OS(UNIX)\n    ssize_t amountRead = 0;',
     '#elif defined(PHOREPOC)\n    arc4random_buf(buffer.data(), buffer.size());\n#elif OS(UNIX)\n    ssize_t amountRead = 0;')
edit("WTF/wtf/RandomDevice.cpp", 'RandomDevice::~RandomDevice()\n{\n    close(m_fd);',
     'RandomDevice::~RandomDevice()\n{\n    if (m_fd >= 0)\n        close(m_fd);')
# ---- WebCore port . The curl backend's TLS helpers speak OpenSSL; with USE(MBEDTLS) the same interfaces
# are implemented on mbedTLS (webkit/webcore/*MbedTLS.cpp): only the types in the shared headers and the curl backend id change.
edit("WebCore/platform/network/curl/OpenSSLHelper.h", '#include "CertificateInfo.h"\n#include <openssl/ssl.h>\n',
     '#include "CertificateInfo.h"\n#if USE(MBEDTLS)   // Phorepoc: OpenSSLHelperMbedTLS.cpp\n#include <mbedtls/ssl.h>\nusing SSL = mbedtls_ssl_context;\n'
     'using X509_STORE_CTX = mbedtls_x509_crt;\n#else\n#include <openssl/ssl.h>\n#endif\n')
edit("WebCore/platform/network/curl/CurlSSLHandle.h", '#include <openssl/crypto.h>\n',
     '#if !USE(MBEDTLS)\n#include <openssl/crypto.h>\n#endif\n')
edit("WebCore/platform/network/curl/CurlSSLVerifier.h", '    static int verifyCallback(int, X509_STORE_CTX*);\n    void collectInfo(X509_STORE_CTX*);\n\n    CertificateInfo::CertificateChain m_certificateChain;\n',
     '#if !USE(MBEDTLS)   // Phorepoc: curl\'s mbedTLS backend owns the verify callback (CurlSSLVerifierMbedTLS.cpp)\n    static int verifyCallback(int, X509_STORE_CTX*);\n'
     '    void collectInfo(X509_STORE_CTX*);\n\n    CertificateInfo::CertificateChain m_certificateChain;\n#endif\n')
edit("WebCore/platform/network/curl/CurlContext.cpp", '    if (!info || info->backend != CURLSSLBACKEND_OPENSSL || !info->internals)\n',
     '#if USE(MBEDTLS)\n    if (!info || info->backend != CURLSSLBACKEND_MBEDTLS || !info->internals)\n#else\n'
     '    if (!info || info->backend != CURLSSLBACKEND_OPENSSL || !info->internals)\n#endif\n')
edit("cmake/WebKitCommon.cmake", '        PlayStation\n        WPE\n        Win\n    )\n', '        PlayStation\n        Symbian\n        WPE\n        Win\n    )\n')
# Skia: no fontconfig (the font manager scans a directory: the ROM's fonts), no GL (CPU raster only)
edit("ThirdParty/skia/CMakeLists.txt", 'if (NOT WIN32 AND NOT PLAYSTATION)\n    find_package(Freetype 2.9.0 REQUIRED)',
     'if (NOT WIN32 AND NOT PLAYSTATION AND NOT PHOREPOC)\n    find_package(Freetype 2.9.0 REQUIRED)')
edit("ThirdParty/skia/CMakeLists.txt", '        target_link_libraries(Skia PRIVATE EXPAT::EXPAT android log)\n    else ()\n',
     '        target_link_libraries(Skia PRIVATE EXPAT::EXPAT android log)\n    elseif (PHOREPOC)   # Symbian: fonts from a directory (FontCacheSkia.cpp)\n'
     '        target_sources(Skia PRIVATE\n            src/ports/SkFontMgr_custom.cpp\n            src/ports/SkFontMgr_custom_directory.cpp\n'
     # (+ Skia sources the list only builds for other configurations: referenced by WebKit's Skia code -- link census)
     '            src/ports/SkLog_stdio.cpp\n            src/core/SkPathDump.cpp\n            src/core/SkStrikeRef.cpp\n'
     '            src/encode/SkICC.cpp\n            src/utils/SkOSPath.cpp\n        )\n    else ()\n')
edit("ThirdParty/skia/CMakeLists.txt", 'if (USE_LIBEPOXY)\n    target_sources(Skia PRIVATE src/gpu/ganesh/gl/epoxy/GrGLMakeEpoxyEGLInterface.cpp)',
     'if (PHOREPOC)   # no GL: CPU raster only\n    target_sources(Skia PRIVATE src/gpu/ganesh/gl/GrGLMakeNativeInterface_none.cpp)\n'
     'elseif (USE_LIBEPOXY)\n    target_sources(Skia PRIVATE src/gpu/ganesh/gl/epoxy/GrGLMakeEpoxyEGLInterface.cpp)')
# Skia on Open C: no malloc.h / malloc_usable_size (the requested size is the capacity), no locale_t (setlocale path),
# IJG libjpeg 9's `boolean` is an enum (Skia assumes libjpeg-turbo's int)
edit("ThirdParty/skia/src/ports/SkMemory_malloc.cpp", '#elif defined(SK_BUILD_FOR_ANDROID) || defined(SK_BUILD_FOR_UNIX)\n#include <malloc.h>',
     '#elif defined(PHOREPOC)   // Open C: no malloc.h\n#elif defined(SK_BUILD_FOR_ANDROID) || defined(SK_BUILD_FOR_UNIX)\n#include <malloc.h>')
edit("ThirdParty/skia/src/ports/SkMemory_malloc.cpp", '    #elif defined(SK_BUILD_FOR_UNIX)\n        completeSize = malloc_usable_size(addr);',
     '    #elif defined(PHOREPOC)   // no malloc_usable_size: the requested size\n    #elif defined(SK_BUILD_FOR_UNIX)\n        completeSize = malloc_usable_size(addr);')
edit("ThirdParty/skia/src/core/SkAutoLocaleSetter.h", '#if defined(SK_BUILD_FOR_ANDROID) || defined(__UCLIBC__) || defined(_NEWLIB_VERSION)\n',
     '#if defined(SK_BUILD_FOR_ANDROID) || defined(__UCLIBC__) || defined(_NEWLIB_VERSION) || defined(PHOREPOC)   // Open C: setlocale only\n')
edit("ThirdParty/skia/src/codec/SkJpegDecoderMgr.cpp", '        src->bytes_in_buffer = 0;\n        return false;\n    }\n    return true;\n}',
     '        src->bytes_in_buffer = 0;\n        return FALSE;   // Phorepoc: IJG libjpeg 9 boolean is an enum\n    }\n    return TRUE;\n}')
edit("WebCore/platform/graphics/skia/FontCacheSkia.cpp", '#else\n#include <skia/ports/SkFontMgr_fontconfig.h>\n#endif\n',
     '#elif defined(PHOREPOC)\n#include <skia/ports/SkFontMgr_directory.h>\n#else\n#include <skia/ports/SkFontMgr_fontconfig.h>\n#endif\n')
edit("WebCore/platform/graphics/skia/FontCacheSkia.cpp", '#else\n        m_fontManager = SkFontMgr_New_FontConfig(',
     '#elif defined(PHOREPOC)   // the ROM\'s fonts (Z:\\resource\\fonts)\n        m_fontManager = SkFontMgr_New_Custom_Directory("z:/resource/fonts/");\n#else\n'
     '        m_fontManager = SkFontMgr_New_FontConfig(')
# Generic CSS families: fontconfig's aliases map serif/sans-serif/monospace to real faces; the directory font manager has none, so
# matchFamilyStyle("sans-serif") found nothing and every text run measured 0x0 (first QEMU run). Map them to the ROM's families.
edit("WebCore/platform/graphics/skia/FontCacheSkia.cpp",
     '    auto familyName = getFamilyNameStringFromFamily(family);\n',
     '    auto familyName = getFamilyNameStringFromFamily(family);\n'
     '#if defined(PHOREPOC)\n'
     '    if (familyName == "monospace"_s)\n'
     '        familyName = "Ubuntu Mono"_s;\n'
     '    else if (familyName == "serif"_s || familyName == "sans-serif"_s || familyName == "cursive"_s || familyName == "fantasy"_s\n'
     '        || familyName == "system-ui"_s || familyName == "math"_s || familyName.startsWith("-webkit-"_s))\n'
     '        familyName = "Series 60 Sans"_s;\n'
     '#endif\n')
# System fallback fonts without fontconfig: ask the font manager (Skia's directory scan of the ROM fonts, FontCacheSkia.cpp) for a face
# covering the cluster's first non-ignorable character
edit("WebCore/platform/graphics/skia/SkiaSystemFallbackFontCache.cpp",
     '#if USE(SKIA) && !OS(ANDROID) && !PLATFORM(WIN)\n#include "FontCache.h"\n#include <fontconfig/fontconfig.h>\n',
     '#if USE(SKIA) && defined(PHOREPOC)   // Phorepoc: no fontconfig\n#include "FontCache.h"\n#include <wtf/text/CharacterProperties.h>\n\n'
     'namespace WebCore {\n\nclass FontSetCache { };\n\nSkiaSystemFallbackFontCache::SkiaSystemFallbackFontCache() = default;\n\n'
     'SkiaSystemFallbackFontCache::~SkiaSystemFallbackFontCache() = default;\n\n'
     'sk_sp<SkTypeface> SkiaSystemFallbackFontCache::fontForCharacterCluster(const String& locale, StringView stringView)\n{\n'
     '    for (char32_t character : stringView.codePoints()) {\n        if (isDefaultIgnorableCodePoint(character))\n            continue;\n'
     '        auto key = std::pair<String, int> { locale.isNull() ? emptyString() : locale, static_cast<int>(character) };\n'
     '        return m_typefaceCache.ensure(key, [&] -> sk_sp<SkTypeface> {\n            CString bcp47 = locale.utf8();\n'
     '            const char* languages[] = { bcp47.data() };\n'
     '            return FontCache::forCurrentThread().fontManager().matchFamilyStyleCharacter(nullptr, SkFontStyle(), locale.isEmpty() ? nullptr : languages, locale.isEmpty() ? 0 : 1, character);\n'
     '        }).iterator->value;\n    }\n    return nullptr;\n}\n\n'
     'void SkiaSystemFallbackFontCache::clear()\n{\n    m_cache.clear();\n    m_typefaceCache.clear();\n}\n\n} // namespace WebCore\n\n'
     '#elif USE(SKIA) && !OS(ANDROID) && !PLATFORM(WIN)\n#include "FontCache.h"\n#include <fontconfig/fontconfig.h>\n')
# Glyph fallback by coverage: SkFontMgr_Custom::onMatchFamilyStyleCharacter is `return nullptr` upstream, so a character the
# page's face lacks (Arabic, Hebrew, CJK ...) could never find another face in the ROM's font directory -> boxes. Search the directory's
# faces (the scan is recursive: Resource\Fonts\web\ holds the Noto script faces) for the first covering the character.
edit("ThirdParty/skia/src/ports/SkFontMgr_custom.cpp",
     "    const char familyName[], const SkFontStyle&,\n    const char* bcp47[], int bcp47Count,\n    SkUnichar) const\n{\n    return nullptr;\n}",
     "    const char familyName[], const SkFontStyle& fontStyle,\n    const char* bcp47[], int bcp47Count,\n    SkUnichar character) const\n{\n"
     "#if defined(PHOREPOC)\n    if (character > 0) {\n"
     "        for (int i = 0; i < fFamilies.size(); ++i) {\n            sk_sp<SkTypeface> t = fFamilies[i]->matchStyle(fontStyle);\n"
     "            if (t && t->unicharToGlyph(character) != 0)\n                return t;\n        }\n"
     "        for (int i = 0; i < fFamilies.size(); ++i) {\n            for (int j = 0; j < fFamilies[i]->fStyles.size(); ++j) {\n"
     "                if (fFamilies[i]->fStyles[j]->unicharToGlyph(character) != 0)\n                    return fFamilies[i]->fStyles[j];\n            }\n        }\n    }\n"
     "#endif\n    return nullptr;\n}")
# The web-script faces live OUTSIDE Resource\Fonts (the S60 font store / rasteriser scans that tree and a boot with a CJK OTF there stalled SSM,
# phone boot 675): the engine's own directory z:/resource/phweb/fonts/, scanned after the system one (appended: never the default family).
edit("ThirdParty/skia/src/ports/SkFontMgr_custom_directory.cpp",
     '        load_directory_fonts(scanner, fBaseDirectory, ".pfb", families);\n',
     '        load_directory_fonts(scanner, fBaseDirectory, ".pfb", families);\n'
     '#if defined(PHOREPOC)\n        {\n            const SkString webDir("z:/resource/phweb/fonts/");\n'
     '            load_directory_fonts(scanner, webDir, ".ttf", families);\n            load_directory_fonts(scanner, webDir, ".otf", families);\n        }\n#endif\n')
edit("WebCore/platform/network/DNS.cpp",'#if OS(QNX)\n#include <sys/socket.h>', '#if OS(QNX) || defined(PHOREPOC)   // Open C: AF_INET* are in sys/socket.h\n#include <sys/socket.h>')
edit("WebCore/page/Page.cpp", '#if USE(SKIA) && !PLATFORM(WIN) && (!PLATFORM(WPE) || ENABLE(WPE_PLATFORM))\n    if (settings().hardwareAccelerationEnabled())',
     '#if USE(SKIA) && !PLATFORM(WIN) && (!PLATFORM(WPE) || ENABLE(WPE_PLATFORM)) && !defined(PHOREPOC)   // no GPU\n    if (settings().hardwareAccelerationEnabled())')
# video-codecs/BitReader.h by relative path (an include dir would change every WebCore compile line)
edit("WebCore/platform/graphics/VP9Utilities.cpp", '#include "BitReader.h"', '#include "../video-codecs/BitReader.h"')
edit("WebCore/platform/graphics/AV1Utilities.cpp", '#include "BitReader.h"', '#include "../video-codecs/BitReader.h"')
edit("WebCore/platform/graphics/iso/ISOTrackEncryptionBox.cpp", '#include "BitReader.h"', '#include "../../video-codecs/BitReader.h"')
# Accessibility: no AT bridge on Symbian yet -> the plain RefCounted wrapper of the PlayStation/Haiku ports
edit("WebCore/accessibility/AXCoreObject.h", '#elif PLATFORM(PLAYSTATION) || PLATFORM(HAIKU)\nclass AccessibilityObjectWrapper',
     '#elif PLATFORM(PLAYSTATION) || PLATFORM(HAIKU) || defined(PHOREPOC)\nclass AccessibilityObjectWrapper')
edit("WebCore/accessibility/AXCoreObject.h", '#elif PLATFORM(PLAYSTATION) || PLATFORM(HAIKU)\n    RefPtr<AccessibilityObjectWrapper> m_wrapper;',
     '#elif PLATFORM(PLAYSTATION) || PLATFORM(HAIKU) || defined(PHOREPOC)\n    RefPtr<AccessibilityObjectWrapper> m_wrapper;')
# The in-process network path. ResourceHandle under USE(CURL) is upstream a block of ASSERT_NOT_REACHED
# stubs (only the network process drives CurlRequest); our webcore/ResourceHandleSymbian.cpp implements it on CurlRequest.
edit("WebCore/platform/network/ResourceHandle.cpp", '#if USE(SOUP) || USE(CURL)\nResourceHandleInternal::~ResourceHandleInternal() = default;',
     '#if USE(SOUP) || (USE(CURL) && !defined(PHOREPOC))   // Phorepoc: ResourceHandleSymbian.cpp\nResourceHandleInternal::~ResourceHandleInternal() = default;')
# ... and an embedding frame-loader client (phwebview PhFrameLoaderClient) derives from the empty one: the methods it overrides
# (navigation/response policy, MIME support, load-finished notifications) must not be final
for m in ("dispatchDidFailProvisionalLoad(const ResourceError&, WillContinueLoading, WillInternallyHandleFailure)",
          "dispatchDidFailLoad(const ResourceError&)", "dispatchDidFinishLoad()",
          "dispatchDecidePolicyForResponse(const ResourceResponse&, const ResourceRequest&, const String&, FramePolicyFunction&&)",
          "dispatchDecidePolicyForNewWindowAction(const NavigationAction&, const ResourceRequest&, FormState*, const String&, std::optional<HitTestResult>&&, FramePolicyFunction&&)",
          "dispatchDecidePolicyForNavigationAction(const NavigationAction&, const ResourceRequest&, const ResourceResponse& redirectResponse, FormState*, const String&, std::optional<NavigationIdentifier>, std::optional<HitTestResult>&&, bool, NavigationUpgradeToHTTPSBehavior, SandboxFlags, PolicyDecisionMode, FramePolicyFunction&&)",
          "committedLoad(DocumentLoader*, const SharedBuffer&)", "createFrame(const AtomString&, HTMLFrameOwnerElement&)", "transitionToCommittedForNewPage(InitializingIframe)", "canHandleRequest(const ResourceRequest&) const", "canShowMIMEType(const String&) const", "canShowMIMETypeAsHTML(const String&) const"):
    edit("WebCore/loader/EmptyFrameLoaderClient.h", m + " final;", m + " override;")
# the BrCtl engine's ChromeClient (phwebview PhChromeClient) derives from EmptyChromeClient: keyboard focus
# must reach links (KeyboardAccessTabsToLinks) and repaints must be seen
edit("WebCore/loader/EmptyClients.h", "    KeyboardUIMode keyboardUIMode() final { return KeyboardAccessDefault; }", "    KeyboardUIMode keyboardUIMode() override { return KeyboardAccessDefault; }")
# a failed transfer prints curl's error buffer (the ResourceError only carries curl_easy_strerror's generic text: "SSL connect error")
edit("WebCore/platform/network/curl/CurlContext.h", "    static const String errorDescription(CURLcode);\n",
     "    static const String errorDescription(CURLcode);\n    const char* phErrorBuffer() const { return m_errorBuffer; }   // PHOREPOC: CurlRequest's failure line\n")
edit("WebCore/platform/network/curl/CurlRequest.cpp", "        auto resourceError = ResourceError(result, m_request.url(), type);\n",
     "        auto resourceError = ResourceError(result, m_request.url(), type);\n#if defined(PHOREPOC)\n"
     "        // (stdio: <cstdio> via the include edit below)\n"
     "        printf(\"curl: %s failed %d: %s\\n\", m_request.url().string().latin1().data(), static_cast<int>(result), m_curlHandle ? m_curlHandle->phErrorBuffer() : \"\");\n#endif\n")
edit("WebCore/platform/network/curl/CurlRequest.cpp", '#include "ResourceError.h"\n', '#include "ResourceError.h"\n#include <cstdio>   // PHOREPOC\n')
# the curl thread sleeps in curl_multi_poll(INT_MAX) and new work wakes it with curl_multi_wakeup -- which needs curl's socketpair,
# and our curl has CURL_DISABLE_SOCKETPAIR: the wake-up was a no-op, so a request queued after the first transfer (example.com's
# s.js) waited forever. Poll in 20 ms slices until curl has a wake-up channel on Open C.
edit("WebCore/platform/network/curl/CurlRequestScheduler.cpp", "        const int selectTimeoutMS = INT_MAX;\n",
     "#if defined(PHOREPOC)\n        const int selectTimeoutMS = 20;   // no curl_multi_wakeup (CURL_DISABLE_SOCKETPAIR)\n#else\n        const int selectTimeoutMS = INT_MAX;\n#endif\n")

bad = 0
for f, old, new in E:
    p = SRC + f
    s = orig = open(p, newline="").read()
    if old == "@SUBS@":
        if "ph_malloc(" not in s and "ph_free(" not in s:
            for a, b in new:
                s = re.sub(r"(?<![A-Za-z0-9_>])" + re.escape(a), b, s)
        if "PH_ALLOC_H" not in s:
            i = s.index("#include")
            decl = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "os", "src", "webkit", "symbian", "PhAlloc.h")).read()
            s = s[:i] + decl + "\n" + s[i:]
        if s != orig:   # never touch an unchanged file: bmalloc.h is in nearly every JSC TU (a new mtime = a full rebuild)
            open(p, "w", newline="").write(s)
        continue
    if new in s:
        continue
    if old not in s:
        print("PATCH MISS:", f, repr(old[:60])); bad += 1; continue
    open(p, "w", newline="").write(s.replace(old, new, 1))
sys.exit(1 if bad else 0)
