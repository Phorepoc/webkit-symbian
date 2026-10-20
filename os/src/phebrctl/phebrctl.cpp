// Copyright The Phorepoc Authors
// SPDX-License-Identifier: BSD-2-Clause

// phebrctl.cpp -- browserengine.dll's replacement. Ordinal 1,
// CreateBrowserControlL, hands BrowserNG (and Messaging, Help, ...) a CBrCtlInterface that shows pages rendered by WebKit 2.54
// (WebCore + JSC + Skia, webkit-symbian) on an engine thread of its own.
#include "phebrctl.h"
#include "PhEngineApi.h"
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
#include <apengineconsts.h>
#include <coemain.h>
#include <w32std.h>
#include <gulicon.h>
#include <eikmenup.h>
#include <string.h>
#include <fepbase.h>
#include <centralrepository.h>
#include <fepipext.h>
#include <aknedsts.h>
#include <aknedstsobs.h>
#include <aknextendedinputcapabilities.h>




// ---- text input: the FEP-aware editor (the 2010 WebKit's CWebFepTextEditor, on our engine's key seam) ----------------------------
// The front-end processor (AknFep) attaches to the focused control's TCoeInputCapabilities: it needs an MCoeFepAwareTextEditor whose
// Extension1()->State() is a CAknEdwinState, and a call ReportAknEdStateEventL(EAknActivatePenInputRequest) to open the on-screen
// keyboard (WebView::activateVirtualKeyboard). The engine has no text-model API, only keys, so the editor keeps a mirror of what it
// typed: the FEP's inline text is applied as a diff (backspaces + characters) through ph_eng_key; a commit just freezes it.
class CPhFepEditor : public CBase, public MObjectProvider, public MCoeFepAwareTextEditor, public MCoeFepAwareTextEditor_Extension1,
                     public TCoeInputCapabilities::MCoeFepSpecificExtensions
    {
public:
    CPhFepEditor(TInt aView, MObjectProvider* aParent) : iView(aView), iParent(aParent), iState(NULL), iExt(NULL), iInlineOn(EFalse), iPenOn(EFalse) {}
    ~CPhFepEditor() { delete iState; delete iExt; }
    void ConstructL() { iExt = CAknExtendedInputCapabilities::NewL(); }
    void Reset() { iDoc.Zero(); iInline.Zero(); iInlineOn = EFalse; }
    CAknEdwinState* EdState() { return static_cast<CAknEdwinState*>(State(KNullUid)); }
    TCoeInputCapabilities Capabilities(TBool aSecret)
        {
        TUint caps = TCoeInputCapabilities::ENavigation | TCoeInputCapabilities::EAllText;
        if (aSecret)
            caps |= TCoeInputCapabilities::ESecretText;
        TCoeInputCapabilities c(caps, this, NULL, TUid::Uid(0x100056de), this);
        c.SetObjectProvider(this);
        return c;
        }
    void ActivatePen()
        {
        CAknEdwinState* st = EdState();
        if (st)
            {
            TUint cap = iExt->Capabilities();
            iExt->SetCapabilities(cap & ~CAknExtendedInputCapabilities::EInputEditorDisableVKB);
            iPenOn = ETrue;
            TRAP_IGNORE(st->ReportAknEdStateEventL(MAknEdStateObserver::EAknActivatePenInputRequest));
            }
        }
    void ClosePen()
        {
        if (!iPenOn)
            return;
        iPenOn = EFalse;
        CAknEdwinState* st = EdState();
        if (st)
            TRAP_IGNORE(st->ReportAknEdStateEventL(MAknEdStateObserver::EAknClosePenInputRequest));
        }
    // from MCoeFepAwareTextEditor
    void StartFepInlineEditL(const TDesC& aText, TInt, TBool, const MFormCustomDraw*, MFepInlineTextFormatRetriever&,
                             MFepPointerEventHandlerDuringInlineEdit&)
        {
        iInline.Zero();
        iInlineOn = ETrue;
        Apply(aText);
        }
    void UpdateFepInlineTextL(const TDesC& aText, TInt) { Apply(aText); }
    void SetInlineEditingCursorVisibilityL(TBool) {}
    void CancelFepInlineEdit() { Apply(KNullDesC); iInlineOn = EFalse; }
    TInt DocumentLengthForFep() const { return iDoc.Length() + iInline.Length(); }
    TInt DocumentMaximumLengthForFep() const { return KMaxTInt; }
    void SetCursorSelectionForFepL(const TCursorSelection&) {}
    void GetCursorSelectionForFep(TCursorSelection& aSel) const { aSel.SetSelection(DocumentLengthForFep(), DocumentLengthForFep()); }
    void GetEditorContentForFep(TDes& aContent, TInt aPos, TInt aLen) const
        {
        aContent.Zero();
        TBuf<KPhDocMax + KPhDocMax> all(iDoc);
        all.Append(iInline);
        if (aPos < 0 || aPos >= all.Length() || aLen <= 0)
            return;
        aContent.Copy(all.Mid(aPos, Min(aLen, all.Length() - aPos)).Left(aContent.MaxLength()));
        }
    void GetFormatForFep(TCharFormat&, TInt) const {}
    void GetScreenCoordinatesForFepL(TPoint& aPos, TInt& aHeight, TInt& aAscent, TInt) const
        {
        aPos = TPoint(0, 0);
        aHeight = 0;
        aAscent = 0;
        }
    void DoCommitFepInlineEditL()
        {
        if (iDoc.Length() + iInline.Length() <= iDoc.MaxLength())
            iDoc.Append(iInline);
        iInline.Zero();
        iInlineOn = EFalse;
        }
    MCoeFepAwareTextEditor_Extension1* Extension1(TBool& aSetToTrue)
        {
        aSetToTrue = ETrue;
        return static_cast<MCoeFepAwareTextEditor_Extension1*>(this);
        }
    void MCoeFepAwareTextEditor_Reserved_2() {}
    // from MCoeFepAwareTextEditor_Extension1
    void SetStateTransferingOwnershipL(CState* aState, TUid)
        {
        delete iState;
        iState = aState;
        }
    CState* State(TUid)
        {
        if (!iState)
            {
            CAknEdwinState* st = new CAknEdwinState();
            if (st)
                {
                st->SetObjectProvider(iParent);
                iState = st;
                }
            }
        return iState;
        }
    void StartFepInlineEditL(TBool& aSetToTrue, const TCursorSelection&, const TDesC& aText, TInt aPos, TBool aVis,
                             const MFormCustomDraw* aDraw, MFepInlineTextFormatRetriever& aRet, MFepPointerEventHandlerDuringInlineEdit& aPtr)
        {
        aSetToTrue = ETrue;
        StartFepInlineEditL(aText, aPos, aVis, aDraw, aRet, aPtr);
        }
    void SetCursorType(TBool&, const TTextCursor&) {}
    void MCoeFepAwareTextEditor_Extension1_Reserved_3() {}
    void MCoeFepAwareTextEditor_Extension1_Reserved_4() {}
    // from MCoeFepSpecificExtensions
    TBool IsValidCharacter(TInt) { return ETrue; }
    void MCoeFepSpecificExtensions_Reserved_1() {}
    void MCoeFepSpecificExtensions_Reserved_2() {}
    // from MObjectProvider
    TTypeUid::Ptr MopSupplyObject(TTypeUid aId)
        {
        if (aId.iUid == CAknExtendedInputCapabilities::ETypeId)
            return aId.MakePtr(iExt);
        return aId.Null();
        }
    MObjectProvider* MopNext() { return iParent; }
private:
    enum { KPhDocMax = 256 };
    // make the field show aNew in place of the current inline text: the common prefix stays, the rest is backspaced and retyped
    void Apply(const TDesC& aNew)
        {
        TInt common = 0;
        while (common < iInline.Length() && common < aNew.Length() && iInline[common] == aNew[common])
            ++common;
        for (TInt i = iInline.Length(); i > common; --i)
            {
            ph_eng_key(iView, 0x08, 0, 1);
            ph_eng_key(iView, 0x08, 0, 0);
            }
        for (TInt i = common; i < aNew.Length(); ++i)
            {
            ph_eng_key(iView, 0, aNew[i], 1);
            ph_eng_key(iView, 0, aNew[i], 0);
            }
        iInline.Copy(aNew.Left(iInline.MaxLength()));
        }
private:
    TInt iView;
    MObjectProvider* iParent;
    CState* iState;
    CAknExtendedInputCapabilities* iExt;
    TBuf<KPhDocMax> iDoc;
    TBuf<KPhDocMax> iInline;
    TBool iInlineOn;
    TBool iPenOn;
    };

namespace
{
// ---- the engine thread (one per process) ------------------------------------------------------------------------------------
struct TWatch
    {
    TInt iView;
    TRequestStatus* iStatus;
    TThreadId iThread;
    };

RFastLock gLock;
TBool gLockMade = EFalse;
RArray<TWatch> gWatches;
RThread gEngine;
TBool gEngineStarted = EFalse;

// ON THE ENGINE THREAD: complete the UI thread's pending request for this view (never touches WebKit, never blocks long)
void NotifyTrampoline(void*, int aView)
    {
    gLock.Wait();
    for (TInt i = 0; i < gWatches.Count(); ++i)
        {
        TWatch& w = gWatches[i];
        if (w.iView == aView && w.iStatus)
            {
            RThread ui;
            if (ui.Open(w.iThread) == KErrNone)
                {
                ui.RequestComplete(w.iStatus, KErrNone);
                ui.Close();
                }
            w.iStatus = NULL;
            }
        }
    gLock.Signal();
    }

TInt EngineThreadFn(TAny*)
    {
    CTrapCleanup* cleanup = CTrapCleanup::New();
    ph_eng_main(NotifyTrampoline, NULL);
    delete cleanup;
    return KErrNone;
    }

void EnsureEngineL()
    {
    if (!gLockMade)
        {
        User::LeaveIfError(gLock.CreateLocal());
        gLockMade = ETrue;
        }
    if (gEngineStarted)
        return;
    // the User-Agent: CenRep 0xE7015B01 key 1 (set by the ROM; empty/missing = the engine's
    // default, a current Android Chrome -- Google and others serve an "outdated browser" page to older strings)
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
        RDebug::Printf("PH: [phebrctl] user agent from CenRep: %d (%d chars)", ge, ua.Length());
        }
    // its own heap (WebKit allocates through malloc = this thread's heap; BrowserNG's MemMan allocator stays out of it) and the
    // flexible memory model's largest stack
    _LIT(KName, "phengine");
    User::LeaveIfError(gEngine.Create(KName, EngineThreadFn, 0x100000, 0x100000, 0x18000000, NULL));
    gEngine.SetPriority(EPriorityNormal);
    gEngine.Resume();
    gEngineStarted = ETrue;
    RDebug::Printf("[phebrctl] engine thread started");
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
            return EFalse;
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
}

// ---- the watcher ------------------------------------------------------------------------------------------------------------
CPhEventWatcher::CPhEventWatcher(CPhBrCtl& aOwner, TInt aView)
    : CActive(EPriorityStandard), iOwner(aOwner), iView(aView)
    {
    CActiveScheduler::Add(this);
    }

CPhEventWatcher::~CPhEventWatcher()
    {
    Cancel();
    gLock.Wait();
    for (TInt i = gWatches.Count() - 1; i >= 0; --i)
        if (gWatches[i].iView == iView)
            gWatches.Remove(i);
    gLock.Signal();
    }

void CPhEventWatcher::Arm()
    {
    if (IsActive())
        return;
    iStatus = KRequestPending;
    gLock.Wait();
    for (TInt i = 0; i < gWatches.Count(); ++i)
        if (gWatches[i].iView == iView)
            {
            gWatches[i].iStatus = &iStatus;
            break;
            }
    gLock.Signal();
    SetActive();
    }

void CPhEventWatcher::RunL()
    {
    Arm();                  // re-arm BEFORE draining: an event posted after the last empty poll then completes this request
    iOwner.DrainEvents();
    }

void CPhEventWatcher::DoCancel()
    {
    gLock.Wait();
    TBool mine = EFalse;
    for (TInt i = 0; i < gWatches.Count(); ++i)
        if (gWatches[i].iView == iView && gWatches[i].iStatus == &iStatus)
            {
            gWatches[i].iStatus = NULL;
            mine = ETrue;
            }
    gLock.Signal();
    if (mine)
        {
        TRequestStatus* s = &iStatus;
        User::RequestComplete(s, KErrCancel);
        }
    }

TInt CPhEventWatcher::RunError(TInt)
    {
    return KErrNone;
    }

// ---- CPhBrCtl ---------------------------------------------------------------------------------------------------------------
CPhBrCtl::CPhBrCtl()
    : iFep(NULL), iTapArmed(EFalse), iFocusKind(0)
    {
    }

CPhBrCtl* CPhBrCtl::NewL(CCoeControl* aParent, TRect aRect, TUint aCaps, TUint aCommandIdBase,
                         MBrCtlSoftkeysObserver* aSoftkeys, MBrCtlLinkResolver*, MBrCtlSpecialLoadObserver* aSpecialLoad,
                         MBrCtlLayoutObserver*, MBrCtlDialogsProvider* aDialogs, MBrCtlWindowObserver* aWindow,
                         MBrCtlDownloadObserver*)
    {
    CPhBrCtl* self = new (ELeave) CPhBrCtl;
    CleanupStack::PushL(self);
    self->iCaps = aCaps;
    self->iCommandIdBase = aCommandIdBase;
    self->iSoftkeys = aSoftkeys;
    self->iSpecialLoad = aSpecialLoad;
    self->iDialogs = aDialogs;
    self->iWindow = aWindow;
    self->ConstructL(aParent, aRect);
    CleanupStack::Pop(self);
    return self;
    }

void CPhBrCtl::ConstructL(CCoeControl* aParent, const TRect& aRect)
    {
    Mem::FillZ(iSettings, sizeof(iSettings));
    // defaults a client may read back before it sets them (the old engine's SettingsContainer defaults)
    iSettings[ESettingsAutoLoadImages] = 1;
    iSettings[ESettingsECMAScriptEnabled] = 1;
    iSettings[ESettingsCookiesEnabled] = 1;
    iSettings[ESettingsCSSFetchEnabled] = 1;
    iZoomLevels.AppendL(100);
    iVisible = ETrue;
    EnsureEngineL();
    iView = ph_eng_new_view_id();
    // the watcher's table entry must exist before the engine can notify
    gLock.Wait();
    TWatch w;
    w.iView = iView;
    w.iStatus = NULL;
    w.iThread = RThread().Id();
    TInt err = gWatches.Append(w);
    gLock.Signal();
    User::LeaveIfError(err);
    SetContainerWindowL(*aParent);
    SetRect(aRect);
    ph_eng_view_create(iView, Max(1, aRect.Width()), Max(1, aRect.Height()));
    iWatcher = new (ELeave) CPhEventWatcher(*this, iView);
    iWatcher->Arm();
    iFep = new (ELeave) CPhFepEditor(iView, this);
    iFep->ConstructL();
    RecreateBitmapL();
    EnableDragEvents();
    ActivateL();
    RDebug::Printf("[phebrctl] view %d created %dx%d caps %x idbase %d", iView, aRect.Width(), aRect.Height(), iCaps, iCommandIdBase);
    }

CPhBrCtl::~CPhBrCtl()
    {
    if (iFep)
        iFep->ClosePen();
    delete iFep;
    delete iWatcher;
    if (iView)
        ph_eng_view_destroy(iView);
    delete iBitmap;
    iLoadObservers.Close();
    iStateObservers.Close();
    iCommandObservers.Close();
    iWmlItems.Close();
    iSubscribe.Close();
    iZoomLevels.Close();
    }

void CPhBrCtl::RecreateBitmapL()
    {
    TSize size = Rect().Size();
    if (size.iWidth <= 0 || size.iHeight <= 0)
        return;
    if (iBitmap && iBitmap->SizeInPixels() == size)
        return;
    CFbsBitmap* bmp = new (ELeave) CFbsBitmap;
    CleanupStack::PushL(bmp);
    User::LeaveIfError(bmp->Create(size, EColor16MU));
    CleanupStack::Pop(bmp);
    delete iBitmap;
    iBitmap = bmp;
    iBitmap->LockHeap();
    Mem::Fill(iBitmap->DataAddress(), CFbsBitmap::ScanLineLength(size.iWidth, EColor16MU) * size.iHeight, 0xFF);   // white until the first paint
    iBitmap->UnlockHeap();
    }

void CPhBrCtl::SizeChanged()
    {
    TSize size = Rect().Size();
    TRAP_IGNORE(RecreateBitmapL());
    if (iView && size.iWidth > 0 && size.iHeight > 0)
        ph_eng_view_resize(iView, size.iWidth, size.iHeight);
    }

void CPhBrCtl::MakeVisible(TBool aVisible)
    {
    CCoeControl::MakeVisible(aVisible);
    iVisible = aVisible;
    if (iView)
        ph_eng_view_visible(iView, aVisible);
    }

void CPhBrCtl::FocusChanged(TDrawNow)
    {
    }

void CPhBrCtl::Draw(const TRect& aRect) const
    {
    CWindowGc& gc = SystemGc();
    if (iBitmap)
        gc.BitBlt(Rect().iTl, iBitmap);
    else
        {
        gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
        gc.SetBrushColor(KRgbWhite);
        gc.Clear(aRect);
        }
    }

void CPhBrCtl::CopyPixels()
    {
    if (!iBitmap)
        return;
    const TUint* px = NULL;
    TInt w = 0, h = 0, stride = 0;
    if (!ph_eng_lock_pixels(iView, &px, &w, &h, &stride))
        return;
    TSize bs = iBitmap->SizeInPixels();
    TInt rows = Min(h, bs.iHeight), cols = Min(w, bs.iWidth);
    iBitmap->LockHeap();
    TUint8* dst = (TUint8*)iBitmap->DataAddress();
    TInt dstStride = CFbsBitmap::ScanLineLength(bs.iWidth, EColor16MU);
    for (TInt y = 0; y < rows; ++y)
        Mem::Copy(dst + y * dstStride, (const TUint8*)px + y * stride, cols * 4);
    iBitmap->UnlockHeap();
    ph_eng_unlock_pixels(iView);
    }

// ---- engine events -> BrCtl events ------------------------------------------------------------------------------------------
void CPhBrCtl::Fire(TBrCtlLoadEvent aEvent, TUint aSize, TUint16 aTrans)
    {
    for (TInt i = 0; i < iLoadObservers.Count(); ++i)
        {
        MBrCtlLoadEventObserver* o = iLoadObservers[i];
        TRAP_IGNORE(o->HandleBrowserLoadEventL(aEvent, aSize, aTrans));
        }
    }

void CPhBrCtl::FireState(TBrCtlState aState, TInt aValue)
    {
    for (TInt i = 0; i < iStateObservers.Count(); ++i)
        iStateObservers[i]->StateChanged(aState, aValue);
    }

void CPhBrCtl::SoftkeysChanged()
    {
    if (iSoftkeys)
        TRAP_IGNORE(iSoftkeys->UpdateSoftkeyL(EKeyRight, KNullDesC, 0, EChangeReasonElementType));
    }

void CPhBrCtl::StartLoad()
    {
    iLoading = ETrue;
    iSentArrived = iSentDisplayed = iSentTitle = EFalse;
    iSentStart = ETrue;
    Fire(EEventNewContentStart);
    Fire(EEventUrlLoadingStart, 0, 0);
    }

void CPhBrCtl::FinishLoad(TBool aError)
    {
    if (!iLoading)
        return;
    iLoading = EFalse;
    if (aError)
        {
        Fire(EEventLoadError);
        Fire(EEventContentFinished);
        Fire(EEventLoadFinished);
        return;
    }
    // the sequence the old engine ended a load with (WebFrameLoaderClient::postProgressFinishedNotificationL)
    if (!iSentArrived)
        {
        iSentArrived = ETrue;
        Fire(EEventNewUrlContentArrived, 100);
        }
    if (!iSentDisplayed)
        {
        iSentDisplayed = ETrue;
        Fire(EEventNewContentDisplayed);
        }
    if (!iSentTitle)
        {
        iSentTitle = ETrue;
        Fire(EEventTitleAvailable);
        }
    Fire(EEventMoreUrlContentArrived, 100);
    Fire(EEventUrlLoadingFinished, 0, 0);
    Fire(EEventContentFinished);
    Fire(EEventLoadFinished);
    }

void CPhBrCtl::HandleEvent(const PhEvent& aEvent)
    {
    if (aEvent.kind != PH_EV_REPAINT && aEvent.kind != PH_EV_PROGRESS)
        RDebug::Printf("[phebrctl] view %d event %d (%d,%d)", iView, aEvent.kind, aEvent.a, aEvent.b);
    switch (aEvent.kind)
        {
    case PH_EV_START:
        if (iLoading)
            FinishLoad(EFalse);     // a new navigation supersedes the running one
        StartLoad();
        break;
    case PH_EV_COMMIT:
        if (!iLoading)
            break;          // late (after the finish): only a START opens a load
        if (!iSentArrived)
            {
            iSentArrived = ETrue;
            Fire(EEventNewUrlContentArrived, 100);
            Fire(EEventMoreUrlContentArrived, 10);
            }
        break;
    case PH_EV_FIRST_LAYOUT:
        if (!iLoading)
            break;          // late (after the finish): only a START opens a load
        if (!iSentArrived)
            {
            iSentArrived = ETrue;
            Fire(EEventNewUrlContentArrived, 100);
            }
        if (!iSentDisplayed)
            {
            iSentDisplayed = ETrue;
            Fire(EEventNewContentDisplayed);
            }
        break;
    case PH_EV_TITLE:
        if (iLoading && iSentDisplayed)
            {
            iSentTitle = ETrue;
            Fire(EEventTitleAvailable);
            }
        break;
    case PH_EV_PROGRESS:
        if (iLoading && iSentArrived)
            Fire(EEventMoreUrlContentArrived, (TUint)aEvent.a);
        break;
    case PH_EV_FINISHED:
        FinishLoad(EFalse);
        break;
    case PH_EV_FAILED:
        {
        if (!iLoading)
            StartLoad();
        if (iDialogs)
            {
            if (aEvent.a >= 400 && aEvent.a < 600)
                {
                TBuf<4> none;
                TRAP_IGNORE(iDialogs->DialogNotifyHttpErrorL(aEvent.a, none));
                }
            else
                TRAP_IGNORE(iDialogs->DialogNotifyErrorL(KErrCouldNotConnect));
            }
        FinishLoad(ETrue);
        break;
        }
    case PH_EV_REPAINT:
        MaybeActivatePenInput();
        CopyPixels();
        if (iVisible && DrawableWindow())
            DrawNow();
        break;
    case PH_EV_HISTORY:
        iCanBack = aEvent.a;
        iCanForward = aEvent.b;
        FireState(EStateHistoryBeginning, !iCanBack);
        FireState(EStateHistoryEnd, !iCanForward);
        break;
    case PH_EV_FOCUS:
        EditFocusChanged(aEvent.a);
        SoftkeysChanged();
        break;
    default:
        break;
        }
    }

void CPhBrCtl::DrainEvents()
    {
    PhEvent ev;
    TInt guard = 0;
    while (ph_eng_poll(iView, &ev) && guard++ < 200)
        HandleEvent(ev);
    }

// ---- network ownership ---------------------------------------------------------------------------
// BrowserNG's connection manager brings the access point up inside NetworkConnectionNeededL (and flips Connection().Connected()).
// The engine has its own sockets (curl over Open C), which need a default interface: the access point's name.
void CPhBrCtl::NetworkConnection()
    {
    TBuf8<96> name;
    if (iSpecialLoad)
        {
        TInt conn = 0, sock = 0;
        TBool newConn = EFalse;
        TApBearerType bearer = EApBearerTypeAll;
        TRAPD(err, iSpecialLoad->NetworkConnectionNeededL(&conn, &sock, &newConn, &bearer));
        RDebug::Printf("PH: [phebrctl] NetworkConnectionNeededL -> %d conn %x sock %x new %d", err, conn, sock, newConn);
        if (err == KErrNone && conn)
            {
            RConnection* c = reinterpret_cast<RConnection*>(conn);
            TBuf<64> iapName;
            _LIT(KIapName, "IAP\\Name");
            if (c->GetDesSetting(KIapName, iapName) == KErrNone)
                Narrow(iapName, name);
            iConnected = ETrue;
            }
        }
    if (name.Length() == 0 && ActiveIapName(name))
        iConnected = ETrue;
    if (name.Length() == 0)
        name.Copy(_L8("Ethernet - DHCP"));
    TBuf8<100> z(name);
    z.Append(0);
    ph_eng_set_iap((const char*)z.Ptr());
    }

// ---- CCoeControl input ------------------------------------------------------------------------------------------------------
void CPhBrCtl::HandlePointerEventL(const TPointerEvent& aEvent)
    {
    TPoint p = aEvent.iPosition - Position();
    switch (aEvent.iType)
        {
    case TPointerEvent::EButton1Down:
        iDown = ETrue;
        iDragging = EFalse;
        iDownPos = iLastPos = p;
        break;
    case TPointerEvent::EDrag:
        if (!iDown)
            break;
        if (!iDragging && (Abs(p.iX - iDownPos.iX) > 10 || Abs(p.iY - iDownPos.iY) > 10))
            iDragging = ETrue;
        if (iDragging)
            {
            ph_eng_scroll(iView, iLastPos.iX - p.iX, iLastPos.iY - p.iY);
            iLastPos = p;
            }
        break;
    case TPointerEvent::EButton1Up:
        if (iDown && !iDragging)
            {
            ph_eng_pointer(iView, PH_PTR_DOWN, iDownPos.iX, iDownPos.iY);
            ph_eng_pointer(iView, PH_PTR_UP, iDownPos.iX, iDownPos.iY);
            iTapArmed = ETrue;      // the engine answers with PH_EV_FOCUS / a repaint: a focused text field opens the keyboard
            }
        iDown = EFalse;
        break;
    default:
        break;
        }
    }

TKeyResponse CPhBrCtl::OfferKeyEventL(const TKeyEvent& aEvent, TEventCode aType)
    {
    RDebug::Printf("[phebrctl] key type %d code %x scan %x", (TInt)aType, aEvent.iCode, aEvent.iScanCode);
    if (aType != EEventKey)
        return EKeyWasNotConsumed;
    enum { VK_BACK = 0x08, VK_TAB = 0x09, VK_RETURN = 0x0D, VK_PRIOR = 0x21, VK_NEXT = 0x22, VK_LEFT = 0x25, VK_UP = 0x26, VK_RIGHT = 0x27, VK_DOWN = 0x28 };
    TInt vk = 0;
    switch (aEvent.iCode)
        {
    case EKeyUpArrow: vk = VK_UP; break;
    case EKeyDownArrow: vk = VK_DOWN; break;
    case EKeyLeftArrow: vk = VK_LEFT; break;
    case EKeyRightArrow: vk = VK_RIGHT; break;
    case EKeyPageUp: vk = VK_PRIOR; break;
    case EKeyPageDown: vk = VK_NEXT; break;
    case EKeyEnter:
    case EKeyDevice3: vk = VK_RETURN; break;
    case EKeyTab: vk = VK_TAB; break;
    case EKeyBackspace: vk = VK_BACK; break;
    default: break;
        }
    PhViewState st;
    ph_eng_get_state(iView, &st);
    if (vk)
        {
        if (vk == VK_BACK && st.focusKind != 2)
            return EKeyWasNotConsumed;      // the clear key goes back in the host
        ph_eng_key(iView, vk, 0, 1);
        ph_eng_key(iView, vk, 0, 0);
        return EKeyWasConsumed;
        }
    if (st.focusKind == 2 && aEvent.iCode >= 0x20 && aEvent.iCode < 0xF700)
        {
        ph_eng_key(iView, 0, aEvent.iCode, 1);
        ph_eng_key(iView, 0, aEvent.iCode, 0);
        return EKeyWasConsumed;
        }
    return EKeyWasNotConsumed;
    }

void CPhBrCtl::SetFocus(TBool aFocus, TDrawNow aDrawNow)
    {
    CCoeControl::SetFocus(aFocus, aDrawNow);
    }

TSize CPhBrCtl::MinimumSize()
    {
    return Size();
    }

// ---- text input (see CPhFepEditor) -------------------------------------------------------------------------------------------------
TCoeInputCapabilities CPhBrCtl::InputCapabilities() const
    {
    if (iFep && iFocusKind == 2)
        return iFep->Capabilities(EFalse);
    return TCoeInputCapabilities(TCoeInputCapabilities::ENone);
    }

// the engine's focused element kind changed (0 none, 1 anchor, 2 input): tell the FEP the capabilities changed
void CPhBrCtl::EditFocusChanged(TInt aKind)
    {
    const TInt old = iFocusKind;
    iFocusKind = aKind;
    RDebug::Printf("PH: [phebrctl] edit focus %d -> %d (tap armed %d)", old, aKind, iTapArmed);
    if (!iFep)
        return;
    if (aKind == 2 && old != 2)
        iFep->Reset();
    if (aKind != 2 && old == 2)
        iFep->ClosePen();
    if (CCoeEnv::Static())
        CCoeEnv::Static()->InputCapabilitiesChanged();
    MaybeActivatePenInput();
    }

// a tap that left a text field focused (a new focus, or a tap into the already focused one): bring up the on-screen keyboard
void CPhBrCtl::MaybeActivatePenInput()
    {
    if (!iTapArmed || !iFep)
        return;
    if (iFocusKind == 2)
        {
        iTapArmed = EFalse;
        if (CCoeEnv::Static() && CCoeEnv::Static()->Fep())
            CCoeEnv::Static()->Fep()->HandleChangeInFocus();
        iFep->ActivatePen();
        RDebug::Printf("PH: [phebrctl] pen input requested");
        }
    }

// ---- the 45 virtuals --------------------------------------------------------------------------------------------------------
void CPhBrCtl::HandleCommandL(TInt aCommand)
    {
    RDebug::Printf("[phebrctl] HandleCommandL %d (cmd %d)", aCommand, aCommand - (TInt)iCommandIdBase);
    TInt cmd = aCommand - (TInt)iCommandIdBase;
    switch (cmd)
        {
    case ECommandBack:
    case ECommandOneStepBack:
        if (iCanBack)
            {
            NetworkConnection();
            ph_eng_cmd(iView, PH_CMD_BACK);
            }
        break;
    case ECommandForward:
        if (iCanForward)
            {
            NetworkConnection();
            ph_eng_cmd(iView, PH_CMD_FORWARD);
            }
        break;
    case ECommandReload:
        NetworkConnection();
        ph_eng_cmd(iView, PH_CMD_RELOAD);
        break;
    case ECommandCancelFetch:
        ph_eng_cmd(iView, PH_CMD_STOP);
        break;
    case ECommandOpen:
        ph_eng_cmd(iView, PH_CMD_ACTIVATE);
        break;
    default:
        break;          // ECommandCancel, Gain/LoseFocus, and every id the old engine owned: a safe no-op
        }
    }

TSize CPhBrCtl::ContentSize()
    {
    PhViewState st;
    ph_eng_get_state(iView, &st);
    return TSize(Max(st.contentW, Rect().Width()), Max(st.contentH, Rect().Height()));
    }

void CPhBrCtl::SetBrowserSettingL(TUint aSetting, TUint aValue)
    {
    if (aSetting < sizeof(iSettings) / sizeof(iSettings[0]))
        iSettings[aSetting] = aValue;
    if (aSetting == (TUint)ESettingsECMAScriptEnabled)
        ph_eng_setting(iView, PH_SET_SCRIPT, aValue);
    else if (aSetting == (TUint)ESettingsAutoLoadImages)
        ph_eng_setting(iView, PH_SET_IMAGES, aValue);
    }

TUint CPhBrCtl::BrowserSettingL(TUint aSetting)
    {
    if (aSetting == (TUint)ESettingsNumOfDownloads || aSetting == (TUint)ESettingsPageOverview)
        return 0;
    return aSetting < sizeof(iSettings) / sizeof(iSettings[0]) ? iSettings[aSetting] : 0;
    }

void CPhBrCtl::LoadDataL(const TDesC&, const TDesC8&, const TDataType&, TUid)
    {
    User::Leave(KErrNotSupported);      // next slice: Help / Feeds
    }

void CPhBrCtl::InitLoadDataL(const TDesC&, const TDataType&, TUid, TUint, MBrCtlDataLoadSupplier*, MBrCtlDataLoadConsumer**)
    {
    User::Leave(KErrNotSupported);
    }

void CPhBrCtl::LoadUrlL(const TDesC& aUrl, TInt, TBrCtlCacheMode)
    {
    RDebug::Printf("[phebrctl] LoadUrlL %S", &aUrl);
    NetworkConnection();            // R3: the handshake before the first request of a page load
    ph_eng_load(iView, aUrl.Ptr(), aUrl.Length());
    }

void CPhBrCtl::LoadFileL(const TDesC&)
    {
    User::Leave(KErrNotSupported);
    }

void CPhBrCtl::LoadFileL(RFile&)
    {
    User::Leave(KErrNotSupported);      // next slice: Messaging's HTML mail
    }

RPointerArray<TBrCtlWmlServiceOption>* CPhBrCtl::WMLOptionMenuItemsL()
    {
    return &iWmlItems;
    }

void CPhBrCtl::AddOptionMenuItemsL(CEikMenuPane&, TInt, TInt)
    {
    }

TUint CPhBrCtl::ClearCache()
    {
    return 0;
    }

TBool CPhBrCtl::IsUrlInCache(TDesC&)
    {
    return EFalse;
    }

TInt CPhBrCtl::ClearItemInCache(TDesC&)
    {
    return KErrNone;
    }

HBufC* CPhBrCtl::PageInfoLC(TBrCtlPageInfo aInfo)
    {
    TInt which = -1;
    switch (aInfo)
        {
    case EPageInfoUrl: which = PH_STR_URL; break;
    case EPageInfoTitle: which = PH_STR_TITLE; break;
    case EPageInfoFocusedNodeUrl: which = PH_STR_FOCUS_URL; break;
    default: break;
        }
    TInt len = which < 0 ? 0 : ph_eng_get_str(iView, which, NULL, 0);
    if (aInfo == EPageInfoFocusedNodeUrl && len == 0)
        User::Leave(KErrNotFound);      // "the function leaves if the focus is not on an anchor"
    HBufC* buf = HBufC::NewLC(len + 1);
    if (len)
        {
        TPtr p = buf->Des();
        p.SetLength(len);
        ph_eng_get_str(iView, which, (unsigned short*)p.Ptr(), len);
        }
    return buf;
    }

TBool CPhBrCtl::NavigationAvailable(TBrCtlNavigationDirection aDirection)
    {
    PhViewState st;
    ph_eng_get_state(iView, &st);
    return aDirection == ENavigationBack ? st.canBack : st.canForward;
    }

TUint16 CPhBrCtl::SaveToFileL(const TDesC&)
    {
    User::Leave(KErrNotSupported);
    return 0;
    }

const TCertInfo* CPhBrCtl::CertInfo()
    {
    return NULL;
    }

void CPhBrCtl::AddLoadEventObserverL(MBrCtlLoadEventObserver* aObserver)
    {
    if (iLoadObservers.Find(aObserver) == KErrNotFound)
        User::LeaveIfError(iLoadObservers.Append(aObserver));
    }

void CPhBrCtl::RemoveLoadEventObserver(MBrCtlLoadEventObserver* aObserver)
    {
    TInt i = iLoadObservers.Find(aObserver);
    if (i != KErrNotFound)
        iLoadObservers.Remove(i);
    }

TInt CPhBrCtl::ImageCountL()
    {
    return 0;
    }

TBrCtlDefs::TBrCtlElementType CPhBrCtl::FocusedElementType()
    {
    PhViewState st;
    ph_eng_get_state(iView, &st);
    return st.focusKind == 1 ? EElementAnchor : st.focusKind == 2 ? EElementInputBox : EElementNone;
    }

void CPhBrCtl::AddStateChangeObserverL(MBrCtlStateChangeObserver* aObserver)
    {
    if (iStateObservers.Find(aObserver) == KErrNotFound)
        User::LeaveIfError(iStateObservers.Append(aObserver));
    }

void CPhBrCtl::RemoveStateChangeObserver(MBrCtlStateChangeObserver* aObserver)
    {
    TInt i = iStateObservers.Find(aObserver);
    if (i != KErrNotFound)
        iStateObservers.Remove(i);
    }

HBufC* CPhBrCtl::VersionInfoLC(TBrCtlVersionInfo aInfo)
    {
    _LIT(KName, "Phorepoc WebKit");
    _LIT(KVersion, "2.54");
    _LIT(KBuild, "1");
    const TDesC& s = aInfo == EVersionInfoName ? static_cast<const TDesC&>(KName) : aInfo == EVersionInfoVersion ? static_cast<const TDesC&>(KVersion) : static_cast<const TDesC&>(KBuild);
    return s.AllocLC();
    }

void CPhBrCtl::SetSelfDownloadContentTypesL(TDesC&)
    {
    }

void CPhBrCtl::SetParamL(TBrCtlParams, const TDesC&)
    {
    }

void CPhBrCtl::LoadSavedPageL(const TDesC&, TDesC&)
    {
    User::Leave(KErrNotSupported);
    }

RArray<TUint>* CPhBrCtl::ZoomLevels() const
    {
    return const_cast<RArray<TUint>*>(&iZoomLevels);
    }

int CPhBrCtl::FindKeyword(const TDesC&)
    {
    return 0;
    }

TBrCtlDefs::TBrCtlFindResponse CPhBrCtl::FindKeywordAgain(TBool)
    {
    return (TBrCtlFindResponse)0;
    }

const RPointerArray<TBrCtlSubscribeTo>& CPhBrCtl::SubscribeToMenuItemsL()
    {
    return iSubscribe;
    }

TBool CPhBrCtl::OkToExit()
    {
    return ETrue;
    }

void CPhBrCtl::HandleDownloadCommandL(TUint16, TBrCtlDownloadCmd)
    {
    }

CGulIcon* CPhBrCtl::GetBitmapData(const TDesC&, TBrCtlBitmapInfo)
    {
    return NULL;
    }

void CPhBrCtl::AddCommandObserverL(MBrCtlCommandObserver* aObserver)
    {
    if (iCommandObservers.Find(aObserver) == KErrNotFound)
        User::LeaveIfError(iCommandObservers.Append(aObserver));
    }

void CPhBrCtl::RemoveCommandObserver(MBrCtlCommandObserver* aObserver)
    {
    TInt i = iCommandObservers.Find(aObserver);
    if (i != KErrNotFound)
        iCommandObservers.Remove(i);
    }

MWidgetExtension* CPhBrCtl::CreateWidgetExtensionL(MWidgetCallback&)
    {
    User::Leave(KErrNotSupported);
    return NULL;
    }

TBrCtlImageCarrier* CPhBrCtl::FocusedImageLC()
    {
    User::Leave(KErrNotSupported);
    return NULL;
    }

void CPhBrCtl::PostUrlL(const TDesC&, const TDesC8&, const TDesC8&, const TDesC8*, TAny*)
    {
    User::Leave(KErrNotSupported);
    }

void CPhBrCtl::TakeSnapshotL(CFbsBitmap&)
    {
    User::Leave(KErrNotSupported);
    }

TBool CPhBrCtl::IsSynchRequestPending()
    {
    return EFalse;
    }

// ---- the export: ordinal 1 --------------------------------------------------------------------------------------------------
EXPORT_C CBrCtlInterface* CreateBrowserControlL(CCoeControl* aParent, TRect aRect, TUint aBrCtlCapabilities, TUint aCommandIdBase,
    MBrCtlSoftkeysObserver* aSoftkeys, MBrCtlLinkResolver* aLinkResolver, MBrCtlSpecialLoadObserver* aSpecialLoad,
    MBrCtlLayoutObserver* aLayout, MBrCtlDialogsProvider* aDialogs, MBrCtlWindowObserver* aWindow, MBrCtlDownloadObserver* aDownload)
    {
    // WidgetUi (0x10282822) runs its widgets through CBrCtlInterface::CreateWidgetExtensionL (the WRT widget.* / menu.* bridge),
    // which this facade does not implement (KErrNotSupported -> WidgetUi died). The S60 WebKit browserengine ships beside it as
    // browserengine_s60.dll (same def, same ordinal 1): that process gets the old engine's control. The library handle is kept
    // open for the life of the process on purpose.
    if (RProcess().SecureId() == TSecureId(0x10282822))
        {
        RLibrary lib;
        if (lib.Load(_L("browserengine_s60.dll")) == KErrNone)
            {
            typedef CBrCtlInterface* (*TCreateFn)(CCoeControl*, TRect, TUint, TUint, MBrCtlSoftkeysObserver*, MBrCtlLinkResolver*,
                MBrCtlSpecialLoadObserver*, MBrCtlLayoutObserver*, MBrCtlDialogsProvider*, MBrCtlWindowObserver*, MBrCtlDownloadObserver*);
            TCreateFn fn = (TCreateFn)lib.Lookup(1);
            if (fn)
                return fn(aParent, aRect, aBrCtlCapabilities, aCommandIdBase, aSoftkeys, aLinkResolver, aSpecialLoad, aLayout,
                          aDialogs, aWindow, aDownload);
            lib.Close();
            }
        }
    return CPhBrCtl::NewL(aParent, aRect, aBrCtlCapabilities, aCommandIdBase, aSoftkeys, aLinkResolver, aSpecialLoad, aLayout,
                          aDialogs, aWindow, aDownload);
    }
