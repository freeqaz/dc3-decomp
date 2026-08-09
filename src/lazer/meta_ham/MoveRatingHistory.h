#pragma once
#include "hamobj\ScoreUtl.h"
#include "meta\FixedSizeSaveable.h"

// size 0x24
class MoveRatingHistory : public FixedSizeSaveable {
public:
    class Key {
    public:
        bool operator<(const Key &k) const { return mMoveSymbol < k.mMoveSymbol; }

        Symbol mMoveSymbol;
    };
    class RatingHistory {
    public:
        MoveRating mRatingArray[4];
    };
    MoveRatingHistory() : mHasModifiedHistory(0) { mSaveSizeMethod = SaveSize; }
    virtual ~MoveRatingHistory() {}
    virtual void SaveFixed(FixedSizeSaveableStream &) const;
    virtual void LoadFixed(FixedSizeSaveableStream &, int);

    void Clear();
    void AddHistory(Symbol, int);
    int GetRating(Symbol, int);
    bool HasRatingHistory(const Key &key) const { return mMoveRatingMap.count(key) > 0; }

    static int SaveSize(int);

    bool HasModifiedHistory() const { return mHasModifiedHistory; }

private:
    std::map<Key, RatingHistory> mMoveRatingMap;
    bool mHasModifiedHistory;
};
