#include "char/CharBones.h"
#include "char/CharClip.h"
#include "math/Mtx.h"
#include "math/Rot.h"
#include "math/Vec.h"
#include "os/Debug.h"
#include "utl/BinStream.h"
#include "utl/MakeString.h"
#include "obj/Object.h"
#include "utl/MemMgr.h"

CharBones *gPropBones;

short MakeShortAng(float f) {
    f = f * 1638.4f + 0.5f;
    MILO_ASSERT(f < 32768 && f > -32767, 0x60);
    f = floor(f);
    return f;
}

short ShortVector3::ToShort(float f) {
    // Scale float to short range: divide by 1300 scale factor, multiply by short max (32767),
    // add 0.5 for rounding, clamp to valid range, then floor to convert to integer
    float mult = f * (1.0f / 1300.0f);
    float scaled = mult * 32767.0f;
    float temp = scaled + 0.5f;
    float clamped = Clamp(-32767.0f, 32767.0f, temp);
    return floor(clamped);
}

void ShortVector3::Set(const Vector3 &vec) {
    x = ToShort(vec.x);
    y = ToShort(vec.y);
    z = ToShort(vec.z);
}

void ShortQuat::Set(const Hmx::Quat &quat) {
    x = (short)floor(Clamp(-32767.0f, 32767.0f, quat.x * 32767.0f + 0.5f));
    y = (short)floor(Clamp(-32767.0f, 32767.0f, quat.y * 32767.0f + 0.5f));
    z = (short)floor(Clamp(-32767.0f, 32767.0f, quat.z * 32767.0f + 0.5f));
    w = (short)floor(Clamp(-32767.0f, 32767.0f, quat.w * 32767.0f + 0.5f));
}

void ShortQuat::ToQuat(Hmx::Quat &quat) const {
    quat.Set(
        (float)(long long)x * 3.051851e-05f,
        (float)(long long)y * 3.051851e-05f,
        (float)(long long)z * 3.051851e-05f,
        (float)(long long)w * 3.051851e-05f
    );
}

void ByteQuat::ToQuat(Hmx::Quat &quat) const {
    quat.Set(
        (float)(long long)x * 0.0078740157f,
        (float)(long long)y * 0.0078740157f,
        (float)(long long)z * 0.0078740157f,
        (float)(long long)w * 0.0078740157f
    );
}

void ByteQuat::Set(const Hmx::Quat &quat) {
    x = (char)floor(Clamp(-127.0f, 127.0f, quat.x * 127.0f + 0.5f));
    y = (char)floor(Clamp(-127.0f, 127.0f, quat.y * 127.0f + 0.5f));
    z = (char)floor(Clamp(-127.0f, 127.0f, quat.z * 127.0f + 0.5f));
    w = (char)floor(Clamp(-127.0f, 127.0f, quat.w * 127.0f + 0.5f));
}

void CharBones::Zero() {
#ifdef HX_NATIVE
    if (!mStart) return;
#endif
    memset(mStart, 0, mTotalSize);
}

int CharBones::TypeSize(int i) const {
    switch (i) {
    case TYPE_POS:
    case TYPE_SCALE:
        if (mCompression >= kCompressVects)
            return 6;
        else
            return sizeof(Vector3);
    case TYPE_QUAT:
        if (mCompression >= kCompressQuats)
            return 4;
        else if (mCompression != kCompressNone)
            return 8;
        else
            return sizeof(Hmx::Quat);

    default:
        if (mCompression != kCompressNone)
            return 2;
        else
            return 4;
    }
}

void CharBones::RecomputeSizes() {
#ifdef HX_NATIVE
    // The original code uses offset[-7] to reach mCounts from mOffsets via
    // pointer arithmetic. On LP64, padding between mCounts and mOffsets may
    // break this assumption. Use direct member access instead.
    mOffsets[0] = 0;
    for (int i = 0; i < TYPE_END; i++) {
        int count_diff = mCounts[i + 1] - mCounts[i];
        mOffsets[i + 1] = mOffsets[i] + TypeSize(i) * count_diff;
    }
    mTotalSize = (mOffsets[TYPE_END] + 0xFU) & 0xFFFFFFF0;
#else
    int i = 0;
    int *offset = &mOffsets[0];
    *offset = 0;
    do {
        int cur_offset = *offset;
        // offset[-7] = mCounts[i], offset[-6] = mCounts[i+1]
        // (mCounts is 7 ints (0x1C bytes) before mOffsets)
        int count_diff = offset[-6] - offset[-7];
        *++offset = cur_offset + TypeSize(i) * count_diff;
        i++;
    } while (i < NUM_TYPES);
    // Round up to nearest 0x10 for alignment
    mTotalSize = mOffsets[TYPE_END] + 0xFU & 0xFFFFFFF0;
#endif
}

void CharBones::SetCompression(CompressionType ty) {
    if (ty != mCompression) {
        mCompression = ty;
        RecomputeSizes();
    }
}

CharBones::Type CharBones::TypeOf(Symbol s) {
    const char *p = s.Str();
    char c = *p;
    while (c != 0) {
        if (c == '.') {
            p++;
            switch (*p) {
            case 'p':
                return TYPE_POS;
            case 's':
                return TYPE_SCALE;
            case 'q':
                return TYPE_QUAT;
            case 'r': {
                // check if rot is x, y, or z
                char next = p[3];
                if (next >= 'x' && next <= 'z')
                    return (Type)(next - 'u');
            }
            default:
                break;
            }
        }
        c = *++p;
    }
    MILO_FAIL("Unknown bone suffix in %s", (String &)s);
    return NUM_TYPES;
}

const char *CharBones::SuffixOf(CharBones::Type t) {
    static const char *suffixes[NUM_TYPES] = { "pos",  "scale", "quat",
                                               "rotx", "roty",  "rotz" };
    MILO_ASSERT(t < TYPE_END, 0x66);
    return suffixes[t];
}

Symbol CharBones::ChannelName(const char *cc, CharBones::Type t) {
    MILO_ASSERT(t < TYPE_END, 0x6F);
    char buf[256];
    strcpy(buf, cc);
    char *chr = strchr(buf, '.');
    if (!chr) {
        chr = buf + strlen(buf);
        *chr = '.';
    }
    strcpy(chr + 1, SuffixOf(t));
    return Symbol(buf);
}

int CharBones::FindOffset(Symbol s) const {
    Type ty = TypeOf(s);
    int nextcount = mCounts[ty + 1];
    int size = TypeSize(ty);
    int count = mCounts[ty];
    int offset = mOffsets[ty];
    for (int i = count; i < nextcount; i++, offset += size) {
        if (mBones[i].name == s)
            return offset;
    }
    return -1;
}

void CharBones::SetWeights(float wt, std::vector<Bone> &bones) {
    for (int i = 0; i < bones.size(); i++) {
        bones[i].weight = wt;
    }
}

void *CharBones::FindPtr(Symbol s) const {
    int offset = FindOffset(s);
    if (offset == -1)
        return 0;
    else
        return (void *)&mStart[offset];
}

void CharBones::Print() {
    for (auto it = mBones.begin(); it != mBones.end(); ++it) {
        MILO_LOG("%s %.2f: %s\n", it->name, it->weight, StringVal(it->name));
    }
}

BinStream &operator<<(BinStream &bs, const CharBones::Bone &bone) {
    bs << bone.name;
    bs << bone.weight;
    return bs;
}

BinStream &operator>>(BinStream &bs, CharBones::Bone &bone) {
    bs >> bone.name;
    bs >> bone.weight;
    return bs;
}

void CharBones::SetWeights(float f) { SetWeights(f, mBones); }

BEGIN_CUSTOM_PROPSYNC(CharBones::Bone)
    SYNC_PROP(name, o.name)
    SYNC_PROP(weight, o.weight)
    SYNC_PROP_SET(preview_val, gPropBones->StringVal(o.name), )
END_CUSTOM_PROPSYNC

void CharBones::ListBones(std::list<Bone> &bones) const {
    for (int i = 0; i < mBones.size(); i++) {
        bones.push_back(mBones[i]);
    }
}

void CharBones::AddBones(const std::vector<Bone> &vec) {
    for (std::vector<Bone>::const_iterator it = vec.begin(); it != vec.end(); ++it) {
        AddBoneInternal(*it);
    }
    ReallocateInternal();
}

void CharBones::AddBones(const std::list<Bone> &bones) {
    for (std::list<Bone>::const_iterator it = bones.begin(); it != bones.end(); ++it) {
        AddBoneInternal(*it);
    }
    ReallocateInternal();
}

void CharBones::ClearBones() {
    mBones.clear();
    for (int i = 0; i < NUM_TYPES; i++) {
        mCounts[i] = 0;
        mOffsets[i] = 0;
    }
    mTotalSize = 0;
    mCompression = kCompressNone;
    ReallocateInternal();
}

void TestDstComplain(Symbol s) {
    MILO_NOTIFY_ONCE("src %s not in dst, punting animation", s);
}

CharBones::CharBones() : mCompression(kCompressNone), mStart(0), mTotalSize(0) {
    for (int i = 0; i < NUM_TYPES; i++) {
        mCounts[i] = 0;
        mOffsets[i] = 0;
    }
}

BEGIN_PROPSYNCS(CharBonesObject)
    gPropBones = this;
    SYNC_PROP(bones, mBones)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

void CharBones::ScaleAdd(CharClip *clip, float f1, float f2, float f3) {
    clip->ScaleAdd(*this, f1, f2, f3);
}

void CharBones::AddBoneInternal(const Bone &bone) {
    CharBones::Type t = TypeOf(bone.name);
    int idx;
    for (idx = mCounts[t]; idx < mCounts[t + 1]; idx++) {
        if (mBones[idx].name == bone.name) {
            return;
        }
        if (strcmp(mBones[idx].name.Str(), bone.name.Str()) >= 0) {
            break;
        }
    }
    mBones.insert(mBones.begin() + idx, bone);
    int size = TypeSize(t);
    for (t = (Type)(t + 1); t < CharBones::NUM_TYPES; t = (Type)(t + 1)) {
        mCounts[t]++;
        mOffsets[t] += size;
    }
    mTotalSize = mOffsets[TYPE_END] + 0xFU & 0xFFFFFFF0; // round up to the nearest 0x10,
                                                         // alignment moment
}

const char *CharBones::StringVal(Symbol s) {
    void *ptr = FindPtr(s);
    CharBones::Type t = TypeOf(s);
    switch (t) {
    case TYPE_POS:
    case TYPE_SCALE:
        if (mCompression >= kCompressVects) {
            Vector3 vshort((short *)ptr);
            return MakeString("%g %g %g", vshort.x, vshort.y, vshort.z);
        } else {
            Vector3 *vptr = (Vector3 *)ptr;
            return MakeString("%g %g %g", vptr->x, vptr->y, vptr->z);
        }
    case TYPE_QUAT: {
        Hmx::Quat q;
        Hmx::Quat *qPtr = (Hmx::Quat *)ptr;
        if (mCompression >= kCompressQuats) {
            ByteQuat *bqPtr = (ByteQuat *)qPtr;
            bqPtr->ToQuat(q);
        } else if (mCompression != kCompressNone) {
            ShortQuat *sqPtr = (ShortQuat *)qPtr;
            sqPtr->ToQuat(q);
        } else
            q = *qPtr;
        Vector3 v40;
        MakeEuler(q, v40);
        v40 *= RAD2DEG;
        return MakeString(
            "quat(%g %g %g %g) euler(%g %g %g)", q.x, q.y, q.z, q.w, v40.x, v40.y, v40.z
        );
    }
    default: {
        float floatVal;
        if (mCompression != kCompressNone) {
            floatVal = *((short *)ptr) * 0.00061035156f;
        } else {
            floatVal = *((float *)ptr);
        }
        floatVal *= RAD2DEG;
        if (mCompression != kCompressNone) {
            return MakeString("deg %g raw %d", floatVal, *((short *)ptr));
        } else {
            return MakeString("deg %g rad %g", floatVal, *((float *)ptr));
        }
    }
    }
}

void CharBones::ScaleAddIdentity() {
    Hmx::Quat *qend = (Hmx::Quat *)(mStart + mOffsets[TYPE_ROTX]);
    Bone *bone = mBones.data() + mCounts[TYPE_QUAT];
    Hmx::Quat *qstart = (Hmx::Quat *)(mStart + mOffsets[TYPE_QUAT]);
    if (qstart == qend) return;
    do {
        float identity = 1.0f - bone->weight;
        float w = qstart->w;
        if (w < 0.0f) {
            w -= identity;
        } else {
            w += identity;
        }
        qstart->w = w;
        qstart++;
        bone++;
    } while (qstart != qend);
}

// MARK: ScaleDown
void CharBones::ScaleDown(CharBones &bones, float f2) const {
    if (!mBones.empty()) {
        Bone *myBonesItr = (Bone *)&mBones[0];
        if (f2 == 0) {
            if (mCounts[TYPE_QUAT] > mCounts[TYPE_POS]) {
                Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_POS]];
                Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_QUAT]];
                Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_QUAT]];
                Vector3 *otherVecItr = (Vector3 *)bones.mStart;
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherVecItr++;
                    }
                    myBonesItr++;
                    otherVecItr->Zero();
                    otherBonesItr->weight = 0;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherVecItr++;
                }
            }
            if (mCounts[TYPE_ROTX] > mCounts[TYPE_QUAT]) {
                Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_QUAT]];
                Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_ROTX]];
                Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_ROTX]];
                Hmx::Quat *otherQuatItr =
                    (Hmx::Quat *)(bones.mStart + bones.mOffsets[TYPE_QUAT]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherQuatItr++;
                    }
                    myBonesItr++;
                    otherQuatItr->Set(0, 0, 0, 0);
                    otherBonesItr->weight = 0;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherQuatItr++;
                }
            }
            if (mCounts[TYPE_END] > mCounts[TYPE_ROTX]) {
                Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_ROTX]];
                Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_END]];
                Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_END]];
                float *otherRotItr = (float *)(bones.mStart + bones.mOffsets[TYPE_ROTX]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherRotItr++;
                    }
                    myBonesItr++;
                    *otherRotItr = 0;
                    otherBonesItr->weight = 0;
                    if (myBonesItr == myBonesEnd) {
                        return;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherRotItr++;
                }
            }
        } else {
            if (mCounts[TYPE_QUAT] > mCounts[TYPE_POS]) {
                Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_POS]];
                Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_QUAT]];
                Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_QUAT]];
                Vector3 *otherVecItr = (Vector3 *)bones.mStart;
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherVecItr++;
                    }
                    myBonesItr++;
                    *otherVecItr *= f2;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherVecItr++;
                }
            }
            if (mCounts[TYPE_ROTX] > mCounts[TYPE_QUAT]) {
                Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_QUAT]];
                Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_ROTX]];
                Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_ROTX]];
                Hmx::Quat *otherQuatItr =
                    (Hmx::Quat *)(bones.mStart + bones.mOffsets[TYPE_QUAT]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherQuatItr++;
                    }
                    myBonesItr++;
                    otherQuatItr->Set(
                        otherQuatItr->x * f2,
                        otherQuatItr->y * f2,
                        otherQuatItr->z * f2,
                        otherQuatItr->w * f2
                    );
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherQuatItr++;
                }
            }
            if (mCounts[TYPE_END] > mCounts[TYPE_ROTX]) {
                Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_ROTX]];
                Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_END]];
                Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_END]];
                float *otherRotItr = (float *)(bones.mStart + bones.mOffsets[TYPE_ROTX]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherRotItr++;
                    }
                    myBonesItr++;
                    *otherRotItr *= f2;
                    if (myBonesItr == myBonesEnd) {
                        return;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherRotItr++;
                }
            }
        }
    }
}

// MARK: Blend
void CharBones::Blend(CharBones &bones) const {
    MILO_ASSERT(!mCompression && !bones.mCompression, 0x311);
    if (!mBones.empty()) {
        Bone *myBonesItr = (Bone *)&mBones[0];
        if (mCounts[TYPE_QUAT] > mCounts[TYPE_POS]) {
            Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_POS]];
            Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_QUAT]];
            Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_QUAT]];
            Vector3 *myVecItr = (Vector3 *)mStart;
            Vector3 *otherVecItr = (Vector3 *)bones.mStart;
            while (true) {
                while (otherBonesItr->name != myBonesItr->name) {
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherVecItr++;
                }
                *otherVecItr *= 1 - myBonesItr->weight;
                *otherVecItr += *myVecItr;
                myBonesItr++;
                if (myBonesItr == myBonesEnd) {
                    break;
                }
                otherBonesItr++;
                if (otherBonesItr >= otherBonesEnd) {
                    TestDstComplain(myBonesItr->name);
                    return;
                }
                otherVecItr++;
                myVecItr++;
            }
        }
        if (mCounts[TYPE_ROTX] > mCounts[TYPE_QUAT]) {
            Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_QUAT]];
            Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_ROTX]];
            Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_ROTX]];
            Hmx::Quat *otherQuatItr = (Hmx::Quat *)(bones.mStart + bones.mOffsets[TYPE_QUAT]);
            Hmx::Quat *myQuatItr = (Hmx::Quat *)(mStart + mOffsets[TYPE_QUAT]);
            while (true) {
                while (otherBonesItr->name != myBonesItr->name) {
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherQuatItr++;
                }
                float scalar = 1 - myBonesItr->weight;
                otherQuatItr->x *= scalar;
                otherQuatItr->y *= scalar;
                otherQuatItr->z *= scalar;
                otherQuatItr->w *= scalar;
                float abs = fabsf(myBonesItr->weight);
                Hmx::Quat q(
                    myQuatItr->x * abs,
                    myQuatItr->y * abs,
                    myQuatItr->z * abs,
                    myQuatItr->w * myBonesItr->weight
                );
                if (q * *otherQuatItr < 0) {
                    otherQuatItr->x -= q.x;
                    otherQuatItr->y -= q.y;
                    otherQuatItr->z -= q.z;
                    otherQuatItr->w -= q.w;
                } else {
                    otherQuatItr->x += q.x;
                    otherQuatItr->y += q.y;
                    otherQuatItr->z += q.z;
                    otherQuatItr->w += q.w;
                }
                myBonesItr++;
                if (myBonesItr == myBonesEnd) {
                    break;
                }
                otherBonesItr++;
                if (otherBonesItr >= otherBonesEnd) {
                    TestDstComplain(myBonesItr->name);
                    return;
                }
                otherQuatItr++;
                myQuatItr++;
            }
        }
        if (mCounts[TYPE_END] > mCounts[TYPE_ROTX]) {
            Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_ROTX]];
            Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_END]];
            Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_END]];
            float *otherRotItr = (float *)(bones.mStart + bones.mOffsets[TYPE_ROTX]);
            float *myRotItr = (float *)(mStart + mOffsets[TYPE_ROTX]);
            while (true) {
                while (otherBonesItr->name != myBonesItr->name) {
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherRotItr++;
                }
                *otherRotItr *= 1 - myBonesItr->weight;
                *otherRotItr += *myRotItr * myBonesItr->weight;
                myBonesItr++;
                if (myBonesItr == myBonesEnd) {
                    return;
                }
                otherBonesItr++;
                if (otherBonesItr >= otherBonesEnd) {
                    TestDstComplain(myBonesItr->name);
                    return;
                }
                otherRotItr++;
                myRotItr++;
            }
        }
    }
}

// MARK: ScaleAdd (CharBones)
void CharBones::ScaleAdd(CharBones &bones, float f2) const {
    if (!mBones.empty()) {
        Bone *myBonesItr = (Bone *)&mBones[0];
        if (mCounts[TYPE_QUAT] > mCounts[TYPE_POS]) {
            Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_POS]];
            Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_QUAT]];
            Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_QUAT]];
            Vector3 *otherVecItr = (Vector3 *)bones.mStart;
            if (mCompression >= kCompressVects) {
                ShortVector3 *myVecItr = (ShortVector3 *)mStart;
                while (true) {
                    Vector3 v;
                    myVecItr->ToVector3(v);
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherVecItr++;
                    }
                    ScaleAddEq(*otherVecItr, v, f2);
                    otherBonesItr->weight += myBonesItr->weight * f2;
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherVecItr++;
                    myVecItr++;
                }
            } else {
                Vector3 *myVecItr = (Vector3 *)mStart;
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherVecItr++;
                    }
                    ScaleAddEq(*otherVecItr, *myVecItr, f2);
                    otherBonesItr->weight += myBonesItr->weight * f2;
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherVecItr++;
                    myVecItr++;
                }
            }
        }
        if (mCounts[TYPE_ROTX] > mCounts[TYPE_QUAT]) {
            float f2abs = fabsf(f2);
            Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_QUAT]];
            Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_ROTX]];
            Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_ROTX]];
            Hmx::Quat *otherQuatItr = (Hmx::Quat *)(bones.mStart + bones.mOffsets[TYPE_QUAT]);
            if (mCompression >= kCompressQuats) {
                float absConstant = f2abs * 0.007874016f;
                float notAbsConstant = f2 * 0.007874016f;
                ByteQuat *myQuatItr = (ByteQuat *)(mStart + mOffsets[TYPE_QUAT]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherQuatItr++;
                    }
                    Hmx::Quat q;
                    q.Set(
                        myQuatItr->x * absConstant,
                        myQuatItr->y * absConstant,
                        myQuatItr->z * absConstant,
                        myQuatItr->w * notAbsConstant
                    );
                    if (q * *otherQuatItr < 0) {
                        otherQuatItr->x -= q.x;
                        otherQuatItr->y -= q.y;
                        otherQuatItr->z -= q.z;
                        otherQuatItr->w -= q.w;
                    } else {
                        otherQuatItr->x += q.x;
                        otherQuatItr->y += q.y;
                        otherQuatItr->z += q.z;
                        otherQuatItr->w += q.w;
                    }
                    otherBonesItr->weight += myBonesItr->weight * f2;
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherQuatItr++;
                    myQuatItr++;
                }
            } else if (mCompression != kCompressNone) {
                float absConstant = f2abs * 0.000030518509f;
                float notAbsConstant = f2 * 0.000030518509f;
                ShortQuat *myQuatItr = (ShortQuat *)(mStart + mOffsets[TYPE_QUAT]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherQuatItr++;
                    }
                    Hmx::Quat q;
                    q.Set(
                        myQuatItr->x * absConstant,
                        myQuatItr->y * absConstant,
                        myQuatItr->z * absConstant,
                        myQuatItr->w * notAbsConstant
                    );
                    if (q * *otherQuatItr < 0) {
                        otherQuatItr->x -= q.x;
                        otherQuatItr->y -= q.y;
                        otherQuatItr->z -= q.z;
                        otherQuatItr->w -= q.w;
                    } else {
                        otherQuatItr->x += q.x;
                        otherQuatItr->y += q.y;
                        otherQuatItr->z += q.z;
                        otherQuatItr->w += q.w;
                    }
                    otherBonesItr->weight += myBonesItr->weight * f2;
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherQuatItr++;
                    myQuatItr++;
                }
            } else {
                Hmx::Quat *myQuatItr = (Hmx::Quat *)(mStart + mOffsets[TYPE_QUAT]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherQuatItr++;
                    }
                    Hmx::Quat q;
                    q.Set(
                        myQuatItr->x * f2abs,
                        myQuatItr->y * f2abs,
                        myQuatItr->z * f2abs,
                        myQuatItr->w * f2
                    );
                    if (q * *otherQuatItr < 0) {
                        otherQuatItr->x -= q.x;
                        otherQuatItr->y -= q.y;
                        otherQuatItr->z -= q.z;
                        otherQuatItr->w -= q.w;
                    } else {
                        otherQuatItr->x += q.x;
                        otherQuatItr->y += q.y;
                        otherQuatItr->z += q.z;
                        otherQuatItr->w += q.w;
                    }
                    otherBonesItr->weight += myBonesItr->weight * f2;
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherQuatItr++;
                    myQuatItr++;
                }
            }
        }
        if (mCounts[TYPE_END] > mCounts[TYPE_ROTX]) {
            Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_ROTX]];
            Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_END]];
            Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_END]];
            float *otherRotItr = (float *)(bones.mStart + bones.mOffsets[TYPE_ROTX]);
            if (mCompression != kCompressNone) {
                float shortConstant = f2 * 0.00061035156f;
                short *myRotItr = (short *)(mStart + mOffsets[TYPE_ROTX]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherRotItr++;
                    }
                    *otherRotItr += *myRotItr * shortConstant;
                    otherBonesItr->weight += myBonesItr->weight * f2;
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        return;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherRotItr++;
                    myRotItr++;
                }
            } else {
                float *myRotItr = (float *)(mStart + mOffsets[TYPE_ROTX]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherRotItr++;
                    }
                    *otherRotItr += *myRotItr * f2;
                    otherBonesItr->weight += myBonesItr->weight * f2;
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        return;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherRotItr++;
                    myRotItr++;
                }
            }
        }
    }
}

// MARK: RotateBy
void CharBones::RotateBy(CharBones &bones) const {
    if (!mBones.empty()) {
        Bone *myBonesItr = (Bone *)&mBones[0];
        if (mCounts[TYPE_QUAT] > mCounts[TYPE_POS]) {
            Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_POS]];
            Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_QUAT]];
            Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_QUAT]];
            Vector3 *otherVecItr = (Vector3 *)bones.mStart;
            if (mCompression >= kCompressVects) {
                ShortVector3 *myVecItr = (ShortVector3 *)mStart;
                while (true) {
                    Vector3 v;
                    myVecItr->ToVector3(v);
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (myBonesItr && otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherVecItr++;
                    }
                    *otherVecItr += v;
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherVecItr++;
                    myVecItr++;
                }
            } else {
                Vector3 *myVecItr = (Vector3 *)mStart;
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherVecItr++;
                    }
                    *otherVecItr += *myVecItr;
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherVecItr++;
                    myVecItr++;
                }
            }
        }
        if (mCounts[TYPE_ROTX] > mCounts[TYPE_QUAT]) {
            Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_QUAT]];
            Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_ROTX]];
            Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_ROTX]];
            Hmx::Quat *otherQuatItr = (Hmx::Quat *)(bones.mStart + bones.mOffsets[TYPE_QUAT]);
            if (mCompression >= kCompressQuats) {
                ByteQuat *myQuatItr = (ByteQuat *)(mStart + mOffsets[TYPE_QUAT]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherQuatItr++;
                    }
                    Hmx::Quat q;
                    myQuatItr->ToQuat(q);
                    Multiply(q, *otherQuatItr, *otherQuatItr);
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherQuatItr++;
                    myQuatItr++;
                }
            } else if (mCompression != kCompressNone) {
                ShortQuat *myQuatItr = (ShortQuat *)(mStart + mOffsets[TYPE_QUAT]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherQuatItr++;
                    }
                    Hmx::Quat q;
                    myQuatItr->ToQuat(q);
                    Multiply(q, *otherQuatItr, *otherQuatItr);
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherQuatItr++;
                    myQuatItr++;
                }
            } else {
                Hmx::Quat *myQuatItr = (Hmx::Quat *)(mStart + mOffsets[TYPE_QUAT]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherQuatItr++;
                    }
                    Multiply(*myQuatItr, *otherQuatItr, *otherQuatItr);
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherQuatItr++;
                    myQuatItr++;
                }
            }
        }
        if (mCounts[TYPE_END] > mCounts[TYPE_ROTX]) {
            Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_ROTX]];
            Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_END]];
            Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_END]];
            float *otherRotItr = (float *)(bones.mStart + bones.mOffsets[TYPE_ROTX]);
            if (mCompression != kCompressNone) {
                short *myRotItr = (short *)(mStart + mOffsets[TYPE_ROTX]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherRotItr++;
                    }
                    *otherRotItr += *myRotItr * 0.00061035156f;
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        return;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherRotItr++;
                    myRotItr++;
                }
            } else {
                float *myRotItr = (float *)(mStart + mOffsets[TYPE_ROTX]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherRotItr++;
                    }
                    *otherRotItr += *myRotItr;
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        return;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherRotItr++;
                    myRotItr++;
                }
            }
        }
    }
}

// MARK: RotateTo
void CharBones::RotateTo(CharBones &bones, float f2) const {
    if (!mBones.empty()) {
        Bone *myBonesItr = (Bone *)&mBones[0];
        if (mCounts[TYPE_QUAT] > mCounts[TYPE_POS]) {
            Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_POS]];
            Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_QUAT]];
            Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_QUAT]];
            Vector3 *otherVecItr = (Vector3 *)bones.mStart;
            if (mCompression >= kCompressVects) {
                ShortVector3 *myVecItr = (ShortVector3 *)mStart;
                while (true) {
                    Vector3 v;
                    myVecItr->ToVector3(v);
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherVecItr++;
                    }
                    ScaleAddEq(*otherVecItr, v, f2);
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherVecItr++;
                    myVecItr++;
                }
            } else {
                Vector3 *myVecItr = (Vector3 *)mStart;
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherVecItr++;
                    }
                    ScaleAddEq(*otherVecItr, *myVecItr, f2);
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherVecItr++;
                    myVecItr++;
                }
            }
        }
        if (mCounts[TYPE_ROTX] > mCounts[TYPE_QUAT]) {
            Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_QUAT]];
            Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_ROTX]];
            Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_ROTX]];
            Hmx::Quat *otherQuatItr = (Hmx::Quat *)(bones.mStart + bones.mOffsets[TYPE_QUAT]);
            if (mCompression >= kCompressQuats) {
                ByteQuat *myQuatItr = (ByteQuat *)(mStart + mOffsets[TYPE_QUAT]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherQuatItr++;
                    }
                    Hmx::Quat q;
                    myQuatItr->ToQuat(q);
                    q.x *= f2;
                    q.y *= f2;
                    q.z *= f2;
                    if (q.w < 0) {
                        q.w = (q.w * f2) - (1 - f2);
                    } else {
                        q.w = (q.w * f2) + (1 - f2);
                    }
                    Multiply(*otherQuatItr, q, *otherQuatItr);
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherQuatItr++;
                    myQuatItr++;
                }
            } else if (mCompression != kCompressNone) {
                ShortQuat *myQuatItr = (ShortQuat *)(mStart + mOffsets[TYPE_QUAT]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherQuatItr++;
                    }
                    Hmx::Quat q;
                    myQuatItr->ToQuat(q);
                    q.x *= f2;
                    q.y *= f2;
                    q.z *= f2;
                    if (q.w < 0) {
                        q.w = (q.w * f2) - (1 - f2);
                    } else {
                        q.w = (q.w * f2) + (1 - f2);
                    }
                    Multiply(*otherQuatItr, q, *otherQuatItr);
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherQuatItr++;
                    myQuatItr++;
                }
            } else {
                Hmx::Quat *myQuatItr = (Hmx::Quat *)(mStart + mOffsets[TYPE_QUAT]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherQuatItr++;
                    }
                    Hmx::Quat q;
                    q.Set(
                        myQuatItr->x * f2,
                        myQuatItr->y * f2,
                        myQuatItr->z * f2,
                        myQuatItr->w * f2
                    );
                    if (myQuatItr->w < 0) {
                        q.w -= (1 - f2);
                    } else {
                        q.w += (1 - f2);
                    }
                    Multiply(*otherQuatItr, q, *otherQuatItr);
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        break;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherQuatItr++;
                    myQuatItr++;
                }
            }
        }
        if (mCounts[TYPE_END] > mCounts[TYPE_ROTX]) {
            Bone *otherBonesItr = (Bone *)&bones.mBones[bones.mCounts[TYPE_ROTX]];
            Bone *otherBonesEnd = (Bone *)&bones.mBones[bones.mCounts[TYPE_END]];
            Bone *myBonesEnd = (Bone *)&mBones[mCounts[TYPE_END]];
            float *otherRotItr = (float *)(bones.mStart + bones.mOffsets[TYPE_ROTX]);
            if (mCompression != kCompressNone) {
                float shortConstant = f2 * 0.00061035156f;
                short *myRotItr = (short *)(mStart + mOffsets[TYPE_ROTX]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherRotItr++;
                    }
                    *otherRotItr += *myRotItr * shortConstant;
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        return;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherRotItr++;
                    myRotItr++;
                }
            } else {
                float *myRotItr = (float *)(mStart + mOffsets[TYPE_ROTX]);
                while (true) {
                    while (otherBonesItr->name != myBonesItr->name) {
                        otherBonesItr++;
                        if (otherBonesItr >= otherBonesEnd) {
                            TestDstComplain(myBonesItr->name);
                            return;
                        }
                        otherRotItr++;
                    }
                    *otherRotItr += *myRotItr * f2;
                    myBonesItr++;
                    if (myBonesItr == myBonesEnd) {
                        return;
                    }
                    otherBonesItr++;
                    if (otherBonesItr >= otherBonesEnd) {
                        TestDstComplain(myBonesItr->name);
                        return;
                    }
                    otherRotItr++;
                    myRotItr++;
                }
            }
        }
    }
}

CharBonesAlloc::~CharBonesAlloc() {
    MemFree(mStart);
}

void CharBonesAlloc::ReallocateInternal() {
    MemFree(mStart);
    mStart = (char *)MemAlloc(mTotalSize, __FILE__, 0x6C0, "CharBones");
}
