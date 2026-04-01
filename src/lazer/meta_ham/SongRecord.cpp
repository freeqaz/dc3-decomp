#include "SongRecord.h"
#include "meta_ham/HamSongMgr.h"

int SongRecord::RankTier() const { return mRankTier; }

SongRecord::SongRecord(const HamSongMetadata *meta) : mShortName(), mMetadata(meta) {
    mShortName = TheHamSongMgr.GetShortNameFromSongID(meta->ID(), 1);
    mRankTier = TheHamSongMgr.RankTier(meta->Rank());
}
