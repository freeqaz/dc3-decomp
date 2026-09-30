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

// 88.889% (normalized, full ninja).  The whole residue is ONE scheduling pair at
// the join: the image emits `stfs f0, 0x10(r11)` first and only then masks the
// bool with `clrlwi r11, r10, 24` -- into r11, the register the store just freed
// -- while we mask in place (`clrlwi r10, r10, 24`) before the store, which
// forces the in-place destination.  The register choice is a CONSEQUENCE of the
// order, not an independent difference: before the stfs, r11 still holds
// &TheKnownIssues.  The sibling OnToggleAllowKnownIssues below is 100% and shows
// the image's preferred shape (`stb r10, 0x14(r11)` / `clrlwi r11, r10, 24`).
//
// REFUTED, each measured by full ninja on this row (normalized):
//   88.889  this form, and the older `bool ret = !TheKnownIssues.unk_0x10;`
//           two-read spelling -- byte-identical objects, so the `!` read was
//           pure noise and is gone
//   88.889  arms reordered (bool assigned before the float) -- no effect
//   88.889  `float &last = TheKnownIssues.unk_0x10;` anchor local -- no effect
//   80.000  any single-`if` form with the values initialised at declaration
//           (bool/unsigned char/int flag, explicit `(int)` cast, or a named
//           `DataNode node(ret)` temp) -- all five lose the image's
//           `mr r10, r9` on the not-taken path
//   77.778  `return ret ? 1 : 0;`
//   59.722  the test inverted to `!= 0` with the arms swapped
//   44.056  `TheKnownIssues.unk_0x10 = ret ? -1.0f : 0.0f;` -- /fp:fast turns
//           the select into fsel and the shape collapses
// Treat the remaining pair as a scheduler floor unless a permuter sweep moves it.
DataNode KnownIssues::OnToggleLastKnownIssues(DataArray *) {
    float f10;
    bool ret;
    if (TheKnownIssues.unk_0x10 == 0) {
        f10 = -1;
        ret = true;
    } else {
        f10 = 0;
        ret = false;
    }
    TheKnownIssues.unk_0x10 = f10;
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
