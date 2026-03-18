#pragma once
#include "hamobj/Difficulty.h"
#include "hamobj/HamLabel.h"
#include "hamobj/StarsDisplay.h"
#include "obj/Data.h"
#include "obj/Object.h"

class HamStarsDisplay : public StarsDisplay {
public:
    enum StarDisplayMode {
        kStarDisplay_0,
        kStarDisplay_1,
        kStarDisplay_2,
        kStarDisplay_3,
        kStarDisplay_4
    };

    // registers as StarsDisplay — transparent factory replacement
    OBJ_CLASSNAME(StarsDisplay)
    OBJ_SET_TYPE(StarsDisplay)
    virtual DataNode Handle(DataArray *, bool);

    void SetSongChallenge(Difficulty);
    void SetSong(int);
    void SetSongCampaign(int);
    void SetSongWithDifficulty(int, Difficulty, bool);

    NEW_OBJ(HamStarsDisplay)

private:
    void SetSongImpl(int, Difficulty, StarDisplayMode);
};
