#include "utl\KnownIssues.h"
#include "obj\Data.h"
#include "obj\DataFunc.h"
#include "obj/Task.h"
#include "os\System.h"
#include "rndobj\Graph.h"
#include "utl\Symbol.h"

KnownIssues TheKnownIssues;

KnownIssues::KnownIssues() : mDisplay(true) {}

void KnownIssues::Display(String str, float f2) {
    if (!mDisplay)
        return;
    else {
        mName = str;
        unk_0x10 = f2;
        mDescription = gNullStr;
        DataArray *cfg = SystemConfig("known_issues");
        DataArray *cfgArr = cfg->FindArray(str.c_str(), false);
        if (cfgArr) {
            for (int i = 1; i < cfgArr->Size(); i++) {
                mDescription += cfgArr->Str(i);
                mDescription += "\n";
            }
        }
    }
}

void KnownIssues::Draw() {
    if (unk_0x10 != 0) {
        RndGraph *graph = RndGraph::GetOneFrame();
        graph->AddRectFilled2D(Hmx::Rect(0.1, 0.1, 0.8, 0.8), Hmx::Color(0, 0, 0, 0.5));
        graph->AddScreenString(
            MakeString("%s known issues:", mName),
            Vector2(0.15, 0.13),
            Hmx::Color(1, 1, 1, 1)
        );
        graph->AddScreenString(
            mDescription.c_str(), Vector2(0.2, 0.2), Hmx::Color(1, 1, 1, 1)
        );
        graph->AddScreenString(
            "Press 'k' to hide/show this", Vector2(0.15, 0.86), Hmx::Color(1, 1, 1, 1)
        );
    }
    if (unk_0x10 > 0) {
        unk_0x10 -= TheTaskMgr.DeltaUISeconds();
        if (unk_0x10 < 0) {
            unk_0x10 = 0;
        }
    }
}

DataNode KnownIssues::OnDisplayKnownIssues(DataArray *msg) {
    if (msg->Size() > 2) {
        TheKnownIssues.Display(msg->Str(1), msg->Float(2));
    } else {
        TheKnownIssues.Display(msg->Str(1), 5.0f);
    }
    return 0;
}

// w19-d: 88.889 -> 100 (byte-identical).  The float is stored IN EACH ARM, not
// through an `f10` local assigned in both arms and stored once after the join:
// MSVC tail-merges the two stores itself, and only that shape puts the stfs
// ahead of the bool's clrlwi (the old local form masked first, then stored).
DataNode KnownIssues::OnToggleLastKnownIssues(DataArray *) {
    bool ret;
    if (TheKnownIssues.unk_0x10 == 0) {
        TheKnownIssues.unk_0x10 = -1;
        ret = true;
    } else {
        TheKnownIssues.unk_0x10 = 0;
        ret = false;
    }
    return ret;
}

DataNode KnownIssues::OnToggleAllowKnownIssues(DataArray *) {
    if (!TheKnownIssues.mDisplay) {
        TheKnownIssues.mDisplay = true;
    } else {
        TheKnownIssues.unk_0x10 = 0;
        TheKnownIssues.mDisplay = false;
    }
    return TheKnownIssues.mDisplay;
}

void KnownIssues::Init() {
    DataRegisterFunc("display_known_issues", OnDisplayKnownIssues);
    DataRegisterFunc("toggle_last_known_issues", OnToggleLastKnownIssues);
    DataRegisterFunc("toggle_allow_known_issues", OnToggleAllowKnownIssues);
}
