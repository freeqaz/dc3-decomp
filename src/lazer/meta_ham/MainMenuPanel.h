#pragma once
#include "HamPanel.h"
#include "flow\PropertyEventProvider.h"
#include "hamobj\HamLabel.h"
#include "meta_ham\MainMenuProvider.h"
#include "obj\Data.h"
#include "obj/Object.h"
#include "os\ContentMgr.h"
#include "rndobj\Tex.h"
#include "stl\_map.h"
#include "stl\_pair.h"
#include "stl\_vector.h"
#include "utl\NetCacheLoader.h"
#include "utl\Str.h"
#include "utl\Symbol.h"
#include <list>

class MainMenuPanel : public HamPanel, public ContentMgr::Callback {
public:
    struct MotdData {
    public:
        MotdData();
        MotdData(const MotdData &);

        Symbol mType;
        String mText;
        float mWidth;
    };

    // Hmx::Object
    virtual ~MainMenuPanel();
    OBJ_CLASSNAME(MainMenuPanel)
    OBJ_SET_TYPE(MainMenuPanel)
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);

    // UIPanel
    virtual void Load();
    virtual void Enter();
    virtual void Exit();
    virtual bool Unloading() const;
    virtual void Poll();
    virtual void Unload();
    virtual void FinishLoad();

    // ContentMgr::Callback
    virtual void ContentDone();

    NEW_OBJ(MainMenuPanel)

    MainMenuPanel();
    MainMenuProvider *GetMainMenuProvider();

protected:
    HamLabel *mMsgLabel; // 0x40
    MainMenuProvider unk44; // 0x44
    bool mIsEntering; // 0x80
    bool mNetCacheActive; // 0x81
    std::list<NetCacheLoader *> mNetCacheLoaders; // 0x84
    RndTex *mDownloadedTexture1; // 0x8c
    RndTex *mDownloadedTexture2; // 0x90
    bool mDLCArtPending; // 0x94
    bool mUtilityArtPending; // 0x95
    bool mMiscArtPending; // 0x96
    std::map<Symbol, std::list<String> > mMotdMessagesByCategory; // 0x98
    bool mMotdProcessingActive; // 0xb0
    std::list<MotdData> mMotdData; // 0xb4
    int mMotdPromoFreq; // 0xbc - how often to insert promo messages
    int mMotdPickCount; // 0xc0 - text pick counter for promo frequency
    int mMotdMaxStatsRun; // 0xc4 - max consecutive stats messages
    int mMotdStatsRunCount; // 0xc8 - current stats run length
    int mMotdMaxCommunityRun; // 0xcc - max consecutive community messages
    int mMotdCommunityRunCount; // 0xd0 - current community run length
    Symbol mMotdLastPromoType; // 0xd4 - alternates dlc/utility
    PropertyEventProvider *mPlayerEventProvider;

private:
    void DeleteDownloadedArts();
    void DownloadMotdArt();
    void HandleNetCacheMgrFailure();
    void HandleNetCacheLoaderFailure(int);
    void UpdateIconState(Symbol);
    void CleanupNetCacheRelated();
    void MotdHandleTextScrolledIn(int);
    void LoadArt(String);
    void UpdateArtLoaders();
    float MotdPickNextText();
    void MotdHandleTextScrolledOut(int);
    void MotdInitializeTexts();
    void MotdSetup(HamLabel *);
};
