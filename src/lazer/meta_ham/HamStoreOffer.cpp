#include "meta_ham/HamStoreOffer.h"
#include "meta/SongMgr.h"
#include "meta/StoreOffer.h"
#include "obj/Data.h"
#include "utl/Symbol.h"

HamStoreOffer::HamStoreOffer(DataArray *d, SongMgr *s) : StoreOffer(d, s) {
    static Symbol preview("preview");
    if (HasData(preview)) {
        mPreviewPath = "previews/";
        Symbol p = preview;
        DataArray *previewArray = mStoreOfferData->FindArray(p);
        mPreviewPath += previewArray->Str(1);
    } else {
        mPreviewPath = gNullStr;
    }

    static Symbol art("art");
    mAlbumArtPath = "album_art/";
    Symbol a = art;
    DataArray *albumArtArray = mStoreOfferData->FindArray(a);
    mAlbumArtPath += albumArtArray->Str(1);
}

HamStoreOffer::~HamStoreOffer() {}

bool HamStoreOffer::Cmp(StoreOffer const &other, Symbol sortBy) const {
    static Symbol title("title");
    static Symbol artist("artist");
    static Symbol difficulty("difficulty");
    static Symbol release_date("release_date");
    if (sortBy == title) {
        return strcmp(OfferName(), other.OfferName()) < 0;
    } else if (sortBy == artist) {
        return strcmp(ArtistName(), other.ArtistName()) < 0;
    } else if (sortBy == difficulty) {
        const HamStoreOffer *hamOther = dynamic_cast<const HamStoreOffer *>(&other);
        if (hamOther) {
            return Difficulty() < hamOther->Difficulty();
        }
        return false;
    } else if (sortBy == release_date) {
        return false; // DateTime has no comparison operator
    }
    return false;
}

int HamStoreOffer::Difficulty() const {
    static Symbol difficulty("difficulty");
    Symbol s = difficulty;
    DataArray *diffArray = mStoreOfferData->FindArray(s);
    return diffArray->Int(1);
}

BEGIN_HANDLERS(HamStoreOffer)
    HANDLE_EXPR(difficulty, Difficulty())
    HANDLE_EXPR(art_path, mAlbumArtPath.c_str())
    HANDLE_EXPR(preview_path, mPreviewPath.c_str())
    HANDLE_SUPERCLASS(StoreOffer)
END_HANDLERS
