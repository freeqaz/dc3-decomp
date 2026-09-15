#include "os\VirtualKeyboard.h"
#include "obj\Dir.h"
#include "os\User.h"
#include "utl\Symbol.h"

VirtualKeyboard TheVirtualKeyboard;

VirtualKeyboard::VirtualKeyboard()
    : mPobjKeyboardCallback(nullptr), mCallbackReady(false), mMsgOk(false) {}

VirtualKeyboard::~VirtualKeyboard() {}

void VirtualKeyboard::Init() { SetName("virtual_keyboard", ObjectDir::Main()); }

VirtualKeyboardResultMsg::VirtualKeyboardResultMsg(bool ok, const char *text)
    : Message(Type(), ok, text ? text : gNullStr) {}

void VirtualKeyboard::Poll() {
    PlatformPoll();
    if (mCallbackReady) {
        VirtualKeyboardResultMsg msg(mMsgOk, mCallbackMsg.c_str());
        if (mPobjKeyboardCallback) {
            mPobjKeyboardCallback->Handle(msg, true);
        }
        mPobjKeyboardCallback = nullptr;
        mCallbackMsg = gNullStr;
        mMsgOk = false;
        mCallbackReady = false;
    }
}

DataNode VirtualKeyboard::OnShowKeyboardUI(const DataArray *array) {
    int i2 = array->Int(2);
    int i3 = array->Int(3);
    class String s4(array->Str(4));
    class String s5(array->Str(5));
    class String s6(array->Str(6));
    mPobjKeyboardCallback = array->GetObj(7);
    int i8 = 0;
    if (array->Size() >= 9)
        i8 = array->Int(8);
    return ShowKeyboardUI(i2, i3, s4, s5, s6, i8);
}

// w8-g 2026-09-15: ?Terminate@VirtualKeyboard@@QAAXXZ reads 0.00% (4 B) and is
// AT_LIMIT by construction, not by codegen.  In the image it is a linker ICF
// thunk -- a single `b OnlyReturns` -- and the two sides of the accounting
// disagree about where it lives: ham_xbox_r.map credits the symbol to
// os:VirtualKeyboard.obj, while config/373307D9/splits.txt puts its ADDRESS
// inside the Memcard_Xbox range (see the note beside MemcardXbox::Terminate in
// os/Memcard_Xbox.cpp, which folded to the same address).  No source spelling of
// an empty member function can produce a 4-byte branch-to-another-function; only
// the linker can.  3 prior attempts, all refuted.
void VirtualKeyboard::Terminate() {}

void VirtualKeyboard::ClearKeyboardCallback() { mPobjKeyboardCallback = nullptr; }

#ifdef HX_NATIVE
void VirtualKeyboard::PlatformPoll() {
    // Xbox keyboard not available on native
}

const char *VirtualKeyboard::GetInputString() {
    return "";
}

DataNode VirtualKeyboard::ShowKeyboardUI(int, int, String, String, String, int) {
    return DataNode(0);
}
#endif

BEGIN_HANDLERS(VirtualKeyboard)
    HANDLE(show_keyboard, OnShowKeyboardUI)
    HANDLE_ACTION(clear_callback, ClearKeyboardCallback())
    HANDLE_EXPR(get_input_string, GetInputString())
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS
