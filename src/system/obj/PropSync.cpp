#include "obj\PropSync.h"
#include "math\Color.h"
#include "math/Geo.h"
#include "math\Mtx.h"
#include "math\Rot.h"
#include "math\Sphere.h"
#include "obj\Data.h"
#include "os\Debug.h"
#include "os\File.h"
#include "utl\FilePath.h"

bool PropSync(class String &str, DataNode &node, DataArray *prop, int i, PropOp op) {
    MILO_ASSERT(i == prop->Size() && (op & (kPropSet|kPropGet|kPropInsert)), 0x12);
    if (op == kPropGet)
        node = str.c_str();
    else
        str = node.Str();
    return true;
}

bool PropSync(FilePath &fp, DataNode &node, DataArray *prop, int i, PropOp op) {
    MILO_ASSERT(i == prop->Size() && (op & (kPropSet|kPropGet|kPropInsert)), 0x1C);
    if (op == kPropGet)
        node = FileRelativePath(FilePath::Root().c_str(), fp.c_str());
    else {
        const char *str = node.Str();
        fp.Set(FilePath::Root().c_str(), str);
    }
    return true;
}

bool PropSync(Hmx::Color &color, DataNode &node, DataArray *prop, int i, PropOp op) {
    MILO_ASSERT(i == prop->Size() && (op & (kPropSet|kPropGet|kPropInsert)), 0x26);
    if (op == kPropGet)
        node = (int)color.Pack();
    else
        color.Unpack(node.Int());
    return true;
}

bool PropSync(Vector2 &vec, DataNode &node, DataArray *prop, int i, PropOp op) {
    if (i == prop->Size())
        return true;
    else {
        Symbol sym = prop->Sym(i);
        {
            static Symbol x("x");
            if (sym == x) {
                return PropSync(vec.x, node, prop, i + 1, op);
            }
        }
        {
            static Symbol y("y");
            if (sym == y) {
                return PropSync(vec.y, node, prop, i + 1, op);
            }
        }
        return false;
    }
}

bool PropSync(Vector3 &vec, DataNode &node, DataArray *prop, int i, PropOp op) {
    if (i == prop->Size())
        return true;
    else {
        Symbol sym = prop->Sym(i);
        {
            static Symbol x("x");
            if (sym == x) {
                return PropSync(vec.x, node, prop, i + 1, op);
            }
        }
        {
            static Symbol y("y");
            if (sym == y) {
                return PropSync(vec.y, node, prop, i + 1, op);
            }
        }
        {
            static Symbol z("z");
            if (sym == z) {
                return PropSync(vec.z, node, prop, i + 1, op);
            }
        }
        return false;
    }
}

// w8-l: 99.979004 normalized, the only function short of 100% in this unit
// (11/12).  12 charged rows out of 381: four fmuls with their two operands
// exchanged ([186] f11/f13, [197] f13/f12, [314] and [371] f12/f0) and six
// lfs's reading the sibling component (+4/-8), all inside the y_scale and
// z_scale arms.  The x_scale arm, which is spelled component-wise
// (_m.x.x/_m.x.y/_m.x.z), already matches.
// Refuted: spelling y_scale and z_scale component-wise too, to mirror x_scale,
// is a large REGRESSION -- 99.979004 -> 98.745410.  The image really does use
// the whole-vector `_m.y *= ratio` / `_m.z *= ratio` form for those two arms
// and the per-component form only for x; the asymmetry below is deliberate.
bool PropSync(Hmx::Matrix3 &_m, DataNode &_val, DataArray *_prop, int _i, PropOp _op) {
    MILO_ASSERT(_i == _prop->Size() - 1 && (_op & (kPropSet|kPropGet|kPropInsert)), 0x4F);
    Symbol sym = _prop->Sym(_i);
    bool ret = false;
    Vector3 euler, scale;
    {
        static Symbol pitch("pitch");
        if (sym == pitch) {
            MakeEulerScale(_m, euler, scale);
            Scale(euler, RAD2DEG, euler);
            ret = PropSync(euler.x, _val, _prop, _i + 1, _op);
        }
    }
    {
        static Symbol roll("roll");
        if (sym == roll) {
            MakeEulerScale(_m, euler, scale);
            Scale(euler, RAD2DEG, euler);
            ret = PropSync(euler.y, _val, _prop, _i + 1, _op);
        }
    }
    {
        static Symbol yaw("yaw");
        if (sym == yaw) {
            MakeEulerScale(_m, euler, scale);
            Scale(euler, RAD2DEG, euler);
            ret = PropSync(euler.z, _val, _prop, _i + 1, _op);
        }
    }
    if (!ret || _op == kPropGet) {
        {
            static Symbol x_scale("x_scale");
            if (sym == x_scale) {
                // w18-d: the three lengths are spelled out rather than through
                // Length(): the extra inline level made MSVC lead the sum with z
                // (y/z_scale) instead of the image's y-first schedule. 99.979 ->
                // 99.995; x_scale still loads z before x (0x825BDD80/84), and the
                // y.y/z.y products of Scale() and of `_m.y *= ratio` keep the
                // other operand order (4 commutative rows); per-component
                // `_m.y.x *= ratio` loses the r29 = &_m.y binding (97.8).
                // w22-a25: the image sums (y*y + x*x) + z*z (0x825BDD74..
                // 0x825BDD9C: fmuls y*y, fmadds x, fmadds z); the flat spelling
                // let /fp:fast build (y*y + z*z) + x*x instead. The parens keep
                // the image's association (normalized 99.890 -> 99.995); only
                // which of x*x / y*y is fused remains. Native (no fast-math,
                // no FMA) computes (x*x + y*y) + z*z either way.
                float len = std::sqrt((_m.x.x * _m.x.x + _m.x.y * _m.x.y) + _m.x.z * _m.x.z);
                float oldLen = len;
                ret = PropSync(len, _val, _prop, _i + 1, _op);
                if (_op != kPropGet) {
                    if (oldLen == 0.0f) {
                        _m.Identity();
                        oldLen = 1.0f;
                    }
                    float ratio = len / oldLen;
                    _m.x.x *= ratio;
                    _m.x.y *= ratio;
                    _m.x.z *= ratio;
                }
            }
        }
        {
            static Symbol y_scale("y_scale");
            if (sym == y_scale) {
                float len = std::sqrt(_m.y.x * _m.y.x + _m.y.y * _m.y.y + _m.y.z * _m.y.z);
                float oldLen = len;
                ret = PropSync(len, _val, _prop, _i + 1, _op);
                if (_op != kPropGet) {
                    if (oldLen == 0.0f) {
                        _m.Identity();
                        oldLen = 1.0f;
                    }
                    float ratio = len / oldLen;
                    _m.y *= ratio;
                }
            }
        }
        {
            static Symbol z_scale("z_scale");
            if (sym == z_scale) {
                float len = std::sqrt(_m.z.x * _m.z.x + _m.z.y * _m.z.y + _m.z.z * _m.z.z);
                float oldLen = len;
                ret = PropSync(len, _val, _prop, _i + 1, _op);
                if (_op != kPropGet) {
                    if (oldLen == 0.0f) {
                        _m.Identity();
                        oldLen = 1.0f;
                    }
                    float ratio = len / oldLen;
                    _m.z *= ratio;
                }
            }
        }
    } else {
        Scale(euler, DEG2RAD, euler);
        MakeRotMatrix(euler, _m, true);
        Scale(scale, _m, _m);
    }
    return ret;
}

bool PropSync(Transform &tf, DataNode &node, DataArray *prop, int i, PropOp op) {
    if (i == prop->Size())
        return true;
    else {
        Symbol sym = prop->Sym(i);
        {
            static Symbol x("x");
            if (sym == x) {
                return PropSync(tf.v.x, node, prop, i + 1, op);
            }
        }
        {
            static Symbol y("y");
            if (sym == y) {
                return PropSync(tf.v.y, node, prop, i + 1, op);
            }
        }
        {
            static Symbol z("z");
            if (sym == z) {
                return PropSync(tf.v.z, node, prop, i + 1, op);
            }
        }
        return PropSync(tf.m, node, prop, i, op) != false;
    }
}

bool PropSync(Sphere &sphere, DataNode &node, DataArray *prop, int i, PropOp op) {
    if (i == prop->Size())
        return true;
    else {
        Symbol sym = prop->Sym(i);
        {
            static Symbol x("x");
            if (sym == x) {
                return PropSync(sphere.center.x, node, prop, i + 1, op);
            }
        }
        {
            static Symbol y("y");
            if (sym == y) {
                return PropSync(sphere.center.y, node, prop, i + 1, op);
            }
        }
        {
            static Symbol z("z");
            if (sym == z) {
                return PropSync(sphere.center.z, node, prop, i + 1, op);
            }
        }
        {
            static Symbol radius("radius");
            if (sym == radius) {
                return PropSync(sphere.radius, node, prop, i + 1, op);
            }
        }
        return false;
    }
}

bool PropSync(Hmx::Rect &rect, DataNode &node, DataArray *prop, int i, PropOp op) {
    if (i == prop->Size())
        return true;
    else {
        Symbol sym = prop->Sym(i);
        {
            static Symbol x("x");
            if (sym == x) {
                return PropSync(rect.x, node, prop, i + 1, op);
            }
        }
        {
            static Symbol y("y");
            if (sym == y) {
                return PropSync(rect.y, node, prop, i + 1, op);
            }
        }
        {
            static Symbol w("w");
            if (sym == w) {
                return PropSync(rect.w, node, prop, i + 1, op);
            }
        }
        {
            static Symbol h("h");
            if (sym == h) {
                return PropSync(rect.h, node, prop, i + 1, op);
            }
        }
        return false;
    }
}

bool PropSync(Box &box, DataNode &node, DataArray *prop, int i, PropOp op) {
    if (i == prop->Size())
        return true;
    else {
        Symbol sym = prop->Sym(i);
        {
            static Symbol min("min");
            if (sym == min) {
                return PropSync(box.mMin, node, prop, i + 1, op);
            }
        }
        {
            static Symbol max("max");
            if (sym == max) {
                return PropSync(box.mMax, node, prop, i + 1, op);
            }
        }
        return false;
    }
}
