# Copyright The Phorepoc Authors
# SPDX-License-Identifier: BSD-2-Clause

# phwebview -- the in-process WebCore embedding. Milestone 1: phwebtest
# (in-memory HTML -> layout -> Skia raster). The executable is an ELF the E32 emitter re-links (cxxrt package step); unresolved
# symbols are allowed here (build_webcore.sh linker flags) and their list is the platform-hook census.
add_executable(phwebtest
    PhDatabaseProvider.cpp
    PhIDBServer.cpp
    PhPlatformStrategies.cpp
    phwebtest.cpp
)
# WebCore does not export its header dirs to consumers: named as WebKit2 does (WebKitFS.cmake)
target_include_directories(phwebtest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR} ${WebCore_PRIVATE_FRAMEWORK_HEADERS_DIR} ${WebCore_PRIVATE_HEADERS_DIR} ${PAL_FRAMEWORK_HEADERS_DIR}
    ${WEBCORE_DIR}/platform/graphics/texmap ${THIRDPARTY_DIR}/ANGLE/include)   # (GraphicsContextSkia.h -> SkiaGPUAtlas.h -> BitmapTexture.h, header-only GL)
add_dependencies(phwebtest WebCore_CopyPrivateHeaders)
target_compile_definitions(phwebtest PRIVATE BUILDING_WITH_CMAKE=1 HAVE_CONFIG_H=1 STATICALLY_LINKED_WITH_WebCore STATICALLY_LINKED_WITH_PAL
    STATICALLY_LINKED_WITH_JavaScriptCore STATICALLY_LINKED_WITH_WTF STATICALLY_LINKED_WITH_bmalloc)
target_link_libraries(phwebtest PRIVATE WebCore)

# the BrCtl facade's engine: the same embedding as a service, packaged as an archive
# (package_web.sh -> libphengine.a) that the `phebrctl` registry component (browserengine.dll) links
add_library(phengine STATIC
    PhDatabaseProvider.cpp
    PhIDBServer.cpp
    PhPlatformStrategies.cpp
    PhEngine.cpp
)
target_include_directories(phengine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR} ${WebCore_PRIVATE_FRAMEWORK_HEADERS_DIR} ${WebCore_PRIVATE_HEADERS_DIR} ${PAL_FRAMEWORK_HEADERS_DIR}
    ${WEBCORE_DIR}/platform/graphics/texmap ${THIRDPARTY_DIR}/ANGLE/include)
add_dependencies(phengine WebCore_CopyPrivateHeaders)
target_compile_definitions(phengine PRIVATE BUILDING_WITH_CMAKE=1 HAVE_CONFIG_H=1 STATICALLY_LINKED_WITH_WebCore STATICALLY_LINKED_WITH_PAL
    STATICALLY_LINKED_WITH_JavaScriptCore STATICALLY_LINKED_WITH_WTF STATICALLY_LINKED_WITH_bmalloc)
target_link_libraries(phengine PRIVATE WebCore)
