#include "char/CharBonesSamples.h"

#include "CharClip.h"
#include "math/Mtx.h"
#include "math/Rot.h"
#include "math/Trig.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "utl/ChunkStream.h"
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
    mRawData = (char *)MemAlloc(
        AllocateSize(), "CharBonesSamples.cpp", 0x2d1, "CharBonesSamples", 0
    );
}

void CharBonesSamples::LoadData(BinStreamRev &d) {
    if (d.rev == 0xE) {
        bool x;
        d >> x;
    }
#ifdef HX_NATIVE
    // Cached .milo_xbox files pad each uncompressed Vector3 to 16 bytes
    // (12 data + 4 zero), but in-memory layout uses sizeof(Vector3)=12.
    // When positions are uncompressed (compression < kCompressVects),
    // we must read element-by-element to skip the on-disk padding.
    bool cachedPaddingMismatch = d.stream.Cached() && mCompression < kCompressVects;
    if (d.stream.Cached() && !cachedPaddingMismatch) {
        int totalBytes = AllocateSize();
        if (totalBytes > 0 && mRawData) {
            d.stream.Read(mRawData, totalBytes);
        }
        // Byte-swap big-endian (Xbox 360) data to native little-endian
        for (int i = 0; i < mNumSamples; i++) {
            unsigned char *base = (unsigned char *)(mRawData + mTotalSize * i);

            // Positions + Scales [0, mOffsets[TYPE_QUAT])
            if (mCompression >= kCompressVects) {
                for (int j = 0; j < mOffsets[TYPE_QUAT]; j += 2) {
                    unsigned char t = base[j]; base[j] = base[j+1]; base[j+1] = t;
                }
            } else {
                for (int j = 0; j < mOffsets[TYPE_QUAT]; j += 4) {
                    unsigned char t;
                    t = base[j]; base[j] = base[j+3]; base[j+3] = t;
                    t = base[j+1]; base[j+1] = base[j+2]; base[j+2] = t;
                }
            }

            // Quaternions [mOffsets[TYPE_QUAT], mOffsets[TYPE_ROTX])
            if (mCompression >= kCompressQuats) {
                // ByteQuat: 1-byte elements, no swap needed
            } else if (mCompression != kCompressNone) {
                // ShortQuat: 2-byte shorts
                for (int j = mOffsets[TYPE_QUAT]; j < mOffsets[TYPE_ROTX]; j += 2) {
                    unsigned char t = base[j]; base[j] = base[j+1]; base[j+1] = t;
                }
            } else {
                // Hmx::Quat: 4-byte floats
                for (int j = mOffsets[TYPE_QUAT]; j < mOffsets[TYPE_ROTX]; j += 4) {
                    unsigned char t;
                    t = base[j]; base[j] = base[j+3]; base[j+3] = t;
                    t = base[j+1]; base[j+1] = base[j+2]; base[j+2] = t;
                }
            }

            // Rotations [mOffsets[TYPE_ROTX], mOffsets[TYPE_END])
            if (mCompression != kCompressNone) {
                for (int j = mOffsets[TYPE_ROTX]; j < mOffsets[TYPE_END]; j += 2) {
                    unsigned char t = base[j]; base[j] = base[j+1]; base[j+1] = t;
                }
            } else {
                for (int j = mOffsets[TYPE_ROTX]; j < mOffsets[TYPE_END]; j += 4) {
                    unsigned char t;
                    t = base[j]; base[j] = base[j+3]; base[j+3] = t;
                    t = base[j+1]; base[j+1] = base[j+2]; base[j+2] = t;
                }
            }
        }
    } else if (cachedPaddingMismatch) {
    // Cached stream with uncompressed positions: read element-by-element
    // but skip 4-byte padding after each Vector3 position/scale
    for (int i = 0; i < mNumSamples; i++) {
        mStart = mRawData + mTotalSize * Min(i, mNumSamples - 1);

        // Positions + Scales: 16 bytes on disk (12 data + 4 pad), 12 in memory
        {
            Vector3 *quatOffset = (Vector3 *)(mStart + mOffsets[TYPE_QUAT]);
            for (Vector3 *p = (Vector3 *)mStart; p < quatOffset; p++) {
                d >> *p;
                float pad; d >> pad; // skip 4-byte zero padding
            }
        }

        // Quaternions
        if (mCompression >= kCompressQuats) {
            char *rotXOffset = mStart + mOffsets[TYPE_ROTX];
            for (char *p = mStart + mOffsets[TYPE_QUAT]; p < rotXOffset; p += 4) {
                d.stream.Read(p, 4);
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

        // Rotations
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

        // Skip per-sample end padding (cached aligns to 16)
        {
            int dataSize = mOffsets[TYPE_END] - mOffsets[TYPE_POS];
            int delta = ((dataSize + 0xF) & ~0xF) - dataSize;
            if (delta > 0) {
                char skip[16];
                d.stream.Read(skip, delta);
            }
        }
    }
    } else {
    // Non-cached: element-by-element
#else
    {
#endif
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
                d.stream.Read(p, 4);
            }
        } else if (mCompression != kCompressNone) {
            short *rotXOffset = (short *)(mStart + mOffsets[TYPE_ROTX]);
            for (short *p = (short *)(mStart + mOffsets[TYPE_QUAT]); p < rotXOffset; p += 4) {
                d >> p[0] >> p[1] >> p[2] >> p[3];
            }
        } else {
            Hmx::Quat *rotXOffset = (Hmx::Quat *)(mStart + mOffsets[TYPE_ROTX]);
            for (Hmx::Quat *p = (Hmx::Quat *)(mStart + mOffsets[TYPE_QUAT]); p < rotXOffset;
                 p++) {
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
    } // end non-cached / #else block
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
    char *src = mRawData + mTotalSize * sample + byteOffset;
    if (frac == 0.0f) {
        if (byteOffset >= mOffsets[TYPE_ROTX]) {
            float val;
            if (mCompression != kCompressNone) {
                val = (float)*(short *)src * (1.0f / 1638.4f);
            } else {
                val = *(float *)src;
            }
            *(float *)dest = val;
            return;
        }
        int comp = mCompression;
        if (byteOffset >= mOffsets[TYPE_QUAT]) {
            if (comp >= kCompressQuats) {
                ((const ByteQuat *)src)->ToQuat(*(Hmx::Quat *)dest);
                return;
            }
            if (comp != kCompressNone) {
                ((const ShortQuat *)src)->ToQuat(*(Hmx::Quat *)dest);
                return;
            }
        } else if (comp >= kCompressVects) {
            short *sv = (short *)src;
            float *out = (float *)dest;
            float scale = 1300.0f / 32767.0f;
            out[2] = (float)sv[2] * scale;
            out[0] = (float)sv[0] * scale;
            out[1] = (float)sv[1] * scale;
            return;
        }
        int *out = (int *)dest;
        int *v = (int *)src;
        out[0] = v[0];
        out[1] = v[1];
        out[2] = v[2];
        out[3] = v[3];
    } else {
        char *srcNext = src + mTotalSize;
        if (byteOffset >= mOffsets[TYPE_ROTX]) {
            float v0, v1;
            if (mCompression != kCompressNone) {
                v0 = (float)*(short *)src;
                v1 = (float)*(short *)srcNext;
                *(float *)dest = (v0 + (v1 - v0) * frac) * (1.0f / 1638.4f);
            } else {
                v0 = *(float *)src;
                v1 = *(float *)srcNext;
                *(float *)dest = v0 + (v1 - v0) * frac;
            }
            return;
        }
        int comp = mCompression;
        if (byteOffset >= mOffsets[TYPE_QUAT]) {
            float *out = (float *)dest;
            if (comp >= kCompressQuats) {
                Hmx::Quat q0, q1;
                ((const ByteQuat *)src)->ToQuat(q0);
                ((const ByteQuat *)srcNext)->ToQuat(q1);
                out[0] = q0.x + (q1.x - q0.x) * frac;
                out[1] = q0.y + (q1.y - q0.y) * frac;
                out[2] = q0.z + (q1.z - q0.z) * frac;
                out[3] = q0.w + (q1.w - q0.w) * frac;
            } else if (comp != kCompressNone) {
                Hmx::Quat q0, q1;
                ((const ShortQuat *)src)->ToQuat(q0);
                ((const ShortQuat *)srcNext)->ToQuat(q1);
                out[0] = q0.x + (q1.x - q0.x) * frac;
                out[1] = q0.y + (q1.y - q0.y) * frac;
                out[2] = q0.z + (q1.z - q0.z) * frac;
                out[3] = q0.w + (q1.w - q0.w) * frac;
            } else {
                float *s0 = (float *)src;
                float *s1 = (float *)srcNext;
                out[0] = s0[0] + (s1[0] - s0[0]) * frac;
                out[1] = s0[1] + (s1[1] - s0[1]) * frac;
                out[2] = s0[2] + (s1[2] - s0[2]) * frac;
                out[3] = s0[3] + (s1[3] - s0[3]) * frac;
            }
        } else {
            if (comp >= kCompressVects) {
                float scale = 1300.0f / 32767.0f;
                short *s0 = (short *)src;
                short *s1 = (short *)srcNext;
                Vector3 sv0, sv1;
                sv0.Set((float)s0[0] * scale, (float)s0[1] * scale, (float)s0[2] * scale);
                sv1.Set((float)s1[0] * scale, (float)s1[1] * scale, (float)s1[2] * scale);
                Interp(sv0, sv1, frac, *(Vector3 *)dest);
            } else {
                Interp(*(const Vector3 *)src, *(const Vector3 *)srcNext, frac, *(Vector3 *)dest);
            }
        }
    }
}

void CharBonesSamples::Save(BinStream &bs) {
    SAVE_REVS(0x10, 0)
    bs << mBones;
    for (int i = 0; i < NUM_TYPES; i++) {
        bs << mCounts[i];
    }
    bs << (int)mCompression;
    bs << mNumSamples;
    bs << mFrames;

    bool cached = bs.Cached() && (bs.GetPlatform() == kPlatformPS3 || bs.GetPlatform() == kPlatformXBox);
    int delta = 0;
    if (cached) {
        int dataSize = mOffsets[TYPE_END] - mOffsets[TYPE_POS];
        delta = ((dataSize + 0xF) & ~0xF) - dataSize;
        MILO_ASSERT(delta >= 0 && delta < 16, 0x24c);
    }

    for (unsigned int i = 0; i < (unsigned int)mNumSamples; i++) {
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
                if (cached) {
                    float zero = 0.0f;
                    bs << zero;
                }
            }
        }

        if (mCompression >= kCompressQuats) {
            char *rotXOffset = mStart + mOffsets[TYPE_ROTX];
            for (char *p = mStart + mOffsets[TYPE_QUAT]; p < rotXOffset; p += 4) {
                char b;
                b = p[0]; bs.Write(&b, 1);
                b = p[1]; bs.Write(&b, 1);
                b = p[2]; bs.Write(&b, 1);
                b = p[3]; bs.Write(&b, 1);
            }
        } else if (mCompression != kCompressNone) {
            short *rotXOffset = (short *)(mStart + mOffsets[TYPE_ROTX]);
            for (short *p = (short *)(mStart + mOffsets[TYPE_QUAT]); p < rotXOffset; p += 4) {
                bs << p[0] << p[1] << p[2] << p[3];
            }
        } else {
            Vector4 *rotXOffset = (Vector4 *)(mStart + mOffsets[TYPE_ROTX]);
            for (Vector4 *p = (Vector4 *)(mStart + mOffsets[TYPE_QUAT]); p < rotXOffset; p++) {
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

        if (cached) {
            long long pad = 0;
            bs.Write(&pad, delta);
        }

        if (bs.GetPlatform() == kPlatformWii && (i & 0x7F) == 0x7F) {
            MarkChunk(bs);
        }
    }
}

extern CharBones *gPropBones;

BEGIN_PROPSYNCS(CharBonesSamples)
    SYNC_PROP(num_samples, mNumSamples)
    {
        static Symbol _s("preview_sample");
        if (sym == _s) {
            if (_op == kPropSet) {
                int val = _val.Int(0);
                int clamped = mNumSamples - 1;
                if (val <= clamped) {
                    clamped = Clamp(0, mNumSamples - 1, val);
                }
                mPreviewSample = clamped;
                mStart = mRawData + mTotalSize * clamped;
            } else if (_op == kPropSize) {
                return false;
            } else {
                _val = DataNode(mPreviewSample);
            }
            return true;
        }
    }
    SYNC_PROP(frames, mFrames)
    SYNC_PROP_SET(compression, mCompression, )
    gPropBones = this;
    SYNC_PROP(bones, mBones)
END_PROPSYNCS
