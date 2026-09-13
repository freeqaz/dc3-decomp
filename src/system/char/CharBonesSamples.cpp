#include "char\CharBonesSamples.h"

#include "CharClip.h"
#include "math\Mtx.h"
#include "math\Rot.h"
#include "math\Trig.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "os\Timer.h"
#include "utl\ChunkStream.h"
#include "utl\MemMgr.h"
#ifdef HX_NATIVE
#include <cstdlib> // getenv (DC3_CLIP_SWAP_LEGACY / DC3_CLIP_SWAP_DIAG)
#include <cstring> // strstr
#include <cstdio>  // fprintf
#endif
#line 8 "CharBonesSamples.cpp"
CharBonesSamples::CharBonesSamples()
    : mNumSamples(0), mPreviewSample(0), mRawData(nullptr) {}

CharBonesSamples::~CharBonesSamples() { MemFree(mRawData); }

INIT_REVS(0x10, 0)

void CharBonesSamples::Load(BinStream &bs) {
    LOAD_REVS(bs)
    if (0x10 < d.rev) {
        MILO_FAIL(
            "%s can\'t load new %s version %d > %d", "", "CharBonesSample", d.rev, gRev
        );
    }
    if (d.altRev > 0) {
        MILO_FAIL(
            "%s can\'t load new %s alt version %d > %d",
            "",
            "CharBonesSample",
            d.altRev,
            gAltRev
        );
    }
    if (!(d.rev > 12)) {
        TheDebugFailer << MakeString(kAssertStr, __FILE__, 0x29d, "d.rev > 12");
    }
    LoadHeader(d);
    LoadData(d);
}

void CharBonesSamples::LoadHeader(BinStreamRev &d) {
    MemFree(mRawData);
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
        d >> mNumSamples;
    } else {
        int i;
        if (d.rev > 5) {
            int count;
            if (d.rev > 7) {
                count = 9;
            } else if (d.rev > 6) {
                count = 6;
            } else {
                count = 10;
            }
            for (i = 0; i < count; i++) {
                int tmp;
                d >> tmp;
            }
            d >> (int &)mCompression;
            d >> mNumSamples;
        } else {
            d >> mNumSamples;
            if (d.rev > 3) {
                d >> (int &)mCompression;
            }
        }
        for (i = 0; i < 7; i++) {
            mCounts[i] = 0;
        }
        for (i = 0; i < mBones.size(); i++) {
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
        AllocateSize(), "CharBonesSamples.cpp", 0x301, "CharBonesSamples", 0
    );
}

#ifdef HX_NATIVE
// Byte-swap one raw clip sample's big-endian channel section to native LE.
// compWidth is the section's per-component element width and MUST mirror
// CharBones::TypeSize for the active compression: 4=float, 2=short, 1=byte
// (ByteQuat is byte-addressable, so width 1 = no swap). Shared by LoadData's
// cached and ReadChunks paths so both stay consistent.
static void SwapBE_Section(char *base, int len, int compWidth) {
    if (compWidth == 2) {
        for (char *p = base; p < base + len; p += 2) {
            unsigned short *u = (unsigned short *)p;
            *u = __builtin_bswap16(*u);
        }
    } else if (compWidth == 4) {
        for (char *p = base; p < base + len; p += 4) {
            unsigned int *u = (unsigned int *)p;
            *u = __builtin_bswap32(*u);
        }
    }
}
#endif

void CharBonesSamples::LoadData(BinStreamRev &d) {
    if (d.rev == 0xE) {
        bool x;
        d >> x;
    }
    bool cached = d.stream.Cached();
    auto& _sub1 = mOffsets[TYPE_QUAT];
    auto& _sub0 = mOffsets[TYPE_END];
#ifdef HX_NATIVE
    if (getenv("DC3_CLIP_SWAP_DIAG")) {
        bool toeQ = false, toeR = false;
        for (int bi = 0; bi < (int)mBones.size(); ++bi) {
            const char *nm = mBones[bi].name.Str();
            if (strstr(nm, "toe.quat")) toeQ = true;
            if (strstr(nm, "toe.rotz")) toeR = true;
        }
        fprintf(stderr,
            "DC3_CLIPDIAG: comp=%d nSamp=%d nBones=%d rev=%d cached=%d path=%s "
            "off[POS=%d QUAT=%d ROTX=%d END=%d] total=%d toeQuat=%d toeRotz=%d\n",
            (int)mCompression, mNumSamples, (int)mBones.size(), d.rev, (int)cached,
            (!cached || d.rev <= 0xE) ? "inline" : "readchunks",
            mOffsets[TYPE_POS], _sub1, mOffsets[TYPE_ROTX], _sub0, mTotalSize,
            (int)toeQ, (int)toeR);
    }
#endif
    if (!cached || d.rev <= 0xE) {
        for (int i = 0; i < mNumSamples; i++) {
            auto _tmp0 = Min(i, mNumSamples - 1);
            mStart = mRawData + mTotalSize * _tmp0;

            if (cached) {
                d.stream.Read(mStart, _sub0 - mOffsets[TYPE_POS]);
#ifdef HX_NATIVE
                // Cached .milo_xbox files store raw big-endian data. Swap each
                // section using its true component width (mirrors TypeSize):
                //   POS/SCALE: short(2) when >=kCompressVects, else float(4)
                //   QUAT: byte(1,no swap) when >=kCompressQuats, short(2) when
                //         compressed, else float(4)
                //   ROTX..ROTZ: short(2) when compressed, else float(4)
                // (Pre-fix code swapped QUAT as short for >=kCompressRots, which
                // corrupted ByteQuat at kCompressQuats/All, and always swapped POS
                // as float. DC3_CLIP_SWAP_LEGACY restores that for A/B.)
                if (!d.stream.LittleEndian()) {
                    bool legacy = getenv("DC3_CLIP_SWAP_LEGACY") != nullptr;
                    int posW  = legacy ? 4 : ((mCompression >= kCompressVects) ? 2 : 4);
                    int quatW = legacy ? ((mCompression >= kCompressRots) ? 2 : 4)
                                       : ((mCompression >= kCompressQuats) ? 1
                                          : (mCompression != kCompressNone) ? 2 : 4);
                    int rotW  = (mCompression != kCompressNone) ? 2 : 4;
                    SwapBE_Section(mStart, _sub1 - mOffsets[TYPE_POS], posW);
                    SwapBE_Section(mStart + _sub1, mOffsets[TYPE_ROTX] - _sub1, quatW);
                    SwapBE_Section(mStart + mOffsets[TYPE_ROTX], _sub0 - mOffsets[TYPE_ROTX], rotW);
                }
#endif
            } else {
                if (mCompression >= kCompressVects) {
                    short *quatOffset = (short *)(mStart + _sub1);
                    for (short *p = (short *)mStart; p < quatOffset; p += 3) {
                        d >> p[0] >> p[1] >> p[2];
                    }
                } else {
                    Vector3 *quatOffset = (Vector3 *)(mStart + _sub1);
                    for (Vector3 *p = (Vector3 *)mStart; p < quatOffset; p++) {
                        d >> *p;
                    }
                }

                if (mCompression >= kCompressQuats) {
                    char *rotXOffset = mStart + mOffsets[TYPE_ROTX];
                    for (char *p = mStart + _sub1; p < rotXOffset; p += 4) {
                        d.stream.Read(p, 1);
                        d.stream.Read(p + 1, 1);
                        d.stream.Read(p + 2, 1);
                        d.stream.Read(p + 3, 1);
                    }
                } else if (mCompression != kCompressNone) {
                    short *rotXOffset = (short *)(mStart + mOffsets[TYPE_ROTX]);
                    for (short *p = (short *)(mStart + _sub1); p < rotXOffset;
                         p += 4) {
                        d >> p[0] >> p[1] >> p[2] >> p[3];
                    }
                } else {
                    Hmx::Quat *rotXOffset =
                        (Hmx::Quat *)(mStart + mOffsets[TYPE_ROTX]);
                    for (Hmx::Quat *p = (Hmx::Quat *)(mStart + _sub1);
                         p < rotXOffset; p++) {
                        d >> *p;
                    }
                }

                if (mCompression != kCompressNone) {
                    short *endOffset = (short *)(mStart + _sub0);
                    for (short *p = (short *)(mStart + mOffsets[TYPE_ROTX]); p < endOffset;
                         p++) {
                        d >> *p;
                    }
                } else {
                    float *endOffset = (float *)(mStart + _sub0);
                    for (float *p = (float *)(mStart + mOffsets[TYPE_ROTX]); p < endOffset;
                         p++) {
                        d >> *p;
                    }
                }
            }

            if ((i & 0x7F) == 0x7F) {
#ifdef HX_NATIVE
                d.stream.WaitUntilReady();
#else
                while (d.stream.Eof() == TempEof) {
                    Timer::Sleep(0);
                }
#endif
            }
        }
    } else {
        mStart = mRawData;
        ReadChunks(d.stream, mRawData, mNumSamples * mTotalSize, mTotalSize << 7);
#ifdef HX_NATIVE
        // ReadChunks reads raw big-endian data — byte-swap all samples using
        // each section's true component width (see SwapBE_Section above; mirrors
        // CharBones::TypeSize). DC3_CLIP_SWAP_LEGACY restores the pre-fix widths
        // for A/B (always-float POS + short-QUAT corrupted ByteQuat clips).
        if (!d.stream.LittleEndian()) {
            bool legacy = getenv("DC3_CLIP_SWAP_LEGACY") != nullptr;
            int posW  = legacy ? 4 : ((mCompression >= kCompressVects) ? 2 : 4);
            int quatW = legacy ? ((mCompression >= kCompressRots) ? 2 : 4)
                               : ((mCompression >= kCompressQuats) ? 1
                                  : (mCompression != kCompressNone) ? 2 : 4);
            int rotW  = (mCompression != kCompressNone) ? 2 : 4;
            for (int i = 0; i < mNumSamples; i++) {
                char *s = mRawData + mTotalSize * i;
                SwapBE_Section(s, _sub1 - mOffsets[TYPE_POS], posW);
                SwapBE_Section(s + _sub1, mOffsets[TYPE_ROTX] - _sub1, quatW);
                SwapBE_Section(s + mOffsets[TYPE_ROTX], _sub0 - mOffsets[TYPE_ROTX], rotW);
            }
        }
#endif
    }
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

#ifdef HX_NATIVE
void CharBonesSamples::Dc3DumpPosChannels(int sample, const char *tag) const {
    if (sample < 0)
        sample = 0;
    if (mNumSamples > 0 && sample >= mNumSamples)
        sample = mNumSamples - 1;
    fprintf(stderr,
        "DC3_CLIP_POS %s: comp=%d totalSize=%d numSamples=%d posBones=[%d,%d) sample=%d\n",
        tag, (int)mCompression, mTotalSize, mNumSamples,
        mCounts[TYPE_POS], mCounts[TYPE_SCALE], sample);
    if (!mRawData)
        return;
    const char *base = mRawData + mTotalSize * sample;
    for (int i = mCounts[TYPE_POS]; i < mCounts[TYPE_SCALE]; i++) {
        int off = FindOffset(mBones[i].name);
        if (mCompression >= kCompressVects) {
            const short *s = (const short *)(base + off);
            fprintf(stderr,
                "DC3_CLIP_POS %s [%d] %s off=%d raw=(%d,%d,%d) dec=(%.2f,%.2f,%.2f)\n",
                tag, i, mBones[i].name.Str(), off, s[0], s[1], s[2],
                s[0] * 0.039674062f, s[1] * 0.039674062f, s[2] * 0.039674062f);
        } else {
            const float *f = (const float *)(base + off);
            fprintf(stderr, "DC3_CLIP_POS %s [%d] %s off=%d dec=(%.2f,%.2f,%.2f)\n",
                tag, i, mBones[i].name.Str(), off, f[0], f[1], f[2]);
        }
    }
}
#endif

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
    auto size = mTotalSize * mNumSamples;
    auto address = mRawData;
    auto compression = mCompression;
    MILO_LOG(
        "samples: %d size: %d address: %x compression %d\n",
        mNumSamples,
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
    auto& bones = mBones;
    if (bones.empty())
        return;

    for (int sample = mNumSamples - 1; sample >= 0; sample--) {
        Bone *bone = &bones[0];
        mStart = mRawData + sample * mTotalSize;

        if (mCompression >= kCompressVects) {
            for (ShortVector3 *pos = (ShortVector3 *)mStart;
                 pos < (ShortVector3 *)(mStart + mOffsets[TYPE_QUAT]); pos++) {
                float startBeat = clip->StartBeat();
                void *channel = clip->GetChannel(bone->name);
                Vector3 evalPos;
                clip->EvaluateChannel(&evalPos, channel, startBeat);
                float sx = (float)pos->x * (1300.0 / 32767.0f);
                float sz = (float)pos->z * (1300.0f / 32767.0f);
                float sy = (float)pos->y * (1300.0f / 32767.0f);
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
    } else {
        float old = *frac;
        ClampEq(*frac, 0.0f, 1.0f);
        int total = Max((int)mFrames.size(), mNumSamples);
        float scaledPos = *frac * (total - 1);
        *frac = scaledPos;
        int sampleIdx = scaledPos;
        if (sampleIdx >= total - 1) {
            *frac = 0.0f;
            return mNumSamples - 1;
        } else {
            *frac = scaledPos - sampleIdx;
            if (mFrames.size() != 0) {
                float interp = Interp(mFrames[sampleIdx], mFrames[sampleIdx + 1], *frac);
                sampleIdx = interp;
                *frac = interp - (float)sampleIdx;
            }
            if (sampleIdx < 0 || sampleIdx >= mNumSamples) {
                MILO_NOTIFY_ONCE(
                    "FracToSample: sample is %d, clip only has %d samples, frac was %g, is %g",
                    sampleIdx,
                    mNumSamples,
                    old,
                    *frac
                );
                sampleIdx = 0;
            }
            if (*frac < 0.0f || *frac >= 1.0f) {
                MILO_NOTIFY_ONCE("FracToSample: frac is %g, outside of 0 and 1", *frac);
                *frac = 0.0f;
            }
            return sampleIdx;
        }
    }
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
        short comp = mCompression;
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
            out[0] = (float)sv[0] * scale;
            out[1] = (float)sv[1] * scale;
            out[2] = (float)sv[2] * scale;
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
            Hmx::Quat q0, q1;
            if (comp >= kCompressQuats) {
                ((const ByteQuat *)src)->ToQuat(q0);
                ((const ByteQuat *)srcNext)->ToQuat(q1);
            } else if (comp != kCompressNone) {
                ((const ShortQuat *)src)->ToQuat(q0);
                ((const ShortQuat *)srcNext)->ToQuat(q1);
            } else {
                float *s0 = (float *)src;
                float *s1 = (float *)srcNext;
                out[0] = s0[0] + (s1[0] - s0[0]) * frac;
                out[1] = s0[1] + (s1[1] - s0[1]) * frac;
                out[2] = s0[2] + (s1[2] - s0[2]) * frac;
                out[3] = s0[3] + (s1[3] - s0[3]) * frac;
                goto quat_done;
            }
            out[0] = q0.x + (q1.x - q0.x) * frac;
            out[1] = q0.y + (q1.y - q0.y) * frac;
            out[2] = q0.z + (q1.z - q0.z) * frac;
            out[3] = q0.w + (q1.w - q0.w) * frac;
            quat_done:;
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
    SAVE_REVS(0x10, 0);
    bs << mBones;
    for (int i = 0; i < NUM_TYPES; i++) {
        bs << mCounts[i];
    }
    bs << (int)mCompression;
    bs << mNumSamples;
    bs << mFrames;
    bool check =
        (bs.Cached()
         && (bs.GetPlatform() == kPlatformPS3 || bs.GetPlatform() == kPlatformXBox));
    int delta = 0;
    if (check) {
        delta = (mOffsets[6] - mOffsets[0]);
        delta = (delta + 0xFU & 0xFFFFFFF0) - delta;
        MILO_ASSERT(delta >= 0 && delta < 16, 0x24C);
    }
    for (int i = 0; i < mNumSamples; i++) {
        SetSamplePointers(i);
        if (mCompression >= kCompressVects) {
            ShortVector3 *vecEnd = (ShortVector3 *)(mStart + mOffsets[TYPE_QUAT]);
            for (ShortVector3 *it = (ShortVector3 *)mStart; it < vecEnd; ++it) {
                bs << it->x << it->y << it->z;
            }
        } else {
            Vector3 *vecEnd = (Vector3 *)(mStart + mOffsets[TYPE_QUAT]);
            for (Vector3 *it = (Vector3 *)mStart; it < vecEnd; ++it) {
                bs << *it;
                if (check) {
                    bs << 0.0f;
                }
            }
        }
        if (mCompression >= kCompressQuats) {
            ByteQuat *quatEnd = (ByteQuat *)(mStart + mOffsets[TYPE_ROTX]);
            for (ByteQuat *it = (ByteQuat *)(mStart + mOffsets[TYPE_QUAT]); it < quatEnd;
                 ++it) {
                bs << it->x << it->y << it->z << it->w;
            }
        } else if (mCompression != kCompressNone) {
            ShortQuat *quatEnd = (ShortQuat *)(mStart + mOffsets[TYPE_ROTX]);
            for (ShortQuat *it = (ShortQuat *)(mStart + mOffsets[TYPE_QUAT]); it < quatEnd;
                 ++it) {
                bs << it->x << it->y << it->z << it->w;
            }
        } else {
            Hmx::Quat *quatEnd = (Hmx::Quat *)(mStart + mOffsets[TYPE_ROTX]);
            for (Hmx::Quat *it = (Hmx::Quat *)(mStart + mOffsets[TYPE_QUAT]); it < quatEnd;
                 ++it) {
                bs << *it;
            }
        }
        if (mCompression != kCompressNone) {
            for (short *it = (short *)(mStart + mOffsets[TYPE_ROTX]);
                 it < (short *)(mStart + mOffsets[TYPE_END]);
                 ++it) {
                bs << *it;
            }
        } else {
            for (float *it = (float *)(mStart + mOffsets[TYPE_ROTX]);
                 it < (float *)(mStart + mOffsets[TYPE_END]);
                 ++it) {
                bs << *it;
            }
        }
        if (check) {
            char zero[16];
            memset(zero, 0, 16);
            bs.Write(zero, delta);
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
            } else if (_op == kPropUnknown0x40) {
                // Same guard SYNC_PROP_SET emits (`_op == (PropOp)0x40`), not kPropSize:
                // the target compares against 0x40 here, as it does in the `compression`
                // handler four lines below.
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
