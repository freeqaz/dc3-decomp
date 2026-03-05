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
#include "math/Rand.h"
#include "synth/Pollable.h"

#pragma region MotdData

MainMenuPanel::MotdData::MotdData() : mWidth(0) {}

MainMenuPanel::MotdData::MotdData(MotdData const &motdData)
    : mType(motdData.mType), mText(motdData.mText), mWidth(motdData.mWidth) {}

#pragma endregion MotdData
#pragma region MainMenuPanel

MainMenuPanel::MainMenuPanel()
    : mMsgLabel(), mIsEntering(false), mNetCacheActive(false), mDownloadedTexture1(),
      mDownloadedTexture2(), mDLCArtPending(false), mUtilityArtPending(false),
      mMiscArtPending(false), mMotdProcessingActive(false), mMotdPromoFreq(), mMotdPickCount(), mMotdMaxStatsRun(),
      mMotdStatsRunCount(), mMotdMaxCommunityRun(), mMotdCommunityRunCount(), mPlayerEventProvider() {}

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
#ifdef HX_NATIVE
        if (TheNetCacheMgr)
#endif
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
#ifdef HX_NATIVE
    if (mState != 1 && TheNetCacheMgr && !TheNetCacheMgr->IsUnloaded())
#else
    if (mState != 1 && !TheNetCacheMgr->IsUnloaded())
#endif
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
#ifdef HX_NATIVE
    if (TheNetCacheMgr) {
#endif
    FOREACH (it, mNetCacheLoaders)
        TheNetCacheMgr->DeleteNetCacheLoader(*it);
    mNetCacheLoaders.clear();
    TheNetCacheMgr->Unload();
#ifdef HX_NATIVE
    } else {
        mNetCacheLoaders.clear();
    }
#endif
    mNetCacheActive = false;
}

void MainMenuPanel::ContentDone() { HandleType(Message("content_refresh_Done")); }

void MainMenuPanel::DownloadMotdArt() {
    if (mIsEntering) {
#ifdef HX_NATIVE
        if (TheNetCacheMgr)
#endif
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
    Symbol type = mMotdData.front().mType;
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
            currentWidth += it->mWidth;
        }
    }

    // Add more text until we have enough to fill scroll area (2x label width)
    while (currentWidth < targetWidth) {
        currentWidth += MotdPickNextText();
    }

    MILO_ASSERT(mMotdData.size(), 0x301);

    // Build the full text string from all queued messages
    String text = mMotdData.front().mText;
    it = mMotdData.begin();
    ++it;
    for (; it != mMotdData.end(); ++it) {
        text += "\n";
        text += it->mText;
    }

    mMsgLabel->ReFitTextScroll(text);
}

void MainMenuPanel::MotdHandleTextScrolledIn(int idx) {
    static Symbol utility("utility");
    static Symbol dlc("dlc");

    if (!mMotdProcessingActive)
        return;

    std::list<MotdData>::iterator it = mMotdData.begin();
    std::advance(it, idx);

    Sound *snd;
    if (it->mType == dlc) {
        snd = DataDir()->Find<Sound>("motd_store_item_new.snd", false);
        if (!snd)
            goto done;
    } else if (it->mType == utility) {
        String soundName = TheRockCentral.GetUtilitySound();
        snd = DataDir()->Find<Sound>(soundName.c_str(), false);
        if (!snd)
            goto done;
    } else {
        return;
    }

    snd->Play(0, 0, 0, 0, 0);

done:
    UpdateIconState(it->mType);
}

float MainMenuPanel::MotdPickNextText() {
    MILO_ASSERT(mMsgLabel, 0x269);

    static Symbol dlc("dlc");
    static Symbol utility("utility");
    static Symbol community("community");
    static Symbol stats("stats");

    MotdData data;
    data.mType = community;
    int iVar8;

    if (mMotdPromoFreq == 0) {
        goto normal_pick;
    }
    if (mMotdPickCount % mMotdPromoFreq != 0) {
        goto normal_pick;
    }

    {
        // Promo path
        Symbol *pPromoType = &dlc;
        if (mMotdLastPromoType != utility) {
            pPromoType = &utility;
        }
        data.mType = *pPromoType;

        Symbol cat = data.mType;
        data.mText = mMotdMessagesByCategory[cat].front();
        String textCopy(data.mText);
        data.mWidth = mMsgLabel->ComputeCharWidthsForText(textCopy)
            + mMsgLabel->Indentation();

        iVar8 = 1;
        mMotdLastPromoType = data.mType;
    }
    goto set_counter;

normal_pick:
    if (mMotdMaxCommunityRun == 0 || mMotdMaxCommunityRun <= mMotdCommunityRunCount) {
        iVar8 = 1;
    pick_stats:
        data.mType = stats;
        mMotdCommunityRunCount = 0;
        mMotdStatsRunCount = iVar8;
    } else if (!(mMotdStatsRunCount < mMotdMaxStatsRun)) {
        mMotdCommunityRunCount = 1;
        mMotdStatsRunCount = 0;
    } else {
        iVar8 = RandomInt(0, 2);
        data.mType = community;
        if (iVar8 == 0) {
            iVar8 = mMotdStatsRunCount + 1;
            goto pick_stats;
        }
        mMotdStatsRunCount = 0;
        mMotdCommunityRunCount = mMotdCommunityRunCount + 1;
    }

    {
        Symbol cat = data.mType;
        int offset = 0;
        if (mMotdMessagesByCategory[cat].size() > 1) {
            offset = RandomInt(0, (unsigned int)mMotdMessagesByCategory[cat].size() >> 1);
        }

        std::list<String>::iterator it = mMotdMessagesByCategory[cat].begin();
        std::advance(it, offset);

        data.mText = *it;
        String textCopy(data.mText);
        data.mWidth = mMsgLabel->ComputeCharWidthsForText(textCopy)
            + mMsgLabel->Indentation();

        mMotdMessagesByCategory[cat].push_back(*it);
        mMotdMessagesByCategory[cat].erase(it);

        if (mMotdPromoFreq == 0)
            goto end;
        iVar8 = mMotdPickCount + 1;
    }

set_counter:
    mMotdPickCount = iVar8;

end:
    mMotdData.push_back(data);
    return mMotdData.back().mWidth;
}

void MainMenuPanel::MotdInitializeTexts() {
    static Symbol dlc("dlc");
    static Symbol utility("utility");
    static Symbol community("community");
    static Symbol stats("stats");
    static Symbol no_profile("no_profile");

    Symbol *pCategory = &no_profile;

    // Ensure label uses scroll marquee wrap always
    if (mMsgLabel->GetFitType() != RndText::kFitScrollMarqueeWrapAlways) {
        MILO_LOG(
            ">>>>>>>>>> Forcing the souce lable to use "
            "kFitScrollMarqueeWrapAlways as the text fit type.\n"
        );
        mMsgLabel->SetFitType(RndText::kFitScrollMarqueeWrapAlways);
    }

    // Clear existing alt style reference and scroll state
    mMsgLabel->SetAltStyle(nullptr);

    mMotdProcessingActive = false;
    mMotdData.clear();

    // Check for no_profile messages - simple case
    if (!mMotdMessagesByCategory[no_profile].empty()) {
        goto show_single;
    }

    // If both DLC and utility are empty...
    if (mMotdMessagesByCategory[dlc].empty() && mMotdMessagesByCategory[utility].empty()) {
        unsigned int commCount = mMotdMessagesByCategory[community].size();
        unsigned int statsCount = mMotdMessagesByCategory[stats].size();
        if (commCount + statsCount == 1) {
            pCategory = &stats;
            goto show_single;
        }
    }

    // Enable scrolling mode
    mMotdProcessingActive = true;
    mMsgLabel->SetAltStyle(this);
    {
        float targetWidth = mMsgLabel->Width() * 2.0f;
        mMotdPromoFreq = TheRockCentral.GetMotdFreq();

        // Count community and stats
        int communityCount = mMotdMessagesByCategory[community].size();
        int statsCount = mMotdMessagesByCategory[stats].size();

        // Adjust promo frequency
        if (mMotdMessagesByCategory[dlc].empty()
            && mMotdMessagesByCategory[utility].empty()) {
            mMotdPromoFreq = 0;
        } else if (mMotdPromoFreq < 1) {
            mMotdPromoFreq = 1;
        } else if (communityCount + statsCount < mMotdPromoFreq - 1) {
            mMotdPromoFreq = communityCount + statsCount + 1;
        }

        // Set community max rotation count
        {
            unsigned int commSize = mMotdMessagesByCategory[community].size();
            if (commSize == 0) {
                mMotdMaxCommunityRun = 0;
            } else {
                unsigned int commSize2 = mMotdMessagesByCategory[community].size();
                if (commSize2 > 1) {
                    mMotdMaxCommunityRun = 2;
                }
            }
        }

        // Set stats max rotation count
        {
            unsigned int statsSize = mMotdMessagesByCategory[stats].size();
            if (statsSize > 1) {
                mMotdMaxStatsRun = 2;
            } else {
                mMotdMaxStatsRun = 1;
            }
        }

        // Initialize counters
        mMotdStatsRunCount = 0;
        mMotdCommunityRunCount = 0;
        mMotdPickCount = 0;
        mMotdLastPromoType = utility;

        // Pick first text
        MotdPickNextText();

        // Fill scroll area with enough text (2x label width)
        float currentWidth = 0.0f;
        while (currentWidth < targetWidth) {
            currentWidth += MotdPickNextText();
        }

        // Assert we have at least one text
        MILO_ASSERT(mMotdData.size(), 0x258);

        // Build combined text string
        String text = mMotdData.front().mText;
        std::list<MotdData>::iterator it = mMotdData.begin();
        ++it;
        for (; it != mMotdData.end(); ++it) {
            text += "\n";
            text += it->mText;
        }

        mMsgLabel->SetPrelocalizedString(text);
    }
    return;

show_single:
    {
        std::list<String> &msgList = mMotdMessagesByCategory[*pCategory];
        mMsgLabel->SetPrelocalizedString(msgList.front());
    }
}

void MainMenuPanel::UpdateArtLoaders() {
#ifdef HX_NATIVE
    if (!TheNetCacheMgr) return;
#endif
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
