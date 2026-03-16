#include "hamobj/HamWardrobe.h"
#include "char/CharClipDriver.h"
#include "char/CharClipGroup.h"
#include "char/CharDriver.h"
#include "char/CharInterest.h"
#include "char/Character.h"
#include "char/FileMerger.h"
#include "world/Crowd.h"
#include "hamobj/HamCharacter.h"
#include "hamobj/HamGameData.h"
#include "math/Rand.h"
#include "obj/Data.h"
#include "obj/DataUtl.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/System.h"
#include "rndobj/Overlay.h"
#include "rndobj/Wind.h"
#include "utl/Symbol.h"
#include "world/Dir.h"
#ifndef HX_NATIVE
#include "xdk/LIBCMT/stdio.h"
#endif

HamWardrobe *TheHamWardrobe;

HamWardrobe::HamWardrobe()
    : mCrowdMembers(this), mMainCharacters(this, (EraseMode)1, kObjListAllowNull),
      unk34("medium"), mCrowdOverrideActive(0), mForcedCrowdAnimation(gNullStr), mCrowdAnimationFlags(0) {
    static DataNode &n = DataVariable("hamwardrobe");
    if (TheHamWardrobe) {
        MILO_NOTIFY("Trying to make > 1 HamWardrobe, which should be single");
    }
    n = this;
    TheHamWardrobe = this;
    for (int i = 0; i < 2; i++) {
        mMainCharacters.push_back(nullptr);
    }
    mOverlay = RndOverlay::Find("crowd_groups", false);
}

HamWardrobe::~HamWardrobe() {
    if (TheHamWardrobe == this) {
        static DataNode &n = DataVariable("hamwardrobe");
        n = NULL_OBJ;
        TheHamWardrobe = nullptr;
    }
}

BEGIN_HANDLERS(HamWardrobe)
    HANDLE(set_venue, OnSetVenue)
    HANDLE_EXPR(chars_dir, Dir())
    HANDLE_EXPR(get_character, GetCharacter(_msg->Int(2)))
    HANDLE_EXPR(get_backup, GetBackup(_msg->Int(2)))
    HANDLE(add_crowd, OnAddCrowd)
    HANDLE_ACTION(set_force_character, mForcedCharacter = _msg->Sym(2))
    HANDLE_ACTION(crowd, PlayCrowdAnimation(_msg->Sym(2), 1, false))
    HANDLE_ACTION(crowd_end_override, EndCrowdOverride())
    HANDLE_ACTION(crowd_force_state_enable, ForceCrowdAnimationStart(_msg->Sym(2)))
    HANDLE_ACTION(crowd_force_state_disable, ForceCrowdAnimationEnd())
    HANDLE_EXPR(get_crew_char, GetCrewChar(_msg->Sym(2), _msg->Int(3)))
    HANDLE(load_characters, OnLoadCharacters)
    HANDLE_ACTION(
        set_backup_override_outfits, SetBackupOverrideOutfits(_msg->Sym(2), _msg->Sym(3))
    )
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(HamWardrobe)
    SYNC_PROP(crowd_members, mCrowdMembers)
    SYNC_PROP_SET(overlay_enabled, mOverlay->Showing(), mOverlay->SetShowing(_val.Int()))
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(HamWardrobe)
    SAVE_REVS(2, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << unk34;
END_SAVES

INIT_REVS(2, 0)

BEGIN_LOADS(HamWardrobe)
    LOAD_REVS(bs)
    ASSERT_REVS(2, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    if (d.rev > 1)
        bs >> unk34;
END_LOADS

BEGIN_COPYS(HamWardrobe)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(HamWardrobe)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(unk34)
    END_COPYING_MEMBERS
END_COPYS

void HamWardrobe::SetBackupOverrideOutfits(Symbol s1, Symbol s2) {
    mBackupOutfitOverrides[0] = s1;
    mBackupOutfitOverrides[1] = s2;
}

namespace {
    Symbol HandleRobot(Symbol s) {
        static Symbol robota01("robota01");
        static Symbol robota02("robota02");
        static Symbol robotb01("robotb01");
        static Symbol robotb02("robotb02");
        if (s == robota02) {
            s = robota01;
        }
        if (s == robotb02) {
            s = robotb01;
        }
        return s;
    }
}

Symbol HamWardrobe::GetBackupOutfitOverride(int x) {
    if (x >= 0 && x < 2) {
        return mBackupOutfitOverrides[x];
    } else
        return gNullStr;
}

Symbol HamWardrobe::GetCrewChar(Symbol s, int i) {
    return DataGetMacro("CREWS")->FindArray(s, "characters")->Sym(i + 1);
}

Symbol GetOutfitBackupDancer(Symbol outfit) {
    MILO_ASSERT(!outfit.Null(), 0x112);
    DataArray *entry = GetOutfitEntry(outfit, true);
    static Symbol backup_dancers("backup_dancers");
    DataArray *backupArr = entry->FindArray(backup_dancers, true);
    return backupArr->Sym(1);
}

Symbol GetDanceBattleBackupOutfit(Symbol s1, Symbol s2) {
    DataArray *charArr = DataGetMacro("CREWS")->FindArray(s2, "characters");
    Symbol out(gNullStr);
    String str88(s1);
    String str90(str88);
    str90 = str90.substr(0, str90.length() - 2);
    if (charArr->Size() > 1) {
        int i = 1;
        Symbol s;
        while (i < charArr->Size()) {
            s = charArr->Sym(i);
            if (str90 != s.Str()) {
                const char *cStr = s.Str();
                const char *p = cStr;
                while (*p) p++;
                unsigned int len = (unsigned int)(p - cStr);
                if ((int)len < 0x1E) {
                    char buf[30];
                    const char *src = cStr;
                    char *dst = buf;
                    do {
                        *dst = *src;
                        dst++;
                        src++;
                    } while (*src);
                    const char *baseStr = str90.c_str();
                    unsigned int baseLen = str90.length();
                    buf[len - 2] = baseStr[baseLen - 2];
                    buf[len - 1] = baseStr[baseLen - 1];
                    *dst = 0;
                    out = GetOutfitRemap(Symbol(buf), false);
                }
            }
            i++;
        }
    }
    return out;
}

HamCharacter *HamWardrobe::GetBackup(int i) const {
    return Dir()->Find<HamCharacter>(MakeString("backup%d", i), false);
}

void HamWardrobe::EndCrowdOverride() {
    if (mCrowdOverrideActive) {
        if (mForcedCrowdAnimation == gNullStr) {
            mCrowdOverrideActive = false;
            int flags = mCrowdAnimationFlags;
            if (mCrowdAnimationFlags & 2) {
                // If flag 1 is set, clear bits 0-1 and set bit 0
                flags = (mCrowdAnimationFlags & ~3) | 1;
            }
            PlayCrowdAnimation(mPreviousCrowdAnimation, flags, false);
        }
    }
}

void HamWardrobe::ForceCrowdAnimationEnd() {
    mForcedCrowdAnimation = gNullStr;
    EndCrowdOverride();
}

void HamWardrobe::ForceCrowdAnimationStart(Symbol s) {
    static Symbol none("none");
    if (s == gNullStr || s == none || s == mForcedCrowdAnimation) {
        if (mForcedCrowdAnimation != gNullStr) {
            ForceCrowdAnimationEnd();
        }
    } else {
        MILO_LOG(
            "HamWardrobe::ForceCrowdAnimationStart: %s : current mCrowdForceState = '%s'\n",
            s.Str(),
            mForcedCrowdAnimation.Str()
        );
        mForcedCrowdAnimation = gNullStr;
        PlayCrowdAnimation(s, 1, true);
        mForcedCrowdAnimation = s;
        static Symbol none2("none");
        if (s == none2) {
            mForcedCrowdAnimation = gNullStr;
        }
    }
}

HamCharacter *HamWardrobe::LoadMainCharacter(int index, Symbol s, bool b3) {
    MILO_ASSERT(index < mMainCharacters.size(), 0x160);
    HamCharacter *c = mMainCharacters[index];
    c->SetOutfit(s);
    c->StartLoad(b3);
    return c;
}

void HamWardrobe::LoadCrowdClips(Symbol s1, Symbol s2, bool b3) {
    FileMerger *fm = Dir()->Find<FileMerger>("crowd_clips.fm", false);
    if (fm) {
        static Message msg("load_tempo", 0, 0, 0, 0);
        msg[0] = s1;
        msg[1] = b3;
        msg[2] = s2;
        fm->HandleType(msg);
    }
}

bool HamWardrobe::AllCharsLoaded() {
    for (int i = 0; i < 2; i++) {
        HamCharacter *c = mMainCharacters[i];
        if (c && c->IsLoading())
            return false;
    }
    HamCharacter *c = GetBackup(0);
    int i = 1;
    for (; c != nullptr; c = GetBackup(i++)) {
        if (c->IsLoading())
            return false;
    }
    FileMerger *fm = Dir()->Find<FileMerger>("crowd_clips.fm", false);
    if (fm && fm->HasPendingFiles())
        return false;
    else
        return true;
}

HamCharacter *HamWardrobe::GetCharacter(int i) const {
    MILO_ASSERT((0) <= (i) && (i) < (2), 0x213);
    return (HamCharacter *)mMainCharacters[i];
}

void HamWardrobe::ClearCrowdClips() { LoadCrowdClips(gNullStr, gNullStr, false); }

void HamWardrobe::ClearCrowd() {
    mCrowdMembers.clear();
    mForcedCrowdAnimation = gNullStr;
    mCrowdOverrideActive = false;
}

void HamWardrobe::SyncInterestObjects(ObjectDir *dir) {
    ObjPtrList<CharInterest> interests(this);
    for (ObjDirItr<CharInterest> it(dir, true); it != nullptr; ++it) {
        interests.push_back(it);
    }
    for (ObjDirItr<Character> it(dir, true); it != nullptr; ++it) {
        for (ObjDirItr<CharInterest> cit(it, true); cit != nullptr; ++cit) {
            interests.push_back(cit);
        }
    }
    for (int i = 0; i < 2; i++) {
        HamCharacter *c = mMainCharacters[i];
        if (c) {
            c->SetInterestObjects(interests, nullptr);
        }
    }
    HamCharacter *c = GetBackup(0);
    int i = 1;
    for (; c != nullptr; c = GetBackup(i++)) {
        c->SetInterestObjects(interests, nullptr);
    }
}

void HamWardrobe::UpdateOverlay() {
    if (!mOverlay || !mOverlay->Showing()) return;

    for (ObjPtrList<Character>::iterator it = mCrowdMembers.begin();
         it != mCrowdMembers.end(); ++it) {
        Character *cur = *it;
        if (!cur) continue;

        *mOverlay << cur->Name() << ": ";
        CharDriver *driver = cur->Driver();
        if (!driver) goto output_newline;
        {
            CharClipGroup *clipGroup = driver->GetClipGroup();
            if (!clipGroup) goto output_newline;
            *mOverlay << clipGroup->Name() << "    [ ";
            std::set<String> seen;
            for (CharClipDriver *cd = driver->First(); cd != nullptr; cd = cd->mNext) {
                CharClip *clip = cd->mClip;
                const char *name = clip ? clip->Name() : "<NULL>";
                String s(name);
                if (seen.find(s) == seen.end()) {
                    seen.insert(s);
                    *mOverlay << s.c_str() << " ";
                }
            }
            *mOverlay << "]\n";
            continue;
        }
    output_newline:
        *mOverlay << "\n";
    }
}

void HamWardrobe::SetDir(ObjectDir *dir) {
    RndWind *wind = dir->Find<RndWind>("world.wind", false);
    if (wind) {
        wind->SetWindOwner(dir->Find<RndWind>("wind.wind", false));
    }
    HamCharacter *c = GetBackup(0);
    int i = 1;
    for (; c != nullptr; c = GetBackup(i++)) {
        c->SetFocusInterest(nullptr, 0);
        c->EnableBlinks(true, false);
    }
    for (int i = 0; i < 2; i++) {
        HamCharacter *c = mMainCharacters[i];
        if (c) {
            c->SetFocusInterest(nullptr, 0);
            c->EnableBlinks(true, false);
        }
    }
    SyncInterestObjects(dir);
}

void HamWardrobe::PlayCrowdAnimation(Symbol animName, int flags, bool override) {
    if (mCrowdMembers.size() == 0) return;
#ifdef HX_NATIVE
    // On native, crowd character clip subdirs aren't loaded (char/crowd/anim/),
    // so clip groups contain null ObjPtrs. Skip crowd animation to avoid crashes.
    return;
#endif

    mPreviousCrowdAnimation = animName;
    mCrowdAnimationFlags = flags;

    if ((override || !mCrowdOverrideActive) && mForcedCrowdAnimation == gNullStr) {
        mCrowdOverrideActive = override;

        float maxRandomBeat = 0.0f;
        if ((flags & 0xf0) != 0x10) {
            maxRandomBeat = 4.0f;
        }

        for (ObjPtrList<Character>::iterator it = mCrowdMembers.begin();
             it != mCrowdMembers.end(); ++it) {
            Character *c = *it;
            if (animName == (int)gNullStr) {
                c->Exit();
            } else {
                auto stanceSym = ("stance");
                Symbol stance = c->Property(stanceSym, true)->Sym(nullptr);
                if (stance == gNullStr) {
                    TheDebug << "    stance = NULL!\n";
                }
                char buf[120];
                _snprintf(buf, 0x78, "%s_%s", stance.Str(), animName.Str());
                c->Driver()->SetBlendWidth(3.0f);
                CharClipDriver *cd = c->Driver()->PlayGroup(
                    buf, flags | 0x30, -1.0f, 1e30f, 0.0f
                );
                if (nullptr == cd) {
                    auto errMsg = MakeString("clip not found - groupName = %s\n", buf);
                    TheDebug << errMsg;
                    MILO_NOTIFY(
                        "%s could not find clip from group %s", PathName(c), buf
                    );
                    _snprintf(buf, 0x78, "%s", stance.Str());
                    c->Driver()->SetBlendWidth(3.0f);
                    cd = c->Driver()->PlayGroup(
                        buf, flags | 0x30, -1.0f, 1e30f, 0.0
                    );
                    if (cd == nullptr) {
                        auto errMsg2 = MakeString(
                            "  clip not found - groupName = %s\n", buf
                        );
                        TheDebug << errMsg2;
                        MILO_NOTIFY(
                            "  %s could not find clip from group %s", PathName(c), buf
                        );
                    } else if (cd->mNext) {
                        cd->mRampIn = RandomFloat(0.0f, maxRandomBeat);
                    }
                } else if (cd->mNext) {
                    cd->mRampIn = RandomFloat(0.0f, maxRandomBeat);
                }
            }
        }
    }
}

template <class _T>
__declspec(noinline) auto _outline_Null(_T* _obj) -> decltype(_obj->Null()) {
    return _obj->Null();
}

void HamWardrobe::LoadCharacters(
    Symbol outfit1,
    Symbol outfit2,
    Symbol crew1,
    Symbol crew2,
    HamBackupDancers backupType,
    Symbol speed,
    Symbol venue,
    bool asyncLoad
) {
    outfit2 = HandleRobot(outfit2);
    outfit1 = HandleRobot(outfit1);

    mMainCharacters.clear();
    for (int i = 0; i < 2; i++) {
        HamCharacter *c = Dir()->Find<HamCharacter>(MakeString("player%d", i), true);
        mMainCharacters.push_back(c);
    }

    unk34 = speed;

    if (!_outline_Null(&outfit1)) {
        LoadMainCharacter(0, outfit1, asyncLoad);
    }
    if (!_outline_Null(&outfit2)) {
        LoadMainCharacter(1, outfit2, asyncLoad);
    }

    for (int i = 0; i < 2; i++) {
        Symbol *outfit = (i == 0) ? &outfit1 : &outfit2;
        Symbol backupOutfit(gNullStr);
        Symbol *crew = (i == 0) ? &crew1 : &crew2;

        if (backupType == kBackupDancersOutfit) {
            if (*outfit != gNullStr) {
                Symbol dancer = GetOutfitBackupDancer(*outfit);
                auto backupSym = Symbol(MakeString("%s_bd0%d", dancer.Str(), i + 1));
                backupOutfit = backupSym;
            }
        } else if (backupType == kBackupDancersTan) {
            if (i == 0) {
                static Symbol tan01("tan01");
                backupOutfit = tan01;
            }
        } else {
            if (backupType == kBackupDancersOverride) {
                backupOutfit = GetBackupOutfitOverride(i);
            } else {
                MILO_ASSERT(backupType == kBackupDancersDanceBattle, 0x1ac);
                auto battleBackup = GetDanceBattleBackupOutfit(*outfit, *crew);
                backupOutfit = battleBackup;
            }
        }

        HamCharacter *backup = GetBackup(i);
        backup->SetOutfit(backupOutfit);
        const char *outfitDir = "char/main/backup";
        if (backupType != kBackupDancersOutfit) {
            outfitDir = "char/main/dancer";
        }
        backup->SetOutfitDir(Symbol(outfitDir));
        backup->StartLoad(asyncLoad);
    }

    LoadCrowdClips(unk34, venue, asyncLoad);
}

DataNode HamWardrobe::OnSetVenue(DataArray *a) {
    ObjectDir *dir = a->Obj<ObjectDir>(2);
    SetDir(dir);

    Symbol venue = TheGameData->Venue();
    if (venue.Null() && TheWorld) {
        String worldPath(TheWorld->GetPathName());
        unsigned int lastSlash = worldPath.find_last_of('/');
        worldPath = worldPath.substr(lastSlash + 1);

        DataArray *venuesArr = SystemConfig()->FindArray("venues", false);
        if (venuesArr) {
            for (int i = 1; i < venuesArr->Size(); i++) {
                DataArray *venueEntry = venuesArr->Node(i).Array(venuesArr);
                MILO_ASSERT(venueEntry, 0x27d);
                Symbol entryName = venueEntry->Sym(0);
                if (worldPath.contains(entryName.Str())) {
                    venue = entryName;
                    break;
                }
            }
        }
    }

    LoadCharacters(
        Symbol("mo01"), Symbol("emilia01"), Symbol("crew01"), Symbol("crew02"),
        kBackupDancersOutfit, unk34, venue, false
    );
    return 0;
}

DataNode HamWardrobe::OnAddCrowd(DataArray *a) {
    WorldCrowd *crowd = a->Obj<WorldCrowd>(2);
    auto& crowdMembers = mCrowdMembers;
    for (std::list<WorldCrowd::CharData>::iterator it = crowd->mCharacters.begin();
         crowd->mCharacters.end() != it; ++it) {
        Character *c = it->mDef.mChar;
        if (c) {
            ObjPtrList<Character>::iterator mit = crowdMembers.begin();
            for (; mit != crowdMembers.end(); ++mit) {
                Character *existing = *mit;
                if (existing && (ObjectDir *)existing == (ObjectDir *)c) {
                    break;
                }
            }
            if (mit == crowdMembers.end()) {
                crowdMembers.push_back(c);
            }
        }
    }
    return 0;
}

DataNode HamWardrobe::OnLoadCharacters(DataArray *a) {
    short size = a->Size();
    Symbol crew1 = 4 < size ? a->Sym(4) : Symbol(gNullStr);
    Symbol crew2 = size > 5 ? a->Sym(5) : Symbol(gNullStr);
    int backupType;
    bool asyncLoad = false;
    Symbol speed;
    if (size > 6) {
        backupType = bool(a->Int(6));
        speed = a->Sym(7);
    } else {
        backupType = 0;
        speed = gNullStr;
    }
    if (size > 7) {
        asyncLoad = bool(a->Int(8));
    }
    Symbol venue = TheGameData->Venue();
    LoadCharacters(a->Sym(2), a->Sym(3), crew1, crew2, (HamBackupDancers)backupType, speed, venue, asyncLoad);
    return 0;
}
