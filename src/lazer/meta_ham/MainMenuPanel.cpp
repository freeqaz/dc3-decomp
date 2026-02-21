#include "meta_ham/MainMenuPanel.h"
#include "HamPanel.h"
#include "HamProfile.h"
#include "HamSongMgr.h"
#include "MainMenuPanel.h"
#include "ProfileMgr.h"
#include "hamobj/HamLabel.h"
#include "macros.h"
#include "meta_ham/MainMenuProvider.h"
#include "meta_ham/MetaPanel.h"
#include "meta_ham/MetagameStats.h"
#include "net_ham/RockCentral.h"
#include "obj/Data.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/ContentMgr.h"
#include "os/Debug.h"
#include "rndobj/Bitmap.h"
#include "rndobj/Tex.h"
#include "synth/Sound.h"
#include "ui/UIListProvider.h"
#include "ui/UIPanel.h"
#include "utl/BufStream.h"
#include "utl/Locale.h"
#include "utl/MakeString.h"
#include "utl/NetCacheLoader.h"
#include "utl/NetCacheMgr.h"
#include "utl/Std.h"
#include "utl/Symbol.h"

#pragma region MotdData

MainMenuPanel::MotdData::MotdData() : unkc(0) {}

MainMenuPanel::MotdData::MotdData(MotdData const &motdData)
    : unk0(motdData.unk0), unk4(motdData.unk4), unkc(motdData.unkc) {}

#pragma endregion MotdData
#pragma region MainMenuPanel

MainMenuPanel::MainMenuPanel()
    : mMsgLabel(), mIsEntering(false), mNetCacheActive(false), mDownloadedTexture1(),
      mDownloadedTexture2(), mDLCArtPending(false), mUtilityArtPending(false),
      mMiscArtPending(false), mMotdProcessingActive(false), unkbc(), unkc0(), unkc4(),
      unkc8(), unkcc(), unkd0(), mPlayerEventProvider() {}

MainMenuPanel::~MainMenuPanel() { DeleteDownloadedArts(); }

BEGIN_PROPSYNCS(MainMenuPanel)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

void MainMenuPanel::Load() {
    UIPanel::Load();
    TheContentMgr.StartRefresh();
    mNetCacheActive = false;
    mIsEntering = true;
    mDLCArtPending = false;
    mUtilityArtPending = false;
    mMiscArtPending = false;
    DeleteDownloadedArts();
    mDownloadedTexture1 = New<RndTex>();
    mDownloadedTexture2 = New<RndTex>();
}

void MainMenuPanel::Enter() {
    HamPanel::Enter();
    if (mIsEntering) {
        TheNetCacheMgr->Load((NetCacheMgr::CacheSize)1);
        mIsEntering = false;
        mNetCacheActive = true;
    }
    TheContentMgr.RegisterCallback(this, true);
}

void MainMenuPanel::Exit() {
    UIPanel::Exit();
    mMotdMessagesByCategory.clear();
    mMotdData.clear();
    mMsgLabel = 0;
    mMotdProcessingActive = false;
    TheContentMgr.UnregisterCallback(this, true);
}

bool MainMenuPanel::Unloading() const {
    if (mState != 1 && !TheNetCacheMgr->IsUnloaded())
        return true;
    else
        return UIPanel::Unloading();
}

void MainMenuPanel::Poll() {
    HamPanel::Poll();
    UpdateArtLoaders();
}

void MainMenuPanel::Unload() {
    if (mNetCacheActive)
        CleanupNetCacheRelated();
    DeleteDownloadedArts();
    UIPanel::Unload();
}

void MainMenuPanel::FinishLoad() {
    UIPanel::FinishLoad();
    mPlayerEventProvider = DataDir()->Find<PropertyEventProvider>("player.ep", true);
}

void MainMenuPanel::UpdateIconState(Symbol s) {
    static Symbol dlc("dlc");
    static Symbol utility("utility");
    static Symbol no_profile("no_profile");
    static Symbol profile("profile");
    static Symbol state("state");
    static Symbol rank("rank");
    static Symbol tier("tier");
    static Symbol gamertag("gamertag");
    if (s == dlc || s == utility) {
        mPlayerEventProvider->SetProperty(state, s);
    } else {
        HamProfile *pProfile = TheProfileMgr.GetActiveProfile(true);
        if (pProfile) {
            mPlayerEventProvider->SetProperty(state, profile);
            mPlayerEventProvider->SetProperty(rank, pProfile->GetMetagameRank()->RankNumber());
            mPlayerEventProvider->SetProperty(tier, pProfile->GetMetagameRank()->GetTier());
            mPlayerEventProvider->SetProperty(gamertag, pProfile->GetName());
        } else {
            mPlayerEventProvider->SetProperty(state, no_profile);
        }
    }
    Flow *f = DataDir()->Find<Flow>("udpate_icon_state.flow", true);
    f->Activate();
}

void MainMenuPanel::CleanupNetCacheRelated() {
    FOREACH (it, mNetCacheLoaders)
        TheNetCacheMgr->DeleteNetCacheLoader(*it);
    mNetCacheLoaders.clear();
    TheNetCacheMgr->Unload();
    mNetCacheActive = false;
}

void MainMenuPanel::ContentDone() { HandleType(Message("content_refresh_Done")); }

void MainMenuPanel::DownloadMotdArt() {
    if (mIsEntering) {
        TheNetCacheMgr->Load((NetCacheMgr::CacheSize)1);
        mNetCacheActive = true;
        mIsEntering = false;
    }
    mDLCArtPending = true;
    mUtilityArtPending = true;
    mMiscArtPending = true;
}

void MainMenuPanel::DeleteDownloadedArts() {
    if (mDownloadedTexture1) {
        delete mDownloadedTexture1;
        mDownloadedTexture1 = nullptr;
    }
    if (mDownloadedTexture2) {
        delete mDownloadedTexture2;
        mDownloadedTexture2 = nullptr;
    }
}

void MainMenuPanel::HandleNetCacheMgrFailure() {
    NetCacheMgrFailType failType = TheNetCacheMgr->GetFailType();
    switch (failType) {
    case kNCMFT_StoreServer:
        break;
    case kNCMFT_NoSpace:
        MILO_LOG("[MainMenuPanel::HandleNetCacheMgrFailure] kNCMFT_NoSpace.\n");
        break;
    case kNCMFT_StorageDeviceMissing:
        MILO_LOG(
            "[MainMenuPanel::HandleNetCacheMgrFailure] "
            "kNCMFT_StorageDeviceMissing.\n"
        );
        break;
    default:
        MILO_NOTIFY("Unknown failure %d in NetCacheMgr.", failType);
        break;
    }
}

void MainMenuPanel::HandleNetCacheLoaderFailure(int failType) {
    MILO_ASSERT_RANGE(failType, 0, kNCMFT_Max, 0x166);
    if (failType == kNCMFT_Unknown)
        failType = TheNetCacheMgr->GetFailType();

    switch (failType) {
    case kNCMFT_StoreServer:
        MILO_LOG("[MainMenuPanel::HandleNetCacheLoaderFailure] kNCMFT_StoreServer.\n");
        break;
    case kNCMFT_NoSpace:
        break;
    case kNCMFT_StorageDeviceMissing:
        MILO_LOG(
            "[MainMenuPanel::HandleNetCacheLoaderFailure] "
            "kNCMFT_StorageDeviceMissing.\n"
        );
        break;
    default:
        MILO_NOTIFY("Unknown failure %d in a net cache loader!", failType);
        break;
    }
}

void MainMenuPanel::MotdSetup(HamLabel *label) {
    MILO_ASSERT(label, 0x183);
    static Symbol dlc("dlc");
    static Symbol utility("utility");
    static Symbol community("community");
    static Symbol stats("stats");
    static Symbol no_profile("no_profile");
    mMotdMessagesByCategory.clear();
    HamProfile *activeProfile = TheProfileMgr.GetActiveProfile(true);
    if (activeProfile) {
        if (TheRockCentral.HasDlcMsg()) {
            String msg;
            TheRockCentral.GetDlcMsg(msg);
            mMotdMessagesByCategory[dlc].push_back(msg);
        }
        if (TheRockCentral.HasUtilityMsg()) {
            String msg;
            TheRockCentral.GetUtilityMsg(msg);
            mMotdMessagesByCategory[utility].push_back(msg);
        }
        int commMsgCount = TheRockCentral.GetCommunityMsgCount();
        for (int i = 0; i < commMsgCount; i++) {
            String msg;
            TheRockCentral.GetCommunityMsg(i, msg);
            mMotdMessagesByCategory[community].push_back(msg);
        }

        MetagameStats *playerStats = activeProfile->GetMetagameStats();
        MILO_ASSERT(playerStats, 0x1a9);
        int timesPlayed =
            playerStats->GetCount(MetagameStats::kCountStat_TimesPlayedPerform);
        timesPlayed +=
            playerStats->GetCount(MetagameStats::kCountStat_TimesPlayedMultiplayer);
        timesPlayed +=
            playerStats->GetCount(MetagameStats::kCountStat_TimesPlayedPractice);
        int numData = playerStats->NumData();
        if (MetaPanel::sMotdCheat || timesPlayed >= 10) {
            for (int i = 0; i < numData; i++) {
                String stat;
                playerStats->InqStatString(i, stat);
                mMotdMessagesByCategory[stats].push_back(stat);
            }
            if (TheContentMgr.RefreshDone()) {
                static Symbol stat_curr_library_size("stat_curr_library_size");
                int totalNumSongs = TheHamSongMgr.GetTotalNumLibrarySongs();
                String retval = MakeString(
                    Localize(stat_curr_library_size, false, TheLocale),
                    LocalizeSeparatedInt(totalNumSongs, TheLocale)
                );
                mMotdMessagesByCategory[stats].push_back(retval);
            }
        } else {
            static Symbol stat_welcome("stat_welcome");
            String locale = Localize(stat_welcome, false, TheLocale);
            mMotdMessagesByCategory[stats].push_back(locale);
        }
    } else {
        static Symbol message_noprofile("message_noprofile");
        String locale = Localize(message_noprofile, false, TheLocale);
        mMotdMessagesByCategory[no_profile].push_back(locale);
    }
    mMsgLabel = label;
    MotdInitializeTexts();
}

void MainMenuPanel::MotdHandleTextScrolledOut(int i) {
    static Symbol dlc("dlc");
    static Symbol utility("utility");
    static Symbol none("none");

    // Early exit if MOTD system not initialized
    if (!mMotdProcessingActive) {
        return;
    }

    // Clear icon state for DLC/utility messages
    Symbol type = mMotdData.front().unk0;
    if (type == dlc || type == utility) {
        UpdateIconState(none);
    }

    mMotdData.pop_front();

    // Calculate current width of remaining MOTD texts
    std::list<MotdData>::iterator it = mMotdData.begin();
    float currentWidth = 0.0f;
    float targetWidth = mMsgLabel->Width() * 2.0f;

    if (it == mMotdData.end()) {
        MotdPickNextText();
    } else {
        for (++it; it != mMotdData.end(); ++it) {
            currentWidth += it->unkc;
        }
    }

    // Add more text until we have enough to fill scroll area (2x label width)
    while (currentWidth < targetWidth) {
        currentWidth += MotdPickNextText();
    }

    MILO_ASSERT(mMotdData.size(), 0x301);

    // Build the full text string from all queued messages
    String text = mMotdData.front().unk4;
    it = mMotdData.begin();
    ++it;
    for (; it != mMotdData.end(); ++it) {
        text += "\n";
        text += it->unk4;
    }

    mMsgLabel->ReFitTextScroll(text);
}

void MainMenuPanel::UpdateArtLoaders() {
    if (TheNetCacheMgr->GetHasFailed()) {
        HandleNetCacheMgrFailure();
        if (mNetCacheActive) {
            CleanupNetCacheRelated();
        }
        mIsEntering = true;
    } else {
        if (TheNetCacheMgr->IsReady()) {
            if (mDLCArtPending) {
                mDLCArtPending = false;
                LoadArt(TheRockCentral.GetDLCImage());
            }
            if (mUtilityArtPending) {
                mUtilityArtPending = false;
                LoadArt(TheRockCentral.GetUtilityImage());
            }
            if (mMiscArtPending) {
                mMiscArtPending = false;
                LoadArt(TheRockCentral.GetMiscImage());
            }
            FOREACH (it, mNetCacheLoaders) {
                NetCacheLoader *loader = *it;
                if (loader->IsLoaded()) {
                    int size = loader->GetSize();
                    char *buffer = loader->GetBuffer();
                    MILO_ASSERT(buffer, 0x10d);
                    RndBitmap bitmap;
                    BufStream stream = BufStream(loader, size, true);
                    bitmap.Load(stream);
                    bitmap.SetMip(nullptr);
                    TheNetCacheMgr->DeleteNetCacheLoader(loader);
                    if (TheRockCentral.GetDLCImage() == loader->GetRemotePath()) {
                        mDownloadedTexture1->SetBitmap(bitmap, nullptr, false, RndTex::kRegular);
                        if (mState == 1) {
                            static Message dlc_image_loaded("dlc_image_loaded");
                            Handle(dlc_image_loaded, false);
                        }
                    }
                    if (TheRockCentral.GetUtilityImage() == loader->GetRemotePath()) {
                        mDownloadedTexture2->SetBitmap(bitmap, nullptr, false, RndTex::kRegular);
                        if (mState == 1) {
                            static Message utility_image_loaded("utility_image_loaded");
                            Handle(utility_image_loaded, false);
                        }
                    }
                    if (TheRockCentral.GetMiscImage() == loader->GetRemotePath()) {
                        TheRockCentral.SetMiscArtBitMap(bitmap);
                    }
                    mNetCacheLoaders.pop_front();
                } else {
                    if (loader->HasFailed()) {
                        NetCacheMgrFailType failType = loader->GetFailType();
                        TheNetCacheMgr->DeleteNetCacheLoader(loader);
                        mNetCacheLoaders.pop_front();
                        HandleNetCacheLoaderFailure(failType);
                    }
                }
            }
        }
    }
}

BEGIN_HANDLERS(MainMenuPanel)
    HANDLE_ACTION(
        update_main_menu_provider, unk44.UpdateList(_msg->Obj<UIListProvider>(2))
    )
    HANDLE_EXPR(get_main_menu_provider, &unk44) // not a perfect match for some reason
    HANDLE_EXPR(dlc_image, mDownloadedTexture1)
    HANDLE_EXPR(utility_image, mDownloadedTexture2)
    HANDLE_ACTION(update_icon_state, UpdateIconState(_msg->Sym(2)))
    HANDLE_ACTION(motd_setup, MotdSetup(_msg->Obj<HamLabel>(2)))
    HANDLE_ACTION(download_motd_art, DownloadMotdArt())
    HANDLE_ACTION(text_scrolled_in, MotdHandleTextScrolledIn(_msg->Int(2)))
    HANDLE_ACTION(text_scrolled_out, MotdHandleTextScrolledOut(_msg->Int(2)))
    HANDLE_SUPERCLASS(HamPanel)
END_HANDLERS

#pragma endregion MainMenuPanel
