// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause

/*
 * PhPlatformStrategies.cpp -- see the header. Loads over curl (ResourceHandleSymbian.cpp).
 */
#include "config.h"
#include "PhPlatformStrategies.h"

#include <WebCore/BlobRegistry.h>
#include <WebCore/BlobRegistryImpl.h>
#include <WebCore/CachedResource.h>
#include <WebCore/LoaderStrategy.h>
#include <WebCore/MediaStrategy.h>
#include <WebCore/NetworkLoadMetrics.h>
#include <WebCore/ResourceError.h>
#include <WebCore/ResourceRequest.h>
#include <WebCore/ResourceResponse.h>
#include <WebCore/SubresourceLoader.h>
#include <WebCore/Document.h>
#include <WebCore/FrameLoader.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/LocalFrameInlines.h>   // LocalFrame::document() is inline: without this include it was a link stub
#include <WebCore/ResourceHandle.h>
#include <WebCore/SecurityOrigin.h>
#include <wtf/RunLoop.h>
#include <cstdio>
#include <wtf/NeverDestroyed.h>

namespace Phorepoc {
using namespace WebCore;

static const String& errorDomain()
{
    static NeverDestroyed<String> domain { "Phorepoc"_s };
    return domain;
}

// The WebKitLegacy WebResourceLoadScheduler, minus the per-host queues: every load (main resource and
// subresources) becomes a SubresourceLoader started at once from the run loop; ResourceLoader::start() creates a ResourceHandle,
// which is our on CurlRequest (webcore/ResourceHandleSymbian.cpp). Sync loads: ResourceHandle's synchronous path (libcurl).
class PhLoaderStrategy final : public LoaderStrategy {
public:
    void loadResource(LocalFrame& frame, CachedResource& resource, ResourceRequest&& request, const ResourceLoaderOptions& options, CompletionHandler<void(RefPtr<SubresourceLoader>&&)>&& completion) final
    {
        printf("phweb: load %s\n", request.url().string().utf8().data());
        SubresourceLoader::create(frame, resource, WTF::move(request), options, [completion = WTF::move(completion)](RefPtr<SubresourceLoader>&& loader) mutable {
            printf("phweb: loader %s\n", loader ? "created" : "REFUSED");
            if (loader) {
                RunLoop::mainSingleton().dispatch([loader] {
                    if (!loader->reachedTerminalState())
                        loader->start();
                });
            }
            completion(WTF::move(loader));
        });
    }
    void loadResourceSynchronously(FrameLoader& frameLoader, ResourceLoaderIdentifier, const ResourceRequest& request, ClientCredentialPolicy, const FetchOptions& options, const HTTPHeaderMap&, ResourceError& error, ResourceResponse& response, Vector<uint8_t>& data) final
    {
        RefPtr document = frameLoader.frame().document();
        ResourceHandle::loadResourceSynchronously(frameLoader.networkingContext(), request, options.credentials == FetchOptions::Credentials::Omit ? StoredCredentialsPolicy::DoNotUse : StoredCredentialsPolicy::Use,
            document ? &document->securityOrigin() : nullptr, error, response, data);
    }
    void pageLoadCompleted(Page&) final { }
    void browsingContextRemoved(LocalFrame&) final { }
    void remove(ResourceLoader*) final { }
    void setDefersLoading(ResourceLoader& loader, bool defers) final
    {
        if (!defers && !loader.deferredRequest().isNull()) {
            loader.setRequest(loader.takeDeferredRequest());
            loader.start();
        }
    }
    void crossOriginRedirectReceived(ResourceLoader*, const URL&) final { }
    void servePendingRequests(ResourceLoadPriority) final { }
    void suspendPendingRequests() final { }
    void resumePendingRequests() final { }
    void startPingLoad(LocalFrame&, ResourceRequest&, const HTTPHeaderMap&, const FetchOptions&, ContentSecurityPolicyImposition, PingLoadCompletionHandler&& completion) final
    {
        if (completion)
            completion(ResourceError { ResourceError::Type::Cancellation }, { });
    }
    void preconnectTo(FrameLoader&, ResourceRequest&&, StoredCredentialsPolicy, ShouldPreconnectAsFirstParty, PreconnectCompletionHandler&& completion) final
    {
        if (completion)
            completion(ResourceError { ResourceError::Type::Cancellation });
    }
    void setCaptureExtraNetworkLoadMetricsEnabled(bool) final { }
    bool isOnLine() const final { return true; }
    void addOnlineStateChangeListener(Function<void(bool)>&&) final { }
    void isResourceLoadFinished(CachedResource&, CompletionHandler<void(bool)>&& callback) final { callback(true); }

    ResourceError cancelledError(const ResourceRequest& request) const final { return { errorDomain(), 1, request.url(), "Cancelled"_s, ResourceError::Type::Cancellation }; }
    ResourceError blockedError(const ResourceRequest& request) const final { return { errorDomain(), 2, request.url(), "Blocked"_s }; }
    bool isBlockedError(const ResourceError& error) const final { return error.domain() == errorDomain() && error.errorCode() == 2; }
    ResourceError blockedByContentBlockerError(const ResourceRequest& request) const final { return { errorDomain(), 3, request.url(), "Blocked by content blocker"_s }; }
    ResourceError cannotShowURLError(const ResourceRequest& request) const final { return { errorDomain(), 4, request.url(), "Cannot show URL"_s }; }
    ResourceError interruptedForPolicyChangeError(const ResourceRequest& request) const final { return { errorDomain(), 5, request.url(), "Interrupted by policy change"_s }; }
    ResourceError cannotShowMIMETypeError(const ResourceResponse& response) const final { return { errorDomain(), 7, response.url(), "Cannot show content of this type"_s }; }
    ResourceError fileDoesNotExistError(const ResourceResponse& response) const final { return { errorDomain(), 8, response.url(), "File does not exist"_s }; }
    ResourceError httpsUpgradeRedirectLoopError(const ResourceRequest& request) const final { return { errorDomain(), 9, request.url(), "HTTPS upgrade redirect loop"_s }; }
    ResourceError httpNavigationWithHTTPSOnlyError(const ResourceRequest& request) const final { return { errorDomain(), 10, request.url(), "HTTP navigation with HTTPS-only"_s }; }
    bool isHttpNavigationWithHTTPSOnlyError(const ResourceError& error) const final { return error.domain() == errorDomain() && error.errorCode() == 10; }
    ResourceError pluginWillHandleLoadError(const ResourceResponse& response) const final { return { errorDomain(), 11, response.url(), "Plug-in will handle load"_s }; }
};

class PhMediaStrategy final : public MediaStrategy {
};

// Blob URLs: WebCore's own BlobRegistryImpl, as WebKitLegacy's WebBlobRegistry -- its first registration installs the "blob"
// ResourceHandle constructor (BlobResourceHandle), which ResourceHandle::create picks for blob: loads. It was once a stub:
// every blob: iframe/fetch went nowhere (WPT html5lib_url.html: all of html/syntax/parsing's url runs timed out).
class PhBlobRegistry final : public BlobRegistry {
public:
    void registerInternalFileBlobURL(const URL& url, Ref<BlobDataFileReference>&& file, const String&, const String& contentType) final { m_impl.registerInternalFileBlobURL(url, WTF::move(file), contentType); }
    void registerInternalBlobURL(const URL& url, Vector<BlobPart>&& parts, const String& contentType) final { m_impl.registerInternalBlobURL(url, WTF::move(parts), contentType); }
    void registerBlobURL(const URL& url, const URL& srcURL, const PolicyContainer& policy, const std::optional<SecurityOriginData>& topOrigin) final { m_impl.registerBlobURL(url, srcURL, policy, topOrigin); }
    void registerInternalBlobURLOptionallyFileBacked(const URL& url, const URL& srcURL, RefPtr<BlobDataFileReference>&& file, const String& contentType) final { m_impl.registerInternalBlobURLOptionallyFileBacked(url, srcURL, WTF::move(file), contentType, { }); }
    void registerInternalBlobURLForSlice(const URL& url, const URL& srcURL, long long start, long long end, const String& contentType) final { m_impl.registerInternalBlobURLForSlice(url, srcURL, start, end, contentType); }
    void unregisterBlobURL(const URL& url, const std::optional<SecurityOriginData>& topOrigin) final { m_impl.unregisterBlobURL(url, topOrigin); }
    void registerBlobURLHandle(const URL& url, const std::optional<SecurityOriginData>& topOrigin) final { m_impl.registerBlobURLHandle(url, topOrigin); }
    void unregisterBlobURLHandle(const URL& url, const std::optional<SecurityOriginData>& topOrigin) final { m_impl.unregisterBlobURLHandle(url, topOrigin); }
    String blobType(const URL& url) final { return m_impl.blobType(url); }
    unsigned long long blobSize(const URL& url) final { return m_impl.blobSize(url); }
    void writeBlobsToTemporaryFilesForIndexedDB(const Vector<String>& urls, CompletionHandler<void(Vector<String>&&)>&& completion) final { m_impl.writeBlobsToTemporaryFilesForIndexedDB(urls, WTF::move(completion)); }
    BlobRegistryImpl* blobRegistryImpl() final { return &m_impl; }
private:
    BlobRegistryImpl m_impl;
};

void PhPlatformStrategies::initialize()
{
    static NeverDestroyed<PhPlatformStrategies> strategies;
    setPlatformStrategies(&strategies.get());
}

LoaderStrategy* PhPlatformStrategies::createLoaderStrategy() { return new PhLoaderStrategy; }
PasteboardStrategy* PhPlatformStrategies::createPasteboardStrategy() { return nullptr; }
MediaStrategy* PhPlatformStrategies::createMediaStrategy() { return new PhMediaStrategy; }
BlobRegistry* PhPlatformStrategies::createBlobRegistry() { return new PhBlobRegistry; }

} // namespace Phorepoc
