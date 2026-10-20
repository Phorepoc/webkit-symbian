// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause

/*
 * ResourceHandleSymbian.cpp -- the in-process
 * (WebKitLegacy-style) network path. WPE's ResourceHandle under USE(CURL) is a block of ASSERT_NOT_REACHED stubs -- upstream,
 * only the network process drives CurlRequest (NetworkDataTaskCurl). This file implements ResourceHandle on CurlRequest so
 * ResourceLoader::start() (main resource + subresources, via PhLoaderStrategy) loads over curl + OpenSSL in the web process.
 * patch.py compiles the upstream stub block out for PHOREPOC.
 *
 * CurlRequest's contract (CurlRequest.cpp): every client callback arrives on the main thread; after curlDidReceiveResponse
 * the transfer stays paused until completeDidReceiveResponse(); redirects are NOT followed by curl -- a 3xx comes back as
 * a response and the client restarts with the new request (as NetworkDataTaskCurl does).
 */
#include "config.h"
#include "ResourceHandle.h"

#if USE(CURL) && defined(PHOREPOC)

#include "CurlRequest.h"
#include "CurlRequestClient.h"
#include "CurlResponse.h"
#include "HTTPHeaderNames.h"
#include "NetworkLoadMetrics.h"
#include "ResourceError.h"
#include "ResourceHandleClient.h"
#include "ResourceHandleInternal.h"
#include "ResourceResponse.h"
#include "SharedBuffer.h"
#include "SynchronousLoaderClient.h"
#include <curl/curl.h>
#include <optional>
#include <stdio.h>
#include <string.h>
#include <wtf/HashMap.h>
#include <wtf/MonotonicTime.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/RunLoop.h>
#include <unistd.h>

namespace WebCore {

class PhCurlBridge final : public CurlRequestClient, public RefCounted<PhCurlBridge> {
public:
    static Ref<PhCurlBridge> create(ResourceHandle& handle) { return adoptRef(*new PhCurlBridge(handle)); }

    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    void start(const ResourceRequest& request)
    {
        printf("curl: start %s\n", request.url().string().utf8().data());
        m_start = MonotonicTime::now();
        m_request = CurlRequest::create(request, *this);
        m_request->resume();
    }

    void cancel()
    {
        if (m_handle && m_request)
            printf("curl: cancel %s\n", m_handle->firstRequest().url().string().utf8().data());
        m_handle = nullptr;
        if (RefPtr request = std::exchange(m_request, nullptr)) {
            request->invalidateClient();
            request->cancel();
        }
    }

private:
    explicit PhCurlBridge(ResourceHandle& handle)
        : m_handle(&handle)
    {
    }

    // load latency in the log (WPT's relevant-mutations gives a re-fetch 1 s)
    int msSinceStart() const { return static_cast<int>((MonotonicTime::now() - m_start).milliseconds()); }

    ResourceHandleClient* client() const { return m_handle ? m_handle->client() : nullptr; }

    void curlDidSendData(CurlRequest&, unsigned long long bytesSent, unsigned long long totalBytesToBeSent) final
    {
        if (auto* c = client())
            c->didSendData(m_handle, bytesSent, totalBytesToBeSent);
    }

    void curlDidReceiveResponse(CurlRequest& request, CurlResponse&& curlResponse) final
    {
        auto* c = client();
        if (!c)
            return;
        ResourceResponse response(curlResponse);
        if (response.isRedirection() && !response.httpHeaderField(HTTPHeaderName::Location).isEmpty()) {
            redirect(WTF::move(response));
            return;
        }
        printf("curl: %s response %d +%d ms\n", response.url().string().utf8().data(), response.httpStatusCode(), msSinceStart());
        if (m_handle->shouldContentSniff() && response.httpHeaderField(HTTPHeaderName::ContentType).isEmpty()) {
            // no Content-Type: hold the response, sniff the first bytes (upstream WPE: libsoup's content sniffer)
            m_heldResponse = WTF::move(response);
            m_sniffing = true;
            request.completeDidReceiveResponse();
            return;
        }
        c->didReceiveResponseAsync(m_handle, WTF::move(response), [protectedThis = Ref { *this }, request = Ref { request }] {
            printf("curl: response handshake done (current %d)\n", protectedThis->m_request.get() == request.ptr());
            if (protectedThis->m_request.get() == request.ptr())
                request->completeDidReceiveResponse();
        });
    }

    void curlDidReceiveData(CurlRequest&, Ref<SharedBuffer>&& buffer) final
    {
        m_bytes += buffer->size();
        if (m_sniffing || m_awaitingHandshake) {
            m_held.append(buffer->span());
            if (m_sniffing && m_held.size() >= 512)
                finishSniffing();
            return;
        }
        if (auto* c = client())
            c->didReceiveData(m_handle, buffer.get(), static_cast<int>(buffer->size()));
    }

    void curlDidComplete(CurlRequest&, NetworkLoadMetrics&& metrics) final
    {
        printf("curl: %s complete, %u body bytes, client %d, +%d ms\n", m_handle->firstRequest().url().string().utf8().data(), m_bytes,
            !!client(), msSinceStart());
        m_request = nullptr;
        if (m_sniffing || m_awaitingHandshake) {
            m_completePending = true;
            m_pendingMetrics = WTF::move(metrics);
            if (m_sniffing)
                finishSniffing();
            return;
        }
        if (auto* c = client())
            c->didFinishLoading(m_handle, metrics);
    }

    // WHATWG MIME Sniffing, "identify an unknown MIME type" (sniff-scriptable): HTML/XML/PDF signatures, the image signatures,
    // else text/plain without binary bytes, else application/octet-stream
    static String sniffUnknownType(std::span<const uint8_t> d)
    {
        auto startsWith = [&](size_t at, const char* sig, size_t n, bool ci) {
            if (d.size() < at + n)
                return false;
            for (size_t i = 0; i < n; i++) {
                uint8_t c = d[at + i];
                if (ci && c >= 'a' && c <= 'z')
                    c -= 32;
                if (c != static_cast<uint8_t>(sig[i]))
                    return false;
            }
            return true;
        };
        size_t ws = 0;
        while (ws < d.size() && (d[ws] == ' ' || d[ws] == '\t' || d[ws] == '\n' || d[ws] == '\r' || d[ws] == '\f'))
            ws++;
        static const char* const htmlTags[] = { "<!DOCTYPE HTML", "<HTML", "<HEAD", "<SCRIPT", "<IFRAME", "<H1", "<DIV", "<FONT",
            "<TABLE", "<A", "<STYLE", "<TITLE", "<B", "<BODY", "<BR", "<P", "<!--" };
        for (const char* tag : htmlTags) {
            size_t n = strlen(tag);
            if (startsWith(ws, tag, n, true) && d.size() > ws + n && (d[ws + n] == ' ' || d[ws + n] == '>'))
                return "text/html"_s;
        }
        if (startsWith(ws, "<?xml", 5, false))
            return "text/xml"_s;
        if (startsWith(0, "%PDF-", 5, false))
            return "application/pdf"_s;
        if (startsWith(0, "GIF87a", 6, false) || startsWith(0, "GIF89a", 6, false))
            return "image/gif"_s;
        if (startsWith(0, "\x89PNG\r\n\x1a\n", 8, false))
            return "image/png"_s;
        if (startsWith(0, "\xff\xd8\xff", 3, false))
            return "image/jpeg"_s;
        if (startsWith(0, "RIFF", 4, false) && startsWith(8, "WEBPVP", 6, false))
            return "image/webp"_s;
        if (startsWith(0, "BM", 2, false))
            return "image/bmp"_s;
        if (startsWith(0, "\xfe\xff", 2, false) || startsWith(0, "\xff\xfe", 2, false) || startsWith(0, "\xef\xbb\xbf", 3, false))
            return "text/plain"_s;
        for (uint8_t c : d) {
            if (c <= 0x08 || c == 0x0B || (c >= 0x0E && c <= 0x1A) || (c >= 0x1C && c <= 0x1F))
                return "application/octet-stream"_s;
        }
        return "text/plain"_s;
    }

    void finishSniffing()
    {
        m_sniffing = false;
        auto* c = client();
        if (!c)
            return;
        m_heldResponse->setMimeType(sniffUnknownType(m_held.span()));
        printf("curl: %s sniffed as %s\n", m_heldResponse->url().string().utf8().data(), m_heldResponse->mimeType().utf8().data());
        m_awaitingHandshake = true;
        c->didReceiveResponseAsync(m_handle, ResourceResponse(*std::exchange(m_heldResponse, std::nullopt)), [protectedThis = Ref { *this }] {
            protectedThis->m_awaitingHandshake = false;
            Vector<uint8_t> held = std::exchange(protectedThis->m_held, { });
            if (auto* c = protectedThis->client(); c && !held.isEmpty()) {
                int size = held.size();
                c->didReceiveData(protectedThis->m_handle, SharedBuffer::create(WTF::move(held)), size);
            }
            if (auto* c = protectedThis->client(); c && protectedThis->m_completePending)
                c->didFinishLoading(protectedThis->m_handle, protectedThis->m_pendingMetrics);
        });
    }

    void curlDidFailWithError(CurlRequest&, ResourceError&& error, CertificateInfo&&) final
    {
        m_request = nullptr;
        if (auto* c = client())
            c->didFail(m_handle, error);
    }

    // HTTP redirect: the new request per RFC 7231 (303, and 301/302 after a POST, become a body-less GET), then the client's
    // willSendRequest decides; an empty request = blocked (the loader has already reported it)
    void redirect(ResourceResponse&& response)
    {
        if (RefPtr request = std::exchange(m_request, nullptr)) {
            request->invalidateClient();
            request->cancel();
        }
        if (++m_redirectCount > 20) {
            if (auto* c = client())
                c->didFail(m_handle, ResourceError(String(), CURLE_TOO_MANY_REDIRECTS, response.url(), "Too many redirects"_s));
            return;
        }
        URL newURL(response.url(), response.httpHeaderField(HTTPHeaderName::Location));
        ResourceRequest newRequest = m_handle->firstRequest();
        newRequest.setURL(WTF::move(newURL));
        const int status = response.httpStatusCode();
        if (status == 303 || ((status == 301 || status == 302) && equalLettersIgnoringASCIICase(newRequest.httpMethod(), "post"_s))) {
            newRequest.setHTTPMethod("GET"_s);
            newRequest.setHTTPBody(nullptr);
            newRequest.clearHTTPContentType();
        }
        client()->willSendRequestAsync(m_handle, WTF::move(newRequest), WTF::move(response), [protectedThis = Ref { *this }](ResourceRequest&& request) {
            if (!protectedThis->m_handle || request.isNull())
                return;
            protectedThis->start(request);
        });
    }

    ResourceHandle* m_handle;
    RefPtr<CurlRequest> m_request;
    unsigned m_redirectCount { 0 };
    unsigned m_bytes { 0 };
    MonotonicTime m_start;
    // content sniffing (no Content-Type): the held response, the body bytes until the client has taken the response
    std::optional<ResourceResponse> m_heldResponse;
    Vector<uint8_t> m_held;
    bool m_sniffing { false };
    bool m_awaitingHandshake { false };
    bool m_completePending { false };
    NetworkLoadMetrics m_pendingMetrics;
};

static HashMap<ResourceHandle*, Ref<PhCurlBridge>>& bridges()
{
    static NeverDestroyed<HashMap<ResourceHandle*, Ref<PhCurlBridge>>> map;
    return map;
}

ResourceHandleInternal::~ResourceHandleInternal() = default;

ResourceHandle::~ResourceHandle()
{
    if (auto bridge = bridges().take(this))
        bridge->cancel();
}

bool ResourceHandle::start()
{
    Ref bridge = PhCurlBridge::create(*this);
    bridges().set(this, bridge.copyRef());
    bridge->start(firstRequest());
    return true;
}

void ResourceHandle::cancel()
{
    if (auto bridge = bridges().take(this))
        bridge->cancel();
}

void ResourceHandle::platformSetDefersLoading(bool)
{
    // CurlRequest cannot pause an ongoing transfer from outside; defers only matter for not-yet-started loads (PhLoaderStrategy)
}

// Synchronous loads (sync XHR, some legacy paths): the same CurlRequest path as async loads, collected by WebCore's
// SynchronousLoaderClient while a nested run loop delivers CurlRequest's main-thread callbacks (WebKitLegacy's nested-loop
// model). This was once a bare curl_easy GET: method, request headers and body were dropped, and the response kept
// only its status code + MIME type (WPT xhr/status-basic: every sync statusText empty).
void ResourceHandle::platformLoadResourceSynchronously(NetworkingContext* context, const ResourceRequest& request, StoredCredentialsPolicy storedCredentialsPolicy, SecurityOrigin* sourceOrigin, ResourceError& error, ResourceResponse& response, Vector<uint8_t>& data)
{
    SynchronousLoaderClient client;
    client.setAllowStoredCredentials(storedCredentialsPolicy == StoredCredentialsPolicy::Use);
    RefPtr handle = ResourceHandle::create(context, request, &client, false /* defersLoading */, false /* shouldContentSniff */,
        ContentEncodingSniffingPolicy::Default, RefPtr { sourceOrigin }, false /* isMainFrameNavigation */);
    auto deadline = MonotonicTime::now() + 120_s;   // curl's own timeouts end it first; this only guards the nested loop
    while (handle && !client.messageQueue().killed() && MonotonicTime::now() < deadline) {
        RunLoop::cycle();
        usleep(1000);
    }
    if (handle && !client.messageQueue().killed()) {
        handle->cancel();
        error = ResourceError(String(), CURLE_OPERATION_TIMEDOUT, request.url(), "Synchronous load timed out"_s);
        return;
    }
    error = client.error();
    data = WTF::move(client.mutableData());
    response = client.response();
}

bool ResourceHandle::shouldUseCredentialStorage() { return false; }
void ResourceHandle::didReceiveAuthenticationChallenge(const AuthenticationChallenge&) { }
void ResourceHandle::receivedCredential(const AuthenticationChallenge&, const Credential&) { }
void ResourceHandle::receivedRequestToContinueWithoutCredential(const AuthenticationChallenge&) { }
void ResourceHandle::receivedCancellation(const AuthenticationChallenge&) { }
void ResourceHandle::receivedRequestToPerformDefaultHandling(const AuthenticationChallenge&) { }
void ResourceHandle::receivedChallengeRejection(const AuthenticationChallenge&) { }

} // namespace WebCore

#endif // USE(CURL) && defined(PHOREPOC)
