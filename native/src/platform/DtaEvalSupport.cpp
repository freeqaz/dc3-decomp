// DC3 Native Port — DTA eval support: interpreter-state guard + result serializer.
// See DtaEvalSupport.h for the rationale. Native-only; src/ is untouched.

#include "platform/DtaEvalSupport.h"

#include "obj/Data.h"
#include "obj/DataUtl.h"
#include "utl/MemMgr.h"
#include "utl/MemHeap.h"
#include "utl/Symbol.h"

#include <cstdio>
#include <cmath>
#include <cstring>

// gVarStack / gVarStackPtr live in src/system/obj/DataUtl.cpp with external
// linkage but no header declaration. Re-declare with an incomplete type: we
// only ever save and restore the pointer value, never dereference it.
struct VarStack;
extern VarStack *gVarStackPtr;

namespace DtaEval {

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

bool IsValidUtf8(const char *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    size_t i = 0;
    while (i < len) {
        unsigned char c = p[i];
        size_t extra;
        unsigned int cp;
        if (c < 0x80) {
            i++;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            extra = 1;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            cp = c & 0x07;
        } else {
            return false; // continuation byte or 5/6-byte form
        }
        if (i + extra >= len)
            return false; // truncated sequence
        for (size_t k = 1; k <= extra; k++) {
            unsigned char cc = p[i + k];
            if ((cc & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        // Reject overlong encodings, surrogates and out-of-range code points.
        if (extra == 1 && cp < 0x80)
            return false;
        if (extra == 2 && cp < 0x800)
            return false;
        if (extra == 3 && cp < 0x10000)
            return false;
        if (cp > 0x10FFFF)
            return false;
        if (cp >= 0xD800 && cp <= 0xDFFF)
            return false;
        i += extra + 1;
    }
    return true;
}

static void AppendUEscape(std::string &out, unsigned char c) {
    static const char *kHex = "0123456789abcdef";
    out += "\\u00";
    out += kHex[(c >> 4) & 0xF];
    out += kHex[c & 0xF];
}

std::string JsonEscape(const std::string &s) {
    // Fast path decision: if the whole string is valid UTF-8 we can pass
    // multi-byte sequences through untouched; otherwise every high byte is
    // escaped so the result is still valid UTF-8 (and thus valid JSON).
    const bool utf8 = IsValidUtf8(s.data(), s.size());

    std::string out;
    out.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':
            out += "\\\"";
            continue;
        case '\\':
            out += "\\\\";
            continue;
        case '\b':
            out += "\\b";
            continue;
        case '\f':
            out += "\\f";
            continue;
        case '\n':
            out += "\\n";
            continue;
        case '\r':
            out += "\\r";
            continue;
        case '\t':
            out += "\\t";
            continue;
        default:
            break;
        }
        if (c < 0x20 || c == 0x7F) {
            AppendUEscape(out, c); // includes NUL -> \u0000
        } else if (c < 0x80 || utf8) {
            out += (char)c;
        } else {
            AppendUEscape(out, c); // lossy Latin-1 reading of a non-UTF-8 byte
        }
    }
    return out;
}

std::string Base64Encode(const unsigned char *data, size_t len) {
    static const char *kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < len) {
        unsigned int v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += kAlphabet[(v >> 18) & 0x3F];
        out += kAlphabet[(v >> 12) & 0x3F];
        out += kAlphabet[(v >> 6) & 0x3F];
        out += kAlphabet[v & 0x3F];
        i += 3;
    }
    if (i + 1 == len) {
        unsigned int v = data[i] << 16;
        out += kAlphabet[(v >> 18) & 0x3F];
        out += kAlphabet[(v >> 12) & 0x3F];
        out += "==";
    } else if (i + 2 == len) {
        unsigned int v = (data[i] << 16) | (data[i + 1] << 8);
        out += kAlphabet[(v >> 18) & 0x3F];
        out += kAlphabet[(v >> 12) & 0x3F];
        out += kAlphabet[(v >> 6) & 0x3F];
        out += "=";
    }
    return out;
}

std::string JsonFloat(float f) {
    if (!std::isfinite(f))
        return "null"; // JSON has no NaN/Infinity literal
    char buf[64];
    snprintf(buf, sizeof(buf), "%.9g", (double)f);
    // "%g" can emit "1e+09"; that is valid JSON. It can also emit "inf"/"nan"
    // on some libcs, but isfinite() already excluded those.
    return std::string(buf);
}

const char *TypeName(int dataType) {
    switch (dataType) {
    case kDataInt:
        return "int";
    case kDataFloat:
        return "float";
    case kDataVar:
        return "var";
    case kDataFunc:
        return "func";
    case kDataObject:
        return "object";
    case kDataSymbol:
        return "symbol";
    case kDataUnhandled:
        return "unhandled";
    case kDataIfdef:
        return "ifdef";
    case kDataElse:
        return "else";
    case kDataEndif:
        return "endif";
    case kDataArray:
        return "array";
    case kDataCommand:
        return "command";
    case kDataString:
        return "string";
    case kDataProperty:
        return "property";
    case kDataGlob:
        return "glob";
    case kDataDefine:
        return "define";
    case kDataInclude:
        return "include";
    case kDataMerge:
        return "merge";
    case kDataIfndef:
        return "ifndef";
    case kDataAutorun:
        return "autorun";
    case kDataUndef:
        return "undef";
    default:
        return "unknown";
    }
}

// Header shared by every serialized node: {"type":"...","typeId":N
static std::string NodeHeader(int type) {
    return std::string("{\"type\":\"") + TypeName(type) +
        "\",\"typeId\":" + std::to_string(type);
}

// kDataString / kDataGlob store their bytes in the DataArray's mNodes block,
// with mSize = -byteCount. mNodes is the first member, which is exactly how
// DataNode::LiteralStr() reaches the characters.
static const char *GlobBytes(const DataArray *arr) {
    return reinterpret_cast<const DataNode *>(arr)->UncheckedStr();
}

// Emit "encoding" + "value" (+ "bytes") for a raw byte range.
static std::string BytesJson(const char *data, size_t len) {
    std::string out;
    if (IsValidUtf8(data, len)) {
        out += ",\"encoding\":\"utf8\",\"value\":\"" +
            JsonEscape(std::string(data, len)) + "\"";
    } else {
        out += ",\"encoding\":\"base64\",\"value\":\"" +
            Base64Encode((const unsigned char *)data, len) + "\"";
    }
    out += ",\"bytes\":" + std::to_string((unsigned long long)len);
    return out;
}

static std::string NodeToJsonImpl(const DataNode &node, int depth);

static std::string ArrayJson(const DataNode &node, int depth) {
    const int type = (int)node.Type();
    DataArray *arr = node.UncheckedArray();
    std::string out = NodeHeader(type);
    if (!arr) {
        out += ",\"value\":null}";
        return out;
    }
    const int size = arr->Size();
    out += ",\"size\":" + std::to_string(size);
    if (depth >= kMaxArrayDepth) {
        out += ",\"truncated\":true,\"value\":null}";
        return out;
    }
    const int emit = size < kMaxArrayElements ? size : kMaxArrayElements;
    out += ",\"value\":[";
    for (int i = 0; i < emit; i++) {
        if (i)
            out += ",";
        out += NodeToJsonImpl(arr->Node(i), depth + 1);
    }
    out += "]";
    if (emit < size)
        out += ",\"truncated\":true";
    out += "}";
    return out;
}

static std::string NodeToJsonImpl(const DataNode &node, int depth) {
    const int type = (int)node.Type();
    switch (type) {
    case kDataInt:
        return NodeHeader(type) + ",\"value\":" +
            std::to_string(node.UncheckedInt()) + "}";

    case kDataFloat: {
        const float f = node.UncheckedFloat();
        std::string out = NodeHeader(type) + ",\"value\":" + JsonFloat(f);
        if (!std::isfinite(f)) {
            out += ",\"special\":\"";
            out += std::isnan(f) ? "nan" : (f > 0 ? "inf" : "-inf");
            out += "\"";
        }
        return out + "}";
    }

    case kDataSymbol: {
        const char *s = node.UncheckedStr();
        return NodeHeader(type) + ",\"value\":\"" + JsonEscape(s ? s : "") + "\"}";
    }

    case kDataString: {
        DataArray *arr = node.UncheckedArray();
        if (!arr)
            return NodeHeader(type) + ",\"value\":null}";
        // mSize is -(byteCount) and the buffer is NUL-terminated
        // (DataNode(const char*) allocates strlen+1).
        int raw = arr->Size();
        size_t len = raw < 0 ? (size_t)(-raw) : 0;
        const char *data = GlobBytes(arr);
        if (!data)
            return NodeHeader(type) + ",\"value\":null}";
        if (len > 0 && data[len - 1] == '\0')
            len--; // drop the terminator, keep any interior NULs
        return NodeHeader(type) + BytesJson(data, len) + "}";
    }

    case kDataGlob: {
        DataArray *arr = node.UncheckedArray();
        if (!arr)
            return NodeHeader(type) + ",\"value\":null}";
        int raw = arr->Size();
        size_t len = raw < 0 ? (size_t)(-raw) : 0;
        const char *data = GlobBytes(arr);
        if (!data)
            return NodeHeader(type) + ",\"value\":null}";
        // Globs are arbitrary bytes — always base64, never a lossy text read.
        return NodeHeader(type) + ",\"encoding\":\"base64\",\"value\":\"" +
            Base64Encode((const unsigned char *)data, len) + "\",\"bytes\":" +
            std::to_string((unsigned long long)len) + "}";
    }

    case kDataObject: {
        Hmx::Object *obj = node.UncheckedObj();
        if (!obj)
            return NodeHeader(type) + ",\"value\":null}";
        const char *name = obj->Name();
        const char *cls = obj->ClassName().Str();
        return NodeHeader(type) + ",\"value\":\"" + JsonEscape(name ? name : "") +
            "\",\"class\":\"" + JsonEscape(cls ? cls : "") + "\"}";
    }

    case kDataVar: {
        DataNode *var = node.UncheckedVar();
        std::string out = NodeHeader(type);
        const char *name = var ? DataVarName(var) : nullptr;
        if (name)
            out += ",\"name\":\"" + JsonEscape(name) + "\"";
        if (!var || depth >= kMaxArrayDepth)
            return out + ",\"value\":null}";
        return out + ",\"value\":" + NodeToJsonImpl(*var, depth + 1) + "}";
    }

    case kDataFunc:
        // A raw DataFunc pointer. The address is meaningless to a client and
        // unstable across runs, so report the type only.
        return NodeHeader(type) + ",\"value\":null}";

    case kDataArray:
    case kDataCommand:
    case kDataProperty:
        return ArrayJson(node, depth);

    default:
        // kDataUnhandled and the parse-directive types (ifdef/define/...) carry
        // no payload worth serializing; they never appear as an eval result.
        return NodeHeader(type) + ",\"value\":null}";
    }
}

std::string NodeToJson(const DataNode &node) { return NodeToJsonImpl(node, 0); }

// ---------------------------------------------------------------------------
// ScriptStateGuard
// ---------------------------------------------------------------------------

ScriptStateGuard::ScriptStateGuard() : mRestored(false) {
    mCallStackPtr = (void *)gCallStackPtr;
    mPreExecuteFunc = (void *)gPreExecuteFunc;
    mPreExecuteLevel = gPreExecuteLevel;
    mDataThis = (void *)gDataThis;
    mDataDir = (void *)gDataDir;
    mVarStackPtr = (void *)gVarStackPtr;
    mDataFile = DataArray::gFile.Str();

    MemHeapStack &heap = ThreadMemStack(true);
    mMemHeapDepth = heap.mSize;
    mMemTempRefs = heap.mTempRefs;
}

std::string ScriptStateGuard::Restore() {
    if (mRestored)
        return mLastRepair;
    mRestored = true;

    std::string repaired;
    char buf[160];

    if (gCallStackPtr != (DataArray **)mCallStackPtr) {
        snprintf(
            buf,
            sizeof(buf),
            "call stack depth %d -> %d",
            (int)(gCallStackPtr - gCallStack),
            (int)((DataArray **)mCallStackPtr - gCallStack)
        );
        repaired += repaired.empty() ? buf : std::string("; ") + buf;
        gCallStackPtr = (DataArray **)mCallStackPtr;
    }
    if (gPreExecuteFunc != (DataFunc *)mPreExecuteFunc) {
        repaired += repaired.empty() ? "preExecuteFunc" : "; preExecuteFunc";
        gPreExecuteFunc = (DataFunc *)mPreExecuteFunc;
    }
    gPreExecuteLevel = mPreExecuteLevel;

    if (gDataThis != (Hmx::Object *)mDataThis) {
        repaired += repaired.empty() ? "$this" : "; $this";
        gDataThis = (Hmx::Object *)mDataThis;
    }
    if (gDataDir != (ObjectDir *)mDataDir) {
        repaired += repaired.empty() ? "data dir" : "; data dir";
        gDataDir = (ObjectDir *)mDataDir;
    }
    if (gVarStackPtr != (VarStack *)mVarStackPtr) {
        repaired += repaired.empty() ? "var stack" : "; var stack";
        gVarStackPtr = (VarStack *)mVarStackPtr;
    }
    if (DataArray::gFile.Str() != mDataFile) {
        DataArray::SetFile(STR_TO_SYM(mDataFile));
    }

    MemHeapStack &heap = ThreadMemStack(true);
    if (heap.mSize != mMemHeapDepth) {
        snprintf(
            buf, sizeof(buf), "heap stack depth %d -> %d", heap.mSize, mMemHeapDepth
        );
        repaired += repaired.empty() ? buf : std::string("; ") + buf;
        heap.mSize = mMemHeapDepth;
    }
    if (heap.mTempRefs != mMemTempRefs) {
        repaired += repaired.empty() ? "temp refs" : "; temp refs";
        heap.mTempRefs = mMemTempRefs;
    }

    mLastRepair = repaired;
    return mLastRepair;
}

ScriptStateGuard::~ScriptStateGuard() {
    const bool alreadyRestored = mRestored;
    std::string repaired = Restore();
    if (!alreadyRestored && !repaired.empty()) {
        fprintf(
            stderr,
            "[DtaEval] repaired leaked interpreter state after abnormal exit: %s\n",
            repaired.c_str()
        );
    }
}

} // namespace DtaEval
