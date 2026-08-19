#pragma once
#include "AccomplishmentSongConditional.h"
#include "HamProfile.h"
#include "meta_ham\Accomplishment.h"
#include "obj\Data.h"
#include "stl\_vector.h"
#include "utl\Symbol.h"

class AccomplishmentSongListConditional : public AccomplishmentSongConditional {
public:
    AccomplishmentSongListConditional(DataArray *, int);
    virtual ~AccomplishmentSongListConditional();
    virtual AccomplishmentType GetType() const { return kAccomplishmentTypeSongListConditional; }
    virtual bool IsFulfilled(HamProfile *) const;
    virtual bool IsRelevantForSong(Symbol) const;
    virtual bool InqIncrementalSymbols(HamProfile *, std::vector<Symbol> &) const;
    virtual bool HasSpecificSongsToLaunch() const { return true; }

protected:
    virtual int GetNumCompletedSongs(HamProfile *) const;
    virtual int GetTotalNumSongs() const;

private:
    void Configure(DataArray *);

    std::vector<Symbol> mSongs; // 0x70
    int mSongCount; // 0x7c
};
