/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: BSD-2-Clause */

/* config.h -- the phwebview embedding's prefix header, the shape of Source/WebKit/config.h (WebCore's client side). */
#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H && defined(BUILDING_WITH_CMAKE)
#include "cmakeconfig.h"
#endif

#include <JavaScriptCore/JSExportMacros.h>
#include <WebCore/PlatformExportMacros.h>
#include <pal/ExportMacros.h>

#ifdef __cplusplus
#undef new
#undef delete
#include <wtf/FastMalloc.h>
#include <wtf/TZoneMalloc.h>
#endif
