// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause

/*
 * phwebtest.cpp -- milestone 1 of the in-process embedding. A WebCore Page with
 * WebCore's empty clients (the SVGImage pattern), an in-memory HTML document written straight into the main frame (no network), layout,
 * then: the render tree as text (parse + style + layout), and a Skia raster paint of the frame view (pixel statistics + c:/data/page.ppm).
 * Output goes to stdout (phsym.cpp replays it to the serial log as "[js] | ...").
 */
#include "config.h"

#include "PhDatabaseProvider.h"
#include "PhPlatformStrategies.h"
#include <JavaScriptCore/InitializeThreading.h>
#include <JavaScriptCore/JavaScript.h>
#include <JavaScriptCore/Options.h>
#include <WebCore/DocumentLoader.h>
#include <WebCore/DocumentView.h>   // LocalFrame::view() (inline, defined there)
#include <WebCore/DocumentWriter.h>
#include <WebCore/EmptyClients.h>
#include <WebCore/Element.h>
#include <WebCore/DocumentPage.h>   // inline Frame::page() / Node::document() / FrameDestructionObserver::frame(): without their
#include <WebCore/FrameDestructionObserverInlines.h>   // definitions the calls linked to EUSRNIMP stubs
#include <WebCore/NodeDocument.h>
#include <WebCore/FrameIdentifier.h>
#include <WebCore/FrameTreeSyncData.h>
#include <WebCore/HTMLFrameOwnerElement.h>
#include <string.h>
#include <WebCore/EmptyFrameLoaderClient.h>
#include <WebCore/FrameLoadRequest.h>
#include <WebCore/FrameLoader.h>
#include <WebCore/HitTestResult.h>
#include <WebCore/MIMETypeRegistry.h>
#include <WebCore/NavigationAction.h>
#include <WebCore/ResourceError.h>
#include <WebCore/ResourceRequest.h>
#include <WebCore/ResourceResponse.h>
#include <wtf/MonotonicTime.h>
#include <wtf/RunLoop.h>
#include <WebCore/GraphicsContextSkia.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/LocalFrameInlines.h>   // LocalFrame::document() (inline: without it the call linked to an EUSRNIMP stub)
#include <WebCore/LocalFrameView.h>
#include <WebCore/MemoryRelease.h>
#include <WebCore/Page.h>
#include <WebCore/ProcessIdentifier.h>
#include <WebCore/PageConfiguration.h>
#include <WebCore/RenderTreeAsText.h>
#include <WebCore/ScriptController.h>
#include <WebCore/Settings.h>
#include <WebCore/SharedBuffer.h>
#include <skia/core/SkBitmap.h>
#include <skia/core/SkCanvas.h>
#include <skia/core/SkFontMgr.h>
#include <skia/core/SkString.h>
#include <skia/ports/SkFontMgr_directory.h>
#include <curl/curl.h>
#include <pthread.h>
#include <dirent.h>
#include <net/if.h>
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <wtf/MainThread.h>
#include <wtf/Vector.h>

using namespace WebCore;

static const char kPage[] =
    "<!DOCTYPE html><html><head><title>Phorepoc WebKit</title><style>"
    "body{margin:0;font-family:sans-serif;background:#fff}"
    "h1{background:#1565c0;color:#fff;margin:0;padding:12px;font-size:24px}"
    ".box{display:flex;gap:8px;padding:8px}.box div{flex:1;height:60px;border-radius:8px}"
    "p{padding:0 12px;line-height:1.4}</style></head><body>"
    "<h1>Hello from WebKit</h1>"
    "<div class=box><div style='background:#e53935'></div><div style='background:#43a047'></div><div style='background:#fdd835'></div></div>"
    "<p>WebCore on Symbian (Phorepoc x32): HTML parser, CSS cascade and flexbox layout, painted by Skia on the CPU.</p>"
    "<p id=js></p><script>document.getElementById('js').textContent='JavaScript ran: '+(6*7);</script>"
    "</body></html>";

// Diagnostics (first QEMU run: text measured 0x0): what the font directory holds, and what Skia's directory font manager makes of it
static void dumpFonts()
{
    const char* dir = "z:/resource/fonts/";
    if (DIR* d = opendir(dir)) {
        int n = 0;
        while (dirent* e = readdir(d))
            if (n++ < 12)
                printf("phwebtest: font file %s\n", e->d_name);
        closedir(d);
        printf("phwebtest: %d entries in %s\n", n, dir);
    } else
        printf("phwebtest: opendir(%s) failed\n", dir);
    sk_sp<SkFontMgr> mgr = SkFontMgr_New_Custom_Directory(dir);
    int families = mgr->countFamilies();
    printf("phwebtest: Skia font families %d\n", families);
    for (int i = 0; i < families && i < 12; ++i) {
        SkString name;
        mgr->getFamilyName(i, &name);
        printf("phwebtest: family %s (%d styles)\n", name.c_str(), mgr->createStyleSet(i)->count());
    }
}

// The page over the network -- plain libcurl (the same curl + OpenSSL 3 build WebCore's curl backend uses,
// the ROM's CA bundle), retried while the comms stack comes up. Step B moves this into a LoaderStrategy driving CurlRequest.
static size_t collect(char* data, size_t size, size_t count, void* user)
{
    static_cast<Vector<uint8_t>*>(user)->append(std::span { reinterpret_cast<const uint8_t*>(data), size * count });
    return size * count;
}

// Open C has no default connection: every socket (curl's DNS first) fails until one access point is named -- C:\data\phweb_iap.txt if present (the phone: the WLAN IAP, e.g. "ASUS"), else x86's "Ethernet - DHCP".
// The BrCtl facade takes the IAP from BrowserNG's access-point setting instead.
extern "C" int setdefaultif(const struct ifreq*);
extern "C" void ph_serial(const char*);   // phsym.cpp
extern "C" void ph_serialf(const char*, ...);   // phsym.cpp: WPT results straight to serial (no file I/O)
static void selectAccessPoint()
{
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    FILE* f = fopen("C:\\data\\phweb_iap.txt", "r");
    if (!f || !fgets(ifr.ifr_name, sizeof ifr.ifr_name, f))
        strncpy(ifr.ifr_name, "Ethernet - DHCP", sizeof ifr.ifr_name - 1);
    if (f)
        fclose(f);
    ifr.ifr_name[strcspn(ifr.ifr_name, "\r\n")] = 0;
    printf("phwebtest: setdefaultif(%s) -> %d\n", ifr.ifr_name, setdefaultif(&ifr));
}

static bool fetch(const char* url, Vector<uint8_t>& body, String& contentType, int attempts = 30)
{
    selectAccessPoint();
    curl_global_init(CURL_GLOBAL_DEFAULT);
    printf("phwebtest: %s\n", curl_version());
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        body.clear();
        CURL* curl = curl_easy_init();
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_CAINFO, "z:/resource/ssl/ca-bundle.crt");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Symbian/3; Phorepoc) AppleWebKit/605.1.15 (KHTML, like Gecko)");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        CURLcode rc = curl_easy_perform(curl);
        long status = 0;
        char* type = nullptr;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &type);
        if (type)
            contentType = String::fromLatin1(type);
        printf("phwebtest: fetch %s attempt %d: curl %d (%s), HTTP %ld, %zu bytes, %s\n", url, attempt, (int)rc, curl_easy_strerror(rc),
            status, body.size(), type ? type : "-");
        curl_easy_cleanup(curl);
        if (rc == CURLE_OK && status == 200)
            return true;
        sleep(2);
    }
    return false;
}

// The same fetch on a second thread: WebKit's CurlRequestScheduler runs curl on its own thread, and its TLS handshake
// ended in SSL_ERROR_SYSCALL (peer EOF after ~24 s) while this probe on the main thread got HTTP 200
static void* fetchOnThread(void* url)
{
    Vector<uint8_t> body;
    String contentType;
    auto t0 = MonotonicTime::now();
    bool ok = fetch(static_cast<const char*>(url), body, contentType, 2);
    printf("phwebtest: second-thread fetch %s after %.1f s\n", ok ? "OK" : "FAILED", (MonotonicTime::now() - t0).seconds());
    return nullptr;
}

// The main frame's loader client -- the empty client with navigation allowed (the empty one never answers policy
// checks, so a navigation would wait forever) and the load outcome recorded. The BrCtl facade's client grows from this.
class PhFrameLoaderClient final : public EmptyFrameLoaderClient {
public:
    explicit PhFrameLoaderClient(FrameLoader& frameLoader)
        : EmptyFrameLoaderClient(frameLoader)
        , m_frameLoader(frameLoader)
    {
    }

    static inline bool s_finished { false };
    static inline bool s_failed { false };

private:
    // the load outcome is the main frame's (it completes only after its subframes); subframe events are not the page's
    bool isMain() const { return m_frameLoader.frame().isMainFrame(); }

    // <iframe>/<frame>: WebFrame::createSubframe without the UI process -- a LocalFrame with its own client of this class.
    // The empty client returned nullptr: every iframe had no document (101 of the first 102 WPT failures)
    RefPtr<LocalFrame> createFrame(const AtomString& name, HTMLFrameOwnerElement& ownerElement) final
    {
        RefPtr page = ownerElement.document().page();
        RefPtr parent = ownerElement.document().frame();
        if (!page || !parent)
            return nullptr;
        auto sandboxFlags = ownerElement.sandboxFlags();
        sandboxFlags.add(parent->effectiveSandboxFlags());
        auto referrerPolicy = ownerElement.referrerPolicy();
        if (referrerPolicy == ReferrerPolicy::EmptyString)
            referrerPolicy = ownerElement.document().referrerPolicy();
        Ref subframe = LocalFrame::createSubframe(*page, [](LocalFrame&, FrameLoader& frameLoader) -> UniqueRef<LocalFrameLoaderClient> {
            return makeUniqueRefWithoutRefCountedCheck<PhFrameLoaderClient>(frameLoader);
        }, generateFrameIdentifier(), sandboxFlags, referrerPolicy, ownerElement, FrameTreeSyncData::create());
        subframe->tree().setSpecifiedName(name);
        subframe->init();
        // creating the frame can run script that already removed it
        return subframe->page() ? RefPtr<LocalFrame> { WTF::move(subframe) } : nullptr;
    }
    // a committed subframe document needs a view to lay out in (WebLocalFrameLoaderClient::transitionToCommittedForNewPage);
    // RenderIFrame sizes it. The main frame keeps the view phwebtest made.
    void transitionToCommittedForNewPage(InitializingIframe) final
    {
        if (!isMain())
            m_frameLoader.frame().createView(IntSize(300, 150), std::nullopt, IntSize());
    }

    void dispatchDecidePolicyForNavigationAction(const NavigationAction&, const ResourceRequest& request, const ResourceResponse&, FormState*, const String&, std::optional<NavigationIdentifier>,
        std::optional<HitTestResult>&&, bool, NavigationUpgradeToHTTPSBehavior, SandboxFlags, PolicyDecisionMode, FramePolicyFunction&& function) final
    {
        printf("phwebtest: navigate %s\n", request.url().string().utf8().data());
        function(PolicyAction::Use);
    }
    void dispatchDecidePolicyForResponse(const ResourceResponse& response, const ResourceRequest&, const String&, FramePolicyFunction&& function) final
    {
        printf("phwebtest: response HTTP %d %s (%s)\n", response.httpStatusCode(), response.url().string().utf8().data(), response.mimeType().utf8().data());
        function(MIMETypeRegistry::canShowMIMEType(response.mimeType()) ? PolicyAction::Use : PolicyAction::Ignore);
    }
    void dispatchDecidePolicyForNewWindowAction(const NavigationAction&, const ResourceRequest&, FormState*, const String&, std::optional<HitTestResult>&&, FramePolicyFunction&& function) final
    {
        function(PolicyAction::Ignore);
    }
    // the response body reaches the document only through the client (WebLocalFrameLoaderClient::committedLoad); the empty
    // client drops it -- the load "finished" with an empty <body>
    void committedLoad(DocumentLoader* loader, const SharedBuffer& data) final { loader->commitData(data); }
    bool canHandleRequest(const ResourceRequest&) const final { return true; }
    bool canShowMIMEType(const String& mimeType) const final { return MIMETypeRegistry::canShowMIMEType(mimeType); }
    bool canShowMIMETypeAsHTML(const String&) const final { return true; }
    void dispatchDidFinishLoad() final
    {
        if (!isMain())
            return;
        printf("phwebtest: load finished\n");
        s_finished = true;
    }
    void dispatchDidFailLoad(const ResourceError& error) final
    {
        // the previous page's load, cancelled by the next navigation, is not the new page's failure (the WPT runner
        // counted it as one: 39 false LOAD-FAILED)
        if (!isMain() || error.isCancellation())
            return;
        printf("phwebtest: load FAILED: %d %s\n", error.errorCode(), error.localizedDescription().utf8().data());
        s_finished = s_failed = true;
    }
    void dispatchDidFailProvisionalLoad(const ResourceError& error, WillContinueLoading, WillInternallyHandleFailure) final
    {
        // the previous page's load, cancelled by the next navigation, is not the new page's failure (the WPT runner
        // counted it as one: 39 false LOAD-FAILED)
        if (!isMain() || error.isCancellation())
            return;
        printf("phwebtest: provisional load FAILED: %d %s\n", error.errorCode(), error.localizedDescription().utf8().data());
        s_finished = s_failed = true;
    }

    FrameLoader& m_frameLoader;
};

static constexpr int kWidth = 360;
static constexpr int kHeight = 640;

// One turn of the page loop: the run loop (CurlRequest's callbacks, timers, tasks), then -- every 16 ms -- WebKit's rendering
// update (requestAnimationFrame, resize/intersection observers, lazy-load viewport checks, image decode promises, layout),
// as DrawingAreaCoordinatedGraphics does per frame, minus finalizeRenderingUpdate (the compositing layer flush: compositing is
// off here, and it asserted in finalizeRenderingUpdateForRootFrame -> abort). The empty ChromeClient never
// schedules one: those WPT tests timed out. cycle() never waits (RunLoop::Iterate): the sleep yields to the
// network and decoder threads.
static Page* gPage;
static void pump()
{
    static MonotonicTime nextFrame;
    RunLoop::cycle();
    if (gPage && MonotonicTime::now() >= nextFrame) {
        nextFrame = MonotonicTime::now() + 16_ms;
        gPage->isolatedUpdateRendering();
    }
    usleep(5000);
}

// WPT mode: each URL of the list loads into the same frame; our resources/testharnessreport.js puts
// the results into <pre id="ph-wpt-results">, printed here as "wpt| " lines. One "wpt: <url> <outcome>" line per test.
static int runWpt(LocalFrame& frame, const char* listPath)
{
    FILE* list = fopen(listPath, "r");
    if (!list) {
        printf("wpt: cannot open %s\n", listPath);
        return 1;
    }
    char line[1024];
    unsigned tests = 0, complete = 0;
    while (fgets(line, sizeof(line), list)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!*line || *line == '#')
            continue;
        ++tests;
        // between tests, what a browser does under memory pressure (WebKitTestRunner collects between tests too): drop
        // caches + a full synchronous GC. 268 dom/nodes tests in one process ended in JSC MemoryExhaustion.
        if (tests > 1)
            WebCore::releaseMemory(Critical::Yes, Synchronous::Yes);
        ph_serial(line);   // the serial log names the test even when it panics the process
        PhFrameLoaderClient::s_finished = PhFrameLoaderClient::s_failed = false;
        auto t0 = MonotonicTime::now();
        frame.loader().load(FrameLoadRequest(frame, ResourceRequest(URL { String::fromLatin1(line) })));
        RefPtr<Element> results;
        while (MonotonicTime::now() - t0 < 90_s) {   // 90 s: the sizes-parsing tests carry ~180 subtests each
            pump();
            if (PhFrameLoaderClient::s_finished) {
                RefPtr document = frame.document();
                if (PhFrameLoaderClient::s_failed || (document && (results = document->getElementById(AtomString("ph-wpt-results"_s)))))
                    break;
            }
        }
        const char* outcome = results ? "RESULTS" : PhFrameLoaderClient::s_failed ? "LOAD-FAILED" : PhFrameLoaderClient::s_finished ? "NO-RESULTS" : "LOAD-TIMEOUT";
        ph_serialf("wpt: %s %s %.1f s", line, outcome, (MonotonicTime::now() - t0).seconds());
        if (results) {
            ++complete;
            // the HARNESS line, the non-PASS subtests, then the counts (classlist alone has 1420 passing subtests)
            CString text = results->textContent().utf8();
            unsigned pass = 0, other = 0;
            for (const char* p = text.data(); *p; ) {
                const char* end = strchr(p, '\n');
                int n = end ? static_cast<int>(end - p) : static_cast<int>(strlen(p));
                if (!strncmp(p, "PASS ", 5))
                    ++pass;
                else {
                    other += !!strncmp(p, "HARNESS ", 8);
                    ph_serialf("wpt| %.*s", n, p);
                }
                p += n + (end ? 1 : 0);
            }
            ph_serialf("wpt| COUNT pass %u not-pass %u", pass, other);
        }
    }
    fclose(list);
    ph_serialf("wpt: done, %u tests, %u reported results", tests, complete);
    return 0;
}

// The JIT check: which JSC tiers are on, and a hot loop timed three times on its own context -- the 1st run
// starts in the LLInt, later runs are tiered up when the Baseline JIT works (a CLoop/LLInt-only VM shows no speed-up).
static void jitProbe()
{
    printf("phwebtest: JSC useJIT %d useBaselineJIT %d useDFGJIT %d useLLInt %d\n", JSC::Options::useJIT(),
        JSC::Options::useBaselineJIT(), JSC::Options::useDFGJIT(), JSC::Options::useLLInt());
    JSGlobalContextRef ctx = JSGlobalContextCreate(nullptr);
    JSStringRef src = JSStringCreateWithUTF8CString(
        "(function(){var t=Date.now(),s=0;for(var i=0;i<3000000;i++){s=(s+i*7)%1000003;}return (Date.now()-t)+' ms (s='+s+')';})()");
    for (int run = 0; run < 3; ++run) {
        JSValueRef exception = nullptr;
        JSValueRef v = JSEvaluateScript(ctx, src, nullptr, nullptr, 1, &exception);
        char buf[96] = "exception";
        if (v && !exception) {
            JSStringRef r = JSValueToStringCopy(ctx, v, nullptr);
            JSStringGetUTF8CString(r, buf, sizeof(buf));
            JSStringRelease(r);
        }
        printf("phwebtest: JS loop 3M iterations, run %d: %s\n", run + 1, buf);
    }
    JSStringRelease(src);
    JSGlobalContextRelease(ctx);
}

int main(int argc, char** argv)
{
    printf("phwebtest: start\n");
    // `--wpt <file>`: web-platform-tests from a list of URLs; else one page + its paint
    const char* wptList = argc > 2 && !strcmp(argv[1], "--wpt") ? argv[2] : nullptr;
    // `--offline` (wlanjoin, the join failed): no fetch at all -- with no connection curl's resolver thread never returns
    const bool offline = argc > 1 && !strcmp(argv[1], "--offline");
    const char* url = wptList ? "http://host.docker.internal:8000/resources/testharness.js" : argc > 1 && !offline ? argv[1] : "https://example.com/";
    Vector<uint8_t> body;
    String contentType;
    const bool fetched = !offline && fetch(url, body, contentType);
    pthread_t prober;
    pthread_attr_t proberAttr;   // Open C's default thread stack overflows in OpenSSL 3.5's ML-KEM key share (KERN-EXEC 3)
    pthread_attr_init(&proberAttr);
    pthread_attr_setstacksize(&proberAttr, 1024 * 1024);
    if (!offline && !pthread_create(&prober, &proberAttr, fetchOnThread, const_cast<char*>(url)))
        pthread_join(prober, nullptr);
    dumpFonts();
    JSC::initialize();
    WTF::initializeMainThread();
    Phorepoc::PhPlatformStrategies::initialize();
    // the process identifier WebKitLegacy sets at startup (IDB connections, workers read it: an unset optional aborted
    // WPT xhr/close-worker-with-xhr-in-progress)
    WebCore::Process::setIdentifier(WebCore::Process::generateIdentifier());

    auto configuration = pageConfigurationWithEmptyClients(std::nullopt, PAL::SessionID::defaultSessionID());
    // real in-process IndexedDB (WebKitLegacy model) in place of EmptyDatabaseProvider: dedicated workers / indexedDB.
    // empty path = memory-backed; pass e.g. "c:/data/idb" for SQLite files
    configuration.databaseProvider = Phorepoc::PhDatabaseProvider::create();
    // The empty-clients configuration is made for SVGImage and sandboxes the main frame completely (SandboxFlags::all(): no scripts)
    auto& mainFrameParameters = std::get<PageConfiguration::LocalMainFrameCreationParameters>(configuration.mainFrameCreationParameters);
    mainFrameParameters.effectiveSandboxFlags = { };
    mainFrameParameters.clientCreator = [](LocalFrame&, FrameLoader& frameLoader) -> UniqueRef<LocalFrameLoaderClient> {
        return makeUniqueRefWithoutRefCountedCheck<PhFrameLoaderClient>(frameLoader);
    };
    Ref page = Page::create(WTF::move(configuration));
    gPage = page.ptr();
    page->settings().setAcceleratedCompositingEnabled(false);
    page->settings().setScriptEnabled(true);
    // A bare WebCore Settings carries the engine's defaults, not a browser's (LoadsImagesAutomatically is false there --
    // Wikipedia painted without a single image). PhWpeSettings.inc = every preference whose upstream WPE WebKit
    // default differs from ours, generated from UnifiedWebPreferences.yaml (webkit/gen_wpe_settings.py). Then the scope
    // overrides: no backends for media, WebGL, shared/service workers (separate-process SW = later), and no
    // accelerated drawing/compositing in this software-painted view.
    auto& settings = page->settings();
#include "PhWpeSettings.inc"
    settings.setMediaSourceEnabled(false);
    settings.setMediaCapabilitiesEnabled(false);
    settings.setMediaCapabilitiesExtensionsEnabled(false);
    settings.setRequestVideoFrameCallbackEnabled(false);
    settings.setEnableOpaqueLoadingForMedia(false);
    settings.setWebGLEnabled(false);
    settings.setServiceWorkersEnabled(false);
    settings.setSharedWorkerEnabled(false);
    settings.setAcceleratedDrawingEnabled(false);
    // async scrolling needs a compositor (and ENABLE_ASYNC_SCROLLING, off here): an overflow scroller asked for a
    // GraphicsLayer this view has no factory for -> abort (WPT image-loading-lazy-in-scroller-far)
    settings.setAsyncOverflowScrollingEnabled(false);
    settings.setAsyncFrameScrollingEnabled(false);

    RefPtr frame = page->localMainFrame();
    if (!frame) {
        printf("phwebtest: no main frame\n");
        return 1;
    }
    frame->setView(LocalFrameView::create(*frame));
    frame->init();
    RefPtr view = frame->view();
    view->resize(IntSize(kWidth, kHeight));
    printf("phwebtest: page + frame + view, scripts allowed %d\n",
        frame->script().canExecuteScripts(ReasonForCallingCanExecuteScripts::NotAboutToExecuteScript));

    if (wptList)
        return runWpt(*frame, wptList);

    if (fetched) {
        // WebKit's own loader -- FrameLoader -> PhLoaderStrategy -> ResourceLoader -> ResourceHandle (CurlRequest);
        // the run loop delivers CurlRequest's main-thread callbacks. Subresources (CSS, images, scripts) load the same way.
        auto t0 = MonotonicTime::now();
        frame->loader().load(FrameLoadRequest(*frame, ResourceRequest(URL { String::fromLatin1(url) })));
        // cycle() = RunMode::Iterate (generic RunLoop): it never waits, so a bare loop spins the main thread and starves the
        // curl thread -- the server then dropped the half-done exchange (SSL_ERROR_SYSCALL). Yield between cycles.
        while (!PhFrameLoaderClient::s_finished && MonotonicTime::now() - t0 < 120_s) {
            pump();
        }
        printf("phwebtest: WebKit load of %s: %s after %.1f s\n", url, !PhFrameLoaderClient::s_finished ? "TIMEOUT" : PhFrameLoaderClient::s_failed ? "failed" : "finished",
            (MonotonicTime::now() - t0).seconds());
    } else {
        printf("phwebtest: network not reachable -- the built-in page instead\n");
        RefPtr loader = frame->loader().activeDocumentLoader();
        loader->writer().setMIMEType("text/html"_s);
        loader->writer().begin(URL { "about:blank"_s });
        loader->writer().addData(SharedBuffer::create(std::span { reinterpret_cast<const uint8_t*>(kPage), sizeof(kPage) - 1 }));
        loader->writer().end();
    }
    view->updateLayoutAndStyleIfNeededRecursive();
    printf("phwebtest: document laid out (%s)\n", fetched ? url : "built-in");
    jitProbe();
    if (RefPtr document = frame->document(); document && document->documentElement())
        printf("phwebtest: document %s, %u chars of markup\n", document->url().string().utf8().data(),
            document->documentElement()->outerHTML().length());

    String tree = externalRepresentation(frame.get());
    CString utf8 = tree.utf8();
    // the first 60 lines only: Wikipedia's tree is ~2000 lines and the paint results after it got lost in the serial log
    {
        const char* text = utf8.data();
        const char* cut = text;
        for (int lines = 0; *cut && lines < 60; ++cut)
            lines += *cut == '\n';
        printf("---- render tree (%u bytes) ----\n%.*s---- end ----\n", static_cast<unsigned>(utf8.length()), static_cast<int>(cut - text), text);
    }

    SkBitmap bitmap;
    bitmap.allocN32Pixels(kWidth, kHeight);
    bitmap.eraseColor(SK_ColorWHITE);
    // Bitmap images decode asynchronously: the first paint only requests the decodes (Wikipedia's JPEGs had arrived and still
    // painted as nothing). A view repaints when they land; here: paint, let the run loop deliver for 2 s, paint again.
    for (int pass = 0; pass < 2; ++pass) {
        if (pass) {
            auto settle = MonotonicTime::now() + 2_s;
            while (MonotonicTime::now() < settle) {
                pump();
            }
            view->updateLayoutAndStyleIfNeededRecursive();
            bitmap.eraseColor(SK_ColorWHITE);
        }
        SkCanvas canvas(bitmap);
        GraphicsContextSkia context(canvas, RenderingMode::Unaccelerated, RenderingPurpose::Unspecified);
        view->paint(context, IntRect(0, 0, kWidth, kHeight));
    }
    unsigned nonWhite = 0, blue = 0, red = 0;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            SkColor c = bitmap.getColor(x, y);
            if (c != SK_ColorWHITE)
                ++nonWhite;
            if (SkColorGetR(c) == 0x15 && SkColorGetG(c) == 0x65 && SkColorGetB(c) == 0xc0)
                ++blue;
            if (SkColorGetR(c) == 0xe5 && SkColorGetG(c) == 0x39 && SkColorGetB(c) == 0x35)
                ++red;
        }
    }
    printf("phwebtest: painted %dx%d, non-white %u, header-blue %u, box-red %u\n", kWidth, kHeight, nonWhite, blue, red);

    if (FILE* f = fopen("c:/data/page.ppm", "wb")) {
        fprintf(f, "P6\n%d %d\n255\n", kWidth, kHeight);
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                SkColor c = bitmap.getColor(x, y);
                unsigned char rgb[3] = { (unsigned char)SkColorGetR(c), (unsigned char)SkColorGetG(c), (unsigned char)SkColorGetB(c) };
                fwrite(rgb, 1, 3, f);
            }
        }
        fclose(f);
        printf("phwebtest: wrote c:/data/page.ppm\n");
    }
    printf("phwebtest: done\n");
    return 0;
}
