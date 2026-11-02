#include "hamobj\SongCollision.h"
#include "hamobj\Difficulty.h"
#include "hamobj\HamCharacter.h"
#include "hamobj\HamDirector.h"
#include "hamobj\HamGameData.h"
#include "hamobj\MocapSkeletonIterator.h"
#include "hamobj\MoveDir.h"
#include "math\Mtx.h"
#include "math\Vec.h"
#include "obj\Data.h"
#include "obj\DataUtl.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "rndobj\Trans.h"
#include "utl/BinStream.h"
#include "utl\Std.h"
#include "utl\TimeConversion.h"
#include <float.h>

float SongCollision::sCollisionTolerance;

std::vector<const char *> sCollisionUsefulBoneNames;
float sCollisionToleranceValue = 0.0f;

void bones_min_max_x(
    float &minX,
    float &maxX,
    std::vector<RndTransformable *> &transes,
    const Transform &xfm
) {
    FOREACH (it, transes) {
        Vector3 v40;
        MultiplyTranspose((*it)->WorldXfm().v, xfm, v40);
        minX = Min(minX, v40.x);
        maxX = Max(maxX, v40.x);
    }
}

namespace {
    void SetSongCollisionOffset(SongCollisionOutput &out, int idx, const Vector3 &pos) {
        const_cast<Vector3 &>(out.Offset(idx)) = pos;
    }

    void SetSongCollisionWorldPos(SongCollisionOutput &out, int idx, const Vector3 &pos) {
        out.mXfms[idx].v = pos;
    }

    void SetSongCollisionColliding(SongCollisionOutput &out, bool colliding) {
        out.mColliding = colliding;
    }
}

bool AreDancersColliding1D(
    std::vector<RndTransformable *> &bones0,
    std::vector<RndTransformable *> &bones1,
    const Vector3 &worldPos0,
    const Vector3 &worldPos1
) {
    if (bones0.empty() || bones1.empty())
        return false;

    Transform xfm;
    xfm.v = worldPos0;
    xfm.m.x.Set(worldPos1.x - worldPos0.x, worldPos1.y - worldPos0.y, 0.0f);
    Normalize(xfm.m.x, xfm.m.x);
    xfm.m.z.Set(0.0f, 0.0f, 1.0f);
    Cross(xfm.m.z, xfm.m.x, xfm.m.y);

    float min0 = FLT_MAX, min1 = FLT_MAX;
    float max1 = FLT_MIN, max0 = FLT_MIN;

    bones_min_max_x(min0, max0, bones0, xfm);
    bones_min_max_x(min1, max1, bones1, xfm);

    float overlap;
    if (min0 < min1) {
        overlap = max0 - min1;
    } else {
        overlap = max1 - min0;
    }

    return overlap > SongCollision::sCollisionTolerance;
}

#pragma region BeatCollisionData

void BeatCollisionData::Set(
    float minX, float maxX, const Transform &start_xfm, const Transform &end_xfm
) {
    using namespace Hmx;
    MILO_ASSERT(start_xfm.m == Matrix3::GetIdentity(), 0x62);
    MILO_ASSERT(end_xfm.m == Matrix3::GetIdentity(), 0x63);
    mMinX = minX;
    mMaxX = maxX;
    Subtract(start_xfm.v, end_xfm.v, mOffset);
}

BinStream &operator<<(BinStream &bs, const BeatCollisionData &bcd) {
    bs << bcd.mMinX << bcd.mMaxX;
    bs << bcd.mOffset;
    return bs;
}

BinStreamRev &operator>>(BinStreamRev &d, BeatCollisionData &bcd) {
    d >> bcd.mMinX;
    d >> bcd.mMaxX;
    if (d.rev > 1) {
        d >> bcd.mOffset;
    } else if (d.rev > 0) {
        Transform xfm;
        d >> xfm;
        bcd.mOffset = xfm.v;
    }
    return d;
}

#pragma endregion
#pragma region SongCollision

SongCollision::SongCollision() {}

BEGIN_HANDLERS(SongCollision)
    HANDLE_ACTION(update, Update(_msg->Obj<MoveDir>(2)))
    HANDLE_ACTION(print, Print())
    HANDLE_EXPR(equals, Equals(_msg->Obj<SongCollision>(2)))
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

// SyncProperty is in FileMergerOrganizer.cpp (cross-unit)

BEGIN_SAVES(SongCollision)
    SAVE_REVS(2, 1)
    SAVE_SUPERCLASS(Hmx::Object)
    for (int i = 0; i < kNumDifficulties; i++) {
        bs << mData[i];
    }
END_SAVES

BEGIN_COPYS(SongCollision)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(SongCollision)
    BEGIN_COPYING_MEMBERS
        for (int i = 0; i < kNumDifficulties; i++) {
            COPY_MEMBER(mData[i])
        }
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(2, 1)

BEGIN_LOADS(SongCollision)
    LOAD_REVS(bs)
    ASSERT_REVS(2, 1)
    LOAD_SUPERCLASS(Hmx::Object)
    if (d.altRev < 1) {
        for (int i = 0; i < kNumDifficultiesDC2; i++) {
            d >> mData[i];
        }
        mData[kDifficultyBeginner] = mData[kDifficultyEasy];
    } else {
        for (int i = 0; i < kNumDifficulties; i++) {
            d >> mData[i];
        }
    }
END_LOADS

bool SongCollision::Equals(SongCollision *other) {
    if (!other)
        return false;
    for (int i = 0; i < kNumDifficulties; i++) {
        if (mData[i].size() != other->mData[i].size())
            return false;
        for (unsigned int j = 0; j < mData[i].size(); j++) {
            // w11-a: the image forms other's element address first and indexes
            // our own element inline for the first test (`add r8, r7, r10`,
            // offset + begin); binding `a` up front, or declaring b then a,
            // flips the add operands.
            const BeatCollisionData &b = other->mData[i][j];
            bool close = std::fabs(mData[i][j].mMaxX - b.mMaxX) < 0.0001f;
            const BeatCollisionData &a = mData[i][j];
            if (!close)
                return false;
            close = std::fabs(a.mMinX - b.mMinX) < 0.0001f;
            if (!close)
                return false;
            close = std::fabs(Distance(a.mOffset, b.mOffset)) < 0.0001f;
            if (!close)
                return false;
        }
    }
    return true;
}

void SongCollision::Print() {
    int maxDatas = 0;
    for (int i = 0; i < kNumDifficulties; i++) {
        int sz = (int)mData[i].size();
        if (maxDatas < sz) {
            maxDatas = sz;
        }
    }
    String str;
    str = "Beat\tData";
    for (Difficulty d = EasiestDifficulty(); d < kNumDifficulties;
         d = DifficultyOneHarder(d)) {
        str += MakeString("\t%s", DifficultyToSym(d).Str());
    }
    TheDebug << MakeString("%s\n", str.c_str());
    for (int i = 0; i < maxDatas; i++) {
        BeatCollisionData allDatas[kNumDifficulties];
        BeatCollisionData *dataIt = allDatas;
        for (int j = 0; j < kNumDifficulties; j++, dataIt++) {
            if (i < mData[j].size()) {
                *dataIt = mData[j][i];
            } else {
                memset(dataIt, 0, sizeof(BeatCollisionData));
            }
        }
        str = MakeString("%d\tMin X", i);
        for (Difficulty d = EasiestDifficulty(); d < kNumDifficulties;
             d = DifficultyOneHarder(d)) {
            str += MakeString("\t%f", allDatas[d].mMinX);
        }
        TheDebug << MakeString("%s\n", str.c_str());
        str = "\tMax X";
        for (Difficulty d = EasiestDifficulty(); d < kNumDifficulties;
             d = DifficultyOneHarder(d)) {
            str += MakeString("\t%f", allDatas[d].mMaxX);
        }
        TheDebug << MakeString("%s\n", str.c_str());
        str = "\tOffset X";
        for (Difficulty d = EasiestDifficulty(); d < kNumDifficulties;
             d = DifficultyOneHarder(d)) {
            str += MakeString("\t%f", allDatas[d].mOffset.x);
        }
        TheDebug << MakeString("%s\n", str.c_str());
        str = "\tOffset Y";
        for (Difficulty d = EasiestDifficulty(); d < kNumDifficulties;
             d = DifficultyOneHarder(d)) {
            str += MakeString("\t%f", allDatas[d].mOffset.y);
        }
        TheDebug << MakeString("%s\n", str.c_str());
        str = "\tOffset Z";
        for (Difficulty d = EasiestDifficulty(); d < kNumDifficulties;
             d = DifficultyOneHarder(d)) {
            str += MakeString("\t%f", allDatas[d].mOffset.z);
        }
        TheDebug << MakeString("%s\n", str.c_str());
    }
}


void SongCollision::Init() {
    REGISTER_OBJ_FACTORY(SongCollision);
    DataArray *tolerance = DataGetMacro("SONG_COLLISION_TOLERANCE");
    if (tolerance) {
        sCollisionTolerance = tolerance->Float(0);
    }
    sCollisionUsefulBoneNames.clear();
    DataArray *bones = DataGetMacro("SONG_COLLISION_BONES");
    if (bones) {
        for (int i = 0; i < bones->Size(); i++) {
            sCollisionUsefulBoneNames.push_back(bones->Str(i));
        }
    }
}

const BeatCollisionData *SongCollision::BeatData(int beat, Difficulty diff) const {
    MILO_ASSERT_RANGE(diff, 0, kNumDifficulties, 0xe8);
    const std::vector<BeatCollisionData> &diffData = mData[diff];
    MILO_ASSERT(beat >= 0, 0xeb);
    if (beat < diffData.size()) {
        return &diffData[beat];
    } else {
        return nullptr;
    }
}

void SongCollision::GatherUsefulBones(
    std::vector<RndTransformable *> &usefulBones, HamCharacter *dancer
) {
    MILO_ASSERT(dancer, 0x19);
    usefulBones.clear();
    for (ObjDirItr<RndTransformable> it(dancer, true); it != nullptr; ++it) {
        const char *curName = it->Name();
        for (int i = 0; i < sCollisionUsefulBoneNames.size(); i++) {
            if (strneq(
                    curName,
                    sCollisionUsefulBoneNames[i],
                    strlen(sCollisionUsefulBoneNames[i])
                )) {
                usefulBones.push_back(it);
                break;
            }
        }
    }
}

void SongCollision::Update(MoveDir *moveDir) {

    // RESIDUAL (w7-ak, 94.2 canonical): 80 rows, 41 of them one r28<->r29
    // rotation plus an r15<->r16 rotation.  The visible cause is here: the image
    // keeps `this` in r30 (`mr r30, r3`) and only forms `this + 0x2c` AFTER the
    // Timer constructor returns (`addi r29, r30, 0x2c`), home-storing it into the
    // 0x50(r31) slot the early MILO_ASSERT argument buffers have just finished
    // with; binding `data` at function scope emits `addi r28, r3, 0x2c` as the
    // second instruction of the body and gives it a slot of its own at 0x64.
    // BOTH ways of expressing that are REFUTED, and both lose more than the 4
    // rows they win, because whatever holds the array base also decides where
    // `dancer` lives:
    //  - moving `auto &data = mData;` down to just after `Timer timer;` costs
    //    94.2 -> 92.4: `dancer` is evicted from r14 into r28 and picks up a home
    //    store of its own;
    //  - dropping the reference entirely and spelling `mData[i]` / `mData[0]` at
    //    the four use sites costs 94.2 -> 92.8.  It DOES close the (0xd0,0xf0)
    //    BeatCollisionData offset swap and takes the stack diff from 4/6 to 2/2,
    //    but `dancer` is evicted the same way (`stw r28, 0x64(r31)`), and the
    //    array base then lands in r14 instead of r29.
    // The r28/r29 rotation is therefore not reachable through the array base on
    // its own -- `dancer`'s allocation has to move with it.
    //
    // w26-k (2026-11-02): mechanism, from the c2 colour-allocator tap, plus a
    // correction to that last sentence: `dancer` is in r14 on BOTH sides in the
    // current tree (`lbz 11, 425(14)` at tgt 0x30C / ours 0x308), so it is not
    // what the rotation is about any more.  The whole knot reduces to one
    // fact: `this` is NOT a colour candidate in our build.  `addi r28, r3,
    // 0x2c` + `stw r28, 0x64(r31)` are the first two instructions of the body,
    // so c2 CSE'd `this + 0x2c` and dropped `this`; the image keeps `this` in
    // r30 (`mr 30, 3` at 0x24), forms `addi r29, r30, 0x2c` at the first mData
    // use (0x138) and home-stores THAT to 0x50(r31).  Everything downstream
    // follows: the two hoisted global addresses then take r28/r29 in the image
    // and r29/r30 in ours, which is the whole r28<->r29 rotation (20 rows),
    // and the spurious home store at 0x64 is the (0xd0,0xf0) offset swap.
    // The second rotation, r15<->r16 (11 rows), is an independent tie-0 pair:
    // one hoisted address half and one `li 0` zero constant.  Ours pops the
    // address first (id 58, pri -68 tie 0, code band -> r16) and the k0d zero
    // second (id 4, pri -71 tie 0 -> r15); the image needs the zero popped
    // first, so address -> r15 and zero -> r16.
    // Behaviour re-read against the image listing: same calls in the same
    // order, same stores, same Set(minX, maxX) / push_back /
    // bones_min_max_x sequence; objdiff diff_op none; unicorn EQUIVALENT at
    // harness h4.  The two `MakeString` rows under Function Call Diff are the
    // per-TU assert-instantiation class, not a defect: ICF binds one arbitrary
    // `MakeString<char[N],int,char[M]>` spelling at the folded address: the
    // image shows ONE (`char[19]` file, `char[4]` expression), we emit nine
    // (all `char[18]`, one per distinct expression length), so that row is a
    // naming artifact of the fold and not reachable from this source.
    //
    // w7-ba (floor held at 94.2; four more spellings measured).  The image's
    // shape is now pinned down further, and the knot is a single allocator
    // choice:
    //  - the dead `stw r29, 0x78(r31)` at 0x8251127C, right before the size()
    //    loads, is a NAMED reference bound to `mData[0]` after the loop (a ref
    //    bound to `data[0]` through the function-scope ref is folded away and
    //    byte-identical to this spelling).  With `mData[i]` in the loop and
    //    `std::vector<BeatCollisionData> &easy = mData[0];` at the end, that
    //    store, the r15/r16 order, the 0xd0/0xf0 bcd slots and the 0x88/0x90
    //    Timer::Ms temps all match (281/349 rows equal vs 265/345 here), but
    //    canonical reads 92.8 because `dancer` is still evicted: the hoisted
    //    `this + 0x2c` temp takes r14 and dancer takes r28, the register
    //    TheHamDirector@ha freed, which is the one `beat` then steals inside
    //    the inner loop (image: dancer r14, base r29 = the freed register,
    //    base spilled to 0x50 and reloaded at 0x82511190/98).
    //  - a pointer local after `Timer timer;` is byte-identical to w7-ak's
    //    reference move (92.4); a per-difficulty reference inside the loop body
    //    is byte-identical to the `mData[i]` spelling.
    // What is left is which of {dancer, hoisted base} gets r14; nothing in the
    // source order tried so far moves that.  Kept the higher-canonical spelling.
    //
    // w21-g (floor held at 94.2, 80 rows; behaviour re-read against the image
    // listing 0x82510D98-0x825112C8 -- Set(minX, maxX) argument order, the
    // maxX/minX resets, bones_min_max_x(minX, maxX, ...), the 0x48 size and the
    // 1/30 frame scale all agree; no defect).  One more spelling measured:
    // `auto &data = mData;` declared right after `Timer timer;` but used ONLY
    // for the final `data[0].size()`, with `mData[i]` in the loop -- the image's
    // home slot 0x50 for the base suggests exactly that lifetime -- 92.4, and
    // `dancer` is evicted to r28 with a home store at 0x68 (same failure as
    // w7-ak's move).
    // w21-al (floor held at 94.2): a standalone /FAs probe of this TU scored
    // with objdiff against the target object reproduces every number above
    // (94.24 / 92.79 / 92.44), so the probe is faithful.  Thirteen more
    // spellings, none above 94.24: `easy = data[0]` at the end with `data` at
    // function scope (byte-identical) or after the Timer (92.44); `data`
    // bound before the Timer / before usefulBones (91.67), before `dancer`
    // (93.90), inside `if (moveDir)` or before the first MILO_ASSERT (93.23);
    // a function-scope `std::vector<BeatCollisionData> *` (identical); with
    // `mData[i]` + `easy`: function-scope `int i`, `HamCharacter *const`,
    // a cast WorldXfm call, end()-begin() for size, mData[0].size() direct
    // (all 92.79-92.82).  The Timer::Ms rows (sradi/clrrwi order) come from
    // the PCH inline CyclesToMs and were not touched.
    // w21-bm (floor held at 94.2, 80 rows; same /FAs probe, 30 more spellings,
    // none above 94.24).  The stack layout says what the image did: its base
    // value is an UNNAMED compiler temp spilled to 0x50 (pooled with the
    // assert line temps), which is why minX/maxX sit together at 0x60/0x64
    // and the DifficultyToSym Symbol temp at 0x78 -- whereas the named `data`
    // here gets its own home at 0x64 and pushes minX down onto 0x50.  An
    // unnamed base (`mData[i]` + `easy`) reproduces the slots but MSVC then
    // spills `dancer` instead of the base, every time: invariant (92.79 to
    // three places) under loop-local vs outer Transform/minX/maxX, decl order
    // of current_beat/startXfm/minX/maxX, a renamed bcd, an explicit (int)
    // on SecondsToBeat, a literal 4 bound, while(it){...++it;}, an enum
    // loop counter (88.6), a dancer declared at function scope or split
    // decl/assign, a WorldXfm reference local, RndTransformable::WorldXfm(),
    // a function-scope pointer assigned after the Timer (92.44).  Timer::Ms
    // rows: the image's sradi/clrrwi order varies per caller (CameraTilt vs
    // FlowWhile), so it is context scheduling in the PCH inline.
    // w29-b (C2RS-BRIDGE 8.6, one-basic-block demotion): the c2-rs tap shows
    // `this` being DEMOTED (CDEMOTE 85 -> temp) because this reference forms
    // `addi rN,r3,0x2c` in the ENTRY block; declaring it at function scope also
    // costs a 4-byte frame var, since the base home-stores it to 0x64 and that
    // pushes minX onto 0x50.  The image keeps `this` in r30 and forms the same
    // address at the loop preheader instead (`addi r29,r30,0x2c` at row 79,
    // after the GatherUsefulBones/Timer calls) and home-stores it to 0x50.
    // Moving this declaration inside the if-block, just after `Timer timer;`,
    // reproduces the image's entry block byte-for-byte (rows 0-78 all equal,
    // the early `addi`/`stw 0x64` both gone) and lands the address at the same
    // row 79 -- but in caller-visible r14 with NO home store, so the cascade
    // down the loop is worse: 92.42730 / fuzzy 91.89317, base 337 = target 337
    // instructions, vs 94.19585 / 93.52819 with base 335.  So the higher score
    // here comes from a body two instructions SHORTER than the image (bridge
    // 14), and the remaining handle is c2 choosing a callee-saved home over the
    // image's stack home for a value whose live range spans the loop's calls.
    auto& data = mData;
    if (moveDir) {
        MILO_ASSERT(TheGameData, 0xFB);
        MILO_ASSERT(TheHamDirector, 0xFC);
        HamCharacter *dancer = TheHamDirector->GetCharacter(0);
        MILO_ASSERT(dancer, 0xFF);
        std::vector<RndTransformable *> usefulBones;
        GatherUsefulBones(usefulBones, dancer);
        Timer timer;
        for (int i = 0; i < kNumDifficulties; i++) {
            MILO_LOG("Processing collisions for %s\n", DifficultyToSym((Difficulty)i));
            timer.Restart();
            MILO_ASSERT(TheGameData, 0x10C);
            TheGameData->Player(0)->SetDifficulty((Difficulty)i);
            data[i].clear();
            MocapSkeletonIterator it(0, TheHamDirector->SongAnim(0)->EndFrame());
            int current_beat = -1;
            Transform startXfm;
            float minX, maxX;
            for (; it; ++it) {
                int beat = SecondsToBeat(it.CurrentFrame() * 0.03333333507180214f);
                MILO_ASSERT(beat >= 0, 0x11B);
                if (beat != current_beat) {
                    MILO_ASSERT(beat == current_beat + 1, 0x11F);
                    if (current_beat >= 0) {
                        BeatCollisionData bcd;
                        bcd.Set(minX, maxX, startXfm, dancer->WorldXfm());
                        data[i].push_back(bcd);
                    }
                    startXfm = dancer->WorldXfm();
                    maxX = 0;
                    minX = 0;
                    current_beat = beat;
                }
                bones_min_max_x(minX, maxX, usefulBones, startXfm);
            }
            if (current_beat != -1) {
                BeatCollisionData bcd;
                bcd.Set(minX, maxX, startXfm, dancer->WorldXfm());
                data[i].push_back(bcd);
            } else {
                MILO_NOTIFY(
                    "Could not process collision mocap for %s", TheGameData->GetSong()
                );
            }
            timer.Stop();
            MILO_LOG("Took %fms\n", timer.Ms());
        }
        int sizeKB = data[0].size() * 0x48; // where is this 0x48 coming from
        sizeKB /= 1024;
        MILO_LOG("Approx size = %ikB\n", sizeKB);
    }
}

void SongCollision::CheckCollision(
    int beat,
    const Difficulty *const diffs,
    const Transform *const transforms,
    SongCollisionOutput &out
) const {
    Vector3 dir;
    Subtract(transforms[1].v, transforms[0].v, dir);
    Vector3 normalDir;
    Normalize(dir, normalDir);

    // w16-b (99.943, 23 rows = IV bump order + FPR permutation): a plain
    // `for (i = 0; i < 2; i++)` and a Transform struct assignment in place of
    // the memcpy are both byte-identical to this.
    // w21-aw (23 -> 12 rows): binding `const Transform &xfm = transforms[i]`
    // once fixed the induction-pointer bump order, and Length(dir) written
    // out fixed its sum order.  Stop: the 12 left are the min/max diff loads
    // (f12/f13 permutation, image loads xfm.v.y before minEdge->y), the min
    // projection's commuted fmuls/fmadds operands (image normalDir first),
    // and the max projection's term order (image z, y, x; ours z, x, y).
    // Inert or worse: products commuted (`minDz * normalDir.z`, inert);
    // explicit `(z + y) + x` / `(y + z) + x` on the max projection (sorted
    // to y, z inside the group); Vector3 minDiff/maxDiff by member stores in
    // this order (98.8 -- operands then come out normalDir-first, but the
    // walker bases move) or by the 3-float ctor (98.5); the image's own issue
    // order minDz, minDy, minDx, maxDx, maxDz, maxDy (98.4, see below).
    // w26-c: read the 12 rows off the c2 tap rather than by probing.  The min
    // projection's products are two VARIABLE leaves and the emitted order is
    // the post-lowering commutative sort (C2RS-BRIDGE 8.7), "higher sid
    // first": ours is (minDz sid 24, normalDir.z sid 22) -> minDz first, and
    // the image needs normalDir.z first, i.e. normalDir.z must key ABOVE the
    // diffs.  Its sid cannot rise above a later-declared diff, so the image's
    // normalDir component is either a temp leaf (0x10000|sid<<6, which beats
    // every named local) or a memory leaf -- both of which are about how c2
    // classifies the hoisted `normalDir` component load, not about how the
    // expression is written.  Measured inert here (129/141 unchanged):
    // reference aliases of normalDir (single and double binding), float
    // references to its components, both with the explicit pair parentheses,
    // and reference aliases of *minEdge/*maxEdge.  NB aliasing normalDir is
    // actively worse (121); the w21-aw note's "Vector3 minDiff comes out
    // normalDir-first at 98.8" is the one spelling seen to reach this order,
    // and it costs more elsewhere than it buys.
    int i = 0;
    do {
        const Transform &xfm = transforms[i];
        out.mXfms[i] = xfm;

        const BeatCollisionData *bd = BeatData(beat, diffs[i]);
        Vector3 *minEdge = &out.mMinEdge[i];
        Vector3 *maxEdge = &out.mMaxEdge[i];
        Vector3 *push = &out.mPush[i];

        if (!bd) {
            minEdge->Zero();
            maxEdge->Zero();
            push->Zero();
        } else {
            Vector3 minVec(bd->mMinX, 0.0f, 0.0f);
            Multiply(minVec, xfm, *minEdge);

            Vector3 maxVec(bd->mMaxX, 0.0f, 0.0f);
            Multiply(maxVec, xfm, *maxEdge);

            // Pre-compute all differences (target interleaves min/max loads).
            //
            // This order is LOAD-BEARING and already tuned -- do not "tidy" it
            // into min-then-max.  The image's own issue order is minDz, minDy,
            // minDx, maxDx, maxDz, maxDy; writing exactly that here REGRESSES
            // the function 99.943 -> 98.4 (23 rows -> 45, plus an insert and a
            // delete), because the tidier order costs a stack slot and shifts
            // nine `stfs` and the whole loop-pointer block by 4.  Measured
            // 2026-09-14 in this tree.
            float minDz = minEdge->z - xfm.v.z;
            float minDy = minEdge->y - xfm.v.y;
            float maxDy = maxEdge->y - xfm.v.y;
            float minDx = minEdge->x - xfm.v.x;
            float maxDx = maxEdge->x - xfm.v.x;
            float maxDz = maxEdge->z - xfm.v.z;

            float proj = normalDir.z * minDz + normalDir.y * minDy + normalDir.x * minDx;

            if ((proj <= 0.0f || i != 0) && (proj >= 0.0f || i != 1)) {
                proj = normalDir.z * maxDz + normalDir.y * maxDy + normalDir.x * maxDx;
            }

            Scale(normalDir, proj, *push);
        }
        i++;
    } while (i < 2);

    // w21-aw: Length(dir) written out in the image's association -- the
    // image sums y*y, then z*z, then x*x (fmuls f0,f26,f26 / fmadds f25 /
    // fmadds f27); the Vec.h inline gave z, x, y here.  Also moves native
    // rounding toward the image's.
    float distance = std::sqrt((dir.y * dir.y + dir.z * dir.z) + dir.x * dir.x);
    float totalExtent = 0.0f;
    for (int j = 0; j < 2; j++) {
        totalExtent += Length(out.mPush[j]);
    }

    SetSongCollisionColliding(out, totalExtent - sCollisionTolerance > distance);
}

bool SongCollision::IsCollision(
    int startBeat,
    int endBeat,
    const Difficulty *const diffs,
    const Transform *const transforms,
    std::vector<SongCollisionOutput> *outputs
) const {
    // Copy transforms locally so we can accumulate beat offsets
    Transform localXfms[2];
    __int64 *dst = (__int64 *)localXfms;
    const __int64 *src = (const __int64 *)transforms;
    for (int k = 0; k < 16; k++) {
        dst[k] = src[k];
    }

    bool anyCollision = false;
    int beat = startBeat;
    while (beat < endBeat) {
        SongCollisionOutput out;
        CheckCollision(beat, diffs, localXfms, out);
        if (out.Colliding()) {
            if (!outputs) {
                return true;
            }
            anyCollision = true;
        }
        if (outputs) {
            outputs->push_back(out);
        }

        // Accumulate beat offsets into local transforms
        for (int i = 0; i < 2; i++) {
            const BeatCollisionData *bd = BeatData(beat, diffs[i]);
            if (bd) {
                Add(localXfms[i].v, bd->mOffset, localXfms[i].v);
            }
        }
        beat++;
    }
    return anyCollision;
}
