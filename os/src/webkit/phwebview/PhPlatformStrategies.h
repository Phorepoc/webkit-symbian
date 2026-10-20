/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: BSD-2-Clause */

/*
 * PhPlatformStrategies.h -- the process-wide WebCore:PlatformStrategies of the
 * in-process embedding (upstream, WebKitLegacy's WebPlatformStrategies played this role; WebKit2's lives in the web process).
 * Milestone 1 (in-memory HTML): the loader strategy loads nothing; the curl-backed one (CurlRequest -> ResourceLoader callbacks)
 * replaces it next. No pasteboard yet, no media (ENABLE_VIDEO/WEB_AUDIO are off), blobs minimal.
 */
#pragma once

#include <WebCore/PlatformStrategies.h>

namespace Phorepoc {

class PhPlatformStrategies final : public WebCore::PlatformStrategies {
public:
    static void initialize();

private:
    WebCore::LoaderStrategy* createLoaderStrategy() final;
    WebCore::PasteboardStrategy* createPasteboardStrategy() final;
    WebCore::MediaStrategy* createMediaStrategy() final;
    WebCore::BlobRegistry* createBlobRegistry() final;
};

} // namespace Phorepoc
