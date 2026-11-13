// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause

/*
 * PhEngine.cpp -- the WebKit engine behind the BrCtl facade (src/phebrctl).
 * The in-process WebCore embedding of phwebtest.cpp turned into a service: one thread (ph_eng_main) owns every WebCore object; the
 * facade (UI thread) talks to it through the C API in PhEngineApi.h -- a command queue in, an event queue + a pixel buffer out.
 * Each view is a Page with its own frame loader client; navigation events are derived from the loader (policy decisions, the
 * document loaders, title, progress, finish/fail) and posted for the facade to turn into BrCtl load events.
 */
#include "config.h"

#include "PhDatabaseProvider.h"
#include "PhEngineApi.h"
#include "PhPlatformStrategies.h"
#include <JavaScriptCore/InitializeThreading.h>
#include <WebCore/DocumentLoader.h>
#include <WebCore/DocumentView.h>
#include <WebCore/DocumentWriter.h>
#include <WebCore/CookieJar.h>
#include <WebCore/StorageSessionProvider.h>
#include <WebCore/SecurityOrigin.h>
#include <WebCore/SecurityOriginData.h>
#include <WebCore/StorageArea.h>
#include <WebCore/StorageMap.h>
#include <WebCore/StorageNamespace.h>
#include <WebCore/StorageNamespaceProvider.h>
#include <WebCore/StorageType.h>
#include <sys/stat.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/text/MakeString.h>
#include <WebCore/EmptyClients.h>
#include <WebCore/Editor.h>
#include <WebCore/Element.h>
#include <WebCore/DocumentPage.h>
#include <WebCore/FrameDestructionObserverInlines.h>
#include <WebCore/NodeDocument.h>
#include <WebCore/FrameIdentifier.h>
#include <WebCore/FrameTreeSyncData.h>
#include <WebCore/HTMLFrameOwnerElement.h>
#include <WebCore/HTMLAnchorElement.h>
#include <WebCore/HTMLInputElement.h>
#include <WebCore/HTMLTextAreaElement.h>
#include <WebCore/EmptyFrameLoaderClient.h>
#include <WebCore/EventHandler.h>
#include <WebCore/FocusController.h>
#include <WebCore/FrameLoadRequest.h>
#include <WebCore/HandleUserInputEventResult.h>
#include <WebCore/FrameLoader.h>
#include <WebCore/HitTestResult.h>
#include <WebCore/MIMETypeRegistry.h>
#include <WebCore/NavigationAction.h>
#include <WebCore/PlatformKeyboardEvent.h>
#include <WebCore/PlatformMouseEvent.h>
#include <WebCore/ProgressTracker.h>
#include <WebCore/ResourceError.h>
#include <WebCore/ResourceRequest.h>
#include <WebCore/ResourceResponse.h>
#include <wtf/MonotonicTime.h>
#include <wtf/RunLoop.h>
#include <WebCore/Document.h>
#include <WebCore/GraphicsContextSkia.h>
#include <WebCore/ViewportArguments.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/LocalFrameInlines.h>
#include <WebCore/LocalFrameView.h>
#include <WebCore/MemoryRelease.h>
#include <WebCore/Page.h>
#include <WebCore/ProcessIdentifier.h>
#include <WebCore/PageConfiguration.h>
#include <WebCore/ScriptController.h>
#include <WebCore/Settings.h>
#include <WebCore/SharedBuffer.h>
#include <skia/core/SkBitmap.h>
#include <skia/core/SkCanvas.h>
#include <curl/curl.h>
#include <net/if.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <wtf/MainThread.h>
#include <cmath>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace WebCore {
class StorageSessionProvider;
Ref<StorageSessionProvider> phCookieStorageSessionProvider();   // ResourceHandleSymbian.cpp: the process's cookie jar
void phCookiesFlush(bool force);                                // ResourceHandleSymbian.cpp: writes its persistent cookies
}

using namespace WebCore;

extern "C" int setdefaultif(const struct ifreq*);
extern "C" void ph_serialf(const char*, ...);   // phsym.cpp / phebsym.cpp

namespace {

// ---- shared state (guarded by gLock) ---------------------------------------------------------------------------------------
// Everything that crosses between the UI thread and the engine thread is POD in fixed storage or memory the UI thread itself
// allocates AND frees: the UI thread's heap is BrowserNG's MemMan allocator and the engine thread has an RHeap of its own, so a
// std::string / deque node allocated by one thread and freed by the other is a foreign free (it crashed dlfree at BrowserNG's exit).
constexpr int kStrMax = 2048;
constexpr int kEvents = 256;
struct Shared {
    PhEvent events[kEvents];
    int evHead { 0 }, evCount { 0 };
    bool notifyPending { false };
    bool repaintQueued { false };
    unsigned short str[7][kStrMax];
    int strLen[7] { };
    PhViewState state { };
    unsigned* front { nullptr };   // allocated and freed by the UI thread (ph_eng_view_create / _resize / _destroy)
    int fw { 0 }, fh { 0 };
    bool hasPixels { false };
    bool alive { true };
};

pthread_mutex_t gLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t gAck = PTHREAD_COND_INITIALIZER;   // a view was destroyed
// the User-Agent: the facade passes the CenRep value (ph_eng_set_useragent); empty = this default (a current Android Chrome:
// sites such as Google serve the "outdated browser" page to anything older)
std::string gUserAgent;                           // guarded by gLock
const char kDefaultUserAgent[] = "Mozilla/5.0 (Linux; Android 14; K) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/141.0.0.0 Mobile Safari/537.36";
std::map<int, Shared*> gShared;                   // UI thread inserts/erases (its heap); the engine thread only looks up, under gLock
PhEngNotify gNotify;
void* gNotifyCtx;
volatile int gNextView = 1;
volatile int gRunning;
volatile int gQuit;

constexpr int kTextMax = 4096;
struct Cmd {
    enum Type { Create, Destroy, Resize, Visible, Load, Command, Pointer, Scroll, Key, Setting, SetIap, Zoom, SetField } type;
    int view;
    int a, b, c;
    int textLen;
    unsigned short text[kTextMax];
    char utf8[128];
};
constexpr int kCmds = 128;
Cmd gCmdRing[kCmds];   // static storage: zero-initialised, no constructor
int gCmdHead, gCmdCount;

void post(int view, int kind, int a = 0, int b = 0)
{
    bool wake = false;
    pthread_mutex_lock(&gLock);
    auto it = gShared.find(view);
    if (it != gShared.end() && it->second->alive) {
        Shared& s = *it->second;
        bool queue = true;
        if (kind == PH_EV_REPAINT) {
            queue = !s.repaintQueued;
            s.repaintQueued = true;
        }
        if (queue && s.evCount < kEvents) {
            s.events[(s.evHead + s.evCount) % kEvents] = { kind, a, b };
            ++s.evCount;
        }
        wake = !s.notifyPending;
        s.notifyPending = true;
    }
    pthread_mutex_unlock(&gLock);
    if (wake && gNotify)
        gNotify(gNotifyCtx, view);
}

void setStr(int view, int which, const String& value)
{
    pthread_mutex_lock(&gLock);
    auto it = gShared.find(view);
    if (it != gShared.end()) {
        Shared& s = *it->second;
        int n = static_cast<int>(value.length() < kStrMax ? value.length() : kStrMax);
        for (int i = 0; i < n; ++i)
            s.str[which][i] = value[i];
        s.strLen[which] = static_cast<int>(value.length());   // the full length (a caller may see > its buffer)
    }
    pthread_mutex_unlock(&gLock);
}

String fromU16(const unsigned short* p, int n)
{
    return String(std::span<const char16_t> { reinterpret_cast<const char16_t*>(p), static_cast<size_t>(n) });
}

// ---- engine-thread state ---------------------------------------------------------------------------------------------------
struct PhView;
bool gDirty;   // some ChromeClient invalidation arrived (any view: the clients carry no back pointer)

class PhChromeClient final : public EmptyChromeClient {
public:
    void invalidateContentsAndRootView(const IntRect&) override { gDirty = true; }
    KeyboardUIMode keyboardUIMode() override { return static_cast<KeyboardUIMode>(KeyboardAccessFull | KeyboardAccessTabsToLinks); }
};

struct PhView {
    int id { 0 };
    RefPtr<Page> page;
    SkBitmap bitmap;
    int width { 0 }, height { 0 };
    bool visible { true };
    bool dirty { true };
    MonotonicTime lastPaint;
    // navigation tracking
    DocumentLoader* lastActive { nullptr };
    String lastTitle;
    bool sentLayout { false };
    bool loading { false };
    int lastProgress { -1 };
    int focusKind { 0 };
    const void* focusElement { nullptr }; // the focused field (identity only): a tap from one field to the next is a new focus
    ScrollPosition lastPainted;
    // our own history (BackForwardClient is the empty one): committed URLs
    std::vector<String> history;
    int historyIndex { -1 };
    bool historyNav { false };
    bool stopRequested { false };
    bool lastCanBack { false }, lastCanForward { false };
    // a first connect right after the access point came up often fails once (curl 6/7/28/35/56): retry the UI's load quietly
    int retriesLeft { 0 };
    String retryUrl;
    String requestedUrl;       // the last URL a load was started for
    bool failed { false };     // that load failed finally (no retries left): Reload / Try again retries it, not the older document
    MonotonicTime retryAt;
    // page scale + viewport (mobile model): the page lays out at `layoutW` CSS px (meta viewport, else 980) in a fixed layout and is painted
    // scaled by `zoom` (view pixels per CSS pixel); the frame view's visible size is the view size in CSS pixels (width / zoom)
    float zoom { 1 };
    float initialZoom { 1 };
    float minScale { 0.25f }, maxScale { 5 };
    int layoutW { 0 };
    bool explicitInitial { false };   // the page's meta viewport sets initial-scale: no shrink-to-fit
    bool userZoomed { false };        // the frontend zoomed (pinch / double-tap): the engine stops fitting
    bool viewportDirty { true };      // a new document: re-derive the viewport
    ViewportArguments vpArgs;
};

std::vector<PhView*> gViews;
void trackView(PhView&);   // below: derives COMMIT / FIRST_LAYOUT / TITLE; run before FINISHED is posted so the events keep their order
String gUrlToLoadInternally;   // a "new window" request, loaded into the same frame

PhView* findView(int id)
{
    for (auto* v : gViews)
        if (v->id == id)
            return v;
    return nullptr;
}

class PhFrameLoaderClient final : public EmptyFrameLoaderClient {
public:
    PhFrameLoaderClient(FrameLoader& frameLoader, int view)
        : EmptyFrameLoaderClient(frameLoader)
        , m_frameLoader(frameLoader)
        , m_view(view)
    {
    }

private:
    bool isMain() const { return m_frameLoader.frame().isMainFrame(); }

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
        int view = m_view;
        Ref subframe = LocalFrame::createSubframe(*page, [view](LocalFrame&, FrameLoader& frameLoader) -> UniqueRef<LocalFrameLoaderClient> {
            return makeUniqueRefWithoutRefCountedCheck<PhFrameLoaderClient>(frameLoader, view);
        }, generateFrameIdentifier(), sandboxFlags, referrerPolicy, ownerElement, FrameTreeSyncData::create());
        subframe->tree().setSpecifiedName(name);
        subframe->init();
        return subframe->page() ? RefPtr<LocalFrame> { WTF::move(subframe) } : nullptr;
    }
    void transitionToCommittedForNewPage(InitializingIframe) final
    {
        if (!isMain())
            m_frameLoader.frame().createView(IntSize(300, 150), std::nullopt, IntSize());
    }

    String userAgent(const URL&) const final
    {
        pthread_mutex_lock(&gLock);
        std::string ua = gUserAgent.empty() ? std::string(kDefaultUserAgent) : gUserAgent;
        pthread_mutex_unlock(&gLock);
        return String::fromUTF8(ua.c_str());
    }

    void dispatchDecidePolicyForNavigationAction(const NavigationAction&, const ResourceRequest& request, const ResourceResponse&, FormState*, const String&, std::optional<NavigationIdentifier>,
        std::optional<HitTestResult>&&, bool, NavigationUpgradeToHTTPSBehavior, SandboxFlags, PolicyDecisionMode, FramePolicyFunction&& function) final
    {
        if (isMain()) {
            PhView* v = findView(m_view);
            if (v && !request.url().protocolIsAbout() && !request.url().protocolIsData()) {
                // the UI's own load already announced itself (PH_EV_START a=0 from Load); anything else is the page's
                if (!v->loading) {
                    v->loading = true;
                    v->stopRequested = false;
                    v->sentLayout = false;
                    v->lastProgress = -1;
                    post(m_view, PH_EV_START, 1);
                }
                setStr(m_view, PH_STR_URL, request.url().string());
            }
        }
        function(PolicyAction::Use);
    }
    void dispatchDecidePolicyForResponse(const ResourceResponse& response, const ResourceRequest&, const String&, FramePolicyFunction&& function) final
    {
        function(MIMETypeRegistry::canShowMIMEType(response.mimeType()) ? PolicyAction::Use : PolicyAction::Ignore);
    }
    void dispatchDecidePolicyForNewWindowAction(const NavigationAction&, const ResourceRequest& request, FormState*, const String&, std::optional<HitTestResult>&&, FramePolicyFunction&& function) final
    {
        // target=_blank / window.open: no second window yet -- the page loads here
        gUrlToLoadInternally = request.url().string();
        function(PolicyAction::Ignore);
    }
    void committedLoad(DocumentLoader* loader, const SharedBuffer& data) final { loader->commitData(data); }
    bool canHandleRequest(const ResourceRequest&) const final { return true; }
    bool canShowMIMEType(const String& mimeType) const final { return MIMETypeRegistry::canShowMIMEType(mimeType); }
    bool canShowMIMETypeAsHTML(const String&) const final { return true; }
    void dispatchDidFinishLoad() final
    {
        if (!isMain())
            return;
        if (PhView* v = findView(m_view)) {
            // a navigation already pending (Back right after a finished page): this is the PREVIOUS document's finish, not the new load's
            if (v->loading && m_frameLoader.provisionalDocumentLoader())
                return;
            trackView(*v);
            v->loading = false;
            v->dirty = true;
        }
        post(m_view, PH_EV_FINISHED, 0);
    }
    void dispatchDidFailLoad(const ResourceError& error) final
    {
        if (!isMain() || error.isCancellation())
            return;
        fail(error);
    }
    void dispatchDidFailProvisionalLoad(const ResourceError& error, WillContinueLoading, WillInternallyHandleFailure) final
    {
        if (!isMain() || error.isCancellation())
            return;
        if (PhView* v = findView(m_view)) {
            int code = error.errorCode();
            bool transient = code == 6 || code == 7 || code == 28 || code == 35 || code == 52 || code == 56;
            if (transient && v->retriesLeft > 0 && !v->stopRequested) {
                --v->retriesLeft;
                v->retryUrl = error.failingURL().string();
                v->retryAt = MonotonicTime::now() + 2_s;
                ph_serialf("phengine: load failed (%d), retrying (%d left)", code, v->retriesLeft);
                return;
            }
        }
        fail(error);
    }
    void fail(const ResourceError& error)
    {
        if (PhView* v = findView(m_view)) {
            v->loading = false;
            v->failed = true;
        }
        setStr(m_view, PH_STR_ERROR, error.localizedDescription());
        ph_serialf("phengine: load failed: %d %s", error.errorCode(), error.localizedDescription().utf8().data());
        post(m_view, PH_EV_FAILED, error.errorCode(), error.isTimeout() ? 0 : 1);
    }

    FrameLoader& m_frameLoader;
    int m_view;
};

// ---- Web Storage (localStorage / sessionStorage) ----------------------------------------------------------------------------
// The empty clients' StorageNamespaceProvider hands out EmptyStorageArea: setItem drops the value and getItem is always null,
// so a site that keeps state in localStorage lost it on every page load. Cookie-consent managers (cookie-script.com and others)
// keep the consent string there next to their cookie and re-open the banner when it is missing: on myabandonware.com "Accept"
// and "Decline" were forgotten on the next page. Upstream the UI process keeps the areas (WebKit2) or StorageAreaImpl +
// StorageAreaSync (WebKitLegacy), neither of which is in this build. Here: session storage = an in-memory map per origin per
// page; local storage = a map per origin, loaded from and saved to one file per origin under kPhStorageDir (the browser's
// private directory, like the cookie file in ResourceHandleSymbian.cpp), so it survives a browser restart. A change marks
// the area dirty; phStorageFlush (the engine loop, and once more when the engine stops) writes dirty areas at most once a second.
static const char kPhStorageDir[] = "c:/private/e7016b20/webkit/localstorage";

class PhStorageArea;
static Vector<PhStorageArea*>& phDirtyAreas()
{
    static NeverDestroyed<Vector<PhStorageArea*>> areas;
    return areas.get();
}

class PhStorageArea final : public StorageArea {
public:
    PhStorageArea(StorageType type, unsigned quota, CString path) : m_type(type), m_map(quota), m_path(WTF::move(path))
    {
        if (!m_path.isNull())
            load();
    }
    void save()
    {
        m_dirty = false;
        String tmp = makeString(String::fromUTF8(m_path.data()), ".tmp"_s);
        CString tmpPath = tmp.utf8();
        FILE* f = fopen(tmpPath.data(), "wb");
        if (!f)
            return;
        bool ok = true;
        for (auto& item : m_map.items()) {
            CString k = item.key.utf8(), v = item.value.utf8();
            uint32_t lens[2] = { static_cast<uint32_t>(k.length()), static_cast<uint32_t>(v.length()) };
            ok &= fwrite(lens, sizeof lens, 1, f) == 1 && fwrite(k.data(), 1, k.length(), f) == k.length()
                && fwrite(v.data(), 1, v.length(), f) == v.length();
        }
        ok &= fclose(f) == 0;
        if (ok) {
            remove(m_path.data());
            rename(tmpPath.data(), m_path.data());
        } else
            remove(tmpPath.data());
    }
private:
    void load()
    {
        FILE* f = fopen(m_path.data(), "rb");
        if (!f)
            return;
        HashMap<String, String> items;
        uint32_t lens[2];
        while (fread(lens, sizeof lens, 1, f) == 1 && lens[0] < (16u << 20) && lens[1] < (16u << 20)) {
            Vector<char> buf(lens[0] + lens[1]);
            if (fread(buf.mutableSpan().data(), 1, buf.size(), f) != buf.size())
                break;
            items.set(String::fromUTF8(buf.span().first(lens[0])), String::fromUTF8(buf.span().subspan(lens[0])));
        }
        fclose(f);
        m_map.importItems(WTF::move(items));
    }
    void changed()
    {
        if (m_path.isNull() || m_dirty)
            return;
        m_dirty = true;
        ref();                      // the dirty list keeps the area alive until it is written
        phDirtyAreas().append(this);
    }
    unsigned length() final { return m_map.length(); }
    String key(unsigned index) final { return m_map.key(index); }
    String item(const String& key) final { return m_map.getItem(key); }
    void setItem(LocalFrame&, const String& key, const String& value, bool& quotaException) final
    {
        String oldValue;
        m_map.setItem(key, value, oldValue, quotaException);
        if (!quotaException)
            changed();
    }
    void removeItem(LocalFrame&, const String& key) final
    {
        String oldValue;
        m_map.removeItem(key, oldValue);
        changed();
    }
    void clear(LocalFrame&) final
    {
        m_map.clear();
        changed();
    }
    bool contains(const String& key) final { return m_map.contains(key); }
    StorageType storageType() const final { return m_type; }
    size_t memoryBytesUsedByCache() final { return 0; }

    StorageType m_type;
    StorageMap m_map;
    CString m_path;             // null = memory only (session and transient storage)
    bool m_dirty { false };
};

// the engine loop: write the dirty local storage areas (all of them when force, i.e. the engine is stopping)
static void phStorageFlush(MonotonicTime now, bool force)
{
    phCookiesFlush(force);
    static MonotonicTime last;
    if (phDirtyAreas().isEmpty() || (!force && now - last < 1_s))
        return;
    last = now;
    for (auto* area : std::exchange(phDirtyAreas(), { })) {
        area->save();
        area->deref();
    }
}

class PhStorageNamespace final : public StorageNamespace {
public:
    PhStorageNamespace(StorageType type, unsigned quota, PAL::SessionID sessionID) : m_type(type), m_quota(quota), m_sessionID(sessionID) { }
private:
    Ref<StorageArea> storageArea(const SecurityOrigin& origin) final
    {
        return m_areas.ensure(origin.data(), [&] {
            CString path;
            if (m_type == StorageType::Local && !origin.isOpaque()) {
                mkdir("c:/private/e7016b20/webkit", 0700);
                mkdir(kPhStorageDir, 0700);
                path = makeString(String::fromLatin1(kPhStorageDir), '/', origin.data().databaseIdentifier(), ".ls"_s).utf8();
            }
            return Ref<StorageArea>(adoptRef(*new PhStorageArea(m_type, m_quota, WTF::move(path))));
        }).iterator->value;
    }
    const SecurityOrigin* topLevelOrigin() const final { return nullptr; }
    Ref<StorageNamespace> copy(Page&) final { return adoptRef(*new PhStorageNamespace(m_type, m_quota, m_sessionID)); }
    PAL::SessionID sessionID() const final { return m_sessionID; }
    void setSessionIDForTesting(PAL::SessionID sessionID) final { m_sessionID = sessionID; }

    StorageType m_type;
    unsigned m_quota;
    PAL::SessionID m_sessionID;
    HashMap<SecurityOriginData, Ref<StorageArea>> m_areas;
};

class PhStorageNamespaceProvider final : public StorageNamespaceProvider {
public:
    static PhStorageNamespaceProvider& shared()
    {
        static PhStorageNamespaceProvider* provider = &adoptRef(*new PhStorageNamespaceProvider).leakRef();
        return *provider;
    }
private:
    static constexpr unsigned kQuota = 5 * 1024 * 1024;  // the 5 MB per origin desktop browsers give
    Ref<StorageNamespace> createLocalStorageNamespace(unsigned, PAL::SessionID sessionID) final
    {
        return adoptRef(*new PhStorageNamespace(StorageType::Local, kQuota, sessionID));
    }
    Ref<StorageNamespace> createTransientLocalStorageNamespace(SecurityOrigin&, unsigned, PAL::SessionID sessionID) final
    {
        return adoptRef(*new PhStorageNamespace(StorageType::TransientLocal, kQuota, sessionID));
    }
    // one session namespace per page (keyed by the pointer only; a closed view's small entry stays until the engine stops)
    RefPtr<StorageNamespace> sessionStorageNamespace(const SecurityOrigin&, Page& page, ShouldCreateNamespace create) final
    {
        auto it = m_sessionNamespaces.find(&page);
        if (it != m_sessionNamespaces.end())
            return it->value.ptr();
        if (create == ShouldCreateNamespace::No)
            return nullptr;
        Ref<StorageNamespace> ns = adoptRef(*new PhStorageNamespace(StorageType::Session, kQuota, page.sessionID()));
        m_sessionNamespaces.add(&page, ns.copyRef());
        return ns.ptr();
    }
    HashMap<Page*, Ref<StorageNamespace>> m_sessionNamespaces;
};

// ---- the engine's one-time setup -------------------------------------------------------------------------------------------
void createView(const Cmd& cmd)
{
    auto* v = new PhView;
    v->id = cmd.view;
    v->width = cmd.a;
    v->height = cmd.b;

    auto configuration = pageConfigurationWithEmptyClients(std::nullopt, PAL::SessionID::defaultSessionID());
    configuration.databaseProvider = Phorepoc::PhDatabaseProvider::create();
    // document.cookie: the same jar the network path (ResourceHandleSymbian.cpp) stores Set-Cookie in and sends Cookie from
    // (the empty clients' jar has no storage session, so scripts saw no cookies)
    configuration.cookieJar = CookieJar::create(phCookieStorageSessionProvider());
    configuration.storageNamespaceProvider = &PhStorageNamespaceProvider::shared();   // localStorage that keeps its items
    configuration.chromeClient = UniqueRef<ChromeClient>(makeUniqueRef<PhChromeClient>());
    auto& mainFrameParameters = std::get<PageConfiguration::LocalMainFrameCreationParameters>(configuration.mainFrameCreationParameters);
    mainFrameParameters.effectiveSandboxFlags = { };
    int id = v->id;
    mainFrameParameters.clientCreator = [id](LocalFrame&, FrameLoader& frameLoader) -> UniqueRef<LocalFrameLoaderClient> {
        return makeUniqueRefWithoutRefCountedCheck<PhFrameLoaderClient>(frameLoader, id);
    };
    Ref page = Page::create(WTF::move(configuration));
    v->page = page.ptr();
    page->settings().setAcceleratedCompositingEnabled(false);
    page->settings().setScriptEnabled(true);
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
    settings.setAsyncOverflowScrollingEnabled(false);
    settings.setAsyncFrameScrollingEnabled(false);

    RefPtr frame = page->localMainFrame();
    if (!frame) {
        delete v;
        return;
    }
    frame->setView(LocalFrameView::create(*frame));
    frame->init();
    v->layoutW = v->width;
    frame->view()->resize(IntSize(v->width, v->height));
    // an active, focused page: focus traversal (Tab / arrows), the caret and :focus styling need it
    page->focusController().setActive(true);
    page->focusController().setFocused(true);
    v->bitmap.allocN32Pixels(v->width, v->height);
    v->lastActive = frame->loader().documentLoader();
    gViews.push_back(v);
    ph_serialf("phengine: view %d created %dx%d", v->id, v->width, v->height);
}

void destroyView(int id)
{
    for (size_t i = 0; i < gViews.size(); ++i) {
        if (gViews[i]->id != id)
            continue;
        PhView* v = gViews[i];
        gViews.erase(gViews.begin() + i);
        if (RefPtr frame = v->page ? v->page->localMainFrame() : nullptr)
            frame->loader().stopAllLoaders();
        // the Page goes with the view (its destructor tears the frame tree down on this thread)
        v->page = nullptr;
        delete v;
        break;
    }
    pthread_mutex_lock(&gLock);
    auto it = gShared.find(id);
    if (it != gShared.end())
        it->second->alive = false;
    pthread_cond_broadcast(&gAck);
    pthread_mutex_unlock(&gLock);
}

LocalFrame* mainFrame(PhView& v) { return v.page ? v.page->localMainFrame() : nullptr; }

int visibleW(const PhView& v) { return std::max(1, static_cast<int>(std::ceil(v.width / v.zoom))); }
int visibleH(const PhView& v) { return std::max(1, static_cast<int>(std::ceil(v.height / v.zoom))); }

// the frame view for the current view size, layout width and zoom
void applyGeometry(PhView& v)
{
    RefPtr frame = mainFrame(v);
    RefPtr view = frame ? frame->view() : nullptr;
    if (!view)
        return;
    int vw = visibleW(v), vh = visibleH(v);
    int lw = v.layoutW > 0 ? v.layoutW : vw;
    view->setUseFixedLayout(true);
    view->setFixedLayoutSize(IntSize(lw, vh));
    view->resize(IntSize(vw, vh));
    v.dirty = true;
}

// the lowest zoom the user may reach: the content width fits the view (never below the page's / the default minimum scale)
float fitZoom(PhView& v)
{
    RefPtr frame = mainFrame(v);
    RefPtr view = frame ? frame->view() : nullptr;
    int cw = std::max(view ? view->contentsWidth() : 0, std::max(v.layoutW, 1));
    return std::max(v.minScale, static_cast<float>(v.width) / cw);
}

// meta viewport (width, initial / minimum / maximum scale), else a 980 px layout shown at the scale that fits the view. Text, image and media
// documents have no layout of their own to shrink: they use the device width.
void applyViewport(PhView& v)
{
    RefPtr frame = mainFrame(v);
    RefPtr document = frame ? frame->document() : nullptr;
    ViewportArguments args(ViewportArguments::Type::Implicit);
    if (document) {
        args = document->viewportArguments();
        if (document->isTextDocument() || document->isImageDocument() || document->isMediaDocument() || document->isPluginDocument()) {
            args = ViewportArguments(ViewportArguments::Type::ViewportMeta);
            args.width = ViewportArguments::ValueDeviceWidth;
        }
    }
    v.vpArgs = args;
    ViewportAttributes a = computeViewportAttributes(args, 980, v.width, v.height, 1.0f, IntSize(v.width, v.height));
    v.layoutW = std::max(1, static_cast<int>(std::lround(a.layoutSize.width())));
    v.minScale = std::max(0.05f, a.minimumScale);
    v.maxScale = std::max(v.minScale, a.maximumScale);
    v.initialZoom = a.initialScale > 0 ? a.initialScale : static_cast<float>(v.width) / 980;
    v.explicitInitial = args.zoom != ViewportArguments::ValueAuto;
    v.zoom = v.userZoomed ? std::clamp(v.zoom, v.minScale, v.maxScale) : v.initialZoom;
    applyGeometry(v);
    ph_serialf("phengine: viewport view %d: layout %d px, initial scale %d/1000, min %d, max %d (meta %s)", v.id, v.layoutW,
        static_cast<int>(v.initialZoom * 1000), static_cast<int>(v.minScale * 1000), static_cast<int>(v.maxScale * 1000),
        args.type == ViewportArguments::Type::ViewportMeta ? "yes" : "no");
}

// shrink-to-fit: a page wider than its layout viewport is shown at the scale where it fits (unless the page or the user chose a scale)
void autoFit(PhView& v)
{
    if (v.userZoomed || v.explicitInitial)
        return;
    float target = std::clamp(std::min(v.initialZoom, fitZoom(v)), v.minScale, v.maxScale);
    if (std::fabs(target - v.zoom) > 0.004f) {
        v.zoom = target;
        applyGeometry(v);
    }
}

void loadUrl(PhView& v, const String& url, bool retry = false)
{
    RefPtr frame = mainFrame(v);
    if (!frame)
        return;
    URL u { url };
    if (!u.isValid() || u.protocol().isEmpty())
        u = URL { makeString("http://"_s, url) };
    v.loading = true;
    v.stopRequested = false;
    v.requestedUrl = u.string();
    v.sentLayout = false;
    v.lastProgress = -1;
    setStr(v.id, PH_STR_URL, u.string());
    if (!retry) {
        v.retriesLeft = 3;
        post(v.id, PH_EV_START, 0);
    }
    // a URL the frontend asked for = user input (typed, a bookmark, the command line): WebKit then lets the top frame show a
    // data: URL, as desktop browsers do for a typed one (DocumentLoader::disallowDataRequest cancelled it after the response)
    FrameLoadRequest request(*frame, ResourceRequest(URL { u }));
    request.setIsRequestFromClientOrUserInput();
    frame->loader().load(WTF::move(request));
}

void historyGo(PhView& v, int delta)
{
    int target = v.historyIndex + delta;
    if (target < 0 || target >= static_cast<int>(v.history.size()))
        return;
    v.historyIndex = target;
    v.historyNav = true;
    loadUrl(v, v.history[target]);
}

void updateHistoryState(PhView& v)
{
    bool canBack = v.historyIndex > 0;
    bool canForward = v.historyIndex + 1 < static_cast<int>(v.history.size());
    if (canBack != v.lastCanBack || canForward != v.lastCanForward) {
        v.lastCanBack = canBack;
        v.lastCanForward = canForward;
        post(v.id, PH_EV_HISTORY, canBack, canForward);
    }
}

int focusKindOf(PhView& v)
{
    RefPtr frame = mainFrame(v);
    RefPtr document = frame ? frame->document() : nullptr;
    RefPtr element = document ? document->focusedElement() : nullptr;
    if (!element)
        return 0;
    if (is<HTMLAnchorElement>(*element))
        return 1;
    if (is<HTMLInputElement>(*element) || is<HTMLTextAreaElement>(*element))
        return 2;
    return 0;
}

void refreshFocus(PhView& v)
{
    int kind = focusKindOf(v);
    String href;
    if (kind == 1)
        if (RefPtr frame = mainFrame(v))
            if (RefPtr element = frame->document()->focusedElement())
                href = downcast<HTMLAnchorElement>(*element).href().string();
    setStr(v.id, PH_STR_FOCUS_URL, href);
    const void* focused = nullptr;
    if (kind == 2)
        if (RefPtr frame = mainFrame(v))
            focused = frame->document()->focusedElement();
    // a new kind, or another text field: kind 2 -> 2 (a tap from the user name straight to the password) is a new focus
    if (kind != v.focusKind || (kind == 2 && focused != v.focusElement)) {
        String value;                    // the frontend's field editor starts with it (never a password)
        int password = 0;                // b = 1: a password field (the frontend edits it in a secret editor)
        if (kind == 2)
            if (RefPtr frame = mainFrame(v))
                if (RefPtr element = frame->document()->focusedElement()) {
                    if (auto* in = dynamicDowncast<HTMLInputElement>(element.get())) {
                        if (in->isPasswordField())
                            password = 1;
                        else
                            value = in->value();
                    } else if (auto* ta = dynamicDowncast<HTMLTextAreaElement>(element.get()))
                        value = ta->value();
                }
        setStr(v.id, PH_STR_FOCUS_VALUE, value);
        if (kind == 2 && v.focusKind == 2)
            post(v.id, PH_EV_FOCUS, 0);  // the previous field lost the caret first (the frontend's cue that this is a new field)
        v.focusKind = kind;
        v.focusElement = focused;
        post(v.id, PH_EV_FOCUS, kind, password);
    }
}

void scrollBy(PhView& v, int dx, int dy)
{
    RefPtr frame = mainFrame(v);
    RefPtr view = frame ? frame->view() : nullptr;
    if (!view)
        return;
    // dx, dy are view pixels; the scroll position is in CSS pixels
    auto css = [&](int d) { return d == 0 ? 0 : (std::abs(d) < v.zoom ? (d < 0 ? -1 : 1) : static_cast<int>(std::lround(d / v.zoom))); };
    IntPoint p = view->scrollPosition();
    int maxX = std::max(0, view->contentsWidth() - visibleW(v));
    int maxY = std::max(0, view->contentsHeight() - visibleH(v));
    IntPoint q(std::clamp(p.x() + css(dx), 0, maxX), std::clamp(p.y() + css(dy), 0, maxY));
    if (q != p) {
        view->setScrollPosition(q);
        v.dirty = true;
    }
}

// zoom to `permille` about the view point (cx, cy): the CSS point under it stays under it
void zoomTo(PhView& v, int permille, int cx, int cy)
{
    RefPtr frame = mainFrame(v);
    RefPtr view = frame ? frame->view() : nullptr;
    if (!view || permille <= 0)
        return;
    float lo = fitZoom(v), hi = v.maxScale;
    float nz = std::clamp(permille / 1000.0f, lo, std::max(lo, hi));
    if (std::fabs(nz - v.zoom) < 0.004f)
        return;
    IntPoint sp = view->scrollPosition();
    float px = sp.x() + cx / v.zoom, py = sp.y() + cy / v.zoom;   // the CSS point under the focus
    v.zoom = nz;
    v.userZoomed = true;
    applyGeometry(v);
    view->updateLayoutAndStyleIfNeededRecursive();
    int maxX = std::max(0, view->contentsWidth() - visibleW(v));
    int maxY = std::max(0, view->contentsHeight() - visibleH(v));
    view->setScrollPosition(IntPoint(std::clamp(static_cast<int>(std::lround(px - cx / nz)), 0, maxX), std::clamp(static_cast<int>(std::lround(py - cy / nz)), 0, maxY)));
    v.dirty = true;
    ph_serialf("phengine: zoom view %d -> %d/1000", v.id, static_cast<int>(nz * 1000));
}

void pointer(PhView& v, int type, int x, int y)
{
    RefPtr frame = mainFrame(v);
    if (!frame)
        return;
    PlatformEvent::Type t = type == PH_PTR_DOWN ? PlatformEvent::Type::MousePressed : type == PH_PTR_UP ? PlatformEvent::Type::MouseReleased : PlatformEvent::Type::MouseMoved;
    DoublePoint pos(x / v.zoom, y / v.zoom);   // view pixels -> CSS pixels
    PlatformMouseEvent event(pos, pos, type == PH_PTR_MOVE ? MouseButton::None : MouseButton::Left, t, type == PH_PTR_MOVE ? 0 : 1, { }, MonotonicTime::now(), 0, SyntheticClickType::NoTap, MouseEventInputSource::UserDriven);
    if (type == PH_PTR_DOWN)
        frame->eventHandler().handleMousePressEvent(event);
    else if (type == PH_PTR_UP)
        frame->eventHandler().handleMouseReleaseEvent(event);
    else
        frame->eventHandler().handleMouseMoveEvent(event);
    v.dirty = true;
    refreshFocus(v);
}

enum { VK_BACK = 0x08, VK_TAB = 0x09, VK_RETURN = 0x0D, VK_PRIOR = 0x21, VK_NEXT = 0x22, VK_LEFT = 0x25, VK_UP = 0x26, VK_RIGHT = 0x27, VK_DOWN = 0x28 };

void key(PhView& v, int winKey, int unicode, int down)
{
    ph_serialf("phengine: key vk %x ch %x down %d focus %d", winKey, unicode, down, focusKindOf(v));
    RefPtr frame = mainFrame(v);
    if (!frame)
        return;
    bool editing = focusKindOf(v) == 2;
    if (!editing && down) {
        switch (winKey) {
        case VK_UP: scrollBy(v, 0, -v.height / 4); return;
        case VK_DOWN: scrollBy(v, 0, v.height / 4); return;
        case VK_PRIOR: scrollBy(v, 0, -v.height * 3 / 4); return;
        case VK_NEXT: scrollBy(v, 0, v.height * 3 / 4); return;
        case VK_LEFT:
        case VK_RIGHT:
            v.page->focusController().advanceFocus(winKey == VK_LEFT ? FocusDirection::Backward : FocusDirection::Forward, nullptr);
            v.dirty = true;
            refreshFocus(v);
            return;
        default:
            break;
        }
    }
    String text = unicode >= 0x20 ? String(std::span<const char16_t> { reinterpret_cast<const char16_t*>(&unicode), 1 }) : winKey == VK_RETURN ? "\r"_s : String();
    OptionSet<PlatformEvent::Modifier> none;
    // DOM key / identifier: a link's Enter handling (HTMLAnchorElement) tests keyIdentifier == "Enter"
    String ident = winKey == VK_RETURN ? "Enter"_s : winKey == VK_TAB ? "U+0009"_s : winKey == VK_BACK ? "U+0008"_s : winKey == VK_UP ? "Up"_s : winKey == VK_DOWN ? "Down"_s
        : winKey == VK_LEFT ? "Left"_s : winKey == VK_RIGHT ? "Right"_s : String();
    String domKey = winKey == VK_RETURN ? "Enter"_s : winKey == VK_TAB ? "Tab"_s : winKey == VK_BACK ? "Backspace"_s : winKey == VK_UP ? "ArrowUp"_s : winKey == VK_DOWN ? "ArrowDown"_s
        : winKey == VK_LEFT ? "ArrowLeft"_s : winKey == VK_RIGHT ? "ArrowRight"_s : text;
    if (down) {
        // the field's value (the EmptyEditorClient inserts nothing by itself: a typed character / Backspace that left the value
        // unchanged is applied through the Editor)
        auto fieldValue = [&]() -> String {
            RefPtr doc = frame->document();
            RefPtr el = doc ? doc->focusedElement() : nullptr;
            if (auto* in = dynamicDowncast<HTMLInputElement>(el.get()))
                return in->value();
            if (auto* ta = dynamicDowncast<HTMLTextAreaElement>(el.get()))
                return ta->value();
            return String();
        };
        String before = editing ? fieldValue() : String();
        PlatformKeyboardEvent raw(PlatformEvent::Type::RawKeyDown, text, text, domKey, domKey, ident, winKey, false, false, false, none, MonotonicTime::now());
        frame->eventHandler().keyEvent(raw);
        if (!text.isEmpty()) {
            PlatformKeyboardEvent ch(PlatformEvent::Type::Char, text, text, domKey, domKey, ident, winKey, false, false, false, none, MonotonicTime::now());
            frame->eventHandler().keyEvent(ch);
        }
        if (editing && winKey != VK_RETURN && fieldValue() == before) {
            // the Editor path did not take (the selection is not in the field here): append / drop the last character in the value
            RefPtr doc = frame->document();
            RefPtr el = doc ? doc->focusedElement() : nullptr;
            String nv = unicode >= 0x20 ? makeString(before, text) : winKey == VK_BACK && before.length() ? before.left(before.length() - 1) : before;
            if (auto* in = dynamicDowncast<HTMLInputElement>(el.get()))
                in->setValue(nv, DispatchInputAndChangeEvent);
            else if (auto* ta = dynamicDowncast<HTMLTextAreaElement>(el.get()))
                ta->setValue(nv, DispatchInputAndChangeEvent);
            ph_serialf("phengine: key applied through the Editor: value %u -> %u chars", before.length(), fieldValue().length());
        }
    } else {
        PlatformKeyboardEvent up(PlatformEvent::Type::KeyUp, text, text, domKey, domKey, ident, winKey, false, false, false, none, MonotonicTime::now());
        frame->eventHandler().keyEvent(up);
    }
    v.dirty = true;
    refreshFocus(v);
}

void command(PhView& v, int cmd)
{
    RefPtr frame = mainFrame(v);
    if (!frame)
        return;
    switch (cmd) {
    case PH_CMD_STOP:
        v.retryUrl = String();
        if (v.loading) {
            v.stopRequested = true;
            frame->loader().stopAllLoaders();
            v.loading = false;
            post(v.id, PH_EV_FINISHED, 1);   // a = 1: cancelled
        }
        break;
    case PH_CMD_RELOAD: {
        // after a failed load the document is still the previous page: Reload (the frontend's Try again) retries the failed URL
        String url = v.failed && !v.requestedUrl.isEmpty() ? v.requestedUrl : (frame->document() ? frame->document()->url().string() : String());
        if (!url.isEmpty())
            loadUrl(v, url);
        break;
    }
    case PH_CMD_BACK:
        historyGo(v, -1);
        break;
    case PH_CMD_FORWARD:
        historyGo(v, 1);
        break;
    case PH_CMD_ACTIVATE:
        key(v, VK_RETURN, 0, 1);
        key(v, VK_RETURN, 0, 0);
        break;
    }
}

void setting(PhView& v, int id, int value)
{
    if (!v.page)
        return;
    switch (id) {
    case PH_SET_SCRIPT:
        v.page->settings().setScriptEnabled(value != 0);
        break;
    case PH_SET_IMAGES:
        v.page->settings().setLoadsImagesAutomatically(value != 0);
        break;
    default:
        break;
    }
}

void resizeView(PhView& v, int w, int h)
{
    if (w <= 0 || h <= 0 || (w == v.width && h == v.height))
        return;
    v.width = w;
    v.height = h;
    v.bitmap.reset();
    v.bitmap.allocN32Pixels(w, h);
    applyViewport(v);   // the layout width and the fit follow the view size (rotation)
    v.dirty = true;
}

void selectAccessPoint(const std::string& name)
{
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, name.c_str(), sizeof ifr.ifr_name - 1);
    ph_serialf("phengine: setdefaultif(%s) -> %d", ifr.ifr_name, setdefaultif(&ifr));
}

void runCommands()
{
    for (;;) {
        pthread_mutex_lock(&gLock);
        if (!gCmdCount) {
            pthread_mutex_unlock(&gLock);
            return;
        }
        static Cmd cmd;   // (engine thread only; a POD copy out of the ring)
        cmd = gCmdRing[gCmdHead];
        gCmdHead = (gCmdHead + 1) % kCmds;
        --gCmdCount;
        pthread_mutex_unlock(&gLock);
        if (cmd.type == Cmd::Create) {
            createView(cmd);
            continue;
        }
        if (cmd.type == Cmd::Destroy) {
            destroyView(cmd.view);
            continue;
        }
        if (cmd.type == Cmd::SetIap) {
            selectAccessPoint(std::string(cmd.utf8));
            continue;
        }
        PhView* v = findView(cmd.view);
        if (!v)
            continue;
        switch (cmd.type) {
        case Cmd::Resize: resizeView(*v, cmd.a, cmd.b); break;
        case Cmd::Visible: v->visible = cmd.a; v->dirty = true; break;
        case Cmd::Load: loadUrl(*v, fromU16(cmd.text, cmd.textLen)); break;
        case Cmd::Command: command(*v, cmd.a); break;
        case Cmd::Pointer: pointer(*v, cmd.a, cmd.b, cmd.c); break;
        case Cmd::Scroll: scrollBy(*v, cmd.a, cmd.b); break;
        case Cmd::Zoom: zoomTo(*v, cmd.a, cmd.b, cmd.c); break;
        case Cmd::Key: key(*v, cmd.a, cmd.b, cmd.c); break;
        case Cmd::SetField:                   // the frontend's field editor committed: the whole new value
            if (RefPtr frame = mainFrame(*v))
                if (RefPtr element = frame->document()->focusedElement()) {
                    String nv = fromU16(cmd.text, cmd.textLen);
                    if (auto* in = dynamicDowncast<HTMLInputElement>(element.get()))
                        in->setValue(nv, DispatchInputAndChangeEvent);
                    else if (auto* ta = dynamicDowncast<HTMLTextAreaElement>(element.get()))
                        ta->setValue(nv, DispatchInputAndChangeEvent);
                    v->dirty = true;
                }
            break;
        case Cmd::Setting: setting(*v, cmd.a, cmd.b); break;
        default: break;
        }
    }
}

// derive the navigation events the loader does not hand us directly
void trackView(PhView& v)
{
    RefPtr frame = mainFrame(v);
    if (!frame)
        return;
    RefPtr document = frame->document();
    DocumentLoader* active = frame->loader().documentLoader();
    if (active && active != v.lastActive) {
        v.lastActive = active;
        String url = active->url().string();
        if (!url.isEmpty() && url != "about:blank"_s) {
            if (v.historyNav)
                v.historyNav = false;
            else if (v.historyIndex >= 0 && v.history[v.historyIndex] == url)
                ;   // a reload
            else {
                v.history.resize(v.historyIndex + 1);
                v.history.push_back(url);
                v.historyIndex = static_cast<int>(v.history.size()) - 1;
            }
            v.failed = false;
            setStr(v.id, PH_STR_URL, url);
            v.lastTitle = String();
            v.sentLayout = false;
            v.viewportDirty = true;
            v.userZoomed = false;
            v.dirty = true;
            post(v.id, PH_EV_COMMIT);
            updateHistoryState(v);
        }
    }
    RefPtr view = frame->view();
    if (document && view) {
        if (v.viewportDirty || document->viewportArguments() != v.vpArgs) {   // a new document, or its meta viewport arrived / changed
            v.viewportDirty = false;
            applyViewport(v);
        }
        autoFit(v);
        if (!v.sentLayout && v.lastActive && document->documentElement() && view->didFirstLayout() && view->isVisuallyNonEmpty()) {
            v.sentLayout = true;
            v.dirty = true;
            post(v.id, PH_EV_FIRST_LAYOUT);
        }
        String title = document->title();
        if (title != v.lastTitle) {
            v.lastTitle = title;
            setStr(v.id, PH_STR_TITLE, title);
            if (!title.isEmpty())
                post(v.id, PH_EV_TITLE);
        }
        if (view->scrollPosition() != v.lastPainted)
            v.dirty = true;
    }
    if (v.loading && v.page) {
        int p = static_cast<int>(v.page->progress().estimatedProgress() * 100);
        if (p != v.lastProgress) {
            v.lastProgress = p;
            post(v.id, PH_EV_PROGRESS, p);
        }
    }
    // state snapshot for the facade
    PhViewState st { };
    st.canBack = v.lastCanBack;
    st.canForward = v.lastCanForward;
    st.loading = v.loading;
    st.progress = v.lastProgress < 0 ? 0 : v.lastProgress;
    st.focusKind = v.focusKind;
    if (view) {
        // view pixels (the zoom applied), as the seam says
        st.scrollX = static_cast<int>(std::lround(view->scrollPosition().x() * v.zoom));
        st.scrollY = static_cast<int>(std::lround(view->scrollPosition().y() * v.zoom));
        st.contentW = static_cast<int>(std::ceil(view->contentsWidth() * v.zoom));
        st.contentH = static_cast<int>(std::ceil(view->contentsHeight() * v.zoom));
        st.zoomPermille = static_cast<int>(std::lround(v.zoom * 1000));
    }
    pthread_mutex_lock(&gLock);
    auto it = gShared.find(v.id);
    if (it != gShared.end())
        it->second->state = st;
    pthread_mutex_unlock(&gLock);
}

void paintView(PhView& v)
{
    RefPtr frame = mainFrame(v);
    RefPtr view = frame ? frame->view() : nullptr;
    if (!view)
        return;
    view->updateLayoutAndStyleIfNeededRecursive();
    v.bitmap.eraseColor(SK_ColorWHITE);
    {
        SkCanvas canvas(v.bitmap);
        GraphicsContextSkia context(canvas, RenderingMode::Unaccelerated, RenderingPurpose::Unspecified);
        context.scale(FloatSize(v.zoom, v.zoom));
        view->paint(context, IntRect(0, 0, visibleW(v), visibleH(v)));
    }
    v.lastPainted = view->scrollPosition();
    pthread_mutex_lock(&gLock);
    auto it = gShared.find(v.id);
    if (it != gShared.end()) {
        Shared& s = *it->second;
        // the UI thread sized `front` for ITS view size; a resize still in flight is repainted by the next frame
        int w = v.width < s.fw ? v.width : s.fw, h = v.height < s.fh ? v.height : s.fh;
        if (s.front && w > 0 && h > 0) {
            const unsigned* src = static_cast<const unsigned*>(v.bitmap.getPixels());
            for (int y = 0; y < h; ++y)
                memcpy(s.front + static_cast<size_t>(y) * s.fw, src + static_cast<size_t>(y) * v.width, w * sizeof(unsigned));
            s.hasPixels = true;
        }
    }
    pthread_mutex_unlock(&gLock);
    post(v.id, PH_EV_REPAINT);
}

} // namespace

// ---- the C API --------------------------------------------------------------------------------------------------------------
extern "C" {

int ph_eng_new_view_id(void) { return __sync_fetch_and_add(&gNextView, 1); }

static void initCmd(Cmd& c, Cmd::Type type, int view)
{
    c.type = type;
    c.view = view;
    c.a = c.b = c.c = 0;
    c.textLen = 0;
    c.utf8[0] = 0;
}

static void enqueue(const Cmd& cmd)
{
    pthread_mutex_lock(&gLock);
    while (gCmdCount >= kCmds) {   // the engine is far behind: wait for it (the ring holds POD copies, nothing is freed across threads)
        pthread_mutex_unlock(&gLock);
        usleep(2000);
        pthread_mutex_lock(&gLock);
    }
    gCmdRing[(gCmdHead + gCmdCount) % kCmds] = cmd;
    ++gCmdCount;
    pthread_mutex_unlock(&gLock);
}

static unsigned* newFront(int w, int h)
{
    size_t n = static_cast<size_t>(w > 0 ? w : 1) * (h > 0 ? h : 1);
    unsigned* p = new unsigned[n];
    for (size_t i = 0; i < n; ++i)
        p[i] = 0xFFFFFFFFu;
    return p;
}

void ph_eng_view_create(int view, int width, int height)
{
    Shared* s = new Shared;
    s->front = newFront(width, height);
    s->fw = width > 0 ? width : 1;
    s->fh = height > 0 ? height : 1;
    pthread_mutex_lock(&gLock);
    gShared[view] = s;
    pthread_mutex_unlock(&gLock);
    Cmd c;
    initCmd(c, Cmd::Create, view);
    c.a = width;
    c.b = height;
    enqueue(c);
}

void ph_eng_view_destroy(int view)
{
    if (gRunning) {
        Cmd c;
        initCmd(c, Cmd::Destroy, view);
        enqueue(c);
        pthread_mutex_lock(&gLock);
        for (;;) {
            auto it = gShared.find(view);
            if (it == gShared.end() || !it->second->alive || !gRunning)
                break;
            pthread_cond_wait(&gAck, &gLock);
        }
        pthread_mutex_unlock(&gLock);
    }
    Shared* dead = nullptr;
    pthread_mutex_lock(&gLock);
    auto it = gShared.find(view);
    if (it != gShared.end()) {
        it->second->alive = false;
        dead = it->second;
        gShared.erase(it);
    }
    pthread_mutex_unlock(&gLock);
    if (dead) {
        delete[] dead->front;   // allocated by the UI thread (newFront), freed by it
        delete dead;
    }
}

void ph_eng_view_resize(int view, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    unsigned* fresh = newFront(w, h);
    unsigned* old = nullptr;
    pthread_mutex_lock(&gLock);
    auto it = gShared.find(view);
    if (it != gShared.end()) {
        old = it->second->front;
        it->second->front = fresh;
        it->second->fw = w;
        it->second->fh = h;
        it->second->hasPixels = false;
        fresh = nullptr;
    }
    pthread_mutex_unlock(&gLock);
    delete[] old;
    delete[] fresh;
    Cmd c;
    initCmd(c, Cmd::Resize, view);
    c.a = w;
    c.b = h;
    enqueue(c);
}

void ph_eng_view_visible(int view, int visible)
{
    Cmd c;
    initCmd(c, Cmd::Visible, view);
    c.a = visible;
    enqueue(c);
}

void ph_eng_set_iap(const char* name)
{
    Cmd c;
    initCmd(c, Cmd::SetIap, 0);
    if (name) {
        strncpy(c.utf8, name, sizeof(c.utf8) - 1);
        c.utf8[sizeof(c.utf8) - 1] = 0;
    }
    enqueue(c);
}

void ph_eng_set_useragent(const unsigned short* s, int len)
{
    std::string ua;
    for (int i = 0; s && i < len && i < 512; ++i)
        if (s[i] >= 0x20 && s[i] < 0x7f)
            ua += (char)s[i];
    pthread_mutex_lock(&gLock);
    gUserAgent = ua;
    pthread_mutex_unlock(&gLock);
}

void ph_eng_load(int view, const unsigned short* url, int len)
{
    Cmd c;
    initCmd(c, Cmd::Load, view);
    c.textLen = len < kTextMax ? len : kTextMax;
    memcpy(c.text, url, c.textLen * sizeof(unsigned short));
    enqueue(c);
}

void ph_eng_set_field(int view, const unsigned short* s, int len)
{
    Cmd c;
    initCmd(c, Cmd::SetField, view);
    c.textLen = len < kTextMax ? len : kTextMax;
    memcpy(c.text, s, c.textLen * sizeof(unsigned short));
    enqueue(c);
}

void ph_eng_cmd(int view, int cmd)
{
    Cmd c;
    initCmd(c, Cmd::Command, view);
    c.a = cmd;
    enqueue(c);
}

void ph_eng_pointer(int view, int type, int x, int y)
{
    Cmd c;
    initCmd(c, Cmd::Pointer, view);
    c.a = type;
    c.b = x;
    c.c = y;
    enqueue(c);
}

void ph_eng_scroll(int view, int dx, int dy)
{
    Cmd c;
    initCmd(c, Cmd::Scroll, view);
    c.a = dx;
    c.b = dy;
    enqueue(c);
}

void ph_eng_zoom(int view, int permille, int cx, int cy)
{
    Cmd c;
    initCmd(c, Cmd::Zoom, view);
    c.a = permille;
    c.b = cx;
    c.c = cy;
    enqueue(c);
}

void ph_eng_key(int view, int winKey, int unicode, int down)
{
    Cmd c;
    initCmd(c, Cmd::Key, view);
    c.a = winKey;
    c.b = unicode;
    c.c = down;
    enqueue(c);
}

void ph_eng_setting(int view, int id, int value)
{
    Cmd c;
    initCmd(c, Cmd::Setting, view);
    c.a = id;
    c.b = value;
    enqueue(c);
}

int ph_eng_poll(int view, PhEvent* out)
{
    int got = 0;
    pthread_mutex_lock(&gLock);
    auto it = gShared.find(view);
    if (it != gShared.end()) {
        Shared& s = *it->second;
        if (s.evCount) {
            *out = s.events[s.evHead];
            s.evHead = (s.evHead + 1) % kEvents;
            --s.evCount;
            if (out->kind == PH_EV_REPAINT)
                s.repaintQueued = false;
            got = 1;
        } else
            s.notifyPending = false;
    }
    pthread_mutex_unlock(&gLock);
    return got;
}

void ph_eng_get_state(int view, PhViewState* out)
{
    memset(out, 0, sizeof *out);
    pthread_mutex_lock(&gLock);
    auto it = gShared.find(view);
    if (it != gShared.end())
        *out = it->second->state;
    pthread_mutex_unlock(&gLock);
}

int ph_eng_get_str(int view, int which, unsigned short* buf, int cap)
{
    int len = 0;
    pthread_mutex_lock(&gLock);
    auto it = gShared.find(view);
    if (it != gShared.end() && which >= 0 && which < 7) {
        len = it->second->strLen[which];
        int n = len < cap ? len : cap;
        if (n > kStrMax)
            n = kStrMax;
        if (buf && n > 0)
            memcpy(buf, it->second->str[which], n * sizeof(unsigned short));
    }
    pthread_mutex_unlock(&gLock);
    return len;
}

int ph_eng_lock_pixels(int view, const unsigned int** px, int* width, int* height, int* strideBytes)
{
    pthread_mutex_lock(&gLock);
    auto it = gShared.find(view);
    if (it == gShared.end() || !it->second->front || !it->second->hasPixels) {
        pthread_mutex_unlock(&gLock);
        return 0;
    }
    Shared& s = *it->second;
    *px = s.front;
    *width = s.fw;
    *height = s.fh;
    *strideBytes = s.fw * 4;
    return 1;   // the lock stays held until ph_eng_unlock_pixels
}

void ph_eng_unlock_pixels(int) { pthread_mutex_unlock(&gLock); }

int ph_eng_running(void) { return gRunning; }

void ph_eng_quit(void) { gQuit = 1; }

int ph_eng_main(PhEngNotify notify, void* ctx)
{
    gNotify = notify;
    gNotifyCtx = ctx;
    ph_serialf("phengine: start");
    curl_global_init(CURL_GLOBAL_DEFAULT);
    JSC::initialize();
    WTF::initializeMainThread();
    Phorepoc::PhPlatformStrategies::initialize();
    WebCore::Process::setIdentifier(WebCore::Process::generateIdentifier());
    gRunning = 1;
    ph_serialf("phengine: running");
    MonotonicTime nextFrame;
    // Self-profile (one "phengine: prof" line per 5 s): where the engine thread's time goes. On the phone the thread sat at ~100% of a
    // core after the failed loads; the usleep below bounds the loop rate, so a hot thread means one section is expensive.
    MonotonicTime profAt = MonotonicTime::now() + 5_s;
    double tCmd = 0, tCycle = 0, tRender = 0, tPaint = 0;
    unsigned iters = 0, paints = 0, slowCycles = 0;
    MonotonicTime lastActive = MonotonicTime::now();
    while (!gQuit) {
        MonotonicTime t0 = MonotonicTime::now();
        runCommands();
        MonotonicTime t1 = MonotonicTime::now();
        RunLoop::cycle();
        MonotonicTime now = MonotonicTime::now();
        tCmd += (t1 - t0).seconds();
        tCycle += (now - t1).seconds();
        if ((now - t1).seconds() > 0.05)
            ++slowCycles;
        ++iters;
        bool busy = false;
        bool anyLoading = false;
        for (auto* v : gViews)
            anyLoading |= v->loading || !v->retryUrl.isEmpty();
        // Idle (no load in flight, nothing dirty, no command for a second): the per-frame rendering update only runs every 100 ms.
        bool quiet = !anyLoading && now - lastActive > 1_s;
        if (now >= nextFrame) {
            nextFrame = now + (quiet ? 100_ms : 16_ms);
            for (auto* v : gViews) {
                v->page->isolatedUpdateRendering();
                trackView(*v);
                busy |= v->loading;
            }
            tRender += (MonotonicTime::now() - now).seconds();
        }
        for (auto* v : gViews)
            if (!v->retryUrl.isEmpty() && now >= v->retryAt)
                loadUrl(*v, std::exchange(v->retryUrl, String()), true);
        if (!gUrlToLoadInternally.isEmpty()) {
            String url = std::exchange(gUrlToLoadInternally, String());
            if (!gViews.empty())
                loadUrl(*gViews.front(), url);
        }
        if (gDirty) {
            gDirty = false;
            for (auto* v : gViews)
                v->dirty = true;
        }
        for (auto* v : gViews) {
            // a repaint at most every 80 ms (every 250 ms while a load is in flight and nothing else asked)
            if (v->dirty && v->visible && now - v->lastPaint >= 80_ms) {
                v->dirty = false;
                v->lastPaint = now;
                MonotonicTime p0 = MonotonicTime::now();
                paintView(*v);
                tPaint += (MonotonicTime::now() - p0).seconds();
                ++paints;
                lastActive = now;
            }
        }
        pthread_mutex_lock(&gLock);
        bool idle = !gCmdCount;
        pthread_mutex_unlock(&gLock);
        if (!idle || busy)
            lastActive = now;
        if (now >= profAt) {
            profAt = now + 5_s;
            ph_serialf("phengine: prof %u iters, cmds %.0f ms, cycle %.0f ms (%u slow), render %.0f ms, paint %.0f ms (%u), loading %d",
                iters, tCmd * 1000, tCycle * 1000, slowCycles, tRender * 1000, tPaint * 1000, paints, anyLoading ? 1 : 0);
            iters = paints = slowCycles = 0;
            tCmd = tCycle = tRender = tPaint = 0;
        }
        phStorageFlush(now, false);
        usleep(idle && !busy ? (quiet ? 40000 : 20000) : 5000);
    }
    phStorageFlush(MonotonicTime::now(), true);
    gRunning = 0;
    pthread_mutex_lock(&gLock);
    pthread_cond_broadcast(&gAck);
    pthread_mutex_unlock(&gLock);
    return 0;
}

} // extern "C"
