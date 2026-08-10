#pragma once
#include "utl\MemMgr.h"
#include "utl\TextStream.h"
#include "utl\Symbol.h"
#include <cstring>
#include <vector>

// i can't think of a better place to put this
inline bool IsAsciiNum(char c) { return c >= 0x30 && c <= 0x39; }

// ditto
inline bool streq(const char *s1, const char *s2) { return strcmp(s1, s2) == 0; }

inline bool strneq(const char *s1, const char *s2, int n) {
    return strncmp(s1, s2, n) == 0;
}

#ifdef HX_NATIVE
inline bool strieq(const char *s1, const char *s2) { return strcasecmp(s1, s2) == 0; }
#else
inline bool strieq(const char *s1, const char *s2) { return stricmp(s1, s2) == 0; }
#endif

class FixedString {
protected:
    char *mStr; // 0x0
public:
    FixedString();
    FixedString(char *, int);

    unsigned int length() const { return strlen(mStr); }
    unsigned int size() const { return strlen(mStr); }
    unsigned int capacity() const { return *(unsigned int *)(mStr - 4); }
    const char *c_str() const { return mStr; }
    bool empty() const { return *mStr == '\0'; }

    bool operator<(const FixedString &) const;
    FixedString &operator+=(const char *);
    bool contains(const char *) const;

    unsigned int find(const char *) const;
    unsigned int find(char, unsigned int) const;
    unsigned int find(char) const;
    unsigned int find_last_of(char) const;
    unsigned int find_last_of(const char *) const;
    unsigned int find(const char *, unsigned int) const;
    unsigned int find_first_of(const char *, unsigned int) const;

    char &operator[](unsigned int);

    void ToLower();
    void ToUpper();
    void ReplaceAll(char, char);
    int compare(unsigned int, unsigned int, const char *) const;

    static const unsigned int npos;
};

// Base order is `FixedString, TextStream` and that is CORRECT — settled
// 2026-08-04, do not re-litigate. See the RESOLVED note below for the two
// independent proofs and the whole-build measurement.
class String : public FixedString, public TextStream {
    // MSVC hoists the polymorphic base to offset 0 regardless of the order the
    // bases are *declared* in, so the layout is:
    //   +0x0  TextStream  ({vfptr})
    //   +0x4  FixedString (mStr)
    // Verified with cl.exe /d1reportAllClassLayout on this exact toolchain
    // (X360/16.00.11886.00): `class String size(8)` with TextStream at 0 and
    // FixedString::mStr at 4 — byte-identical dump for BOTH declaration orders.
    //
    // RESOLVED: "swap to `TextStream, FixedString` because the target does a
    // `p ? p+4 : 0` base adjustment before loading mStr" — that reasoning is
    // wrong. Evidence:
    //
    //  1. The +4 adjustment (e.g. CharLipSync::Print: `addi rN, rM, 4` then
    //     `lwz r4, 0(rN)`) proves only that FixedString sits at +4. It already
    //     does under this order, so the tell is *satisfied*, not violated; it
    //     cannot discriminate between the two orders at all.
    //
    //  2. Declaration order does still control base *construction* order, and
    //     there the target is unambiguous. In `String::String(const String &)`
    //     the target inlines FixedString's gEmpty setup FIRST, then calls the
    //     empty TextStream ctor (ICF-merged with StackString<128>::~StackString),
    //     then stores ??_7String@@6B@. That is FixedString-then-TextStream,
    //     i.e. exactly this declaration order.
    //
    //  3. Whole-build A/B of the swap (full rebuild, measure_progress.sh vs
    //     9ad5c4c8): 4 regressions, 0 improvements, overall fuzzy 53.83% ->
    //     53.83% (-0.00%). Only the four String constructors moved — String(),
    //     String(const char *), String(const String &), String(unsigned, char),
    //     all 100% -> 34-64% — and nothing else in the binary changed, which is
    //     the expected signature of a layout-neutral, ctor-order-only edit.
    //
    // og-dc3-decomp/src/system/utl/Str.h agrees (same declaration order); its
    // companion comment "TextStream vtable = 0x0, FixedString = 0x4" is the
    // accurate one — the two offsets below used to be stated backwards here.
    // RB2 DWARF is NOT usable for this: RB2's String predates FixedString
    // (`class String : public TextStream`, size 0xC, mCap/mStr).
public:
    virtual ~String();
    virtual void Print(const char *str) { *this += str; }

    String();
    String(const char *);
    String(Symbol);
    String(const String &);
    String(unsigned int, char);

    bool operator==(const FixedString &) const;

    void reserve(unsigned int);

    String operator+(const char *) const;
    String operator+(char) const;
    String operator+(const FixedString &) const;
    String &operator+=(const char *);
    String &operator+=(Symbol);
    String &operator+=(const FixedString &);
    String &operator+=(char);
    String &operator=(const char *);
    String &operator=(Symbol);
    String &operator=(const FixedString &);
    String &operator=(const String &);

    // char rindex(int) const;
    // char &rindex(int);

    bool operator!=(const char *) const;
    bool operator!=(const FixedString &) const;
    bool operator==(const char *) const;
    // bool operator==(const String &) const;
    // bool operator<(const String &) const;
    bool operator==(Symbol) const;

    void resize(unsigned int);
    // unsigned int rfind(const char *) const;

    int split(const char *token, std::vector<String> &subStrings) const;

    String substr(unsigned int) const;
    String substr(unsigned int, unsigned int) const;

    void swap(String &);
    String &replace(unsigned int, unsigned int, const char *);
    String &erase();
    String &erase(unsigned int);
    String &erase(unsigned int, unsigned int);
    String &insert(unsigned int, unsigned int, char);
    String &insert(unsigned int, const char *);
    // String &insert(unsigned int, const String &);
};

bool SearchReplace(const char *, const char *, const char *, char *);
bool StrNCopy(char *, const char *, int);
void RemoveSpaces(char *, int, const char *);

inline TextStream &operator<<(TextStream &ts, const String &str) {
    ts.Print(str.c_str());
    return ts;
}

template <int N>
class StackString : public FixedString, public TextStream {
private:
    char mStack[N + 5];

public:
    StackString() : TextStream(), FixedString((char *)mStack, N + 5) {}
    StackString(const char *str) : TextStream(), FixedString((char *)mStack, N + 5) {
        *this += str;
    }
    // virtual ~StackString() {} // dtor is at 0x8269E480
    virtual void Print(const char *str) { *this += str; }

    StackString &operator=(const StackString &rhs) {
        const FixedString *src = &rhs;
        *mStr = '\0';
        FixedString::operator+=(src->c_str());
        for (int i = 0; i < N + 5; i++) {
            mStack[i] = rhs.mStack[i];
        }
        return *this;
    }
};
