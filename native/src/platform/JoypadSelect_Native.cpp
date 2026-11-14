// DC3 Native Port - the game's answer to the scripted-input `highlight NAME`
// directive (engine platform/JoypadScriptHook.h).
//
// The focused component, when it is a HamNavList (song select, the difficulty
// list, every menu list), is asked through its own DTA handlers -- the ones a
// person could call from the debug console -- where NAME is relative to the
// highlight:
//
//   data_index NAME        the entry's data row (-1 when the list has none)
//   get_selected_sym       the highlighted entry
//   is_scrolling_settled   false while a scroll is still animating
//
// HamNavList::GetSelectedSym reads the provider at mListState.SelectedData()
// and data_index asks the same provider, so the two rows are in one space;
// header rows (song select's tier headers) are rows of that space too, and the
// directive's closed loop does not care how many rows one press moves.

#include "hamobj/HamNavList.h"
#include "obj/Msg.h"
#include "platform/JoypadScriptHook.h"
#include "ui/UI.h"
#include "ui/UIComponent.h"

#ifdef MILO_JOYPAD_SCRIPT_HIGHLIGHT

namespace {

JoypadScriptSelectState NativeSelectQuery(const char *name, int *delta) {
    if (!TheUI) return kJoypadSelectNoList;
    HamNavList *list = dynamic_cast<HamNavList *>(TheUI->FocusComponent());
    if (!list) return kJoypadSelectNoList;
    Symbol target(name);
    static Symbol data_index("data_index");
    static Symbol get_selected_sym("get_selected_sym");
    static Symbol is_scrolling_settled("is_scrolling_settled");
    if (!list->Handle(Message(is_scrolling_settled), true).Int()) return kJoypadSelectMoving;
    Symbol on = list->Handle(Message(get_selected_sym), true).Sym();
    if (on == target) {
        *delta = 0;
        return kJoypadSelectReady;
    }
    int want = list->Handle(Message(data_index, target), true).Int();
    if (want < 0) return kJoypadSelectNotFound;
    int here = list->Handle(Message(data_index, on), true).Int();
    *delta = want - here;
    // Two rows with one name (cannot be told apart by name): never report 0
    // for a row that is not the target.
    if (*delta == 0) *delta = 1;
    return kJoypadSelectReady;
}

} // namespace

void JoypadSelect_NativeRegister() { JoypadScriptSetSelectQuery(NativeSelectQuery); }

#else

// An engine without the directive: scripts that use it time out and say so.
void JoypadSelect_NativeRegister() {}

#endif
