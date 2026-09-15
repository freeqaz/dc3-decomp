#include "midi\Midi.h"
#include "os\Debug.h"
#include "utl\MultiTempoTempoMap.h"
#include "utl/MeasureMap.h"
#include "midi/MidiConstants.h"
#include "utl\TempoMap.h"
#include "midi\MidiVarLen.h"
#include "utl\MBT.h"
#include <algorithm>

const MidiChunkID MidiChunkID::kMThd("MThd");
const MidiChunkID MidiChunkID::kMTrk("MTrk");
bool MidiReader::sVerify = false;

// w8-e 2026-09-15: ??3FileStream@@SAXPAX@Z (24 B, 0%) -- FileStream's class
// `operator delete` -- is a real same-TU gap.  ham_xbox_r.map contributes it
// from `midi:MidiReader.obj` at 8254e100, i.e. the image instantiates
// FileStream's OBJ_MEM_OVERLOAD deallocator in THIS translation unit, which only
// happens if this file `delete`s a FileStream (or destroys one held by value
// through a path that emits the deleting dtor).  Our MidiReader.cpp does not
// mention FileStream at all -- no include, no use -- so the COMDAT is never
// emitted and the row reads 0.0%.  The lever is finding the FileStream the
// reader really owns (most likely a `new FileStream` opened for a .mid and
// deleted on teardown); it is NOT a missing body in utl/File.

namespace {
    int MidiRank(unsigned char status) {
        switch (status & 0xF0) {
        case kNoteOff:
            return 1;
        case kController:
            return 2;
        case kProgramChange:
            return 3;
        case kChannelPressure:
            return 4;
        case kPitchModulation:
            return 5;
        case kAfterTouch:
            return 6;
        case kNoteOn:
            return 7;
        default:
            return 8;
        }
    }

    bool DefaultMidiLess(const MidiReader::Midi &m1, const MidiReader::Midi &m2) {
        return MidiRank(m1.mStat) < MidiRank(m2.mStat);
    }
}

void MidiReader::Init() {
    mOwnMaps = true;
    mTempoMap = new MultiTempoTempoMap();
    mMeasureMap = new MeasureMap();
    mRcvr.SetMidiReader(this);
}

const char *MidiReader::GetFilename() const { return mStreamName.c_str(); }

bool MidiReader::ClaimMaps(MeasureMap *&mmap, TempoMap *&tmap) {
    if (mOwnMaps) {
        mmap = mMeasureMap;
        tmap = mTempoMap;
        mOwnMaps = false;
        return true;
    } else
        return false;
}

void MidiReader::ReadTrackHeader(BinStream &bs) {
    MILO_ASSERT(mState == kNewTrack, 0x180);
    MidiChunkHeader header(bs);
    if (header.mID != MidiChunkID::kMTrk) {
        MILO_NOTIFY(
            "%s: MIDI track header for track %d is corrupt",
            mStreamName.c_str(),
            mCurTrackIndex
        );
        mFail = true;
    } else {
        mTrackEndPos = bs.Tell() + header.Length();
        mCurTrackIndex++;
        mPrevStatus = 0;
        mCurTick = 0;
        mMidiListTick = -1;
        mState = kInTrack;
        mRcvr.OnNewTrack(mCurTrackIndex - 1);
    }
}

void MidiReader::SkipCurrentTrack() {
    if (mState == kInTrack) {
        if (mCurTrackIndex == mNumTracks) {
            mState = kEnd;
            mRcvr.OnEndOfTrack();
            mRcvr.OnAllTracksRead();
        } else {
            mState = kNewTrack;
            mStream->Seek(mTrackEndPos, BinStream::kSeekBegin);
            mRcvr.OnEndOfTrack();
        }
    }
}

MidiReader::MidiReader(BinStream &bs, MidiReceiver &rec, const char *name)
    : mStream(&bs), mStreamCreatedHere(0), mStreamName(name), mRcvr(rec), mState(kStart),
      mNumTracks(0), mTicksPerQuarter(0), mDesiredTPQ(480), mCurTrackIndex(0),
      mCurTick(0), mPrevStatus(0), mCurTrackName(), mMidiListTick(0),
      mLessFunc(DefaultMidiLess), mFail(0) {
    MILO_ASSERT(!mStream->LittleEndian(), 0xAA);
    Init();
}

MidiReader::~MidiReader() {
    if (mStreamCreatedHere)
        delete mStream;
    if (mOwnMaps) {
        delete mTempoMap;
        delete mMeasureMap;
    }
}

void MidiReader::QueueChannelMsg(
    int tick, unsigned char status, unsigned char data1, unsigned char data2
) {
    if (!mLessFunc) {
        mRcvr.OnMidiMessage(tick, status, data1, data2);
    } else {
        mMidiList.push_back(Midi(status, data1, data2));
    }
}

void MidiReader::ProcessMidiList() {
    std::sort(mMidiList.begin(), mMidiList.end(), mLessFunc);
    for (std::vector<Midi>::iterator it = mMidiList.begin(); it != mMidiList.end();
         ++it) {
        mRcvr.OnMidiMessage(mMidiListTick, it->mStat, it->mD1, it->mD2);
        if (mState != kInTrack)
            break;
    }
    mMidiList.clear();
}

void MidiReader::ReadMidiEvent(
    int tick, unsigned char status, unsigned char data1, BinStream &bs
) {
    unsigned char data2;
    bool queue = false;
    int statusType = status & 0xF0;
    switch (statusType) {
    case kNoteOn:
        bs >> data2;
        queue = true;
        if (data2 == 0)
            status = status & 0xF | kNoteOff;
        break;
    case kNoteOff:
        bs >> data2;
        queue = true;
        break;
    case kController:
        bs >> data2;
        queue = true;
        break;
    case kPitchModulation:
    case kAfterTouch:
        bs >> data2;
        break;
    case kProgramChange:
    case kChannelPressure:
        data2 = 0;
        break;
    default:
        MILO_NOTIFY(
            "%s (%s): Cannot parse event %i",
            mStreamName.c_str(),
            mCurTrackName.c_str(),
            status & 0xF0
        );
        break;
    }
    if (queue)
        QueueChannelMsg(tick, status, data1, data2);
}

float pow(float base, int exponent) {
    int exp = exponent;
    if (exponent < 0)
        exp = -exponent;
    float result = 1.0f;
    for (;;) {
        if (exp & 1)
            result *= base;
        exp = (unsigned)exp >> 1;
        if (!exp) break;
        base *= base;
    }
    if (exponent < 0)
        result = 1.0f / result;
    return result;
}

void MidiReader::ReadMetaEvent(int tick, unsigned char type, BinStream &bs) {
    MidiVarLenNumber num(bs);
    unsigned int numVal = num.Value();
    int oldtell = bs.Tell();

    switch (type) {
    case kTextEvent:
    case kCopyrightNotice:
    case kTrackname:
    case kLyricEvent: {
        char buf[0x100];
        if (numVal >= 0x100) {
            bs.Read(buf, 8);
            buf[8] = 0;
            MILO_NOTIFY(
                "%s (%s): Text event beginning with '%s' at %s exceeds maximum allowed length of %d characters",
                mStreamName.c_str(),
                mCurTrackName.c_str(),
                buf,
                TickFormat(0, *mMeasureMap),
                0xFFu
            );
        } else {
            bs.Read(buf, numVal);
            buf[numVal] = 0;
            if (type == 3) {
                if (tick != 0) {
                    MILO_NOTIFY(
                        "%s (%s): MIDI track name event must appear at %s; found track name '%s' at %s",
                        mStreamName.c_str(),
                        buf,
                        TickFormat(0, *mMeasureMap),
                        buf,
                        TickFormat(tick, *mMeasureMap)
                    );
                    mFail = true;
                    return;
                }
                String &str = mTrackNames[mCurTrackIndex - 1];
                if (str.empty())
                    str = buf;
                else if (str != buf) {
                    MILO_NOTIFY(
                        "%s (%s): Track contains multiple track name events (%s and %s)",
                        mStreamName.c_str(),
                        str.c_str(),
                        str.c_str(),
                        buf
                    );
                    mFail = true;
                    return;
                }
                mCurTrackName = buf;
            }
            mRcvr.OnText(tick, buf, type);
        }
        break;
    }
    case kTempoSetting: {
        unsigned char c, b, a;
        bs >> c >> b >> a;
        int product = a + c * 0x10000 + b * 0x100;
        if (product < 200000) {
            MILO_NOTIFY(
                "%s (%s): Tempo marker at %s (%f bpm) is too fast; maximum is 300 bpm",
                mStreamName.c_str(),
                mCurTrackName.c_str(),
                TickFormat(tick, *mMeasureMap),
                6e+07f / (float)product
            );
        }
        if (product > 1500000) {
            MILO_NOTIFY(
                "%s (%s): Tempo marker at %s (%f bpm) is too slow; minimum is 40 bpm",
                mStreamName.c_str(),
                mCurTrackName.c_str(),
                TickFormat(tick, *mMeasureMap),
                6e+07f / (float)product
            );
        }
        if (mTempoMap->AddTempoInfoPoint(tick, product)) {
            mRcvr.OnTempo(tick, product);
        } else {
            MILO_NOTIFY(
                "%s (%s): Tempo marker at %s (%.f bpm) conflicts with other tempo markers",
                mStreamName.c_str(),
                mCurTrackName.c_str(),
                TickFormat(tick, *mMeasureMap),
                6e+07f / (float)product
            );
        }
        break;
    }
    case kEndOfTrack: {
        ProcessMidiList();
        if (mCurTrackIndex == mNumTracks) {
            mState = kEnd;
            mRcvr.OnEndOfTrack();
            mRcvr.OnAllTracksRead();
        } else {
            mState = kNewTrack;
            if (mCurTrackIndex == 1 && mOwnMaps) {
                mOwnMaps = mRcvr.OnAcceptMaps(mTempoMap, mMeasureMap) == 0;
            }
            mRcvr.OnEndOfTrack();
        }
        break;
    }
    case kTimeSignature: {
        unsigned char ts_num, ts_den;
        // ts_m/ts_b/ts_t are declared at CASE scope, not inside the else branch
        // where they are used, and that is load-bearing: it is the whole 106-row
        // residual this function used to carry.  Declared in the inner scope they
        // are dead while `pow()` runs, so MSVC overlays the float->int conversion
        // temp (stfd/lwz for `int powed = pow(...)`) onto ts_b's 8-byte slot.  At
        // case scope they are live across the call, the conversion temp needs its
        // own word, and -- cascading through the allocator -- `a` from the
        // kTempoSetting case stops sharing a word with ts_den.  Two extra words
        // push `buf` from 0x80 to 0x90 and the frame from 0x1d0 to 0x1e0, which is
        // exactly the shipped layout.  Declaration ORDER is inert here (measured:
        // splitting the `c, b, a` and `ts_num, ts_den` declarations, and reversing
        // `c, b, a` to `a, b, c`, are both byte-identical); declaration SCOPE is not.
        int ts_m, ts_b, ts_t;
        bs >> ts_num >> ts_den;
        if (ts_den > 6) {
            MILO_NOTIFY(
                "%s (%s): Time signature at %s has invalid denominator (2^%d); max is 64 (2^6)",
                mStreamName.c_str(),
                mCurTrackName.c_str(),
                TickFormat(tick, *mMeasureMap),
                ts_den
            );
        } else {
            int powed = pow(2.0f, (int)ts_den);
            if (ts_num == 0) {
                MILO_NOTIFY(
                    "%s (%s): Time signature %d/%d at %s has invalid numerator (%d)",
                    mStreamName.c_str(),
                    mCurTrackName.c_str(),
                    ts_num,
                    powed,
                    TickFormat(tick, *mMeasureMap),
                    ts_num
                );
            }
            mMeasureMap->TickToMeasureBeatTick(tick, ts_m, ts_b, ts_t);
            if (mMeasureMap->AddTimeSignature(ts_m, ts_num, powed, true)) {
                mRcvr.OnTimeSig(tick, ts_num, powed);
            } else {
                MILO_NOTIFY(
                    "%s (%s): Time signature %d/%d at %s overlaps or conflicts with nearby time signatures",
                    mStreamName.c_str(),
                    mCurTrackName.c_str(),
                    ts_num,
                    powed,
                    TickFormat(tick, *mMeasureMap)
                );
            }
            bs.Seek(2, BinStream::kSeekCur);
        }
        break;
    }
    case kInstrumentName:
    case kMarkerText:
    case kCuePoint:
    case kChannelPrefix:
    case kMidiPort:
    case kSMPTEOffset:
    case kKeySignature:
        break;
    default:
        MILO_NOTIFY(
            "%s (%s): Cannot parse meta event %i",
            mStreamName.c_str(),
            mCurTrackName.c_str(),
            type
        );
        break;
    }
    if (mState == kInTrack)
        bs.Seek(oldtell + numVal, BinStream::kSeekBegin);
}

void MidiReader::ReadFileHeader(BinStream &bs) {
    MILO_ASSERT(mState == kStart, 0x146);
    MidiChunkHeader header(bs);

    if ((header.mID != MidiChunkID::kMThd) || header.Length() != 6U) {
        MILO_NOTIFY("%s: MIDI file header is corrupt", mStreamName.c_str());
    }
    short midiType;
    bs >> midiType;
    if (midiType != 1) {
        MILO_NOTIFY(
            "%s: Only type 1 MIDI files are supported; this file is type %d",
            mStreamName.c_str(),
            midiType
        );
    }
    bs >> mNumTracks;
    if (mNumTracks <= 0) {
        MILO_NOTIFY("%s: MIDI file has no tracks", mStreamName.c_str());
    } else {
        mTrackNames.resize(mNumTracks, "");
    }
    bs >> mTicksPerQuarter;
    if ((unsigned short)mTicksPerQuarter & 0x8000U) {
        MILO_NOTIFY(
            "%s: MIDI file uses SMPTE time division; this is not allowed",
            mStreamName.c_str()
        );
    }
    if (mTicksPerQuarter != 480) {
        MILO_NOTIFY(
            "%s: Time division must be 480 ticks per quarter; this file is %d ticks per quarter",
            mStreamName.c_str(),
            mTicksPerQuarter
        );
    }
    if (mNumTracks == 0 || midiType != 1 || ((unsigned short)mTicksPerQuarter & 0x8000U)
        || mTicksPerQuarter != 480) {
        mFail = true;
        return;
    }
    mState = kNewTrack;
}

void MidiReader::ReadSystemEvent(int tick, unsigned char type, BinStream &bs) {
    switch (type) {
    case 0xF0: // sysexstart
    case 0xF7: { // sysexend
        MidiVarLenNumber num(bs);
        bs.Seek(num.Value(), BinStream::kSeekCur);
        break;
    }
    case 0xFF: { // meta event incoming
        unsigned char read;
        bs >> read;
        ReadMetaEvent(tick, read, bs);
        break;
    }
    default:
        MILO_NOTIFY(
            "%s (%s): Cannot parse system event %i",
            mStreamName.c_str(),
            mCurTrackName.c_str(),
            type
        );
        break;
    }
}

void MidiReader::ReadEvent(BinStream &bs) {
    bool b;
    MILO_ASSERT(mState == kInTrack, 0x19E);
    MidiVarLenNumber num(bs);
    mCurTick += num.Value();
    int tpq = mCurTick * mDesiredTPQ / mTicksPerQuarter;
    if (tpq != mMidiListTick) {
        ProcessMidiList();
        if (mState != kInTrack)
            return;
        mMidiListTick = tpq;
    }
    unsigned char midichar;
    unsigned char nextchar;
    bs >> midichar;
    if (MidiIsStatus(midichar)) {
        b = false;
        if (!MidiIsSystem(midichar))
            mPrevStatus = midichar;
    } else {
        b = true;
        nextchar = midichar;
        midichar = mPrevStatus;
    }
    if (MidiIsSystem(midichar)) {
        ReadSystemEvent(tpq, midichar, bs);
    } else {
        if (!b)
            bs >> nextchar;
        ReadMidiEvent(tpq, midichar, nextchar, bs);
    }
}

void MidiReader::ReadNextEvent() {
    if (sVerify) {
        MILO_TRY { ReadNextEventImpl(); }
        MILO_CATCH(errMsg) {
            Error(errMsg);
            mFail = true;
        }
    } else {
        ReadNextEventImpl();
    }
}

void MidiReader::ReadNextEventImpl() {
    if (mFail)
        return;
    switch (mState) {
    case kInTrack:
        ReadEvent(*mStream);
        return;
    case kNewTrack:
        ReadTrackHeader(*mStream);
        return;
    case kStart:
        ReadFileHeader(*mStream);
        return;
    default:
        break;
    }
}

void MidiReader::ReadAllTracks() {
    if (mStream->Tell() != 0) {
        mStream->Seek(0, BinStream::kSeekBegin);
    }
    while (ReadTrack())
        ;
}

bool MidiReader::ReadSomeEvents(int num_events) {
    for (int i2 = 0; i2 < num_events; i2++) {
        ReadNextEvent();
        if (mState == kEnd || mFail)
            return true;
    }
    return false;
}

bool MidiReader::ReadTrack() {
    do {
        ReadNextEvent();
        if (mState == kEnd || mState == kNewTrack)
            break;
    } while (!mFail);
    return mState == kNewTrack;
}
