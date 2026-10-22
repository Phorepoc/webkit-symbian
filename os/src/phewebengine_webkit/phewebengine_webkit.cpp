// Copyright The Phorepoc Authors
// SPDX-License-Identifier: Apache-2.0

// phewebengine_webkit.cpp -- the WebKit engine as a Phorepoc web-engine package: an ECom plug-in implementing the PheWebEngineApi
// table (phewebengine repository, interface 0xE7016B01) over the existing embedding seam (PhEngineApi.h, the same engine thread
// that browserengine.dll / phebrctl drives). A thin function table: every call maps to a ph_eng_* call, the events are translated,
// the pixels are the engine's own buffer. Capabilities are reported honestly -- see KCaps below.
#include <e32std.h>
#include <e32base.h>
#include <e32debug.h>
#include <e32math.h>
#include <es_sock.h>
#include <es_enum.h>
#ifdef __arm__
#include <commdb.h>
#include <cdbcols.h>
#include <commdbconnpref.h>
#include <f32file.h>
#endif
#include <centralrepository.h>
#include <ecom/implementationproxy.h>
#include <string.h>
#include <phorepoc/phewebengine_ecom.h>
#include "PhEngineApi.h"

namespace
{

// ---- the engine thread (one per process) ------------------------------------------------------------------------------------
PheNotify gNotify = NULL;
void* gNotifyCtx = NULL;
RThread gEngine;
TBool gEngineStarted = EFalse;
TBool gNetDone = EFalse;

// ON THE ENGINE THREAD: forward to the frontend's notify (which only completes a request on the UI thread)
void NotifyTrampoline(void*, int aView)
	{
	if (gNotify)
		gNotify(gNotifyCtx, aView);
	}

TInt EngineThreadFn(TAny*)
	{
	CTrapCleanup* cleanup = CTrapCleanup::New();
	ph_eng_main(NotifyTrampoline, NULL);
	delete cleanup;
	return KErrNone;
	}

void Narrow(const TDesC& aSrc, TDes8& aDst)
    {
    aDst.Zero();
    for (TInt i = 0; i < aSrc.Length() && aDst.Length() < aDst.MaxLength(); ++i)
        aDst.Append(aSrc[i] < 0x80 ? (TUint8)aSrc[i] : '?');
    }

// BrowserNG's connection manager gave no connection (NetworkConnectionNeededL left: on the phone the access point is
// brought up by someone else, e.g. wlanjoin, or by the system's default-connection logic). Name the interface of an
// already running connection instead of the x86 fallback: enumerate the comms connections, attach to the first one and
// read its IAP name (the Open C interface name curl needs for setdefaultif). The attached RConnection (and its socket
// server session) is kept for the process lifetime, which also keeps the interface from being closed under the engine.
RSocketServ gSockServ;
RConnection gAttached;
TBool gAttachedOpen = EFalse;

#ifdef __arm__
// Nothing is up (the phone has no default access point for the browser and the system brings none up on its own): start the
// WLAN IAP the way wlanjoin does -- RConnection::Start with a TCommDbConnPref naming the IAP id, no dialog -- and keep the
// connection for the process lifetime. Which IAP: C:/data/phweb_iap.txt (as phwebtest), else the stored "ASUS", else the
// first IAP on the LAN bearer. Blocks the calling (UI) thread until the join/DHCP finished or 90 s passed.
TBool StartWlanIapL(TDes8& aName)
    {
    TBuf<64> want;
    {
    RFs fs;
    if (fs.Connect() == KErrNone)
        {
        RFile f;
        if (f.Open(fs, _L("C:\\data\\phweb_iap.txt"), EFileRead) == KErrNone)
            {
            TBuf8<64> b;
            if (f.Read(b) == KErrNone)
                {
                TInt n = b.Locate('\r');
                if (n < 0) n = b.Locate('\n');
                if (n >= 0) b.SetLength(n);
                want.Copy(b);
                }
            f.Close();
            }
        fs.Close();
        }
    }
    if (want.Length() == 0)
        want = _L("ASUS");
    CCommsDatabase* db = CCommsDatabase::NewL();
    CleanupStack::PushL(db);
    TUint32 iap = 0;
    TBuf<64> iapName;
    CCommsDbTableView* v = db->OpenViewMatchingTextLC(TPtrC(IAP), TPtrC(COMMDB_NAME), want);
    if (v->GotoFirstRecord() == KErrNone)
        {
        v->ReadUintL(TPtrC(COMMDB_ID), iap);
        iapName = want;
        }
    CleanupStack::PopAndDestroy(v);
    if (!iap)
        {
        v = db->OpenViewMatchingTextLC(TPtrC(IAP), TPtrC(IAP_BEARER_TYPE), _L("LANBearer"));
        if (v->GotoFirstRecord() == KErrNone)
            {
            v->ReadUintL(TPtrC(COMMDB_ID), iap);
            v->ReadTextL(TPtrC(COMMDB_NAME), iapName);
            }
        CleanupStack::PopAndDestroy(v);
        }
    CleanupStack::PopAndDestroy(db);
    RDebug::Printf("PH: [phebrctl] WLAN IAP %u '%S'", iap, &iapName);
    if (!iap)
        return EFalse;
    if (gAttachedOpen)
        gAttached.Close();
    gAttachedOpen = EFalse;
    User::LeaveIfError(gAttached.Open(gSockServ));
    TCommDbConnPref pref;
    pref.SetIapId(iap);
    pref.SetDialogPreference(ECommDbDialogPrefDoNotPrompt);
    TRequestStatus st;
    gAttached.Start(pref, st);
    RTimer t;
    t.CreateLocal();
    TRequestStatus ts;
    t.After(ts, 90 * 1000000);
    User::WaitForRequest(st, ts);
    TInt r = st.Int();
    if (st == KRequestPending)
        {
        gAttached.Stop();
        User::WaitForRequest(st);
        r = KErrTimedOut;
        }
    else
        {
        t.Cancel();
        User::WaitForRequest(ts);
        }
    t.Close();
    RDebug::Printf("PH: [phebrctl] RConnection::Start(iap %u) -> %d", iap, r);
    if (r != KErrNone)
        {
        gAttached.Close();
        return EFalse;
        }
    gAttachedOpen = ETrue;
    Narrow(iapName, aName);
    return aName.Length() > 0;
    }
#endif

TBool ActiveIapName(TDes8& aName)
    {
    if (!gAttachedOpen)
        {
        if (gSockServ.Connect() != KErrNone)
            return EFalse;
        RConnection probe;
        if (probe.Open(gSockServ) != KErrNone)
            return EFalse;
        TUint count = 0;
        TInt r = probe.EnumerateConnections(count);
        TBool attached = EFalse;
        if (r == KErrNone)
            {
            for (TUint i = 1; i <= count && !attached; ++i)
                {
                TPckgBuf<TConnectionInfo> info;
                if (probe.GetConnectionInfo(i, info) != KErrNone)
                    continue;
#ifndef __arm__
                break;      // x86: never attach to a connection another process owns (it may be going down, e.g. nettest exiting): Start our own below
#endif
                if (gAttached.Open(gSockServ) != KErrNone)
                    break;
                if (gAttached.Attach(info, RConnection::EAttachTypeNormal) == KErrNone)
                    attached = ETrue;
                else
                    gAttached.Close();
                }
            }
        RDebug::Printf("PH: [phebrctl] active connections %d (enum %d) attached %d", count, r, attached);
        probe.Close();
        if (!attached)
            {
#ifdef __arm__
            TBool up = EFalse;
            TRAPD(sr, up = StartWlanIapL(aName));
            RDebug::Printf("PH: [phebrctl] start WLAN: left %d up %d", sr, up);
            return sr == KErrNone && up;
#else
            // no browser connection manager here (phebrctl gets the connection from BrowserNG): bring the default access point
            // up ourselves, no dialog, and keep the connection for the process lifetime
            if (gAttached.Open(gSockServ) != KErrNone)
                return EFalse;
            TRequestStatus cst;
            gAttached.Start(cst);
            User::WaitForRequest(cst);
            RDebug::Printf("PH: [phewebengine_webkit] RConnection::Start(default) -> %d", cst.Int());
            if (cst.Int() != KErrNone)
                {
                gAttached.Close();
                return EFalse;
                }
#endif
            }
        gAttachedOpen = ETrue;
        }
    TBuf<64> iapName;
    _LIT(KIapName, "IAP\\Name");
    if (gAttached.GetDesSetting(KIapName, iapName) != KErrNone || iapName.Length() == 0)
        return EFalse;
    Narrow(iapName, aName);
    RDebug::Printf("PH: [phebrctl] active IAP '%S'", &iapName);
    return aName.Length() > 0;
    }

// the engine's default interface (curl's setdefaultif): an access point that is already up, else the x86 fallback name.
// Done once, on the first navigation (the UI thread; on the phone it can bring the WLAN up and block for a while).
void EnsureNetwork()
	{
	if (gNetDone)
		return;
	gNetDone = ETrue;
	TBuf8<96> name;
	ActiveIapName(name);
	if (name.Length() == 0)
		name.Copy(_L8("Ethernet - DHCP"));
	TBuf8<100> z(name);
	z.Append(0);
	ph_eng_set_iap((const char*)z.Ptr());
	}

void StartEngineL()
	{
	if (gEngineStarted)
		return;
	// the User-Agent: CenRep 0xE7015B01 key 1 (set by the image); empty/missing = the engine's default
		{
		TBuf<512> ua;
		CRepository* repo = NULL;
		TRAPD(re, repo = CRepository::NewL(TUid::Uid(0xE7015B01)));
		TInt ge = re;
		if (re == KErrNone)
			{
			ge = repo->Get(1, ua);
			delete repo;
			}
		ph_eng_set_useragent(ua.Ptr(), ge == KErrNone ? ua.Length() : 0);
		}
	// its own heap (WebKit allocates through malloc = this thread's heap) and the flexible memory model's largest stack
	_LIT(KName, "phengine");
	User::LeaveIfError(gEngine.Create(KName, EngineThreadFn, 0x100000, 0x100000, 0x08000000, NULL));
	gEngine.SetPriority(EPriorityNormal);
	gEngine.Resume();
	gEngineStarted = ETrue;
	RDebug::Printf("PH: [phewebengine_webkit] engine thread started");
	}

// ---- the views the frontend created (UI thread only): poll(0) walks them round-robin --------------------------------------------
RArray<TInt> gViews;
TInt gNextPoll = 0;

// the security level the seam wants, from the committed URL (the engine reports no certificate state: a failed TLS handshake
// is a FAILED load, so a committed https page is a valid https page)
int SecurityOf(int aView)
	{
	unsigned short u[16];
	const int n = ph_eng_get_str(aView, PH_STR_URL, u, 16);
	if (n >= 6 && u[0] == 'h' && u[1] == 't' && u[2] == 't' && u[3] == 'p' && u[4] == 's' && u[5] == ':')
		return PHE_SEC_SECURE;
	return PHE_SEC_NONE;
	}

// the TKeyCode values the frontend forwards -> the Windows virtual-key codes the engine expects
int VirtualKey(int aCode)
	{
	switch (aCode)
		{
	case EKeyUpArrow: return 0x26;
	case EKeyDownArrow: return 0x28;
	case EKeyLeftArrow: return 0x25;
	case EKeyRightArrow: return 0x27;
	case EKeyPageUp: return 0x21;
	case EKeyPageDown: return 0x22;
	case EKeyEnter:
	case EKeyDevice3: return 0x0D;
	case EKeyTab: return 0x09;
	case EKeyBackspace: return 0x08;
	default: return 0;
		}
	}

// ---- the table ---------------------------------------------------------------------------------------------------------------
int A_init(PheNotify aNotify, void* aCtx)
	{
	gNotify = aNotify;
	gNotifyCtx = aCtx;
	TRAPD(r, StartEngineL());
	return r;
	}
void A_shutdown()
	{
	gNotify = NULL;
	for (TInt i = 0; i < gViews.Count(); ++i)
		ph_eng_view_destroy(gViews[i]);
	gViews.Close();
	}
void A_set_useragent(const unsigned short* s, int len) { ph_eng_set_useragent(s, len); }
void A_set_iap(const char* n) { ph_eng_set_iap(n); gNetDone = ETrue; }
void A_memory_pressure(int) {}
PheView A_new_view_id() { return ph_eng_new_view_id(); }
void A_view_create(PheView v, int w, int h, int /*aPrivate: PHE_CAP_PRIVATE is not offered*/, const unsigned char*, int)
	{
	gViews.Append(v);
	ph_eng_view_create(v, w, h);
	}
void A_view_destroy(PheView v)
	{
	ph_eng_view_destroy(v);
	const TInt ix = gViews.Find(v);
	if (ix >= 0)
		gViews.Remove(ix);
	}
void A_view_resize(PheView v, int w, int h) { ph_eng_view_resize(v, w, h); }
void A_view_visible(PheView v, int vis) { ph_eng_view_visible(v, vis); }
void A_view_suspend(PheView) {}
int A_view_save_state(PheView, unsigned char*, int) { return 0; }
void A_load(PheView v, const unsigned short* url, int len)
	{
	EnsureNetwork();
	ph_eng_load(v, url, len);
	}
void A_cmd(PheView v, int cmd)
	{
	switch (cmd)
		{
	case PHE_CMD_STOP: ph_eng_cmd(v, PH_CMD_STOP); break;
	case PHE_CMD_RELOAD:
	case PHE_CMD_RELOAD_BYPASS_CACHE: ph_eng_cmd(v, PH_CMD_RELOAD); break;
	case PHE_CMD_BACK: ph_eng_cmd(v, PH_CMD_BACK); break;
	case PHE_CMD_FORWARD: ph_eng_cmd(v, PH_CMD_FORWARD); break;
	case PHE_CMD_ACTIVATE: ph_eng_cmd(v, PH_CMD_ACTIVATE); break;
	default: break;
		}
	}
void A_get_state(PheView v, PheViewState* out)
	{
	PhViewState st;
	ph_eng_get_state(v, &st);
	out->can_back = st.canBack;
	out->can_forward = st.canForward;
	out->loading = st.loading;
	out->progress = st.progress;
	out->security = SecurityOf(v);
	out->scroll_x = st.scrollX;
	out->scroll_y = st.scrollY;
	out->content_w = st.contentW;
	out->content_h = st.contentH;
	out->zoom_permille = 1000;
	}
int A_get_str(PheView v, int which, unsigned short* buf, int cap)
	{
	int w = -1;
	switch (which)
		{
	case PHE_STR_URL: w = PH_STR_URL; break;
	case PHE_STR_TITLE: w = PH_STR_TITLE; break;
	case PHE_STR_ERROR: w = PH_STR_ERROR; break;
	case PHE_STR_NEWVIEW: w = PH_STR_NEWWIN; break;
	case PHE_STR_FOCUS_URL: w = PH_STR_FOCUS_URL; break;
	default: break;
		}
	return w < 0 ? 0 : ph_eng_get_str(v, w, buf, cap);
	}
// one engine event -> a seam event (FIRST_LAYOUT is dropped); returns 1 when aOut was filled
int Translate(int aView, const PhEvent& e, PheEvent* aOut)
	{
	aOut->view = aView;
	aOut->a = e.a;
	aOut->b = e.b;
	aOut->id = 0;
	switch (e.kind)
		{
	case PH_EV_START: aOut->kind = PHE_EV_START; return 1;
	case PH_EV_COMMIT: aOut->kind = PHE_EV_COMMIT; aOut->b = SecurityOf(aView); return 1;
	case PH_EV_TITLE: aOut->kind = PHE_EV_TITLE; return 1;
	case PH_EV_PROGRESS: aOut->kind = PHE_EV_PROGRESS; return 1;
	case PH_EV_FINISHED: aOut->kind = PHE_EV_FINISHED; return 1;
	case PH_EV_FAILED: aOut->kind = PHE_EV_FAILED; return 1;
	case PH_EV_REPAINT: aOut->kind = PHE_EV_REPAINT; return 1;
	case PH_EV_HISTORY: aOut->kind = PHE_EV_HISTORY; return 1;
	case PH_EV_FOCUS: aOut->kind = PHE_EV_FOCUS; return 1;
	case PH_EV_NEW_WINDOW: aOut->kind = PHE_EV_NEW_VIEW; aOut->a = 0; return 1;
	default: return 0;
		}
	}
int A_poll(PheView v, PheEvent* out)
	{
	PhEvent e;
	if (v != 0)
		{
		while (ph_eng_poll(v, &e))
			if (Translate(v, e, out))
				return 1;
		return 0;
		}
	const TInt n = gViews.Count();
	for (TInt k = 0; k < n; ++k)
		{
		const TInt ix = (gNextPoll + k) % n;
		while (ph_eng_poll(gViews[ix], &e))
			if (Translate(gViews[ix], e, out))
				{
				gNextPoll = ix;		// stay on this view until it is drained (event order per view)
				return 1;
				}
		}
	return 0;
	}
void A_dialog_reply(PheView, unsigned, int, const unsigned short*, int, const unsigned short*, int) {}
void A_scroll(PheView v, int dx, int dy) { ph_eng_scroll(v, dx, dy); }
void A_zoom(PheView, int, int, int) {}
void A_pointer(PheView v, int type, int x, int y)
	{
	switch (type)
		{
	case PHE_PTR_DOWN: ph_eng_pointer(v, PH_PTR_DOWN, x, y); break;
	case PHE_PTR_MOVE: ph_eng_pointer(v, PH_PTR_MOVE, x, y); break;
	case PHE_PTR_UP: ph_eng_pointer(v, PH_PTR_UP, x, y); break;
	default: break;		// long press: no context menu in the engine yet
		}
	}
// scan_code: a Symbian TKeyCode for the editing / arrow keys (EKeyBackspace, EKeyEnter, EKeyUpArrow...), 0 for a character
void A_key(PheView v, int scan, int unicode, int down)
	{
	const int vk = scan ? VirtualKey(scan) : 0;
	ph_eng_key(v, vk, unicode, down);
	}
void A_text(PheView v, const unsigned short* s, int len)
	{
	for (int i = 0; i < len; ++i)
		{
		ph_eng_key(v, 0, s[i], 1);
		ph_eng_key(v, 0, s[i], 0);
		}
	}
void A_setting(PheView v, int id, int value)
	{
	switch (id)
		{
	case PHE_SET_SCRIPT: ph_eng_setting(v, PH_SET_SCRIPT, value); break;
	case PHE_SET_IMAGES: ph_eng_setting(v, PH_SET_IMAGES, value); break;
	case PHE_SET_FONTSCALE: ph_eng_setting(v, PH_SET_FONTSCALE, value); break;
	case PHE_SET_COOKIES: ph_eng_setting(v, PH_SET_COOKIES, value); break;
	default: break;		// desktop mode, DNT: not supported yet
		}
	}
int A_lock_pixels(PheView v, const unsigned int** px, int* w, int* h, int* stride)
	{
	return ph_eng_lock_pixels(v, px, w, h, stride);
	}
void A_unlock_pixels(PheView v) { ph_eng_unlock_pixels(v); }
// a box-filtered scaled copy of the view buffer for the tab grid
int A_snapshot(PheView v, unsigned int* dst, int w, int h, int stride)
	{
	const unsigned int* px; int sw, sh, ss;
	if (w <= 0 || h <= 0 || A_lock_pixels(v, &px, &sw, &sh, &ss) != 0)
		return -1;
	if (sw <= 0 || sh <= 0)
		{ A_unlock_pixels(v); return -1; }
	for (int y = 0; y < h; ++y)
		{
		unsigned int* d = (unsigned int*)((unsigned char*)dst + y * stride);
		const int y0 = y * sh / h, y1 = Max(y0 + 1, (y + 1) * sh / h);
		for (int x = 0; x < w; ++x)
			{
			const int x0 = x * sw / w, x1 = Max(x0 + 1, (x + 1) * sw / w);
			unsigned int a = 0, r = 0, g = 0, b = 0, n = 0;
			for (int yy = y0; yy < y1 && yy < sh; ++yy)
				{
				const unsigned int* s = (const unsigned int*)((const unsigned char*)px + yy * ss);
				for (int xx = x0; xx < x1 && xx < sw; ++xx)
					{
					const unsigned int p = s[xx];
					a += p >> 24; r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; ++n;
					}
				}
			d[x] = n ? ((a / n) << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n) : 0;
			}
		}
	A_unlock_pixels(v);
	return 0;
	}
void A_find(PheView, const unsigned short*, int, int) {}
void A_find_next(PheView, int) {}
void A_find_close(PheView) {}
void A_download_accept(PheView, unsigned, const unsigned short*, int) {}
void A_download_cancel(PheView, unsigned) {}
void A_clear_data(int) {}

// What this engine does today: script (JavaScriptCore, CLoop or JIT), TLS 1.3 (BoringSSL-free OpenSSL under curl), a snapshot for the
// tab grid. Not yet: find, zoom (the engine has no page-zoom call), downloads, private views, suspend, session state, reflow.
const unsigned KCaps = PHE_CAP_SCRIPT | PHE_CAP_TLS13 | PHE_CAP_SNAPSHOT;

const PheWebEngineApi KApi =
	{
	sizeof(PheWebEngineApi), PHE_WEBENGINE_VERSION, "WebKit", "2.54.1", KCaps, 0,
	A_init, A_shutdown, A_set_useragent, A_set_iap, A_memory_pressure,
	A_new_view_id, A_view_create, A_view_destroy, A_view_resize, A_view_visible, A_view_suspend, A_view_save_state,
	A_load, A_cmd, A_get_state, A_get_str, A_poll, A_dialog_reply,
	A_scroll, A_zoom, A_pointer, A_key, A_text, A_setting,
	A_lock_pixels, A_unlock_pixels, A_snapshot,
	A_find, A_find_next, A_find_close,
	A_download_accept, A_download_cancel,
	A_clear_data
	};

// ---- the ECom object ---------------------------------------------------------------------------------------------------------
class CWebKitEngine : public CPheWebEnginePlugin
	{
public:
	static CWebKitEngine* NewL() { return new (ELeave) CWebKitEngine; }
	const PheWebEngineApi* Api() { return &KApi; }
	};

const TImplementationProxy KImplementationTable[] =
	{
	IMPLEMENTATION_PROXY_ENTRY(0xE7016B10, CWebKitEngine::NewL)
	};

}	// namespace

extern "C" const PheWebEngineApi* PheWebEngineGetApi(unsigned aWanted)
	{
	return aWanted == PHE_WEBENGINE_VERSION ? &KApi : NULL;
	}

EXPORT_C const TImplementationProxy* ImplementationGroupProxy(TInt& aTableCount)
	{
	aTableCount = sizeof(KImplementationTable) / sizeof(TImplementationProxy);
	return KImplementationTable;
	}
