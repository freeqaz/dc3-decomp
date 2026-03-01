#include "char/CharBonesSamples.h"

#include "CharClip.h"
#include "math/Mtx.h"
#include "math/Rot.h"
#include "math/Trig.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "utl/MemMgr.h"
#line 8 "CharBonesSamples.cpp"
CharBonesSamples::CharBonesSamples()
    : mNumSamples(0), mPreviewSample(0), mRawData(nullptr) {}

CharBonesSamples::~CharBonesSamples() { MemFree(mRawData); }

INIT_REVS(0x10, 0)

void CharBonesSamples::Load(BinStream &bs) {
    int revs;
    bs >> revs;
    int rev = getHmxRev(revs);
    int altRev = getAltRev(revs);
    BinStreamRev d(bs, revs);
#ifdef HX_NATIVE
    printf("CharBonesSamples::Load: rev=%d altRev=%d tell=%d\n", rev, altRev, bs.Tell());
    fflush(stdout);
#endif
    if (0x10 < rev) {
        MILO_FAIL(
            "%s can\'t load new %s version %d > %d", "", "CharBonesSample", d.rev, gRev
        );
    }
    if (altRev > 0) {
        MILO_FAIL(
            "%s can\'t load new %s alt version %d > %d",
            "",
            "CharBonesSample",
            d.altRev,
            gAltRev
        );
    }
    if (!(rev > 12)) {
        TheDebugFailer << MakeString(kAssertStr, __FILE__, 0x29d, "d.rev > 12");
    }
    LoadHeader(d);
    LoadData(d);
}

void CharBonesSamples::LoadHeader(BinStreamRev &d) {
    MemFree(mRawData);
    mRawData = nullptr;
    int numBones;
    d >> numBones;
#ifdef HX_NATIVE
    printf("CharBonesSamples::LoadHeader: rev=%d numBones=%d tell=%d sizeof(Vector3)=%zu sizeof(Hmx::Quat)=%zu\n",
        d.rev, numBones, d.stream.Tell(), sizeof(Vector3), sizeof(Hmx::Quat));
    fflush(stdout);
#endif
    mBones.resize(numBones);
    if (d.rev > 0xA) {
        for (int i = 0; i < numBones; i++) {
            d >> mBones[i];
        }
    } else {
        for (int i = 0; i < numBones; i++) {
            d >> mBones[i].name;
        }
    }

    if (d.rev > 9) {
        ReadCounts(d.stream, d.rev > 0xF ? 7 : 10);
        d >> (int &)mCompression;
        int numSamples;
        d >> numSamples;
        MILO_ASSERT(numSamples < 32767, 0x2D7);
        mNumSamples = numSamples;
    } else {
        int i;
        if (d.rev > 5) {
            int count;
            if (d.rev > 7) {
                count = 9;
            } else {
                count = 10;
                if (d.rev > 6)
                    count = 6;
            }
            for (i = 0; i < count; i++) {
                int tmp;
                d >> tmp;
            }
            d >> (int &)mCompression;
            int numSamples;
            d >> numSamples;
            MILO_ASSERT(numSamples < 32767, 0x2F1);
            mNumSamples = numSamples;
        } else {
            int numSamples;
            d >> numSamples;
            MILO_ASSERT(numSamples < 32767, 0x2FC);
            mNumSamples = numSamples;
            if (d.rev > 3) {
                d >> (int &)mCompression;
            }
        }
        for (i = 0; i < 7; i++) {
            mCounts[i] = 0;
        }
        for (i = 0; i < (int)mBones.size(); i++) {
            mCounts[CharBones::TypeOf(mBones[i].name) + 1]++;
        }
        for (i = 1; i < 7; i++) {
            mCounts[i] += mCounts[i - 1];
        }
    }

    if (d.rev > 0xB) {
        d >> mFrames;
    } else {
        mFrames.clear();
    }
    RecomputeSizes();
#ifdef HX_NATIVE
    printf("CharBonesSamples::LoadHeader: numBones=%d numSamples=%d compression=%d totalSize=%d allocSize=%d frames=%d tell=%d\n",
        (int)mBones.size(), mNumSamples, (int)mCompression, mTotalSize, AllocateSize(), (int)mFrames.size(), d.stream.Tell());
    printf("  mCounts=[%d,%d,%d,%d,%d,%d,%d] mOffsets=[%d,%d,%d,%d,%d,%d,%d]\n",
        mCounts[0], mCounts[1], mCounts[2], mCounts[3], mCounts[4], mCounts[5], mCounts[6],
        mOffsets[0], mOffsets[1], mOffsets[2], mOffsets[3], mOffsets[4], mOffsets[5], mOffsets[6]);
    for (int b = 0; b < (int)mBones.size(); b++) {
        printf("  bone[%d]: name='%s' weight=%.2f\n", b, mBones[b].name.Str(), mBones[b].weight);
    }
    fflush(stdout);
#endif
    mRawData = (char *)MemAlloc(
        AllocateSize(), "CharBonesSamples.cpp", 0x2d1, "CharBonesSamples", 0
    );
}

void CharBonesSamples::LoadData(BinStreamRev &d) {
    if (d.rev == 0xE) {
        bool x;
        d >> x;
    }
    int totalBytes = AllocateSize();
#ifdef HX_NATIVE
    printf("CharBonesSamples::LoadData: totalBytes=%d tell=%d\n", totalBytes, d.stream.Tell());
    fflush(stdout);
    if (totalBytes > 0 && mRawData) {
        d.stream.Read(mRawData, totalBytes);
    }
    printf("CharBonesSamples::LoadData: done tell=%d\n", d.stream.Tell());
    fflush(stdout);
#else
    for (int i = 0; i < mNumSamples; i++) {
        mStart = mRawData + mTotalSize * Min(i, mNumSamples - 1);

        if (mCompression >= kCompressVects) {
            short *quatOffset = (short *)(mStart + mOffsets[TYPE_QUAT]);
            for (short *p = (short *)mStart; p < quatOffset; p += 3) {
                d >> p[0] >> p[1] >> p[2];
            }
        } else {
            Vector3 *quatOffset = (Vector3 *)(mStart + mOffsets[TYPE_QUAT]);
            for (Vector3 *p = (Vector3 *)mStart; p < quatOffset; p++) {
                d >> *p;
            }
        }

        if (mCompression >= kCompressQuats) {
            char *rotXOffset = mStart + mOffsets[TYPE_ROTX];
            for (char *p = mStart + mOffsets[TYPE_QUAT]; p < rotXOffset; p += 4) {
                d >> p[0] >> p[1] >> p[2] >> p[3];
            }
        } else if (mCompression != kCompressNone) {
            short *rotXOffset = (short *)(mStart + mOffsets[TYPE_ROTX]);
            for (short *p = (short *)(mStart + mOffsets[TYPE_QUAT]); p < rotXOffset; p += 4) {
                d >> p[0] >> p[1] >> p[2] >> p[3];
            }
        } else {
            Hmx::Quat *rotXOffset = (Hmx::Quat *)(mStart + mOffsets[TYPE_ROTX]);
            for (Hmx::Quat *p = (Hmx::Quat *)(mStart + mOffsets[TYPE_QUAT]); p < rotXOffset; p++) {
                d >> *p;
            }
        }

        if (mCompression != kCompressNone) {
            short *endOffset = (short *)(mStart + mOffsets[TYPE_END]);
            for (short *p = (short *)(mStart + mOffsets[TYPE_ROTX]); p < endOffset; p++) {
                d >> *p;
            }
        } else {
            float *endOffset = (float *)(mStart + mOffsets[TYPE_END]);
            for (float *p = (float *)(mStart + mOffsets[TYPE_ROTX]); p < endOffset; p++) {
                d >> *p;
            }
        }
    }
#endif
}

int CharBonesSamples::AllocateSize() { return mTotalSize * mNumSamples; }

void CharBonesSamples::RotateBy(CharBones &bones, int i) {
    mStart = &mRawData[mTotalSize * i];
    CharBones::RotateBy(bones);
}

void CharBonesSamples::RotateTo(CharBones &bones, float f1, int i, float f2) {
    mStart = &mRawData[mTotalSize * i];
    CharBones::RotateTo(bones, (1.0f - f2) * f1);
    if (f2 > 0.0f) {
        mStart = &mRawData[mTotalSize * (i + 1)];
        CharBones::RotateTo(bones, f2 * f1);
    }
}

void CharBonesSamples::ScaleAddSample(CharBones &bones, float f1, int i, float f2) {
    mStart = &mRawData[mTotalSize * i];
    CharBones::ScaleAdd(bones, (1.0f - f2) * f1);
    if (f2 > 0.0f) {
        mStart = &mRawData[mTotalSize * (i + 1)];
        CharBones::ScaleAdd(bones, f2 * f1);
    }
}

void CharBonesSamples::ReadCounts(BinStream &bs, int i2) {
    int i = 0;
    int numTypesToRead = Min(7, i2);
    for (; i < numTypesToRead; i++) {
        bs >> mCounts[i];
    }
    for (int numTypesRead = i; numTypesRead < i2; numTypesRead++) {
        int tmp;
        bs >> tmp;
        MILO_ASSERT((tmp - mCounts[NUM_TYPES - 1]) == 0, 0x2af);
    }
    for (; i < 7; i++) {
        mCounts[i] = 0;
    }
}

void CharBonesSamples::Set(
    const std::vector<CharBones::Bone> &bones, int i, CharBones::CompressionType ty
) {
    ClearBones();
    SetCompression(ty);
    mNumSamples = i;
    AddBones(bones);
    MemFree(mRawData);
    mRawData = (char *)MemAlloc(
        AllocateSize(), "CharBonesSamples.cpp", 0x2d, "CharBonesSamples", 0
    );
    mFrames.clear();
}

void CharBonesSamples::Clone(const CharBonesSamples &samp) {
    Set(samp.mBones, samp.mNumSamples, samp.mCompression);
    memcpy(mRawData, samp.mRawData, AllocateSize());
    mFrames = samp.mFrames;
}

void CharBonesSamples::Print() {
    auto samples = mNumSamples;
    auto size = mTotalSize * mNumSamples;
    auto address = mRawData;
    auto compression = mCompression;
    MILO_LOG(
        "samples: %d size: %d address: %x compression %d\n",
        samples,
        size,
        address,
        compression
    );
    if (mNumSamples == 0) {
        TheDebug << "Bones:\n";
        for (int i = 0; i < mBones.size(); i++) {
            TheDebug << "   " << mBones[i].name << "\n";
        }
    }
    for (int i = 0; i < mNumSamples; i++) {
        TheDebug << i << ")\n";
        mStart = mRawData + mTotalSize * i;
        CharBones::Print();
    }
}

void CharBonesSamples::Relativize(CharClip *clip) {
    if (mBones.empty())
        return;

    for (int sample = mNumSamples - 1; sample >= 0; sample--) {
        Bone *bone = &mBones[0];
        mStart = mRawData + sample * mTotalSize;

        if (mCompression >= kCompressVects) {
            for (ShortVector3 *pos = (ShortVector3 *)mStart;
                 pos < (ShortVector3 *)(mStart + mOffsets[TYPE_QUAT]); pos++) {
                float startBeat = clip->StartBeat();
                void *channel = clip->GetChannel(bone->name);
                Vector3 evalPos;
                clip->EvaluateChannel(&evalPos, channel, startBeat);
                float sx = (float)pos->x * (1300.0f / 32767.0f);
                float sy = (float)pos->y * (1300.0f / 32767.0f);
                float sz = (float)pos->z * (1300.0f / 32767.0f);
                Vector3 v;
                v.x = sx - evalPos.x;
                v.y = sy - evalPos.y;
                v.z = sz - evalPos.z;
                pos->Set(v);
                bone++;
            }
        } else {
            for (Vector3 *pos = (Vector3 *)mStart;
                 pos < (Vector3 *)(mStart + mOffsets[TYPE_QUAT]); pos++) {
                float startBeat = clip->StartBeat();
                void *channel = clip->GetChannel(bone->name);
                Vector3 evalPos;
                clip->EvaluateChannel(&evalPos, channel, startBeat);
                pos->x -= evalPos.x;
                pos->y -= evalPos.y;
                pos->z -= evalPos.z;
                bone++;
            }
        }

        if (mCompression >= kCompressQuats) {
            for (ByteQuat *quat = (ByteQuat *)(mStart + mOffsets[TYPE_QUAT]);
                 quat < (ByteQuat *)(mStart + mOffsets[TYPE_ROTX]); quat++) {
                float startBeat = clip->StartBeat();
                void *channel = clip->GetChannel(bone->name);
                Hmx::Quat evalQuat;
                clip->EvaluateChannel(&evalQuat, channel, startBeat);
                Hmx::Matrix3 evalMat, curMat;
                MakeRotMatrix(evalQuat, evalMat);
                FastInvert(evalMat, evalMat);
                Hmx::Quat tempQuat;
                quat->ToQuat(tempQuat);
                MakeRotMatrix(tempQuat, curMat);
                Multiply(curMat, evalMat, curMat);
                tempQuat.Set(curMat);
                quat->Set(tempQuat);
                bone++;
            }
            for (short *rot = (short *)(mStart + mOffsets[TYPE_ROTX]);
                 rot < (short *)(mStart + mOffsets[TYPE_END]); rot++) {
                float startBeat = clip->StartBeat();
                void *channel = clip->GetChannel(bone->name);
                float evalRot;
                clip->EvaluateChannel(&evalRot, channel, startBeat);
                float rotVal = (float)*rot / 1638.4f;
                *rot = MakeShortAng(LimitAng(rotVal - evalRot));
                bone++;
            }
        } else if (mCompression != kCompressNone) {
            for (ShortQuat *quat = (ShortQuat *)(mStart + mOffsets[TYPE_QUAT]);
                 quat < (ShortQuat *)(mStart + mOffsets[TYPE_ROTX]); quat++) {
                float startBeat = clip->StartBeat();
                void *channel = clip->GetChannel(bone->name);
                Hmx::Quat evalQuat;
                clip->EvaluateChannel(&evalQuat, channel, startBeat);
                Hmx::Matrix3 evalMat, curMat;
                MakeRotMatrix(evalQuat, evalMat);
                FastInvert(evalMat, evalMat);
                Hmx::Quat tempQuat;
                quat->ToQuat(tempQuat);
                MakeRotMatrix(tempQuat, curMat);
                Multiply(curMat, evalMat, curMat);
                tempQuat.Set(curMat);
                quat->Set(tempQuat);
                bone++;
            }
            for (short *rot = (short *)(mStart + mOffsets[TYPE_ROTX]);
                 rot < (short *)(mStart + mOffsets[TYPE_END]); rot++) {
                float startBeat = clip->StartBeat();
                void *channel = clip->GetChannel(bone->name);
                float evalRot;
                clip->EvaluateChannel(&evalRot, channel, startBeat);
                float rotVal = (float)*rot / 1638.4f;
                *rot = MakeShortAng(LimitAng(rotVal - evalRot));
                bone++;
            }
        } else {
            for (Hmx::Quat *quat = (Hmx::Quat *)(mStart + mOffsets[TYPE_QUAT]);
                 quat < (Hmx::Quat *)(mStart + mOffsets[TYPE_ROTX]); quat++) {
                float startBeat = clip->StartBeat();
                void *channel = clip->GetChannel(bone->name);
                Hmx::Quat evalQuat;
                clip->EvaluateChannel(&evalQuat, channel, startBeat);
                Hmx::Matrix3 evalMat, curMat;
                MakeRotMatrix(evalQuat, evalMat);
                FastInvert(evalMat, evalMat);
                MakeRotMatrix(*quat, curMat);
                Multiply(curMat, evalMat, curMat);
                quat->Set(curMat);
                bone++;
            }
            for (float *rot = (float *)(mStart + mOffsets[TYPE_ROTX]);
                 rot < (float *)(mStart + mOffsets[TYPE_END]); rot++) {
                float startBeat = clip->StartBeat();
                void *channel = clip->GetChannel(bone->name);
                float evalRot;
                clip->EvaluateChannel(&evalRot, channel, startBeat);
                *rot = LimitAng(*rot - evalRot);
                bone++;
            }
        }
    }
}

int CharBonesSamples::FracToSample(float *frac) const {
    if (mNumSamples < 2) {
        *frac = 0.0f;
        return 0;
    }
    float inputFrac = *frac;
    float clampedFrac = Clamp(0.0f, 1.0f, inputFrac);
    *frac = clampedFrac;
    int total = Max((int)mFrames.size(), mNumSamples);
    float scaledPos = clampedFrac * (total - 1);
    *frac = scaledPos;
    int sampleIdx = scaledPos;
    if (sampleIdx >= total - 1) {
        *frac = 0.0f;
        return mNumSamples - 1;
    }
    float interpFrac = scaledPos - sampleIdx;
    *frac = interpFrac;
    int ret = sampleIdx;
    if (mFrames.size() != 0) { // sometimes accessing mFrames at 0x50? wtf is going on
        float frame = mFrames[sampleIdx];
        float nextFrame = mFrames[sampleIdx + 1];
        float interpFrame = frame + (nextFrame - frame) * interpFrac;
        ret = interpFrame;
        *frac = interpFrame - ret;
    }
    if (ret < 0 || ret >= mNumSamples) {
        MILO_NOTIFY_ONCE(
            "FracToSample: sample is %d, clip only has %d samples, frac was %g, is %g",
            ret,
            mNumSamples,
            inputFrac,
            *frac
        );
        ret = 0;
    }
    if (*frac < 0.0f || *frac >= 1.0f) {
        MILO_NOTIFY_ONCE("FracToSample: frac is %g, outside of 0 and 1", *frac);
        *frac = 0.0f;
    }
    return ret;
}

void CharBonesSamples::EvaluateChannel(void *dest, int byteOffset, int sample, float frac) {
    int clampedSample = Clamp(0, mNumSamples - 1, sample);
    const char *sampleData = mRawData + mTotalSize * clampedSample;
    const char *nextData = mRawData + mTotalSize * Min(clampedSample + 1, mNumSamples - 1);
    const char *src = sampleData + byteOffset;
    const char *srcNext = nextData + byteOffset;

    if (byteOffset < mOffsets[TYPE_QUAT]) {
        // Position or scale channel
        if (mCompression >= kCompressVects) {
            const ShortVector3 *sv = (const ShortVector3 *)src;
            const ShortVector3 *svNext = (const ShortVector3 *)srcNext;
            Vector3 *out = (Vector3 *)dest;
            float scale = 1300.0f / 32767.0f;
            float invFrac = 1.0f - frac;
            out->x = ((float)sv->x * invFrac + (float)svNext->x * frac) * scale;
            out->y = ((float)sv->y * invFrac + (float)svNext->y * frac) * scale;
            out->z = ((float)sv->z * invFrac + (float)svNext->z * frac) * scale;
        } else {
            const Vector3 *v = (const Vector3 *)src;
            const Vector3 *vNext = (const Vector3 *)srcNext;
            Vector3 *out = (Vector3 *)dest;
            float invFrac = 1.0f - frac;
            out->x = v->x * invFrac + vNext->x * frac;
            out->y = v->y * invFrac + vNext->y * frac;
            out->z = v->z * invFrac + vNext->z * frac;
        }
    } else if (byteOffset < mOffsets[TYPE_ROTX]) {
        // Quaternion channel
        Hmx::Quat q0, q1;
        if (mCompression >= kCompressQuats) {
            ((const ByteQuat *)src)->ToQuat(q0);
            ((const ByteQuat *)srcNext)->ToQuat(q1);
        } else if (mCompression != kCompressNone) {
            ((const ShortQuat *)src)->ToQuat(q0);
            ((const ShortQuat *)srcNext)->ToQuat(q1);
        } else {
            q0 = *(const Hmx::Quat *)src;
            q1 = *(const Hmx::Quat *)srcNext;
        }
        // Slerp
        float dot = q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w;
        if (dot < 0.0f) {
            q1.x = -q1.x; q1.y = -q1.y; q1.z = -q1.z; q1.w = -q1.w;
            dot = -dot;
        }
        Hmx::Quat *out = (Hmx::Quat *)dest;
        float invFrac = 1.0f - frac;
        out->x = q0.x * invFrac + q1.x * frac;
        out->y = q0.y * invFrac + q1.y * frac;
        out->z = q0.z * invFrac + q1.z * frac;
        out->w = q0.w * invFrac + q1.w * frac;
    } else {
        // Rotation (float or short) channel
        if (mCompression != kCompressNone) {
            float v0 = (float)*(const short *)src * (1.0f / 1638.4f);
            float v1 = (float)*(const short *)srcNext * (1.0f / 1638.4f);
            *(float *)dest = v0 * (1.0f - frac) + v1 * frac;
        } else {
            float v0 = *(const float *)src;
            float v1 = *(const float *)srcNext;
            *(float *)dest = v0 * (1.0f - frac) + v1 * frac;
        }
    }
}

void CharBonesSamples::Save(BinStream &bs) {
    SAVE_REVS(0x10, 0)
    int numBones = mBones.size();
    bs << numBones;
    for (int i = 0; i < numBones; i++) {
        bs << mBones[i];
    }
    // Write 7 counts (since rev 0x10 > 0xF)
    for (int i = 0; i < 7; i++) {
        bs << mCounts[i];
    }
    bs << (int)mCompression;
    bs << mNumSamples;
    bs << mFrames;
    // LoadData section (no rev==0xE bool since rev is 0x10)
    for (int i = 0; i < mNumSamples; i++) {
        mStart = mRawData + mTotalSize * i;

        if (mCompression >= kCompressVects) {
            short *quatOffset = (short *)(mStart + mOffsets[TYPE_QUAT]);
            for (short *p = (short *)mStart; p < quatOffset; p += 3) {
                bs << p[0] << p[1] << p[2];
            }
        } else {
            Vector3 *quatOffset = (Vector3 *)(mStart + mOffsets[TYPE_QUAT]);
            for (Vector3 *p = (Vector3 *)mStart; p < quatOffset; p++) {
                bs << *p;
            }
        }

        if (mCompression >= kCompressQuats) {
            char *rotXOffset = mStart + mOffsets[TYPE_ROTX];
            for (char *p = mStart + mOffsets[TYPE_QUAT]; p < rotXOffset; p += 4) {
                bs << p[0] << p[1] << p[2] << p[3];
            }
        } else if (mCompression != kCompressNone) {
            short *rotXOffset = (short *)(mStart + mOffsets[TYPE_ROTX]);
            for (short *p = (short *)(mStart + mOffsets[TYPE_QUAT]); p < rotXOffset; p += 4) {
                bs << p[0] << p[1] << p[2] << p[3];
            }
        } else {
            Hmx::Quat *rotXOffset = (Hmx::Quat *)(mStart + mOffsets[TYPE_ROTX]);
            for (Hmx::Quat *p = (Hmx::Quat *)(mStart + mOffsets[TYPE_QUAT]); p < rotXOffset; p++) {
                bs << *p;
            }
        }

        if (mCompression != kCompressNone) {
            short *endOffset = (short *)(mStart + mOffsets[TYPE_END]);
            for (short *p = (short *)(mStart + mOffsets[TYPE_ROTX]); p < endOffset; p++) {
                bs << *p;
            }
        } else {
            float *endOffset = (float *)(mStart + mOffsets[TYPE_END]);
            for (float *p = (float *)(mStart + mOffsets[TYPE_ROTX]); p < endOffset; p++) {
                bs << *p;
            }
        }
    }
}

extern CharBones *gPropBones;

BEGIN_PROPSYNCS(CharBonesSamples)
    SYNC_PROP(num_samples, mNumSamples)
    SYNC_PROP(frames, mFrames)
    SYNC_PROP_SET(compression, mCompression, )
    gPropBones = this;
    SYNC_PROP(bones, mBones)
END_PROPSYNCS
