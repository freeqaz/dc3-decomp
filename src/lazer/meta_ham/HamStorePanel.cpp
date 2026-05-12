#include "meta_ham/HamStorePanel.h"
#include "HamStoreOffer.h"
#include "macros.h"
#include "meta/SongMgr.h"
#include "meta/StoreEnumeration.h"
#include "meta/StoreOffer.h"
#include "meta/StorePanel.h"
#include "meta/StorePurchaser.h"
#include "meta_ham/HamProfile.h"
#include "meta_ham/HamStoreFilterProvider.h"
#include "meta_ham/HamStoreProvider.h"
#include "meta_ham/HamUI.h"
#include "meta_ham/ProfileMgr.h"
#include "meta_ham/UIEventMgr.h"
#include "net_ham/HamStoreCartJobs.h"
#include "net_ham/RCJobDingo.h"
#include "net_ham/RockCentral.h"
#include "obj/Data.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/ContentMgr.h"
#include "os/Debug.h"
#include "os/Platform.h"
#include "os/PlatformMgr.h"
#include "os/System.h"
#include "os/User.h"
#include "stl/_vector.h"
#include "ui/UI.h"
#include "utl/JobMgr.h"
#include <algorithm>
#include "utl/Loader.h"
#include "utl/MakeString.h"
#include "utl/NetCacheMgr.h"
#include "utl/NetLoader.h"
#include "utl/Std.h"
#include "utl/Symbol.h"

static const char *sIndexString = "s_store_index_%s_%s.dtz";

HamStorePanel::HamStorePanel()
    : unka0(), mMetadata(), mOfferProvider(), mMotd(), mAllowCancel(false), mLockData(),
      unk154(false), mCartEnabled(true), mCartLocked(false), mCartDataLoaded(false),
      mRemovingFromCart(false), mAddingToCart(false), unk184(-1), mXboxPurchaser() {
    for (int i = 0; i < 7; i++) {
        mJobs[i] = 0;
    }
    DataArray *specialOfferArray =
        SystemConfig("store")->FindArray("special_offers", false);
    if (specialOfferArray) {
        int numOffers = specialOfferArray->Size() - 1;
        HamSpecialOffer tempOffer;
        mSpecialOffers.resize(numOffers, tempOffer);
        mSpecialOfferIDs.resize(numOffers, 0);

        for (int i = 0; i < numOffers; i++) {
            DataArray *offerArray = specialOfferArray->Array(i + 1);
            HamSpecialOffer &offer = mSpecialOffers[i];
            offer.mName = offerArray->Sym(0);
            offer.mOwned = false;
            offer.mOfferID = StorePurchaseable::OfferStringToID(offerArray->Str(1));
            mSpecialOfferIDs[i] = offer.mOfferID;
            offer.mCategory = offerArray->ForceSym(2);
        }
    }

    TheContentMgr.RegisterCallback(this, false);
}

HamStorePanel::~HamStorePanel() { TheContentMgr.UnregisterCallback(this, false); }

BEGIN_PROPSYNCS(HamStorePanel)
    SYNC_SUPERCLASS(StorePanel)
END_PROPSYNCS

void HamStorePanel::Load() {
    StorePanel::Load();
    MILO_ASSERT(!mOfferProvider, 0xd3);
    mOfferProvider = new HamStoreProvider(&mOffers, &mFilters, &mCartRows);
    mAllowCancel = false;
    mCancelTimer.Restart();
    RefreshSpecialOfferStatus();
}

bool HamStorePanel::Exiting() const {
    if (mXboxPurchaser != 0)
        return true;
    return StorePanel::Exiting();
}

bool HamStorePanel::IsSongInLibrary(const int &id) const {
    return TheSongMgr.HasSong(id);
}

Profile *HamStorePanel::StoreProfile() const {
    return TheProfileMgr.GetActiveProfile(true);
}

StoreOffer *HamStorePanel::MakeNewOffer(DataArray *d) {
    return new HamStoreOffer(d, &TheSongMgr);
}

void HamStorePanel::DisableCart() {
    if (mCartDataLoaded) {
        MILO_FAIL("Can\'t disable the cart after it is loaded");
    }
    mCartEnabled = false;
    mLockData = 0;
}

void HamStorePanel::RemoveDLCFromCart(int id) {
    mPendingRemoves.push_back(id);
    if (!mRemovingFromCart) {
        mRemovingFromCart = true;
        RemoveNextDLCFromCart();
    }
}

void HamStorePanel::AddDLCToCart(int id) {
    mPendingAdds.push_back(id);
    if (!mAddingToCart) {
        mAddingToCart = true;
        AddNextDLCToCart();
    }
}

void HamStorePanel::RemoveOfferFromCart(StoreOffer *offer) {
    mOfferProvider->UpdateOffersInCart(offer, 1);
    RemoveDLCFromCart(offer->GetSingleSongID());
}

bool HamStorePanel::IsCurrFilterCart(int id) {
    static Symbol store_filter_shopping_cart("store_filter_shopping_cart");
    return mFilters[id]->mFilterSym == store_filter_shopping_cart;
}

void HamStorePanel::GetCart() {
    HamProfile *profile = dynamic_cast<HamProfile *>(StoreProfile());
    MILO_ASSERT(profile, 0x23d);
    mJobs[3] = new GetCartJob(this, profile);
    TheRockCentral.ManageJob(mJobs[3]);
}

void HamStorePanel::LockCart() {
    HamProfile *profile = dynamic_cast<HamProfile *>(StoreProfile());
    MILO_ASSERT(profile, 0x246);
    mCartLocked = true;
    mLockData = 0;
    mJobs[4] = new LockCartJob(this, profile->GetOnlineID()->ToString());
    TheRockCentral.ManageJob(mJobs[4]);
}

void HamStorePanel::UnlockCart() {
    HamProfile *profile = dynamic_cast<HamProfile *>(StoreProfile());
    MILO_ASSERT(profile, 0x252);
    mCartLocked = false;
    mJobs[5] = new UnlockCartJob(this, profile->GetOnlineID()->ToString());
    TheRockCentral.ManageJob(mJobs[5]);
}

void HamStorePanel::RelockCart() {
    HamProfile *profile = dynamic_cast<HamProfile *>(StoreProfile());
    MILO_ASSERT(profile, 0x25d);
    mCartLocked = true;
    mJobs[6] = new LockCartJob(this, profile->GetOnlineID()->ToString());
    TheRockCentral.ManageJob(mJobs[6]);
    mRelockTimer.Restart();
}

void HamStorePanel::EmptyCart() {
    mOfferProvider->UpdateOffersInCart(nullptr, 2);
    HamProfile *profile = dynamic_cast<HamProfile *>(StoreProfile());
    MILO_ASSERT(profile, 0x26c);
    mJobs[2] = new EmptyCartJob(this, profile->GetOnlineID()->ToString());
    TheRockCentral.ManageJob(mJobs[2]);
}

void HamStorePanel::StoreUserProfileSwappedToUser(LocalUser *) {
    RefreshSpecialOfferStatus();
}

void HamStorePanel::ReadLockData() {
    ((LockCartJob *)mJobs[4])->GetLockData(mLockData);
    mJobs[4] = nullptr;
    mRelockTimer.Restart();
}

void HamStorePanel::ReadCartData() {
    GetCartJob *job = (GetCartJob *)mJobs[3];
    mCartRows.clear();
    job->GetRows(&mCartRows);
    mJobs[3] = nullptr;
    mCartDataLoaded = true;
}

StoreOffer *HamStorePanel::FindOffer(Symbol offerName) const {
    FOREACH (it, mOffers) {
        StoreOffer *offer = *it;
        Symbol s = offer->StoreOfferData()->Sym(0);
        if (s == offerName)
            return offer;
    }
    return nullptr;
}

void HamStorePanel::GetOfferIDsToEnumerate(
    std::vector<unsigned long long> &offerIDs, bool pending
) const {
    const std::vector<StoreOffer *> &offers = pending ? mPendingOffers : mOffers;
    for (unsigned int i = 0; i < offers.size(); i++) {
        StorePurchaseable *purchaseable = offers[i];
        if (purchaseable->Exists()) {
            offerIDs.push_back(purchaseable->SongID());
        }
    }
    std::sort(offerIDs.begin(), offerIDs.end());
    std::vector<unsigned long long>::iterator it =
        std::unique(offerIDs.begin(), offerIDs.end());
    offerIDs.resize(it - offerIDs.begin());
}

void HamStorePanel::SetFilterToCart() {
    static Symbol store_filter_shopping_cart("store_filter_shopping_cart");
    for (int i = mFilters.size() - 1; i >= 0; i--) {
        if (mFilters[i]->mFilterSym == store_filter_shopping_cart) {
            mOfferProvider->SetFilter(mFilters[i]);
            return;
        }
    }
}

int HamStorePanel::SetFilterToSongs() {
    static Symbol songs("songs");
    for (int i = mFilters.size() - 1; i >= 0; i--) {
        if (mFilters[i]->mFilterSym == songs) {
            mOfferProvider->SetFilter(mFilters[i]);
            return i;
        }
    }
    return -1;
}

void HamStorePanel::AddNextDLCToCart() {
    HamProfile *profile = dynamic_cast<HamProfile *>(StoreProfile());
    MILO_ASSERT(profile, 0x2da);
    mJobs[0] =
        new AddDLCToCartJob(this, profile->GetOnlineID()->ToString(), mPendingAdds.front());
    TheRockCentral.ManageJob(mJobs[0]);
}

void HamStorePanel::RemoveNextDLCFromCart() {
    HamProfile *profile = dynamic_cast<HamProfile *>(StoreProfile());
    MILO_ASSERT(profile, 0x2c0);
    mJobs[1] = new RemoveDLCFromCartJob(
        this, profile->GetOnlineID()->ToString(), mPendingRemoves.front()
    );
    TheRockCentral.ManageJob(mJobs[1]);
}

void HamStorePanel::AddOfferToCart(StoreOffer *offer) {
    mOfferProvider->UpdateOffersInCart(offer, 0);
    AddDLCToCart(offer->GetSingleSongID());
}

char const *HamStorePanel::GetIndexFile() const {
    Symbol platSym = PlatformSymbol(TheLoadMgr.GetPlatform());
    Symbol sysLang = SystemLanguage();
    return MakeString(sIndexString, platSym, sysLang);
}

void HamStorePanel::ExitStore(StoreError err) const {
    static Symbol store_load_failed("store_load_failed");
    if (TheUIEventMgr->CurrentEvent() != store_load_failed) {
        static Message msg("init", -1);
        msg[0] = err;
        TheUIEventMgr->TriggerEvent(store_load_failed, msg);
    }
}

void HamStorePanel::CreateCartUIs() {
    static Symbol store_filter_shopping_cart("store_filter_shopping_cart");
    static Symbol store_checkout("store_checkout");
    static Symbol type("type");
    static Symbol fake("fake");
    static Symbol name("name");
    static Symbol artist("artist");
    static Symbol album_name("album_name");
    static Symbol description("description");
    static Symbol art("art");
    static Symbol store_filter_song_import_offers("store_filter_song_import_offers");
    auto filtersBegin = mFilters.begin();
    mFilters.insert(filtersBegin, new HamStoreFilter(store_filter_shopping_cart));
    mFilters.push_back(new HamStoreFilter(store_filter_song_import_offers));

    DataArrayPtr ptr;
    ptr->Insert(ptr->Size(), store_checkout);
    ptr->Insert(ptr->Size(), DataArrayPtr(type, fake));
    ptr->Insert(ptr->Size(), DataArrayPtr(name, ""));
    ptr->Insert(ptr->Size(), DataArrayPtr(artist, ""));
    ptr->Insert(ptr->Size(), DataArrayPtr(album_name, ""));
    ptr->Insert(ptr->Size(), DataArrayPtr(description, ""));
    ptr->Insert(ptr->Size(), DataArrayPtr(art, "avatar_theboombox_nomip_xbox.dxt"));
    mOffers.push_back(MakeNewOffer(ptr));
}

bool HamStorePanel::IsSpecialOfferOwned(Symbol offer) const {
    FOREACH (it, mSpecialOffers) {
        if ((*it).mName == offer) {
            return (*it).mOwned;
        }
    }
    Symbol s = offer;
    MILO_NOTIFY("Unknown offer %s", s);
    return false;
}

void HamStorePanel::ResetCancelTimer() {
    mAllowCancel = false;
    mCancelTimer.Restart();
}

bool HamStorePanel::ContentTitleDiscovered(unsigned int ui, Symbol s) {
    static Symbol dc2_gond("dc2_gond");
    if (ui == 0x373307d2) {
        FOREACH (it, mSpecialOffers) {
            if (it->mName == dc2_gond) {
                it->mCategory = s;
                break;
            }
        }
        return false;
    }
    return true;
}

void HamStorePanel::ContentMounted(char const *c1, char const *c2) {
    FOREACH (it, mSpecialOffers) {
        if (it->mCategory == c1) {
            Symbol s = it->mName;
            MILO_LOG("Store: special offer %s on local drive.\n", s);
            it->mOwned = true;
            return;
        }
    }
}

bool HamStorePanel::ContentDiscovered(Symbol s) {
    FOREACH (it, mSpecialOffers) {
        if (it->mCategory == s) {
            return false;
        }
    }
    return true;
}

bool HamStorePanel::BuySpecialOffer(Symbol offer) {
    if (mXboxPurchaser) {
        MILO_FAIL("There is a purchase in progress.");
    }
    FOREACH (it, mSpecialOffers) {
        if ((*it).mName == offer) {
            Profile *profile = StoreProfile();
            if (profile) {
                mXboxPurchaser =
                    new XboxPurchaser(profile->GetPadNum(), it->mOfferID, 0, 0, mPurchaseSource, 0);
                mXboxPurchaser->Initiate();
            }
            return true;
        }
    }
    Symbol s = offer;
    MILO_NOTIFY("Unknown offer %s", s);
    return false;
}

void HamStorePanel::Poll() {
    StorePanel::Poll();
    if (!mAllowCancel && mCancelTimer.SplitMs() > 5000.0f) {
        mAllowCancel = true;
        if (TheHamUI.FocusPanel() == this) {
            TheHamUI.GetHelpBarPanel()->SyncToPanel(this);
        }
    }
    if (!mLoadOk) {
        return;
    }
    if (!TheNetCacheMgr->IsReady()) {
        return;
    }
    if (unk94 != 2) {
        return;
    }
    if (unka0) {
        unka0->PollLoading();
        if (unka0->IsLoaded()) {
            mMetadata = unka0->GetUnk4();
            MILO_ASSERT(mMetadata, 0xfe);
            mMetadata->AddRef();
            static Symbol motd("motd");
            mMetadata->FindData(motd, mMotd, false);
            DataArray *filterArray = mMetadata->FindArray("filters", true);
            DeleteAll(mFilters);
            for (int i = 1; i < filterArray->Size(); i++) {
                mFilters.push_back(new HamStoreFilter(filterArray->Array(i)));
            }
            DataArray *offerArray = mMetadata->FindArray("offers", true);
            PopulateOffers(offerArray, false);
            CreateCartUIs();
            unk154 = true;
        } else {
            if (!unka0->HasFailed()) {
                goto exit;
            }
            MILO_NOTIFY("Request for %s failed.", GetIndexFile());
            ExitError((StoreError)3);
        }
        RELEASE(unka0);
    } else {
        if (mMetadata == 0) {
            String indexFile = GetIndexFile();
            unka0 = new DataNetLoader(indexFile);
        } else if (unk154 && (mCartDataLoaded || !mCartEnabled)) {
            unk154 = false;
            mNeedsReEnum = true;
        }
    }
exit:
    if (mCartLocked && mLockData != 0) {
        if (mRelockTimer.SplitMs() >= mLockData) {
            RelockCart();
        }
    }
    if (mXboxPurchaser) {
        mXboxPurchaser->Poll();
        if (!mXboxPurchaser->IsPurchasing()) {
            bool purchaseMade = false;
            bool needsEnum = false;
            if (mXboxPurchaser->IsSuccess()) {
                purchaseMade = mXboxPurchaser->PurchaseMade();
                needsEnum = mXboxPurchaser->NeedsEnum();
                if (purchaseMade && needsEnum) {
                    RefreshSpecialOfferStatus();
                }
            }

            static Message special_finished("special_finished", 0, 0);
            special_finished[0] = purchaseMade;
            special_finished[1] = needsEnum;
            HandleType(special_finished);
            TheUI->Handle(special_finished, false);
            RELEASE(mXboxPurchaser);
        }
    }
}

void HamStorePanel::Unload() {
    RELEASE(unka0);
    RELEASE(mOfferProvider);
    if (mMetadata) {
        mMetadata->Release();
        mMetadata = nullptr;
    }
    DeleteAll(mFilters);
    mCancelTimer.Stop();
    mAllowCancel = false;
    mRelockTimer.Stop();
    mLockData = 0;
    mCartEnabled = true;
    unk154 = false;
    mCartDataLoaded = false;
    mRemovingFromCart = false;
    mAddingToCart = false;
    if (mCartLocked) {
        UnlockCart();
    }
    StorePanel::Unload();
}

void HamStorePanel::FinishSpecialOfferEnum(std::vector<bool> const &vec, bool b) {
    unk184 = -1;
    if (!b) {
        MILO_LOG("Store: failed to enum our special offers.\n");
    } else {
        for (int i = 0; i < mSpecialOffers.size(); i++) {
            if (!mSpecialOffers[i].mOwned) {
                mSpecialOffers[i].mOwned = vec[i];
            }

            if (mSpecialOffers[i].mOwned) {
                MILO_LOG("Store: special offer %s is owned\n", mSpecialOffers[i].mName);
            }
        }
    }
    static Message refresh_complete("refresh_complete", 0);
    refresh_complete[0] = b;
    TheUI->Handle(refresh_complete, false);
}

BEGIN_HANDLERS(HamStorePanel)
    HANDLE_EXPR(get_motd, mMotd)
    HANDLE_ACTION(set_filter, mOfferProvider->SetFilter(mFilters[_msg->Int(2)]))
    HANDLE_ACTION(
        set_filter_pack_singles, mOfferProvider->SetFilter(_msg->Obj<StoreOffer>(2))
    )
    HANDLE_EXPR(offer_provider, (Hmx::Object *)mOfferProvider)
    HANDLE_EXPR(filter_provider, (Hmx::Object *)mOfferProvider->GetFilterProvider())
    HANDLE_ACTION(reset_cancel_timer, ResetCancelTimer())
    HANDLE_EXPR(allow_cancel, mAllowCancel)
    HANDLE_EXPR(is_cart_enabled, mCartEnabled)
    HANDLE_ACTION(disable_cart, DisableCart())
    HANDLE_ACTION(get_cart, GetCart())
    HANDLE_ACTION(add_offer_to_cart, AddOfferToCart(_msg->Obj<StoreOffer>(2)))
    HANDLE_ACTION(remove_offer_from_cart, RemoveOfferFromCart(_msg->Obj<StoreOffer>(2)))
    HANDLE_ACTION(cart_checkout, MultipleItemsCheckout(mOfferProvider->GetCartOffers()))
    HANDLE_ACTION(lock_cart, LockCart())
    HANDLE_ACTION(unlock_cart, UnlockCart())
    HANDLE_EXPR(is_curr_filter_cart, IsCurrFilterCart(_msg->Int(2)))
    HANDLE_EXPR(is_cart_empty, mOfferProvider->NumOffersInCart() == 0)
    HANDLE_EXPR(is_cart_full, mOfferProvider->NumOffersInCart() == 6)
    HANDLE_ACTION(empty_cart, EmptyCart())
    HANDLE_ACTION(set_filter_to_cart, SetFilterToCart())
    HANDLE_EXPR(set_filter_to_songs, SetFilterToSongs())
    HANDLE_ACTION(refresh_special_offers, RefreshSpecialOfferStatus())
    HANDLE_EXPR(check_owned, IsSpecialOfferOwned(_msg->ForceSym(2)))
    HANDLE_EXPR(buy_special, BuySpecialOffer(_msg->ForceSym(2)))
    HANDLE_MESSAGE(RCJobCompleteMsg)
    HANDLE_ACTION(buy_dc1_import, BuySpecialOffer("dc1_import"))
    HANDLE_ACTION(buy_dc2_import, BuySpecialOffer("dc2_import"))
    HANDLE_ACTION(buy_dc2_pop, BuySpecialOffer("dc2_pop"))
    HANDLE_ACTION(buy_dc2_gond, BuySpecialOffer("dc2_gond"))
    HANDLE_SUPERCLASS(StorePanel)
END_HANDLERS

StoreError HamStorePanel::UpdateOffers(std::list<EnumProduct> const &list, bool b) {
    FOREACH (it, mSpecialOffers) {
        if (!it->mOwned) {
            bool check = false;
            FOREACH (listIt, list) {
                if (listIt->mOfferID == it->mOfferID) {
                    check = true;
                    it->mOwned = (listIt->mPurchased != 0);
                    break;
                }
            }

            if (check) {
                MILO_LOG(
                    "Store: special offer %s is %s\n",
                    it->mName.Str(),
                    (it->mOwned) ? "owned" : "not owned"
                );
            }
        }
    }
    return StorePanel::UpdateOffers(list, b);
}

void HamStorePanel::RefreshSpecialOfferStatus() {
    Profile *profile = StoreProfile();
    if (profile && unk184 == -1) {
        SpecialOfferEnumJob *job = new SpecialOfferEnumJob(this, profile->GetPadNum(), mSpecialOfferIDs);
        ThePlatformMgr.QueueEnumJob(job);
        unk184 = job->ID();
    }
}
DataNode HamStorePanel::OnMsg(const RCJobCompleteMsg &msg) {
    if (msg.Job() == mJobs[4]) {
        if (msg.Success()) {
            ReadLockData();
            GetCart();
        } else {
            MILO_LOG("[HamStorePanel::OnMsg] Cart failed to lock, disabling.\n");
            DisableCart();
        }
    } else if (msg.Job() == mJobs[5]) {
        if (msg.Success()) {
            MILO_LOG("[HamStorePanel::OnMsg] Cart unlocked successfully.\n");
        } else {
            MILO_LOG("[HamStorePanel::OnMsg] Cart failed to unlock.\n");
        }
    } else if (msg.Job() == mJobs[6]) {
        if (!msg.Success()) {
            MILO_LOG("[HamStorePanel::OnMsg] Cart failed to re-lock.\n");
        }
    } else if (msg.Job() == mJobs[3]) {
        if (msg.Success()) {
            ReadCartData();
        } else {
            MILO_LOG("[HamStorePanel::OnMsg] Failed to get cart, disabling.\n");
            DisableCart();
        }
    } else if (msg.Job() == mJobs[1]) {
        if (!msg.Success()) {
            MILO_LOG("[HamStorePanel::OnMsg] Cart failed to remove song.\n");
            ExitError(kStoreErrorCacheRemoved);
        } else {
            mPendingRemoves.pop_front();
            if (!mPendingRemoves.empty()) {
                RemoveNextDLCFromCart();
            } else {
                mJobs[1] = nullptr;
                mRemovingFromCart = false;
            }
        }
    } else if (msg.Job() == mJobs[0]) {
        if (!msg.Success()) {
            MILO_LOG("[HamStorePanel::OnMsg] Cart failed to add song.\n");
            ExitError(kStoreErrorCacheRemoved);
        } else {
            mPendingAdds.pop_front();
            if (!mPendingAdds.empty()) {
                AddNextDLCToCart();
            } else {
                mJobs[0] = nullptr;
                mAddingToCart = false;
            }
        }
    } else if (msg.Job() == mJobs[2]) {
        if (!msg.Success()) {
            MILO_LOG("[HamStorePanel::OnMsg] Cart failed to clear.\n");
        } else {
            MILO_LOG("[HamStorePanel::OnMsg] Cart emptied successfully.\n");
        }
    }
    return 1;
}
SpecialOfferEnumJob::SpecialOfferEnumJob(
    HamStorePanel *panel, int sessionID, std::vector<unsigned long long> &offerIDs
)
    : MultipleItemsEnumJob(nullptr, sessionID, offerIDs), mPanel(panel) {}

SpecialOfferEnumJob::~SpecialOfferEnumJob() {}

void SpecialOfferEnumJob::OnCompletion(Hmx::Object *) {
    HamStorePanel *panel = mPanel;
    if (!panel)
        return;
    if (panel->unk184 != ID())
        return;
    panel->FinishSpecialOfferEnum(mPurchased, mStatus == 2);
}
