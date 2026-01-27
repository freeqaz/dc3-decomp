#include "utl/MeasureMap.h"
#include "os/Debug.h"
#include <algorithm>

bool MeasureMap::CompareTick(int tick, const TimeSigChange &sig) {
    return tick < sig.Tick();
}

bool MeasureMap::CompareMeasure(int measure, const TimeSigChange &sig) {
    return measure < sig.Measure();
}

int MeasureMap::MeasureBeatTickToTick(int measure, int beat, int tick) const {
    const TimeSigChange *change = std::upper_bound(
        mTimeSigChanges.begin(), mTimeSigChanges.end(), measure, CompareMeasure
    );
    if (change != mTimeSigChanges.begin())
        change--;
    return ((measure - change->Measure()) * change->Num() * 1920) / change->Denom()
        + change->Tick() + beat * 480 + tick;
}

void MeasureMap::TickToMeasureBeatTick(
    int tick, int &oMeasure, int &oBeat, int &oTick, int &oBeatsPerMeasure
) const {
    const TimeSigChange *change = std::upper_bound(
        mTimeSigChanges.begin(), mTimeSigChanges.end(), tick, CompareTick
    );
    if (change != mTimeSigChanges.begin())
        change--;
    int numerator = change->Num() * 1920;
    int denom = change->Denom();
    int div = numerator / denom;
    int beatsPerMeasure = div / 480;
    int tickDiff = tick - change->Tick();
    oMeasure = change->Measure() + tickDiff / div;
    int mod = tickDiff % div;
    oBeat = mod / 480;
    oTick = mod % 480;
    oBeatsPerMeasure = beatsPerMeasure;
}

void MeasureMap::TickToMeasureBeatTick(int tick, int &oMeasure, int &oBeat, int &oTick)
    const {
    int bpm;
    TickToMeasureBeatTick(tick, oMeasure, oBeat, oTick, bpm);
}

MeasureMap::MeasureMap() : mTimeSigChanges() {
    mTimeSigChanges.push_back(TimeSigChange());
}

bool MeasureMap::AddTimeSignature(int measure, int num, int denom, bool fail) {
    if (measure == 0) {
        if (mTimeSigChanges.size() != 1) {
            if (fail)
                MILO_FAIL("Multiple time signatures at start of song");
            else
                return false;
        }
        mTimeSigChanges.pop_back();
        mTimeSigChanges.push_back(TimeSigChange(0, num, denom, 0));
    } else {
        TimeSigChange &sig = mTimeSigChanges.back();
        if (measure - sig.Measure() <= 0) {
            if (fail)
                MILO_FAIL("Multiple time signatures at measure %d", measure);
            else
                return false;
        }
        mTimeSigChanges.push_back(TimeSigChange(
            measure,
            num,
            denom,
            sig.Tick() + (sig.Num() * (measure - sig.Measure()) * 1920) / sig.Denom()
        ));
    }
    return true;
}
