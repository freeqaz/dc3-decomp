#pragma once

#include "meta_ham/HamSongMetadata.h"

class SongRecord {
public:
    SongRecord(const HamSongMetadata *meta);
    virtual ~SongRecord() {}

    const Symbol &ShortName() const { return mShortName; }
    int RankTier() const;
    const HamSongMetadata *Metadata() const { return mMetadata; }

protected:
    Symbol mShortName;
    int mRankTier;
    const HamSongMetadata *mMetadata;
};
