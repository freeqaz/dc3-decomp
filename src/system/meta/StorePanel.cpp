#include "meta/StorePanel.h"
#include "macros.h"
#include "meta/Profile.h"
#include "meta/StoreEnumeration.h"
#include "meta/StoreOffer.h"
#include "obj/Data.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "obj/PropSync.h"
#include "os/ContentMgr.h"
#include "os/Debug.h"
#include "os/PlatformMgr.h"
#include "rndobj/Bitmap.h"
#include "rndobj/Tex.h"
#include "ui/UI.h"
#include "ui/UIPanel.h"
#include "utl/BufStream.h"
#include "utl/JobMgr.h"
#include "utl/MakeString.h"
#include "utl/NetCacheMgr.h"
#include "utl/Std.h"
#include "utl/Symbol.h"

StorePanel::StorePanel()
    : mNeedsCacheLoad(false), mLoadOk(false), mShowTestOffers(false), mArtLoader(0),
      mAlbumTex(Hmx::Object::New<RndTex>()), mPendingArtCallback(0), mEnumJobID(-1),
      mStorePreviewMgr(0), mNeedsReEnum(false), mPurchaser(0), mCheckoutItem(nullptr), mCheckoutProfile(0),
      mPurchaseSource(gNullStr), mBackupPurchaseSource(gNullStr), unk94(0), mPostPurchaseJob(0) {}

StorePanel::~StorePanel() {
    DeleteAll(mOffers);
    DeleteAll(mPendingOffers);
    delete mAlbumTex;
}

BEGIN_PROPSYNCS(StorePanel)
    SYNC_PROP(load_ok, mLoadOk)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

void StorePanel::Load() {
    UIPanel::Load();
    mLoadOk = true;
    mNeedsCacheLoad = true;
    ThePlatformMgr.AddSink(this);
    if (StoreProfile() == 0) {
        ExitError(kStoreErrorLiveServer);
    } else if (ThePlatformMgr.IsSignedIntoLive(0) == 0) {
        ExitError(kStoreErrorCacheNoSpace);
    }
    TheContentMgr.StartRefresh();
#ifdef HX_NATIVE
    if (TheNetCacheMgr)
#endif
    TheNetCacheMgr->Load((NetCacheMgr::CacheSize)1);
    MILO_ASSERT(!mStorePreviewMgr, 0x84);
    mStorePreviewMgr = new StorePreviewMgr();
    mStorePreviewMgr->AddSink(this);
    MILO_ASSERT(!mPurchaser, 0x88);
    unk94 = 2;
}

void StorePanel::Enter() {
    UIPanel::Enter();
    Profile *profile = StoreProfile();
    if (profile == 0) {
        if (mLoadOk) {
            mLoadOk = false;
            ExitStore(kStoreErrorLiveServer);
        }
    } else if ((ThePlatformMgr.IsSignedIntoLive(profile->GetPadNum()) == 0 ||
                ThePlatformMgr.IsPadAGuest(profile->GetPadNum()) != 0) &&
               mLoadOk) {
        mLoadOk = false;
        ExitStore(kStoreErrorCacheNoSpace);
    }
    if (mNeedsCacheLoad) {
        TheNetCacheMgr->Load((NetCacheMgr::CacheSize)1);
        mNeedsCacheLoad = false;
    }
    mShowing = (bool)mLoadOk;
    XBackgroundDownloadSetMode(XBACKGROUND_DOWNLOAD_MODE_ALWAYS_ALLOW);
    mNeedsReEnum = false;
}

void StorePanel::Exit() {
    XBackgroundDownloadSetMode(XBACKGROUND_DOWNLOAD_MODE_AUTO);
    Symbol sym = gNullStr;
    ThePlatformMgr.RemoveSink(this, sym);
    if (0 <= mEnumJobID) {
        ThePlatformMgr.CancelEnumJob(mEnumJobID);
    }
    mEnumJobID = -1;
    UIPanel::Exit();
}

bool StorePanel::Exiting() const {
    if (mPurchaser && mPurchaser->IsPurchasing()) {
        return true;
    }
    return UIPanel::Exiting();
}

void StorePanel::Poll() {
    UIPanel::Poll();
    if (!mLoadOk)
        return;
    if (TheNetCacheMgr->GetHasFailed()) {
        HandleNetCacheMgrFailure();
        return;
    }
    if (!TheNetCacheMgr->IsReady())
        return;

    mStorePreviewMgr->Poll();
    NetCacheMgrFailType failType;
    if (mStorePreviewMgr->GetLastFailure(failType)) {
        HandleNetCacheLoaderFailure((int)failType);
    }

    // Iterate NetCacheLoaders
    std::list<NetCacheLoader *>::iterator cur = mNetCacheLoaders.begin();
    while (cur != mNetCacheLoaders.end()) {
        NetCacheLoader *loader = *cur;
        if (loader->IsLoaded()) {
            if (loader == (NetCacheLoader *)mArtLoader) {
                MILO_ASSERT(mPendingArtCallback, 0x167);
                int size = loader->GetSize();
                char *buffer = loader->GetBuffer();
                MILO_ASSERT(buffer, 0x16d);
                RndBitmap bmap;
                BufStream stream(buffer, size, true);
                bmap.Reset();
                bmap.Load(stream);
                bmap.SetMip(0);
                TheNetCacheMgr->DeleteNetCacheLoader(loader);
                mAlbumTex->SetBitmap(bmap, 0, false, RndTex::kRegular);
                if (mPendingArtCallback->GetState() == UIPanel::kUp) {
                    static Message artMsg("art_loaded");
                    mPendingArtCallback->HandleType(artMsg.mData);
                }
                mArtLoader = 0;
                mPendingArtCallback = 0;
            } else {
                TheNetCacheMgr->DeleteNetCacheLoader(loader);
            }
            cur = mNetCacheLoaders.erase(cur);
        } else if (loader->HasFailed()) {
            NetCacheMgrFailType ft = loader->GetFailType();
            TheNetCacheMgr->DeleteNetCacheLoader(loader);
            cur = mNetCacheLoaders.erase(cur);
            HandleNetCacheLoaderFailure((int)ft);
        } else {
            ++cur;
        }
    }

    if (!mPurchaser && mNeedsReEnum && mEnumJobID == -1) {
        mNeedsReEnum = false;
        EnumerateOffers(mPendingOffers.size() != mOffers.size());
    }

    if (mPurchaser) {
        mPurchaser->Initiate();
        if (!mPurchaser->IsPurchasing()) {
            bool enumFinished = false;
            bool purchaseMade = false;
            if (mPurchaser->PurchaseMade()) {
                if (!(mCartOffers.empty())) {
                    // Multiple items checkout
                    if (mPurchaser->IsSuccess()) {
                        std::vector<unsigned long long> songIds;
                        for (size_t i = 0; i < mCartOffers.size(); i++) {
                            songIds.push_back(mCartOffers[i].first->songID);
                        }
                        void *mem = operator new(0x64);
                        MultipleItemsPostPurchaseEnumJob *job = 0;
                        job = new (mem) MultipleItemsPostPurchaseEnumJob(
                                this,
                                mCheckoutProfile,
                                songIds,
                                mPurchaser->mSource,
                                mPurchaser->mUserIndex
                            );
                        mPostPurchaseJob = job;
                        purchaseMade = true;
                    }
                } else {
                    // Single item checkout
                    if (mCheckoutItem != 0 && !mCheckoutItem->isPurchased) {
                        if (mPurchaser->IsSuccess()) {
                            enumFinished = true;
                            mCheckoutItem->isPurchased = true;
                            static Message enumMsg("enum_finished");
                            HandleType(enumMsg.mData);
                            TheUI->Handle(enumMsg.mData, false);
                        } else if (mPurchaser->IsSuccess()) {
                            // purchased
                        } else if (mCheckoutProfile != 0) {
                            void *mem = operator new(0x50);
                            PostPurchaseEnumJob *job = 0;
                            job = new (mem) PostPurchaseEnumJob(
                                    this,
                                    mCheckoutProfile,
                                    mCheckoutItem->songID,
                                    mPurchaser->mSource,
                                    mPurchaser->mUserIndex
                                );
                            mPostPurchaseJob = job;
                            purchaseMade = true;
                        }
                    }
                }
            }

            static Message checkoutMsg("checkout_finished", DataNode(enumFinished), DataNode(purchaseMade));
            HandleType(checkoutMsg.mData);
            TheUI->Handle(checkoutMsg.mData, false);

            delete mPurchaser;
            mPurchaser = 0;
            mCheckoutItem = 0;
            mCheckoutProfile = 0;
            mCartOffers.clear();
        }
    }
}

void StorePanel::ExitStore(StoreError) const {}
Profile *StorePanel::StoreProfile() const { return nullptr; }

bool StorePanel::IsLoaded() const {
    return (UIPanel::IsLoaded() && TheContentMgr.RefreshDone());
}

void StorePanel::Unload() {
    if ((int)mEnumJobID >= 0) {
        ThePlatformMgr.CancelEnumJob(mEnumJobID);
    }
    mEnumJobID = -1;
    RELEASE(mPurchaser);
    mCheckoutItem = 0;
    mCheckoutProfile = 0;
    mCartOffers.clear();
    RemoveSink(mStorePreviewMgr, gNullStr);
    RELEASE(mStorePreviewMgr);
    FOREACH (it, mNetCacheLoaders) {
        TheNetCacheMgr->DeleteNetCacheLoader(*it);
    }
    mNetCacheLoaders.clear();
    DeleteAll(mOffers);
    DeleteAll(mPendingOffers);
    TheNetCacheMgr->Unload();
    UIPanel::Unload();
}

void StorePanel::LoadArt(const char *cc, UIPanel *panel) {
    String str(cc);
    std::list<NetCacheLoader *>::iterator it = std::find(mNetCacheLoaders.begin(), mNetCacheLoaders.end(), str);
    NetCacheLoader *loader = TheNetCacheMgr->AddNetCacheLoader(cc, (NetLoaderPos)0);
    if (loader) {
        mNetCacheLoaders.insert(it, loader);
    }
    mPendingArtCallback = panel;
}

void StorePanel::CheckOut(StorePurchaseable *p) {
    StorePurchaser *purchaser;

    MILO_ASSERT(p->IsAvailable(), 0x2c0);
    MILO_ASSERT(!mPurchaser, 0x2c1);
    Profile *profile = StoreProfile();
    MILO_ASSERT(profile, 0x2c4);

    mCheckoutItem = p;
    mCheckoutProfile = (int)profile;

    // Allocate and construct XboxPurchaser
    void *mem = operator new(0x50);
    if (mem) {
        purchaser = new (mem) XboxPurchaser(
            profile->GetPadNum(),
            p->songID,
            0,
            0,
            mPurchaseSource,
            0
        );
    } else {
        purchaser = 0;
    }
    mPurchaser = purchaser;

    // Manual vtable dispatch: Call Initiate() at vtable offset 1
    // Note: mPurchaser is typed as StorePurchaser*, which lacks Initiate() in its interface.
    // This raw vtable access is required to match the original binary's codegen.
    typedef void (*VirtFunc)(void*);
    void** vptr = (void**)mPurchaser;
    void** vfunc_addr = (void**)*vptr;
    VirtFunc vf = (VirtFunc)vfunc_addr[1];
    vf(mPurchaser);
}

void StorePanel::ExitError(StoreError e) {
    MILO_ASSERT(e != kStoreErrorSuccess, 0x405);
    if (mLoadOk) {
        mLoadOk = false;
        ExitStore(e);
    }
}

void StorePanel::HandleNetCacheMgrFailure() {
    StoreError err;
    NetCacheMgrFailType failTy;

    err = kStoreErrorSuccess;
    failTy = TheNetCacheMgr->GetFailType();
    switch (failTy) {
    case kNCMFT_StoreServer:
    case kNCMFT_NoSpace:
        MILO_WARN("Failure %d in NetCacheMgr.\n", failTy);
        break;
    case kNCMFT_StorageDeviceMissing:
        err = kStoreErrorNoMetadata;
        break;
    default:
        MILO_WARN("Unknown failure %d in NetCacheMgr.\n", failTy);
        break;
    }
    if (err != kStoreErrorNoMetadata && !ThePlatformMgr.IsEthernetCableConnected()) {
        err = kStoreErrorNoMetadata;
    }
    if (err != kStoreErrorSuccess)
        ExitError(err);
}

void StorePanel::HandleNetCacheLoaderFailure(int failType) {

    MILO_ASSERT((0) <= (failType) && (failType) < (4), 0xe5);
    StoreError err = kStoreErrorSuccess;

    switch (failType) {
    case 0:
        break;
    case 1: {
        void (*func)(void *) = (void (*)(void *))*(void **)this;
        func(this);
        if (ThePlatformMgr.IsSignedIntoLive(0) == 0) {
            err = kStoreErrorCacheNoSpace;
        }
        break;
    }
    case 2:
        break;
    case 3:
        break;
    }

    if (ThePlatformMgr.IsEthernetCableConnected() == 0) {
        err = kStoreErrorNoMetadata;
    }

    ExitError(err);
}

void StorePanel::MultipleItemsCheckout(std::list<StoreOffer *> *offers) {
    MILO_ASSERT(!mPurchaser, 0x2e7);

    std::vector<u64> songIds;
    std::vector<std::pair<StorePurchaseable*, const Profile*>> pairs;

    // Populate vectors from offers list
    FOREACH(it, *offers) {
        StoreOffer *offer = *it;
        MILO_ASSERT(offer->IsAvailable(), 0x2ef);

        // Store song ID
        u64 songId = offer->songID;
        songIds.push_back(songId);

        // Store pair
        std::pair<StorePurchaseable*, const Profile*> pair;
        pair.first = offer;
        pairs.push_back(pair);
    }

    // Allocate and construct purchaser
    void* mem = operator new(0x50);
    if (mem) {
        u64 firstSongId = (*offers->begin())->songID;
        mPurchaser = new (mem) XboxMultipleItemsPurchaser(
            (int)firstSongId,
            songIds,
            mPurchaseSource,
            0
        );
    } else {
        mPurchaser = 0;
    }

    // Call Initiate virtual function
    if (mPurchaser) {
        typedef void (*VirtFunc)(void*);
        void** vptr = (void**)mPurchaser;
        void** vfunc_addr = (void**)*vptr;
        VirtFunc vf = (VirtFunc)vfunc_addr[1];
        vf(mPurchaser);
    }

    // Clean up
    songIds.clear();
}

void StorePanel::PopulateOffers(DataArray *arr, bool b) {
    if (mLoadOk) {
        DeleteAll(mPendingOffers);
        if (!b) {
            DeleteAll(mOffers);
        }

        std::vector<StoreOffer *> *offerVec = &mPendingOffers;
        if (!b) {
            offerVec = &mOffers;
        }

        if (arr != NULL) {
            arr->AddRef();
            s16 count = arr->Size();
            s32 i = 1;

            if (count > 1) {
                do {
                    DataArray *child_arr = arr->Array(i);
                    StoreOffer *offer = MakeNewOffer(child_arr);

                    if (((mShowTestOffers == 0) && offer->IsTest()) || !offer->ValidTitle()) {
                        delete offer;
                    } else {
                        offerVec->push_back(offer);
                    }

                    i++;
                } while (i < count);
            }

            ValidateOffers(*offerVec);
            arr->Release();
        }
    }
}

void StorePanel::EnumerateOffers(bool b) {
    Profile *profile = StoreProfile();
    MILO_ASSERT(profile, 0x356);
    Job *job;
    if (EnumerateSubsetOfOfferIDs()) {
        std::vector<UINT64> offerIDs;
        GetOfferIDsToEnumerate(offerIDs, b);
        if (offerIDs.empty()) {
            if (mLoadOk) {
                mLoadOk = false;
                ExitStore(kStoreErrorSignedOut);
            }
            return;
        }
        job = new StoreEnumJob(this, profile->GetPadNum(), &offerIDs);
    } else {
        job = new StoreEnumJob(this, profile->GetPadNum(), 0);
    }
    ThePlatformMgr.QueueEnumJob(job);
    mEnumJobID = job->ID();
    static Message msg("enum_start");
    HandleType(msg);
    TheUI->Handle(msg, false);
}

void StorePanel::FinishEnum(std::list<EnumProduct> const &enumList, bool arg) {
    mEnumJobID = -1;

    if (arg) {
        StoreError err = UpdateOffers(enumList, arg);

        if (0 == err || err == 1) {
            if (!mPendingOffers.empty()) {
                err = UpdateOffers(enumList, true);
            }
        }

        if (err != 0) {
            if (err == 1) {
                if (TheNetCacheMgr->IsDebug() == 0) {
                    FormatString fmt("No offers in this metadata were");
                    TheDebug.Notify(fmt.Str());
                }
            } else {
                ExitError(err);
                return;
            }
            ExitError(err);
            return;
        }

        static unsigned char msg_created = (unsigned char)(0);
        if (!msg_created) {
            static Symbol sym("enum_finished");
            msg_created = 1;
            static Message msg(sym);
        }
    } else {
        FormatString fmt("An enumeration failed!");
        TheDebug.Notify(fmt.Str());

        if (mLoadOk) {
            mLoadOk = false;
            void (*func)(void *, int) = (void (*)(void *, int))*(void **)this;
            func(this, 2);
        }
    }
}

StoreError StorePanel::UpdateOffers(std::list<EnumProduct> const &enumList, bool arg) {
    std::vector<StoreOffer *> *offers;
    StoreError result;

    if (!(arg == 0)) {
        offers = &mPendingOffers;
    } else {
        offers = &mOffers;
    }

    // Check if mShowTestOffers is non-zero
    if (mShowTestOffers != 0) {
        result = kStoreErrorSuccess;
    } else if (offers->size() == 0) {
        // Empty list - format error message
        FormatString fmt("This metadata contained no offer");
        TheDebug.Notify(fmt.Str());
        result = kStoreErrorSignedOut;
    } else {
        result = kStoreErrorNoContent;
    }

    // Iterate through offers
    std::vector<StoreOffer *>::iterator it;
    auto offersEnd = offers->end();
    for (it = offers->begin(); it != offersEnd; ++it) {
        StoreOffer *offer = *it;
        bool _cond = offer->Exists();
        if (_cond) {
            std::list<EnumProduct>::const_iterator enumIt;
            enumIt = enumList.begin();
            bool match = false;
            while (enumIt != enumList.end()) {
                if (offer->songID == *(u64 *)&enumIt->mOfferIDLo) {
                    match = true;
                    break;
                }
                ++enumIt;
            }

            if (match) {
                result = kStoreErrorSuccess;
                // Call virtual function at offset 0x70 (ILP32)
#ifdef HX_NATIVE
                // On LP64, vtable offsets shift — skip this call, offer handling is stubbed
#else
                void (*func)(void *, void *, void *) = (void (*)(void *, void *, void *))*(void **)((u32)this + 0x70);
                func(this, offer, (void *)((u32)offer + 0x38));
#endif
            } else {
                if (offer->IsTest()) {
                    offer->isAvailable = false;
                    offer->isPurchased = false;
                    offer->cost = 0x270f;
                }
            }
        } else {
            if (offer->IsTest()) {
                offer->isAvailable = false;
                offer->isPurchased = false;
                offer->cost = 0x270f;
            }
        }
    }

    return result;
}

void StorePanel::UpdateFromEnumProduct(StorePurchaseable *sp, EnumProduct const *ep) {
    MILO_ASSERT(sp, 0x3f0);
    MILO_ASSERT(ep, 0x3f1);
    sp->isPurchased = (ep->mPurchased != 0);
    sp->cost = ep->mPrice;
    sp->isAvailable = true;
}

void StorePanel::StartReEnum() {
    if (mPostPurchaseJob != 0) {
        ThePlatformMgr.QueueEnumJob(mPostPurchaseJob);
        mPostPurchaseJob = 0;
    }
}

DataNode StorePanel::OnMsg(SigninChangedMsg const &msg) {
    Profile *profile = StoreProfile();
    if (profile != 0) {
        // Check if this profile's pad number is in the signin change mask
        int changedMask;
        int padNum;
        changedMask = bool(msg.mData->Node(3).Int(msg.mData));
        padNum = profile->GetPadNum();
        // If this pad's bit is not set in the change mask, ignore the message
        if (((1 << padNum) & changedMask) == 0) {
            return 0;
        }
    }
    // Signin changed for this profile - exit the store
    if (mLoadOk) {
        mLoadOk = false;
        ExitStore(kStoreErrorLiveServer);
    }
    return 0;
}

DataNode StorePanel::OnMsg(ProfileSwappedMsg const &) { return 0; }

DataNode StorePanel::OnMsg(SingleItemEnumCompleteMsg const &msg) {
    bool hasOffer = false;
    if (msg.Success()) {
        if (msg.HasOfferID()) {
            hasOffer = true;
        }
    }

    if (hasOffer) {
        u64 offerId = msg.OfferID();
        for (std::vector<StoreOffer *>::iterator it = mOffers.begin(); it != mOffers.end();
             ++it) {
            StoreOffer *offer = *it;
            if (offer->songID == offerId) {
                offer->isPurchased = true;
                static Message enumMsg("enum_finished");
                HandleType(enumMsg);
                TheUI->Handle(enumMsg, false);
                break;
            }
        }
    }

    static Message doneMsg("reenum_finished", DataNode(0));
    doneMsg->Node(2) = DataNode((int)hasOffer);
    TheUI->Handle(doneMsg, false);
    return DataNode(0);
}

void StorePanel::ValidateOffers(std::vector<StoreOffer *> &offers) {
    static Symbol song_sym("song");
    static Symbol dummy_upsell_sym("dummy_upsell_offer");
    static Symbol album_sym("album");
    static Symbol pack_sym("pack");

    std::vector<Symbol> song_names;
    std::vector<StoreOffer *> song_offers;

    std::vector<StoreOffer *>::iterator it;
    for (it = offers.begin(); it != offers.end(); ++it) {
        StoreOffer *offer = *it;
        Symbol offer_type = offer->OfferType();

        if (offer_type != dummy_upsell_sym) {
            Symbol short_name = offer->StoreOfferData()->Sym(0);

            std::vector<Symbol>::iterator sit =
                std::find(song_names.begin(), song_names.end(), short_name);

            if (sit != song_names.end()) {
                TheDebug.Notify(MakeString("Duplicate offer short name: %s", short_name));
            } else {
                song_names.push_back(short_name);
            }

            if (offer_type == song_sym) {
                song_offers.push_back(offer);
            }
        }
    }

    Symbol offer_types[2];
    offer_types[0] = album_sym;
    offer_types[1] = pack_sym;

    for (int i = 0; i < 2; i++) {
        Symbol cur_type = offer_types[i];
        std::vector<StoreOffer *>::iterator nit;
        for (nit = song_offers.begin(); nit != song_offers.end(); ++nit) {
            StoreOffer *song_offer = *nit;
            int count = 0;
            std::vector<StoreOffer *>::iterator oit;
            for (oit = offers.begin(); oit != offers.end(); ++oit) {
                StoreOffer *offer_ptr = *oit;
                if (offer_ptr->OfferType() == cur_type && offer_ptr->HasSong(offer_ptr)) {
                    count++;
                }
            }
            if (count > 1) {
                Symbol song_name = song_offer->StoreOfferData()->Sym(0);
                TheDebug.Notify(MakeString("Song %s is in more than one %s", song_name, cur_type));
            }
        }
    }
}

DataNode StorePanel::OnMsg(MultipleItemsEnumCompleteMsg const &) { return 0; }

void StorePanel::SetSource(Symbol src, bool backup) {
    mPurchaseSource = src;
    if (backup)
        mBackupPurchaseSource = src;
}

BEGIN_HANDLERS(StorePanel)
    HANDLE_EXPR(toggle_test_offers, mShowTestOffers = !mShowTestOffers)
    HANDLE_EXPR(test_offers, mShowTestOffers)
    HANDLE_ACTION(load_art, LoadArt(_msg->Str(2), _msg->Obj<UIPanel>(3)))
    HANDLE_EXPR(album_tex, mAlbumTex)
    HANDLE_ACTION(cancel_art, (mArtLoader = 0, mPendingArtCallback = 0))
    HANDLE_ACTION(check_out, CheckOut(_msg->Obj<StorePurchaseable>(2)))
    HANDLE_ACTION(re_download, CheckOut(_msg->Obj<StorePurchaseable>(2)))
    HANDLE_ACTION(set_source, SetSource(_msg->Sym(2), _msg->Int(3)))
    HANDLE_ACTION(set_source_to_backup, mPurchaseSource = mBackupPurchaseSource)
    HANDLE_ACTION(start_reenum_if_needed, StartReEnum())
    HANDLE_MESSAGE(SigninChangedMsg)
    HANDLE_MESSAGE(ProfileSwappedMsg)
    HANDLE_MESSAGE(SingleItemEnumCompleteMsg)
    HANDLE_MESSAGE(MultipleItemsEnumCompleteMsg)
    HANDLE_SUPERCLASS(UIPanel)
END_HANDLERS

StoreEnumJob::StoreEnumJob(StorePanel *panel, int i, std::vector<UINT64> *vec) {
    mEnumeration = new XboxEnumeration(i, vec);
    mStorePanel = panel;
}

StoreEnumJob::~StoreEnumJob() {
    delete mEnumeration;
}

void StoreEnumJob::Start() { mEnumeration->Start(); }
void StoreEnumJob::Cancel(Hmx::Object *) {}

bool StoreEnumJob::IsFinished() {
    if (mEnumeration->IsEnumerating()) {
        mEnumeration->Poll();
    }
    return mEnumeration->IsEnumerating() == false;
}