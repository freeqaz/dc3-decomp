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
#include "rndobj/Tex.h"
#include "ui/UIPanel.h"
#include "utl/MakeString.h"
#include "utl/NetCacheMgr.h"
#include "utl/Std.h"
#include "utl/Symbol.h"

StorePanel::StorePanel()
    : unk50(false), mLoadOk(false), unk52(false), unk5c(0),
      unk60(Hmx::Object::New<RndTex>()), mPendingArtCallback(0), unk68(-1),
      mStorePreviewMgr(0), unk70(false), mPurchaser(0), unk78(nullptr), unk7c(0),
      unk8c(gNullStr), unk90(gNullStr), unk94(0), unk98(0) {}

StorePanel::~StorePanel() {
    DeleteAll(unk38);
    DeleteAll(unk44);
    delete unk60;
}

BEGIN_PROPSYNCS(StorePanel)
    SYNC_PROP(load_ok, mLoadOk)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

void StorePanel::Load() {
    UIPanel::Load();
    unk50 = true;
    mLoadOk = true;
    ThePlatformMgr.AddSink(this);
    if (ThePlatformMgr.IsSignedIn(0) == 0) {
        if (mLoadOk) {
            mLoadOk = false;
        }
    } else if (ThePlatformMgr.IsSignedIntoLive(0)) {
        if (mLoadOk) {
            mLoadOk = false;
        }
    }
    TheContentMgr.StartRefresh();
    TheNetCacheMgr->Load((NetCacheMgr::CacheSize)1);
    MILO_ASSERT(!mStorePreviewMgr, 0x84);
    mStorePreviewMgr = new StorePreviewMgr();
    mStorePreviewMgr->AddSink(this);
    MILO_ASSERT(!mPurchaser, 0x88);
}

void StorePanel::Enter() {}

void StorePanel::Exit() {
    XBackgroundDownloadSetMode(XBACKGROUND_DOWNLOAD_MODE_AUTO);
    ThePlatformMgr.RemoveSink(this, gNullStr);
    if (0 <= unk68)
        ThePlatformMgr.CancelEnumJob(unk68);
    unk68 = -1;
    UIPanel::Exit();
}

bool StorePanel::Exiting() const {
    bool b;
    if (mPurchaser && mPurchaser->unk8 != 0) {
        b = UIPanel::Exiting();
    }
    b = true;

    return b;
}

void StorePanel::Poll() {}

bool StorePanel::IsLoaded() const {
    return (UIPanel::IsLoaded() && TheContentMgr.RefreshDone());
}

void StorePanel::Unload() {
    if (0 < unk68) {
        ThePlatformMgr.CancelEnumJob(unk68);
    }
    unk68 = -1;
    RELEASE(mPurchaser);
    unk78 = 0;
    unk7c = 0;
    unk80.clear();
    RemoveSink(mStorePreviewMgr, gNullStr);
    RELEASE(mStorePreviewMgr);
    FOREACH (it, unk54) {
        TheNetCacheMgr->DeleteNetCacheLoader(*it);
    }
    unk54.clear();
    DeleteAll(unk38);
    DeleteAll(unk44);
    TheNetCacheMgr->Unload();
    UIPanel::Unload();
}

void StorePanel::LoadArt(const char *cc, UIPanel *panel) {
    String str(cc);
    std::list<NetCacheLoader *>::iterator it = unk54.begin();
    std::list<NetCacheLoader *>::iterator end = unk54.end();
    while (it != end) {
        ++it;
    }
    NetCacheLoader *loader = TheNetCacheMgr->AddNetCacheLoader(cc, (NetLoaderPos)0);
    if (loader) {
        unk54.insert(end, loader);
    }
    mPendingArtCallback = panel;
}

void StorePanel::CheckOut(StorePurchaseable *p) {
    MILO_ASSERT(p->IsAvailable(), 0x2c0);
    MILO_ASSERT(!mPurchaser, 0x2c1);
}

void StorePanel::ExitError(StoreError e) {
    MILO_ASSERT(e != kStoreErrorSuccess, 0x405);
    if (mLoadOk) {
        mLoadOk = false;
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
    StoreError err = kStoreErrorSuccess;

    MILO_ASSERT((0) <= (failType) && (failType) < (4), 0xe5);

    switch (failType) {
    case 0:
        break;
    case 1: {
        void (*func)(void *) = (void (*)(void *))*(void **)this;
        func(this);
        if (ThePlatformMgr.IsSignedIntoLive(0) == 0) {
            err = (StoreError)2;
        }
        break;
    }
    case 2:
        break;
    case 3:
        break;
    }

    if (ThePlatformMgr.IsEthernetCableConnected() == 0) {
        err = (StoreError)7;
    }

    ExitError(err);
}

void StorePanel::MultipleItemsCheckout(std::list<StoreOffer *> *offers) {
    MILO_ASSERT(!mPurchaser, 0x2e7);
    // MILO_ASSERT(profile, 0x2ea);
    FOREACH (it, *offers) {
        MILO_ASSERT((*it)->IsAvailable(), 0x2ef);
    }
}

void StorePanel::PopulateOffers(DataArray *arr, bool b) {
    if (mLoadOk) {
        DeleteAll(unk44);
        if (b == 0) {
            DeleteAll(unk38);
        }

        std::vector<StoreOffer *> *offerVec = &unk44;
        if (b == 0) {
            offerVec = &unk38;
        }

        if (arr != NULL) {
            arr->AddRef();
            s16 count = arr->Size();
            s32 i = 1;

            if (count > 1) {
                do {
                    DataArray *child_arr = arr->Array(i);
                    StoreOffer *offer = new StoreOffer(child_arr, 0);

                    if ((unk52 == 0) && (offer->IsTest())) {
                        if (offer != NULL) {
                            delete offer;
                        }
                    } else if (offer->ValidTitle()) {
                        offerVec->push_back(offer);
                    } else {
                        if (offer != NULL) {
                            delete offer;
                        }
                    }

                    i++;
                } while (i < count);
            }

            ValidateOffers(*offerVec);
            arr->Release();
        }
    }
}

void StorePanel::EnumerateOffers(bool) {}

void StorePanel::FinishEnum(std::list<EnumProduct> const &enumList, bool arg) {
    unk68 = -1;

    if (arg) {
        StoreError err = UpdateOffers(enumList, arg);

        if (err == 0 || err == 1) {
            if (!unk44.empty()) {
                err = UpdateOffers(enumList, true);
            }
        }

        if (err > 0) {
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

        static bool msg_created = false;
        if (!msg_created) {
            msg_created = true;
            static Symbol sym("enum_finished");
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
    StoreError result;
    std::vector<StoreOffer *> *offers;

    // Select which vector to use based on arg
    if (arg == 0) {
        offers = &unk38;
    } else {
        offers = &unk44;
    }

    // Check if unk52 is non-zero
    if (unk52 != 0) {
        result = kStoreErrorSuccess;
    } else if (offers->size() == 0) {
        // Empty list - format error message
        FormatString fmt("This metadata contained no offer");
        TheDebug.Notify(fmt.Str());
        result = (StoreError)6;
    } else {
        result = (StoreError)1;
    }

    // Iterate through offers
    std::vector<StoreOffer *>::iterator it;
    for (it = offers->begin(); it != offers->end(); ++it) {
        StoreOffer *offer = *it;
        if (offer->Exists()) {
            std::list<EnumProduct>::const_iterator enumIt;
            enumIt = enumList.begin();
            bool match = false;
            while (enumIt != enumList.end()) {
                if (*(u64 *)&enumIt->unk8 == offer->songID) {
                    match = true;
                    break;
                }
                ++enumIt;
            }

            if (match) {
                result = kStoreErrorSuccess;
                // Call virtual function at offset 0x70
                void (*func)(void *, void *, void *) = (void (*)(void *, void *, void *))*(void **)((u32)this + 0x70);
                func(this, offer, (void *)((u32)offer + 0x38));
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
    sp->isPurchased = (ep->unk10 != 0);
    sp->cost = ep->unk14;
    sp->isAvailable = true;
}

void StorePanel::StartReEnum() {
    if (unk98 != 0) {
        ThePlatformMgr.QueueEnumJob(unk98);
        unk98 = nullptr;
    }
}

DataNode StorePanel::OnMsg(SigninChangedMsg const &msg) {
    int changedMask;
    int mask = msg.GetMask();
    if (mask != 0) {
        changedMask = msg.GetChangedMask();
        if ((1 << changedMask & mask) == 0) {
            return DataNode(0);
        }
    }
    if (mLoadOk) {
        mLoadOk = false;
    }
    return DataNode(0);
}

DataNode StorePanel::OnMsg(ProfileSwappedMsg const &) { return 0; }

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

BEGIN_HANDLERS(StorePanel)
    HANDLE_SUPERCLASS(UIPanel)
END_HANDLERS

StoreEnumJob::StoreEnumJob(StorePanel *panel, int i, std::vector<UINT64> *vec) {
    mEnumeration = new XboxEnumeration(i, vec);
    mStorePanel = panel;
}

StoreEnumJob::~StoreEnumJob() {
    delete mEnumeration;
}

bool StoreEnumJob::IsFinished() {
    if (mEnumeration->IsEnumerating()) {
        mEnumeration->Poll();
    }
    return mEnumeration->IsEnumerating() == false;
}