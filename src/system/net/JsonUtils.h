#pragma once
#include "net\json-c\json_object.h"
#include "types.h"
#include "utl\Str.h"

class JsonObject {
public:
    enum EType { // From RB3
        kType_Null = 0,
        kType_Boolean = 1,
        kType_Double = 2,
        kType_Int = 3,
        kType_Object = 4,
        kType_Array = 5,
        kType_String = 6
    };

    JsonObject() : mObject(nullptr) {}
    // DECOMP BUG (open, 2026-08-19): this destructor was PROTECTED in the
    // original.  ham_xbox_r.map:47873 spells the deleting-destructor thunk
    // `??_GJsonObject@@MAAPAXI@Z` -- `M` = protected virtual -- where we emit
    // `U` = public virtual.  Invisible to every diff, because our `U` spelling
    // is also what config/373307D9/symbols.txt:137107 applies to the TARGET
    // symbol at 0x82563BF0, so both sides agree on a name only we believe.
    // Fixing it means moving this under `protected:` AND correcting symbols.txt
    // + scripts/target_symbol_map.json together.  See
    // docs/analysis/dispatch-data-rescan-20260818.md.
    virtual ~JsonObject() { Release(); }

    lh_table *Get() const { return json_object_get_object(mObject); }
    void Set(json_object *o) { mObject = o; }
    void AddRef() { json_object_get(mObject); }
    void Release() { json_object_put(mObject); }

    EType GetType() const;
    char const *Str() const;
    bool Bool() const;
    int Int() const;

protected:
    json_object *mObject; // 0x4
};

class JsonArray : public JsonObject {
    friend class JsonConverter;

private:
    JsonArray();
    virtual ~JsonArray();

    json_object *operator[](int idx) { return json_object_array_get_idx(mObject, idx); }

public:
    int GetSize() const;
};

class JsonConverter : public JsonArray {
public:
    JsonConverter();
    virtual ~JsonConverter();

    JsonObject *LoadFromString(String const &);
    JsonObject *GetValue(JsonArray *, int);
    const char *Str(JsonArray *, int);
    JsonObject *GetByName(JsonObject *, char const *);
    void PushObject(JsonObject *);

private:
    std::vector<JsonObject *> mObjects; // 0x8
};
