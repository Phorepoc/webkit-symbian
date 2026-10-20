# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause

# PlatformSymbian.cmake (WebCore) -- Skia (CPU) + HarfBuzz + the image decoders, the
# curl network backend on OpenSSL 3 as upstream (+ our CurlSSLHandleSymbian.cpp: the ROM's CA bundle; the *MbedTLS.cpp alternatives
# stay copied in, unused), WebCrypto on OpenSSL, PublicSuffixStore pending (NetSurf's libnspsl is linked; the store itself is in platform-symbian/PlatformSymbian.cpp), and the generic platform files the WPE port also uses. Symbian implementations of the
# remaining platform hooks (screen, pasteboard, MIME registry, ...) go to platform/symbian/ as the link census asks for them.
include(platform/Adwaita.cmake)   # USE_THEME_ADWAITA (OptionsSymbian): sources, exported headers, media-control UA scripts
include(platform/ImageDecoders.cmake)
include(platform/OpenSSL.cmake)   # WebCrypto
include(platform/Skia.cmake)

# Our third-party headers as plain -I: they must beat the compiler wrapper's Open C stdapis dirs, which carry OpenSSL 0.9.8 and an old
# sqlite3.h (imported targets give -isystem = searched after every -I). Khronos GL/EGL headers (ANGLE's include dir, header-only: no GL
# context is ever created) + texmap: WebKit's Skia painting code includes the GPU types even for CPU raster.
list(APPEND WebCore_PRIVATE_INCLUDE_DIRECTORIES
    "${PH_DEPS}/include"
    "${WEBCORE_DIR}/platform/graphics/texmap"
    "${THIRDPARTY_DIR}/ANGLE/include"
)

list(APPEND WebCore_PRIVATE_INCLUDE_DIRECTORIES
    "${WEBCORE_DIR}/crypto/openssl"
    "${WEBCORE_DIR}/platform/generic"
    "${WEBCORE_DIR}/platform/graphics/egl"   # GL-free headers Skia code includes (GLFence.h, GLDisplay.h)
    "${WEBCORE_DIR}/platform/graphics/opentype"
    "${WEBCORE_DIR}/platform/network/curl"
    "${WEBCORE_DIR}/platform/symbian"
    "${WEBCORE_DIR}/platform/text/icu"
)

list(APPEND WebCore_SOURCES
    platform/generic/ScrollbarsControllerGeneric.cpp

    platform/graphics/opentype/OpenTypeVerticalData.cpp

    platform/network/curl/AuthenticationChallengeCurl.cpp
    platform/network/curl/CertificateInfoCurl.cpp
    platform/network/curl/CookieJarDB.cpp
    platform/network/curl/CookieStorageCurl.cpp
    platform/network/curl/CookieUtil.cpp
    platform/network/curl/CurlContext.cpp
    platform/network/curl/CurlFormDataStream.cpp
    platform/network/curl/CurlMultipartHandle.cpp
    platform/network/curl/CurlProxySettings.cpp
    platform/network/curl/CurlRequest.cpp
    platform/network/curl/CurlRequestScheduler.cpp
    platform/network/curl/CurlSSLHandle.cpp
    platform/network/curl/CurlSSLHandleSymbian.cpp
    platform/network/curl/ResourceHandleSymbian.cpp
    platform/network/curl/CurlSSLVerifier.cpp
    platform/network/curl/CurlStream.cpp
    platform/network/curl/CurlStreamScheduler.cpp
    platform/network/curl/DNSResolveQueueCurl.cpp
    platform/network/curl/NetworkStorageSessionCurl.cpp
    platform/network/curl/OpenSSLHelper.cpp
    platform/network/curl/ProtectionSpaceCurl.cpp
    platform/network/curl/ResourceErrorCurl.cpp
    platform/network/curl/ResourceRequestCurl.cpp
    platform/network/curl/ResourceResponseCurl.cpp

    platform/text/Hyphenation.cpp
    platform/text/LocaleICU.cpp

    platform/unix/LoggingUnix.cpp
    platform/unix/SharedMemoryUnix.cpp
)

list(APPEND WebCore_PRIVATE_FRAMEWORK_HEADERS
    platform/network/curl/AuthenticationChallenge.h
    platform/network/curl/CertificateInfo.h
    platform/network/curl/CookieJarDB.h
    platform/network/curl/CookieUtil.h
    platform/network/curl/CurlContext.h
    platform/network/curl/CurlFormDataStream.h
    platform/network/curl/CurlMultipartHandle.h
    platform/network/curl/CurlMultipartHandleClient.h
    platform/network/curl/CurlProxySettings.h
    platform/network/curl/CurlRequest.h
    platform/network/curl/CurlRequestClient.h
    platform/network/curl/CurlRequestScheduler.h
    platform/network/curl/CurlRequestSchedulerClient.h
    platform/network/curl/CurlResponse.h
    platform/network/curl/CurlSSLHandle.h
    platform/network/curl/CurlSSLVerifier.h
    platform/network/curl/CurlStream.h
    platform/network/curl/CurlStreamScheduler.h
    platform/network/curl/DNSResolveQueueCurl.h
    platform/network/curl/OpenSSLHelper.h
    platform/network/curl/ProtectionSpaceCurl.h
    platform/network/curl/ResourceError.h
    platform/network/curl/ResourceRequest.h
    platform/network/curl/ResourceResponse.h
)

list(APPEND WebCore_LIBRARIES
    CURL::libcurl
    LibXml2::LibXml2
    OpenSSL::Crypto
    OpenSSL::SSL
    SQLite3::SQLite3
    ZLIB::ZLIB
)

# ---- Platform hooks (link census). Upstream files that work as-is on this port + our (platform/symbian).
# New sources get per-source include dirs: WebCore's own include list (and so every existing compile line) stays unchanged.
set(PH_PLATFORM_SOURCES
    accessibility/playstation/AccessibilityObjectPlayStation.cpp

    platform/generic/KeyedDecoderGeneric.cpp
    platform/generic/KeyedEncoderGeneric.cpp
    platform/network/playstation/NetworkStateNotifierPlayStation.cpp
    platform/wpe/PlatformScreenWPE.cpp


    platform/symbian/AXObjectCacheSymbian.cpp
    platform/symbian/EditorSymbian.cpp
    platform/symbian/GLStubsSymbian.cpp
    platform/symbian/PasteboardSymbian.cpp
    platform/symbian/PlatformSymbian.cpp
)
list(APPEND WebCore_SOURCES ${PH_PLATFORM_SOURCES})
set_source_files_properties(${PH_PLATFORM_SOURCES} PROPERTIES INCLUDE_DIRECTORIES
    "${WEBCORE_DIR}/accessibility/playstation")

# C++ thread_local: an E32 image has no TLS segment and no TPOFF relocations, and this x86-64 GCC has no -femulated-tls. The few
# sources using it get -Dthread_local= (plain statics): GPU/GL-only code (AtlasTextOp, PlatformDisplaySkia), SkStrikeCache's
# off-by-default per-thread cache guard, the SkSL pool (attached per shader compile), and ComplexTextControllerSkia's shaping
# scratch buffer -- the one that matters if two threads shape text at once (bug-backlog "Browser"). A global flag would
# recompile everything; package_web.sh fails if a new TPOFF/TLSGD relocation appears, naming the object to add here.
set_source_files_properties(
    ${WebCore_DERIVED_SOURCES_DIR}/unified-sources/UnifiedSource-platform-56.cpp   # ComplexTextControllerSkia.cpp
    platform/graphics/skia/PlatformDisplaySkia.cpp
    PROPERTIES COMPILE_OPTIONS "-Dthread_local=")
set_source_files_properties(
    ${THIRDPARTY_DIR}/skia/src/core/SkStrikeCache.cpp
    ${THIRDPARTY_DIR}/skia/src/sksl/SkSLPool.cpp
    ${THIRDPARTY_DIR}/skia/src/gpu/ganesh/ops/AtlasTextOp.cpp
    TARGET_DIRECTORY Skia PROPERTIES COMPILE_OPTIONS "-Dthread_local=")
