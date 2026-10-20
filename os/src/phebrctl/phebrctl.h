/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: BSD-2-Clause */

// phebrctl.h -- the BrCtl facade. A CBrCtlInterface (the UNMODIFIED RCL3
// BrCtlInterface.h) over the WebKit engine thread (PhEngineApi.h). One CPhBrCtl per browser tab = one engine view.
#ifndef PHEBRCTL_H
#define PHEBRCTL_H

#include <e32base.h>
#include <e32std.h>
#include <coecntrl.h>
#include <fbs.h>
#include <brctlinterface.h>
#include <brctldefs.h>
#include <coeinput.h>

class CPhBrCtl;
class CPhFepEditor;

// Wakes the UI thread when the engine thread has posted events for a view (one per CPhBrCtl): a CActive completed from the engine
// thread with RThread::RequestComplete
class CPhEventWatcher : public CActive
    {
public:
    CPhEventWatcher(CPhBrCtl& aOwner, TInt aView);
    ~CPhEventWatcher();
    void Arm();
private:
    void RunL();
    void DoCancel();
    TInt RunError(TInt aError);
private:
    CPhBrCtl& iOwner;
    TInt iView;
    };

class CPhBrCtl : public CBrCtlInterface, private TBrCtlDefs   // (TBrCtlDefs: the enumerators by their short names)
    {
public:
    static CPhBrCtl* NewL(CCoeControl* aParent, TRect aRect, TUint aCaps, TUint aCommandIdBase,
                          MBrCtlSoftkeysObserver* aSoftkeys, MBrCtlLinkResolver* aLinkResolver,
                          MBrCtlSpecialLoadObserver* aSpecialLoad, MBrCtlLayoutObserver* aLayout,
                          MBrCtlDialogsProvider* aDialogs, MBrCtlWindowObserver* aWindow,
                          MBrCtlDownloadObserver* aDownload);
    ~CPhBrCtl();

    void DrainEvents();   // called by the watcher, on the UI thread

    // --- CBrCtlInterface -------------------------------------------------------------------------------------------------------
    virtual void HandleCommandL(TInt aCommand);
    virtual TSize ContentSize();
    virtual void SetBrowserSettingL(TUint aSetting, TUint aValue);
    virtual TUint BrowserSettingL(TUint aSetting);
    virtual void LoadDataL(const TDesC& aUrl, const TDesC8& aData, const TDataType& aDataType, TUid aCharsetUid);
    virtual void InitLoadDataL(const TDesC& aUrl, const TDataType& aDataType, TUid aCharsetUid, TUint aContentLength,
                                        MBrCtlDataLoadSupplier* aSupplier, MBrCtlDataLoadConsumer** aConsumer);
    virtual void LoadUrlL(const TDesC& aUrl, TInt aApId = -1, TBrCtlDefs::TBrCtlCacheMode aCacheMode = TBrCtlDefs::ECacheModeNormal);
    virtual void LoadFileL(const TDesC& aFileName);
    virtual void LoadFileL(RFile& aFileHandle);
    virtual RPointerArray<TBrCtlWmlServiceOption>* WMLOptionMenuItemsL();
    virtual void AddOptionMenuItemsL(CEikMenuPane& aMenuPane, TInt aResourceId, TInt aAfter = -1);
    virtual TUint ClearCache();
    virtual TBool IsUrlInCache(TDesC& aUrl);
    virtual TInt ClearItemInCache(TDesC& aUrl);
    virtual HBufC* PageInfoLC(TBrCtlDefs::TBrCtlPageInfo aPageInfo);
    virtual TBool NavigationAvailable(TBrCtlDefs::TBrCtlNavigationDirection aDirection);
    virtual TUint16 SaveToFileL(const TDesC& aFileName);
    virtual const TCertInfo* CertInfo();
    virtual void AddLoadEventObserverL(MBrCtlLoadEventObserver* aObserver);
    virtual void RemoveLoadEventObserver(MBrCtlLoadEventObserver* aObserver);
    virtual TInt ImageCountL();
    virtual TBrCtlDefs::TBrCtlElementType FocusedElementType();
    virtual void AddStateChangeObserverL(MBrCtlStateChangeObserver* aObserver);
    virtual void RemoveStateChangeObserver(MBrCtlStateChangeObserver* aObserver);
    virtual HBufC* VersionInfoLC(TBrCtlDefs::TBrCtlVersionInfo aVersionInfo);
    virtual void SetSelfDownloadContentTypesL(TDesC& aContentTypes);
    virtual void SetParamL(TBrCtlDefs::TBrCtlParams aParam, const TDesC& aValue);
    virtual void LoadSavedPageL(const TDesC& aUid, TDesC& aSavedPageBuffer);
    virtual RArray<TUint>* ZoomLevels() const;
    virtual int FindKeyword(const TDesC& aKeyword);
    virtual TBrCtlDefs::TBrCtlFindResponse FindKeywordAgain(TBool aForward);
    virtual const RPointerArray<TBrCtlSubscribeTo>& SubscribeToMenuItemsL();
    virtual TSize MinimumSize();
    virtual TKeyResponse OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType);
    virtual void SetFocus(TBool aFocus, TDrawNow aDrawNow = ENoDrawNow);
    virtual TBool OkToExit();
    virtual void HandleDownloadCommandL(TUint16 aTransId, TBrCtlDefs::TBrCtlDownloadCmd aCommand);
    virtual CGulIcon* GetBitmapData(const TDesC& aUrl, TBrCtlDefs::TBrCtlBitmapInfo aBitmapInfo);
    virtual void AddCommandObserverL(MBrCtlCommandObserver* aObserver);
    virtual void RemoveCommandObserver(MBrCtlCommandObserver* aObserver);
    virtual MWidgetExtension* CreateWidgetExtensionL(MWidgetCallback& aWidgetCallback);
    virtual TBrCtlImageCarrier* FocusedImageLC();
    virtual void PostUrlL(const TDesC& aUrl, const TDesC8& aContentType, const TDesC8& aPostData, const TDesC8* aBoundary, TAny* aReserved);
    virtual void TakeSnapshotL(CFbsBitmap& aSnapshot);
    virtual TBool IsSynchRequestPending();

    // --- CCoeControl -----------------------------------------------------------------------------------------------------------
    void SizeChanged();
    void Draw(const TRect& aRect) const;
    void MakeVisible(TBool aVisible);
    void HandlePointerEventL(const TPointerEvent& aPointerEvent);
    void FocusChanged(TDrawNow aDrawNow);
    TCoeInputCapabilities InputCapabilities() const;   // the editable-focus signal to the FEP / pen input (CPhFepEditor)

private:
    CPhBrCtl();
    void ConstructL(CCoeControl* aParent, const TRect& aRect);
    void HandleEvent(const struct PhEvent& aEvent);
    void Fire(TBrCtlDefs::TBrCtlLoadEvent aEvent, TUint aSize = 0, TUint16 aTrans = 0);
    void FireState(TBrCtlDefs::TBrCtlState aState, TInt aValue);
    void FinishLoad(TBool aError);
    void NetworkConnection();
    void RecreateBitmapL();
    void CopyPixels();
    void SoftkeysChanged();
    void StartLoad();
    void EditFocusChanged(TInt aKind);
    void MaybeActivatePenInput();

private:
    TInt iView;
    TUint iCaps;
    TUint iCommandIdBase;
    MBrCtlSoftkeysObserver* iSoftkeys;
    MBrCtlSpecialLoadObserver* iSpecialLoad;
    MBrCtlDialogsProvider* iDialogs;
    MBrCtlWindowObserver* iWindow;
    RPointerArray<MBrCtlLoadEventObserver> iLoadObservers;
    RPointerArray<MBrCtlStateChangeObserver> iStateObservers;
    RPointerArray<MBrCtlCommandObserver> iCommandObservers;
    RPointerArray<TBrCtlWmlServiceOption> iWmlItems;
    RPointerArray<TBrCtlSubscribeTo> iSubscribe;
    RArray<TUint> iZoomLevels;
    TUint iSettings[96];
    CPhEventWatcher* iWatcher;
    CFbsBitmap* iBitmap;
    TBool iVisible;
    // load-event state machine (events must reach BrowserNG in order)
    TBool iLoading;
    TBool iSentStart;
    TBool iSentArrived;
    TBool iSentDisplayed;
    TBool iSentTitle;
    TBool iConnected;
    TBool iCanBack, iCanForward;
    // pointer: a tap is a click, a drag scrolls
    TPoint iDownPos, iLastPos;
    TBool iDown, iDragging;
    // text input: the FEP-aware editor stand-in (WebFepTextEditor) and the "a tap may have focused a field" latch
    CPhFepEditor* iFep;
    TBool iTapArmed;
    TInt iFocusKind;
    friend class CPhEventWatcher;
    };

#endif
