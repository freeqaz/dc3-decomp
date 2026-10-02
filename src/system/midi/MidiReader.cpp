#include "midi\Midi.h"
#include "os\Debug.h"
#include "utl\MultiTempoTempoMap.h"
#include "utl/MeasureMap.h"
#include "midi/MidiConstants.h"
#include "utl\TempoMap.h"
#include "midi\MidiVarLen.h"
#include "utl\MBT.h"
#include "utl\FileStream.h"
#include <algorithm>

const MidiChunkID MidiChunkID::kMThd("MThd");
const MidiChunkID MidiChunkID::kMTrk("MTrk");
bool MidiReader::sVerify = false;

// w8-e 2026-09-15: ??3FileStream@@SAXPAX@Z (24 B) -- FileStream's class
// `operator delete` -- is a real same-TU gap.  ham_xbox_r.map contributes it
// from `midi:MidiReader.obj` at 8254e100, i.e. the image instantiates
// FileStream's OBJ_MEM_OVERLOAD deallocator in THIS translation unit.
// w8-j 2026-09-15: CLOSED, 0.0 -> 100.0.  It is NOT a `delete` site (see the
// probe at the bottom of this file for why a delete-expression emits nothing
// here): it is a `new FileStream(...)`, whose ctor-throws cleanup path takes
// operator delete's ADDRESS and so forces the COMDAT.  The tell was the map
// asymmetry -- ??3FileStream@@ present, ??2FileStream@@ absent everywhere,
// because MEM_OVERLOAD's operator new is inlined straight into the call site.

namespace {
    inline int MidiRank(unsigned char status) {
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

// w18-d: `pow(float, int)` is the XDK math.h overload, which forwards to the
// CRT's `_Pow_int<float>` template; both are written here the way the header
// writes them (the helper name is ours).  The forwarding level is what gives the
// image's `fmr f13, f1` copy of x (85.0 -> 100).
template <class T>
inline T PowInt(T x, int y) {
    unsigned int n;
    if (y >= 0)
        n = (unsigned int)y;
    else
        n = (unsigned int)(-y);
    for (T z = T(1);; x *= x) {
        if ((n & 1) != 0)
            z *= x;
        if ((n >>= 1) == 0)
            return (y < 0 ? T(1) / z : z);
    }
}

float pow(float x, int y) { return PowInt(x, y); }

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

#ifndef HX_NATIVE
// w8-j: orphan-instantiation probe for ??3FileStream@@SAXPAX@Z (0x8254E100,
// 24 B).  ham_xbox_r.map contributes that COMDAT from midi:MidiReader.obj and
// NOTHING in MidiReader.s calls it -- the listing holds only .fn/.endfn, no bl --
// so the odr-use was compiled into this TU and dropped by /OPT:REF.  Note the
// map attributes no ??2FileStream@@ (operator new) to this object, or to any
// other, so the discarded site deleted a FileStream rather than allocating one.
// The sibling ??3TempoMap@@SAXPAX@Z two functions later in the same listing is
// already 100% here, emitted by the utl\TempoMap.h include above -- same
// MEM_OVERLOAD shape, so the body is not in question, only the odr-use.
// REFUTED: `delete fs` emits NOTHING here -- ~FileStream is virtual, so the
// delete-expression dispatches to the scalar deleting destructor and the
// operator delete call lives inside that, in FileStream's own TU.  The site is
// `new FileStream(...)`: MEM_OVERLOAD's operator new is a small inline static
// member that MSVC inlines straight into the call (which is why the map has no
// ??2FileStream@@ anywhere at all), while the matching operator delete is
// reached by ADDRESS from the constructor-throws cleanup path and therefore has
// to be emitted as a real COMDAT.
// External linkage is required; `static` is discarded before it instantiates
// anything it mentions (measured by w8-b in HamMove.cpp).
FileStream *Dc3W8jMidiFileStreamProbe(const char *path) {
    return new FileStream(path, FileStream::kRead, true);
}
#endif

#ifndef HX_NATIVE
// w8-j 2026-09-15 -- FLOOR at 85.000% (match_percent_normalized) for
// ?pow@@YAMMH@Z, 80 B target vs 76 B ours, 13 of 21 instructions equal.
// The whole residual is FPR naming plus one missing copy.  The image opens with
//     fmr   f13, f1          <- a copy of the base parameter
// and then runs the loop on f13, holding the CSE'd 1.0f literal in f12 and the
// accumulator in f0.  We never emit that copy: we mutate the parameter in f1
// directly, hold 1.0f in f13 and the accumulator in f0 -- so our tail is
// already exact (`fdivs f0, f13, f0` / `fmr f1, f0` / `blr` all pair) and only
// the register NAMES differ, four rows of them.
// Three spellings measured, three full ninja builds:
//   (a) `float b = base;` declared first, loop mutates b   -> 82.000%.  It DOES
//       emit the copy, but frees f1 so MSVC then allocates `result` into f1,
//       which costs the final `fmr f1, f0` (it early-returns with `bgelr`) and
//       loses two rows that (c) already has.
//   (b) same, declared after `float result = 1.0f;`        -> 82.000%, byte
//       identical to (a).  Declaration reorder is inert for register-only
//       swaps, as docs/decomp/patterns/fixable-declarations.md says.
//   (c) `int exp = exponent < 0 ? -exponent : exponent;`   -> 57.750%.  The
//       ternary lowers to the branchless abs idiom (srawi/xor/subf) instead of
//       the image's compare-and-negate, deleting five rows at once.  This also
//       refutes the ternary as a way to fix the one remaining insert/delete
//       pair (the image issues `cmpwi cr6, r4, 0` BEFORE `mr r11, r4`; we issue
//       it after).
// Kept (c)-free original at 85.000%.  What is left wants the permuter, not
// another source spelling.  Do not re-derive.
#endif

// w15-i2 2026-10-02: CLOSED 77.182 -> 100.0 by marking MidiRank `inline`
// (the retail map flags it `f i`, a COMDAT).  The floor note below was the
// right diagnosis -- same-TU register-footprint propagation -- with the wrong
// conclusion: the callee does not have to be invisible, it has to be inline.
// See docs/decomp/patterns/map-comdat-flag-gates-clobber-propagation.md.
// Historical note follows.
// w8-j 2026-09-15 -- FLOOR at 77.182% for
// ?DefaultMidiLess@?A0x7d41cf68@@YA_NABUMidi@MidiReader@@0@Z, 88 B target vs
// 68 B ours, 11 of 22 instructions equal.  This one is a COMPILER POLICY
// difference, not a source difference, and the evidence is unusually clean.
// Our build applies MSVC/Xenon's static-callee register-footprint optimisation
// to the two MidiRank calls: MidiRank is a leaf in this same anonymous
// namespace that touches only r3/r11/cr6 (it is 100.0% matched, 124 B), so the
// compiler keeps the second Midi reference in the VOLATILE r4 and the first
// rank in the VOLATILE r10 straight across both `bl`s and needs no frame at
// all.  The image does not: it saves r30/r31 (`std r30, -0x18(r1)` /
// `std r31, -0x10(r1)`), homes r4 into r31, keeps the first rank in r30, and
// pays 16 more bytes of stack for it.  Every one of the 11 mismatched rows is
// that one decision -- 2 saves, 2 restores, the `mr r31, r4`, the +16 stack
// adjust pair, and four r10<->r30 / r31<->r4 renames.  No logic differs.
// REFUTED: defining MidiRank AFTER DefaultMidiLess with a forward declaration
// (one full ninja) is byte-identical -- MSVC computes the footprint over the
// whole TU, not in source order, so it is not reachable by reordering.  The
// comparison itself already matches exactly (subfc/eqv/srwi/addze/clrlwi).
