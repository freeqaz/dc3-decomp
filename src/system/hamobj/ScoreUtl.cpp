#include "hamobj/ScoreUtl.h"
#include "os/Debug.h"

std::vector<Symbol> sRatingStates;
std::vector<float> sDefaultRatingThresholds;

MoveRating DetectFracToMoveRating(float detect_frac, const std::vector<float> *ratings) {
    if (!ratings)
        ratings = &sDefaultRatingThresholds;
    MILO_ASSERT(detect_frac >= 0 && detect_frac <= 1.0f, 0x22);
    for (int i = 0; i < ratings->size(); i++) {
        if (detect_frac >= (*ratings)[i])
            return (MoveRating)i;
    }
    return kNumMoveRatings;
}

void RatingStateThreshold(
    int index, Symbol &ratingState, float &thresh, const std::vector<float> *thresholds
) {
    if (!thresholds)
        thresholds = &sDefaultRatingThresholds;
    MILO_ASSERT((0) <= (index) && (index) < (sRatingStates.size()), 0x77);
    MILO_ASSERT((0) <= (index) && (index) < (thresholds->size()), 0x78);
    ratingState = sRatingStates[index];
    thresh = (*thresholds)[index];
}

int RatingStateToIndex(Symbol s) {
    for (int i = 0; i < sRatingStates.size(); i++) {
        if (s == sRatingStates[i]) {
            return i;
        }
    }
    MILO_NOTIFY("Could not find rating (%s)", s);
    return 0;
}

float RatingToRatingFrac(Symbol rating) {
    unsigned int numRatings = sRatingStates.size();
    for (unsigned int i = 0; i < numRatings; i++) {
        if (rating == sRatingStates[i]) {
            return (float)(int)(numRatings - 1 - i) / (float)(int)(numRatings - 1);
        }
    }
    MILO_NOTIFY("Could not find rating (%s)", rating);
    return 0.0f;
}

Symbol RatingState(int index) {
    MILO_ASSERT((0) <= (index) && (index) < (sRatingStates.size()), 0xA7);
    return sRatingStates[index];
}
