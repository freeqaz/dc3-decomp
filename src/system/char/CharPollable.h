#pragma once
#include "rndobj\Poll.h"
#include <list>

/** "Workhorse unit of the Character system, most Character things inherit from this." */
class CharPollable : public RndPollable {
public:
    CharPollable() {}
    virtual void
    PollDeps(std::list<Hmx::Object *> &changedBy, std::list<Hmx::Object *> &change) = 0;
};

class CharPollableSorter {
public:
    struct Dep {
        // w19-c BEHAVIOUR FIX: the image's map<Object*, Dep>::operator[]
        // (char/Character.obj) stores 0 to the default value's obj (0x60(r31))
        // and poll (0x6c(r31)) and leaves searchID alone, i.e. this ctor.
        // Without it MSVC's value-initialisation of this non-POD left obj as
        // stack garbage, and AddDeps tests `if (!mapDep.obj)` to recognise a
        // freshly inserted entry.  rb3-xenon recovered the same ctor.
        Dep() : obj(nullptr), poll(nullptr) {}
        Hmx::Object *obj; // 0x0
        std::list<Dep *> changedBy; // 0x4
        RndPollable *poll; // 0xc
        int searchID; // 0x10
    };

    struct AlphaSort {
        bool operator()(Dep *d1, Dep *d2) const {
            return strcmp(d1->obj->Name(), d2->obj->Name()) < 0;
        }
    };

    void Sort(std::vector<RndPollable *> &);

protected:
    bool ChangedBy(Dep *, Dep *);

    static int sSearchID;

    std::map<Hmx::Object *, Dep> mDeps;
    Dep *mTarget;

protected:
    void AddDeps(Dep *, const std::list<Hmx::Object *> &, std::list<Dep *> &, bool);
    bool ChangedByRecurse(Dep *);
};
