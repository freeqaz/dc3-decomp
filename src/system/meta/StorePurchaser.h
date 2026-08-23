#pragma once

#include "obj\Data.h"
#include "obj/Object.h"
#include "stl\_vector.h"
#include "ui\UI.h"
#include "utl\Symbol.h"

enum PurchaseState { // just know the val of kSuccess
    purchasestate0 = 0,
    purchasestate1 = 1,
    kSuccess = 2,
    purchasestate3 = 3,
};

class StorePurchaser {
public:
    virtual ~StorePurchaser() {}
    virtual void Initiate() = 0;
    virtual bool IsPurchasing() const = 0;
    virtual bool IsSuccess() const = 0;
    virtual bool PurchaseMade() const = 0;
    /** ham_xbox_r.map puts ?NeedsEnum@StorePurchaser@@UBA_NXZ at 0x82AEAE70
     * (`li r3,0; blr`), and slot 5 of ??_7StorePurchaser@@6B@ names that same
     * address -- the base default is FALSE.  Both Xbox overrides below sit at
     * 0x82E2AB00 (`li r3,1; blr`) instead.  This family was decompiled
     * inverted; see the note on XboxPurchaser::NeedsEnum. */
    virtual bool NeedsEnum() const { return false; }
    virtual void Poll() = 0;

    StorePurchaser(Symbol s, unsigned int i) : mSource(s), mUserIndex(i) {}

    Symbol mSource;
    int mUserIndex;
};

class XboxPurchaser : public StorePurchaser, public Hmx::Object {
public:
    // Hmx::Object
    virtual ~XboxPurchaser();
    virtual DataNode Handle(DataArray *, bool);

    // StorePurchaser
    virtual void Initiate();
    virtual bool IsPurchasing() const;
    virtual bool IsSuccess() const;
    virtual bool PurchaseMade() const;
    /** TRUE, not false.  ??_7XboxPurchaser@@6BStorePurchaser@@@ slot 5 resolves
     * to ?IsLocal@LocalUser@@UBA_NXZ == 0x82E2AB00 == `li r3,1; blr`, and the
     * map lists ?NeedsEnum@XboxPurchaser@@UBA_NXZ at that same address.  An
     * Xbox marketplace purchase DOES need its offers enumerated first; the
     * base default (false) is the one that skips the step.  With this
     * backwards, StorePanel::Poll and OptionsPanel took the no-enum path for
     * every Xbox purchase. */
    virtual bool NeedsEnum() const { return true; }
    virtual void Poll() {}

    XboxPurchaser(
        int,
        unsigned long long,
        unsigned long long,
        unsigned long long,
        Symbol,
        unsigned int
    );

    PurchaseState mState; // 0x38
    u32 unk3c;
    unsigned long long mOfferID;
    int mUserIndex;

private:
    DataNode OnMsg(UIChangedMsg const &);
};

class XboxMultipleItemsPurchaser : public StorePurchaser, Hmx::Object {
public:
    // Hmx::Object
    virtual ~XboxMultipleItemsPurchaser();
    virtual DataNode Handle(DataArray *, bool);

    // StorePurchaser
    virtual void Initiate();
    virtual bool IsPurchasing() const;
    virtual bool IsSuccess() const;
    virtual bool PurchaseMade() const;
    /** TRUE -- same evidence as XboxPurchaser::NeedsEnum above:
     * ??_7XboxMultipleItemsPurchaser@@6BStorePurchaser@@@ slot 5 and the map
     * both land on 0x82E2AB00 (`li r3,1; blr`). */
    virtual bool NeedsEnum() const { return true; }
    virtual void Poll() {}

    XboxMultipleItemsPurchaser(
        int, std::vector<unsigned long long> &, Symbol, unsigned int
    );

    PurchaseState mState;                  // 0x38 - Current purchase state
    std::vector<unsigned long long> mOfferIDs; // Offer IDs to purchase
    int mUserIndex;                             // User index
    DWORD mSelectedCount;                       // Count of items selected by user

private:
    // The overlapped block for the async marketplace call is a class static in the
    // target (?sOverlapped@XboxMultipleItemsPurchaser@@0U_XOVERLAPPED@@A), not a
    // function-local static of Initiate().
    static XOVERLAPPED sOverlapped;

    DataNode OnMsg(UIChangedMsg const &);
};
