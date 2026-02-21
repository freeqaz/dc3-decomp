#include "meta_ham/MoveRatingHistory.h"
#include "meta/FixedSizeSaveable.h"
#include "meta/FixedSizeSaveableStream.h"

void MoveRatingHistory::SaveFixed(FixedSizeSaveableStream &fs) const {
    int size = mMoveRatingMap.size();
    static int sMaxSize = 0x4000;
    if (size > 0x4000) {
        MILO_NOTIFY(
            "The move awards history size is greater than the maximum supplied! size=%i max=%i",
            size,
            sMaxSize
        );
        size = 0x4000;
    }
    fs.Tell();
    fs << size;
    FOREACH (it, mMoveRatingMap) {
        FixedSizeSaveable::SaveSymbolID(fs, it->first.mMoveSymbol);
        for (int i = 0; i < 4; i++) {
            fs << it->second.mRatingArray[i];
        }
    }
    if (size < 0x4000) {
        FixedSizeSaveable::PadStream(fs, (0x4000 - size) * 20);
    }
    const_cast<MoveRatingHistory *>(this)->mHasModifiedHistory = false;
}

void MoveRatingHistory::LoadFixed(FixedSizeSaveableStream &fs, int i2) {
    if (mMoveRatingMap.size() > 0) {
        MILO_NOTIFY("Move award history map is not empty on load!");
        mMoveRatingMap.clear();
    }
    int size;
    fs >> size;
    for (int i = 0; i < size; i++) {
        Key key;
        FixedSizeSaveable::LoadSymbolFromID(fs, key.mMoveSymbol);
        for (int j = 0; j < 4; j++) {
            fs >> (int &)mMoveRatingMap[key].mRatingArray[j];
        }
    }
    if (size < 0x4000) {
        FixedSizeSaveable::DepadStream(fs, (0x4000 - size) * 20);
    }
}

void MoveRatingHistory::Clear() {
    mMoveRatingMap.clear();
    mHasModifiedHistory = false;
}

int MoveRatingHistory::GetRating(Symbol s1, int i2) {
    Key key;
    key.mMoveSymbol = s1;
    if (HasRatingHistory(key)) {
        return mMoveRatingMap[key].mRatingArray[i2];
    } else {
        return -1;
    }
}

int MoveRatingHistory::SaveSize(int) { return 0x50004; }

void MoveRatingHistory::AddHistory(Symbol s1, int i2) {
    Key key;
    key.mMoveSymbol = s1;
    RatingHistory &history = mMoveRatingMap[key];
    MoveRating old = history.mRatingArray[0];
    history.mRatingArray[1] = old;
    history.mRatingArray[2] = old;
    history.mRatingArray[3] = old;
    history.mRatingArray[0] = (MoveRating)i2;
    mHasModifiedHistory = true;
}
