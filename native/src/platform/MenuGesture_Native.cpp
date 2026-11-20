// Menu gestures on a webcam: the native side of the measurement (PLAN P-3,
// branch np-menu-gestures).  See MenuGesture_Native.h.

#include "MenuGesture_Native.h"

#ifdef HX_NATIVE

#include "gesture/ArcDetector.h"
#include "gesture/DirectionGestureFilter.h"
#include "gesture/GestureMgr.h"
#include "gesture/HandHeightGestureFilter.h"
#include "hamobj/HamListRibbon.h"
#include "hamobj/HamNavList.h"
#include "obj/Task.h"
#include "platform/PoseFill_Native.h"
#include "ui/UIListProvider.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

// The one name the image's classes befriend (HX_NATIVE only): it reads their
// private state, it never writes it.
struct Dc3MenuGestureAccess {
    struct List {
        const HamNavList *list = nullptr;
        std::string name;
        std::string sym;
        float lastPoll = -1000.0f; // UI seconds
        int hand = 0; // HamNavList::NavInputType
        int mode = 3; // HamListRibbon::RibbonMode
        int selected = 0, selectedData = 0, firstShowing = 0, numShowing = 0, numData = 0;
        int numItems = 0, highlightItem = 0;
        bool scrollable = false, isScrolling = false, atTop = false, atBottom = false;
        int scrollDir = 0;
        bool enabled = false, selectionEnabled = false;
        float handHeight = 0.0f;
        float swipeAmt = 0.0f, percentPulled = 0.0f;
        bool hasDirection = false, lockedIn = false;
        int nodes = -1; // -1: not a single-user filter
        int hoverMs = -1;
        // since the last reset
        float maxSwipeAmt = 0.0f;
        int maxNodes = 0;
        unsigned polls = 0, sensorUpdates = 0;
        // on request
        bool rowsValid = false;
        std::vector<std::string> rows;
        std::vector<char> rowFlags; // bit 0 header, bit 1 inactive
    };

    static void Read(HamNavList *l, List &s, bool wantRows) {
        s.hand = (int)l->mNavInputType;
        s.mode = (int)l->mRibbonMode;
        const UIListState &st = l->mListState;
        UIListProvider *provider = st.Provider();
        s.selected = st.Selected();
        s.firstShowing = st.FirstShowing();
        s.numShowing = provider ? st.NumShowing() : 0;
        s.numData = provider ? provider->NumData() : 0;
        s.selectedData = provider ? st.SelectedData() : 0;
        s.scrollable = st.ScrollPastMinDisplay();
        s.isScrolling = l->mScrollBehavior.IsScrolling();
        s.atTop = l->mScrollBehavior.AtTop();
        s.atBottom = l->mScrollBehavior.AtBottom();
        s.scrollDir = l->mScrollBehavior.mScrollDir;
        s.enabled = l->mEnabled;
        s.selectionEnabled = l->mSelectionEnabled;
        s.numItems = provider && l->mListRibbonResource ? l->NumItems() : 0;
        s.highlightItem = provider ? l->GetHighlightItem() : 0;
        s.sym.clear();
        if (provider && s.selectedData >= 0 && s.selectedData < s.numData)
            s.sym = provider->DataSymbol(s.selectedData).Str();
        s.handHeight = l->mHandHeight;
        DirectionGestureFilter *f = l->mDirectionGestureFilter;
        s.hasDirection = f && f->HasDirection();
        s.lockedIn = f && f->IsLockedIn();
        s.percentPulled = f ? f->GetPercentPulled() : 0.0f;
        s.nodes = -1;
        s.hoverMs = -1;
        s.swipeAmt = 0.0f;
        if (DirectionGestureFilterSingleUser *single =
                dynamic_cast<DirectionGestureFilterSingleUser *>(f)) {
            s.swipeAmt = single->mArcDetector.GetSwipeAmount();
            s.nodes = single->mArcDetector.NumJointsInPath();
            s.hoverMs = single->mArcDetector.mHoverTimer;
        }
        if (wantRows) {
            s.rows.clear();
            s.rowFlags.clear();
            int n = s.numData < 600 ? s.numData : 600;
            for (int i = 0; i < n; i++) {
                s.rows.push_back(provider->DataSymbol(i).Str());
                s.rowFlags.push_back((provider->IsHeader(i) ? 1 : 0)
                                     | (provider->IsActive(i) ? 0 : 2));
            }
            s.rowsValid = true;
        }
    }
};

namespace Dc3MenuGesture {
namespace {

typedef Dc3MenuGestureAccess::List List;

struct SelectEvent {
    unsigned seq;
    float uiSeconds;
    std::string list;
    std::string sym;
    int hand;
    int selected;
    int firstShowing;
    int source;
};

std::vector<List> gLists;
std::deque<SelectEvent> gEvents;
unsigned gSelects = 0;
bool gWantRows = false;
const float kStaleSeconds = 0.2f;
const size_t kMaxEvents = 256;

float Now() { return TheTaskMgr.UISeconds(); }

List &Find(const HamNavList *list) {
    float now = Now();
    List *oldest = nullptr;
    for (size_t i = 0; i < gLists.size(); i++) {
        if (gLists[i].list == list)
            return gLists[i];
        // a list not polled for a while may be gone, and its address reused
        if (now - gLists[i].lastPoll > 5.0f || now < gLists[i].lastPoll)
            oldest = &gLists[i];
    }
    if (oldest) {
        *oldest = List();
        oldest->list = list;
        return *oldest;
    }
    gLists.push_back(List());
    gLists.back().list = list;
    return gLists.back();
}

void AppendEscaped(std::string &out, const std::string &s) {
    out += '"';
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if ((unsigned char)c < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    out += '"';
}

} // namespace

void NoteSelect(const HamNavList *list, int selected, int firstShowing, const char *sym,
                bool byButton) {
    SelectEvent e;
    e.seq = ++gSelects;
    e.uiSeconds = Now();
    e.list = list ? list->Name() : "";
    e.sym = sym ? sym : "";
    e.hand = 0;
    for (size_t i = 0; i < gLists.size(); i++) {
        if (gLists[i].list == list)
            e.hand = gLists[i].hand;
    }
    e.selected = selected;
    e.firstShowing = firstShowing;
    e.source = byButton ? kSourceButton : kSourceSwipe;
    gEvents.push_back(e);
    while (gEvents.size() > kMaxEvents)
        gEvents.pop_front();
}

void NoteSensorUpdate(const HamNavList *list) { Find(list).sensorUpdates++; }

void NotePoll(HamNavList *list) {
    List &s = Find(list);
    s.name = list->Name();
    Dc3MenuGestureAccess::Read(list, s, gWantRows);
    s.lastPoll = Now();
    s.polls++;
    if (s.swipeAmt > s.maxSwipeAmt)
        s.maxSwipeAmt = s.swipeAmt;
    if (s.nodes > s.maxNodes)
        s.maxNodes = s.nodes;
}

std::string StateJson(unsigned since, bool reset, bool rows) {
    char buf[768];
    float now = Now();
    std::string out;
    const Dc3PoseFill::Filler &fill = NativePoseFiller();
    snprintf(buf, sizeof(buf),
             "{\"ui_seconds\":%.4f,\"controller_mode\":%d,\"selects\":%u,"
             "\"fill\":{\"mode\":\"%s\",\"engaged\":%d,\"fills\":%u,\"reals\":%u},\"lists\":[",
             now, TheGestureMgr && TheGestureMgr->InControllerMode() ? 1 : 0, gSelects,
             Dc3PoseFill::ModeName(fill.GetMode()), fill.Engaged() ? 1 : 0, fill.Fills(), fill.Reals());
    out += buf;
    bool first = true;
    for (size_t i = 0; i < gLists.size(); i++) {
        List &s = gLists[i];
        if (now - s.lastPoll > kStaleSeconds || now < s.lastPoll)
            continue;
        if (!first)
            out += ',';
        first = false;
        out += "{\"name\":";
        AppendEscaped(out, s.name);
        out += ",\"sym\":";
        AppendEscaped(out, s.sym);
        snprintf(buf, sizeof(buf),
                 ",\"hand\":\"%s\",\"mode\":%d,\"selected\":%d,\"selected_data\":%d,"
                 "\"first_showing\":%d,\"num_showing\":%d,\"num_data\":%d,\"num_items\":%d,"
                 "\"highlight_item\":%d,\"scrollable\":%d,\"is_scrolling\":%d,\"at_top\":%d,"
                 "\"at_bottom\":%d,\"scroll_dir\":%d,\"enabled\":%d,\"selection_enabled\":%d,"
                 "\"hand_height\":%.4f,\"swipe_amt\":%.4f,\"percent_pulled\":%.4f,"
                 "\"has_direction\":%d,\"locked_in\":%d,\"nodes\":%d,\"hover_ms\":%d,"
                 "\"max_swipe_amt\":%.4f,\"max_nodes\":%d,\"polls\":%u,\"sensor_updates\":%u",
                 s.hand == 0 ? "right" : "left", s.mode, s.selected, s.selectedData,
                 s.firstShowing, s.numShowing, s.numData, s.numItems, s.highlightItem,
                 s.scrollable ? 1 : 0, s.isScrolling ? 1 : 0, s.atTop ? 1 : 0, s.atBottom ? 1 : 0,
                 s.scrollDir, s.enabled ? 1 : 0, s.selectionEnabled ? 1 : 0, s.handHeight,
                 s.swipeAmt, s.percentPulled, s.hasDirection ? 1 : 0, s.lockedIn ? 1 : 0, s.nodes,
                 s.hoverMs, s.maxSwipeAmt, s.maxNodes, s.polls, s.sensorUpdates);
        out += buf;
        if (rows && s.rowsValid) {
            out += ",\"rows\":[";
            for (size_t r = 0; r < s.rows.size(); r++) {
                if (r)
                    out += ',';
                AppendEscaped(out, s.rows[r]);
            }
            out += "],\"row_flags\":[";
            for (size_t r = 0; r < s.rowFlags.size(); r++) {
                if (r)
                    out += ',';
                out += (char)('0' + s.rowFlags[r]);
            }
            out += ']';
        }
        out += '}';
        if (reset) {
            s.maxSwipeAmt = 0.0f;
            s.maxNodes = 0;
            s.polls = 0;
            s.sensorUpdates = 0;
        }
    }
    out += "],\"events\":[";
    first = true;
    for (size_t i = 0; i < gEvents.size(); i++) {
        const SelectEvent &e = gEvents[i];
        if (e.seq <= since)
            continue;
        if (!first)
            out += ',';
        first = false;
        snprintf(buf, sizeof(buf), "{\"seq\":%u,\"ui_seconds\":%.4f,\"list\":", e.seq, e.uiSeconds);
        out += buf;
        AppendEscaped(out, e.list);
        out += ",\"sym\":";
        AppendEscaped(out, e.sym);
        snprintf(buf, sizeof(buf), ",\"hand\":\"%s\",\"selected\":%d,\"first_showing\":%d,\"source\":\"%s\"}",
                 e.hand == 0 ? "right" : "left", e.selected, e.firstShowing,
                 e.source == kSourceButton ? "button" : "swipe");
        out += buf;
    }
    out += "]}";
    // Rows are captured by the lists' next polls and dropped again once read.
    if (!rows) {
        for (size_t i = 0; i < gLists.size(); i++) {
            gLists[i].rowsValid = false;
            gLists[i].rows.clear();
            gLists[i].rowFlags.clear();
        }
    }
    gWantRows = rows;
    return out;
}

} // namespace Dc3MenuGesture

#endif // HX_NATIVE
