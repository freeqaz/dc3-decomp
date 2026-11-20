// test_text_input.cpp -- the engine's text-editing input queue
// (platform/TextInput_Native.h), the keyboard half of the natively drawn UI
// (lane np-ui-engine / engine np-ui2d).
//
// No window: the tests stand in for the keyboard through the device calls the
// GLFW callbacks make (TextInputDeviceChar / TextInputDeviceKey, and the
// callbacks themselves for the chain), and read the pad map through
// JoypadKeyboardButtons as JoypadPoll does.
//
// Each TEST starts from TextInputTerminate() (TextInputTest's SetUp): the
// state is process-global.
//
// Sabotages tried, 2026-11-20, one edit to the engine at a time, each restored
// (19 edits, every one red, and every test here red under at least one):
//   TextInput_Native.cpp: characters queued without capture; text keys not
//     taken from the pad under capture, and taken from it without capture; no
//     held-over latch; a key held at capture typing; the key chain and the
//     character chain each dropped; overlong UTF-8 accepted; a full queue
//     keeping the newest; no clear on a capture change; Backspace not a text
//     key; edit-key modifiers dropped; Left reported as Right; control
//     characters queued; key repeat ignored; injection needing capture; a
//     four-byte sequence decoded as three; a wrong three-byte lead on encode.
//   Joypad_Native.cpp: the pad map not asking TextInputPadKeyAllowed.

#include "platform/JoypadKeyMap.h"
#include "platform/TextInput_Native.h"

#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace {

class TextInputTest : public ::testing::Test {
protected:
    void SetUp() override {
        TextInputSetChainForTest(nullptr, nullptr);
        TextInputTerminate();
    }
    void TearDown() override {
        TextInputSetChainForTest(nullptr, nullptr);
        TextInputTerminate();
    }
};

std::vector<TextInputEvent> DrainAll() {
    std::vector<TextInputEvent> all;
    TextInputEvent buf[7]; // smaller than the queue: Drain is called repeatedly
    for (int n; (n = TextInputDrain(buf, 7)) > 0;) all.insert(all.end(), buf, buf + n);
    return all;
}

// The drained events as text: characters as UTF-8, edit keys as <Name>.
std::string Typed() {
    std::string s;
    for (const TextInputEvent& e : DrainAll()) {
        switch (e.type) {
        case kTextInputChar: {
            char utf8[5];
            TextInputEncodeUtf8(e.codepoint, utf8);
            s += utf8;
            break;
        }
        case kTextInputBackspace: s += "<Bksp>"; break;
        case kTextInputDelete:    s += "<Del>"; break;
        case kTextInputLeft:      s += "<Left>"; break;
        case kTextInputRight:     s += "<Right>"; break;
        case kTextInputHome:      s += "<Home>"; break;
        case kTextInputEnd:       s += "<End>"; break;
        }
    }
    return s;
}

// A key tap as GLFW delivers it: key press, the character it types, release.
void Tap(int key, unsigned int ch = 0) {
    TextInputDeviceKey(key, GLFW_PRESS, 0);
    if (ch) TextInputDeviceChar(ch);
    TextInputDeviceKey(key, GLFW_RELEASE, 0);
}

bool HeldIn(int key, void* ctx) { return ((std::set<int>*)ctx)->count(key) != 0; }
unsigned int PadButtons(std::set<int> held) { return JoypadKeyboardButtons(HeldIn, &held); }

// ---------------------------------------------------------------------------
// Capture and the queue
// ---------------------------------------------------------------------------

TEST_F(TextInputTest, OffByDefaultAndNothingIsQueued) {
    EXPECT_FALSE(TextInputCapture());
    Tap(GLFW_KEY_Q, 'q');
    Tap(GLFW_KEY_BACKSPACE);
    Tap(GLFW_KEY_LEFT);
    EXPECT_EQ(TextInputPending(), 0) << "with capture off the keyboard types nothing";
    EXPECT_EQ(Typed(), "");
}

TEST_F(TextInputTest, CaptureQueuesCharactersAndEditKeysInOrder) {
    TextInputSetCapture(true);
    EXPECT_TRUE(TextInputCapture());
    Tap(GLFW_KEY_Q, 'q');
    Tap(GLFW_KEY_U, 'u');
    Tap(GLFW_KEY_LEFT);
    Tap(GLFW_KEY_BACKSPACE);
    Tap(GLFW_KEY_E, 'E'); // Shift+E: the OS composed the capital
    Tap(GLFW_KEY_HOME);
    Tap(GLFW_KEY_DELETE);
    Tap(GLFW_KEY_END);
    Tap(GLFW_KEY_RIGHT);
    EXPECT_EQ(TextInputPending(), 9);
    EXPECT_EQ(Typed(), "qu<Left><Bksp>E<Home><Del><End><Right>");
    EXPECT_EQ(TextInputPending(), 0) << "Drain removes what it returns";
}

TEST_F(TextInputTest, KeysThatAreNotTextQueueNothing) {
    TextInputSetCapture(true);
    for (int key : {GLFW_KEY_ENTER, GLFW_KEY_ESCAPE, GLFW_KEY_TAB, GLFW_KEY_UP, GLFW_KEY_DOWN,
                    GLFW_KEY_F1, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_PAGE_UP, GLFW_KEY_KP_ENTER})
        Tap(key);
    // Control characters from the OS are not text either (Enter, Tab, Backspace
    // and Delete arrive as keys).
    for (unsigned int ch : {0x08u, 0x09u, 0x0Du, 0x1Bu, 0x7Fu}) TextInputDeviceChar(ch);
    EXPECT_EQ(Typed(), "");
}

TEST_F(TextInputTest, KeyRepeatIsDeliveredAsTheOsSendsIt) {
    TextInputSetCapture(true);
    // Hold Backspace: one press, three repeats, one release.
    TextInputDeviceKey(GLFW_KEY_BACKSPACE, GLFW_PRESS, 0);
    for (int i = 0; i < 3; i++) TextInputDeviceKey(GLFW_KEY_BACKSPACE, GLFW_REPEAT, 0);
    TextInputDeviceKey(GLFW_KEY_BACKSPACE, GLFW_RELEASE, 0);
    EXPECT_EQ(Typed(), "<Bksp><Bksp><Bksp><Bksp>");
    // Hold A: the character callback repeats with the key.
    TextInputDeviceKey(GLFW_KEY_A, GLFW_PRESS, 0);
    TextInputDeviceChar('a');
    for (int i = 0; i < 2; i++) {
        TextInputDeviceKey(GLFW_KEY_A, GLFW_REPEAT, 0);
        TextInputDeviceChar('a');
    }
    TextInputDeviceKey(GLFW_KEY_A, GLFW_RELEASE, 0);
    EXPECT_EQ(Typed(), "aaa");
}

TEST_F(TextInputTest, EditKeysCarryTheirModifiers) {
    TextInputSetCapture(true);
    TextInputDeviceKey(GLFW_KEY_LEFT, GLFW_PRESS, GLFW_MOD_CONTROL | GLFW_MOD_SHIFT);
    TextInputDeviceKey(GLFW_KEY_BACKSPACE, GLFW_PRESS, GLFW_MOD_ALT | GLFW_MOD_CAPS_LOCK);
    TextInputDeviceChar('x');
    std::vector<TextInputEvent> ev = DrainAll();
    ASSERT_EQ(ev.size(), 3u);
    EXPECT_EQ(ev[0].type, kTextInputLeft);
    EXPECT_EQ(ev[0].mods, kTextInputCtrl | kTextInputShift);
    EXPECT_EQ(ev[0].codepoint, 0u);
    EXPECT_EQ(ev[1].type, kTextInputBackspace);
    EXPECT_EQ(ev[1].mods, kTextInputAlt) << "lock keys are not modifiers";
    EXPECT_EQ(ev[2].type, kTextInputChar);
    EXPECT_EQ(ev[2].codepoint, (uint32_t)'x');
    EXPECT_EQ(ev[2].mods, 0);
}

TEST_F(TextInputTest, TurningCaptureOnOrOffEmptiesTheQueue) {
    TextInputInjectUtf8("stale");
    EXPECT_EQ(TextInputPending(), 5) << "injection queues with capture off";
    TextInputSetCapture(true);
    EXPECT_EQ(TextInputPending(), 0) << "a field that just took focus starts empty";
    Tap(GLFW_KEY_A, 'a');
    TextInputSetCapture(true); // no transition: nothing is dropped
    EXPECT_EQ(TextInputPending(), 1);
    TextInputSetCapture(false);
    EXPECT_EQ(TextInputPending(), 0);
}

TEST_F(TextInputTest, AFullQueueKeepsWhatWasTypedFirst) {
    TextInputSetCapture(true);
    for (int i = 0; i < kTextInputQueueMax + 40; i++) TextInputDeviceChar('a' + (i % 26));
    EXPECT_EQ(TextInputPending(), (int)kTextInputQueueMax);
    std::vector<TextInputEvent> ev = DrainAll();
    ASSERT_EQ(ev.size(), (size_t)kTextInputQueueMax);
    EXPECT_EQ(ev.front().codepoint, (uint32_t)'a');
    EXPECT_EQ(ev.back().codepoint, (uint32_t)('a' + ((kTextInputQueueMax - 1) % 26)));
    // The ring wraps: after a drain it takes a full load again, in order.
    for (int i = 0; i < 10; i++) TextInputDeviceChar('0' + i);
    EXPECT_EQ(Typed(), "0123456789");
}

// A key held when capture turns on was pressed for something else (Y opens
// the search box): its repeats type nothing; released and pressed again, it
// types.
TEST_F(TextInputTest, AKeyHeldWhenCaptureTurnsOnTypesNothingUntilReleased) {
    TextInputDeviceKey(GLFW_KEY_Y, GLFW_PRESS, 0); // capture off: the pad's Y
    TextInputDeviceChar('y');
    TextInputSetCapture(true);
    for (int i = 0; i < 3; i++) {
        TextInputDeviceKey(GLFW_KEY_Y, GLFW_REPEAT, 0);
        TextInputDeviceChar('y');
    }
    EXPECT_EQ(Typed(), "") << "the held key's repeat typed into the field";
    // Another key typed meanwhile is text.
    Tap(GLFW_KEY_A, 'a');
    TextInputDeviceKey(GLFW_KEY_Y, GLFW_REPEAT, 0);
    TextInputDeviceChar('y');
    EXPECT_EQ(Typed(), "a");
    TextInputDeviceKey(GLFW_KEY_Y, GLFW_RELEASE, 0);
    Tap(GLFW_KEY_Y, 'y');
    EXPECT_EQ(Typed(), "y");
    // The same for an edit key held across the switch.
    TextInputSetCapture(false);
    TextInputDeviceKey(GLFW_KEY_BACKSPACE, GLFW_PRESS, 0);
    TextInputSetCapture(true);
    TextInputDeviceKey(GLFW_KEY_BACKSPACE, GLFW_REPEAT, 0);
    EXPECT_EQ(Typed(), "");
    TextInputDeviceKey(GLFW_KEY_BACKSPACE, GLFW_RELEASE, 0);
    Tap(GLFW_KEY_BACKSPACE);
    EXPECT_EQ(Typed(), "<Bksp>");
}

// ---------------------------------------------------------------------------
// Injection and UTF-8
// ---------------------------------------------------------------------------

TEST_F(TextInputTest, InjectionQueuesWithoutCaptureAndPressesNoPadButton) {
    TextInputInjectChar('q');
    TextInputInjectKey(kTextInputBackspace);
    TextInputInjectKey(kTextInputLeft, kTextInputShift);
    TextInputInjectKey(kTextInputChar); // not a key: ignored
    TextInputInjectChar(0x0A);          // a control character: ignored
    std::vector<TextInputEvent> ev = DrainAll();
    ASSERT_EQ(ev.size(), 3u);
    EXPECT_EQ(ev[0].type, kTextInputChar);
    EXPECT_EQ(ev[0].codepoint, (uint32_t)'q');
    EXPECT_EQ(ev[1].type, kTextInputBackspace);
    EXPECT_EQ(ev[2].type, kTextInputLeft);
    EXPECT_EQ(ev[2].mods, kTextInputShift);
    EXPECT_EQ(PadButtons({}), 0u);
}

TEST_F(TextInputTest, Utf8InjectionDecodesEveryLength) {
    // 1, 2, 3 and 4 byte characters: q, e-acute, a check mark, an emoji.
    EXPECT_EQ(TextInputInjectUtf8("q\xC3\xA9\xE2\x9C\x93\xF0\x9F\x98\x80"), 4);
    std::vector<TextInputEvent> ev = DrainAll();
    ASSERT_EQ(ev.size(), 4u);
    EXPECT_EQ(ev[0].codepoint, 0x71u);
    EXPECT_EQ(ev[1].codepoint, 0xE9u);
    EXPECT_EQ(ev[2].codepoint, 0x2713u);
    EXPECT_EQ(ev[3].codepoint, 0x1F600u);
    for (const TextInputEvent& e : ev) EXPECT_EQ(e.type, kTextInputChar);
    // And back: the drained characters re-encode to the same bytes.
    TextInputInjectUtf8("q\xC3\xA9\xE2\x9C\x93\xF0\x9F\x98\x80");
    EXPECT_EQ(Typed(), "q\xC3\xA9\xE2\x9C\x93\xF0\x9F\x98\x80");
}

TEST_F(TextInputTest, InvalidUtf8IsSkippedNotInvented) {
    // A lone continuation byte, a truncated 3-byte lead, an overlong '/',
    // a UTF-8 encoded surrogate, a value past U+10FFFF, and control bytes.
    EXPECT_EQ(TextInputInjectUtf8("a\x80" "b\xE2\x9C" "c\xC0\xAF" "d\xED\xA0\x80" "e\xF4\x90\x80\x80"
                                  "f\n\tg"),
              7);
    EXPECT_EQ(Typed(), "abcdefg");
    EXPECT_EQ(TextInputInjectUtf8(nullptr), 0);
    EXPECT_EQ(TextInputInjectUtf8(""), 0);
}

TEST_F(TextInputTest, Utf8EncodeDecodeRoundTrip) {
    struct { uint32_t cp; int bytes; } cases[] = {
        {0x24, 1}, {0x7F, 1}, {0x80, 2}, {0x7FF, 2}, {0x800, 3}, {0xFFFD, 3},
        {0xFFFF, 3}, {0x10000, 4}, {0x10FFFF, 4},
    };
    for (const auto& c : cases) {
        char buf[5];
        ASSERT_EQ(TextInputEncodeUtf8(c.cp, buf), c.bytes) << std::hex << c.cp;
        EXPECT_EQ(buf[c.bytes], 0) << "NUL-terminated";
        uint32_t back = 0;
        EXPECT_EQ(TextInputDecodeUtf8(buf, &back), c.bytes) << std::hex << c.cp;
        EXPECT_EQ(back, c.cp);
    }
    char buf[5];
    EXPECT_EQ(TextInputEncodeUtf8(0xD800, buf), 0) << "a surrogate is not a character";
    EXPECT_EQ(TextInputEncodeUtf8(0x110000, buf), 0);
    uint32_t cp = 1;
    EXPECT_EQ(TextInputDecodeUtf8("", &cp), 0) << "end of string";
    EXPECT_EQ(TextInputDecodeUtf8("\xFF" "a", &cp), 1);
    EXPECT_EQ(cp, 0xFFFDu) << "an invalid byte decodes as U+FFFD, one byte consumed";
}

// ---------------------------------------------------------------------------
// Capture against the pad map (platform/JoypadKeyMap.h)
// ---------------------------------------------------------------------------

struct PadKey {
    int key;
    JoypadButton button;
    const char* what;
};
// The pad map's keys that type or edit text...
const PadKey kTextPadKeys[] = {
    {GLFW_KEY_X, kPad_Square, "X"},  {GLFW_KEY_Y, kPad_Tri, "Y"},
    {GLFW_KEY_Q, kPad_L1, "Q"},      {GLFW_KEY_E, kPad_R1, "E"},
    {GLFW_KEY_Z, kPad_L2, "Z"},      {GLFW_KEY_C, kPad_R2, "C"},
    {GLFW_KEY_SPACE, kPad_Start, "Space"},
    {GLFW_KEY_BACKSPACE, kPad_Circle, "Backspace"},
};
// ...and the ones that keep their pad meaning under capture.
const PadKey kNavPadKeys[] = {
    {GLFW_KEY_UP, kPad_DUp, "Up"},        {GLFW_KEY_DOWN, kPad_DDown, "Down"},
    {GLFW_KEY_LEFT, kPad_DLeft, "Left"},  {GLFW_KEY_RIGHT, kPad_DRight, "Right"},
    {GLFW_KEY_ENTER, kPad_X, "Enter"},    {GLFW_KEY_ESCAPE, kPad_Circle, "Esc"},
    {GLFW_KEY_TAB, kPad_Select, "Tab"},
};

TEST_F(TextInputTest, PadMapIsUnchangedWithCaptureOff) {
    for (const PadKey& k : kTextPadKeys) EXPECT_EQ(PadButtons({k.key}), 1u << k.button) << k.what;
    for (const PadKey& k : kNavPadKeys) EXPECT_EQ(PadButtons({k.key}), 1u << k.button) << k.what;
    // The two lists are the whole map: a key added to it must be classified.
    int count = 0;
    JoypadKeyBindings(&count);
    EXPECT_EQ((size_t)count, std::size(kTextPadKeys) + std::size(kNavPadKeys));
}

// Typing "queen" must not change the sort (Q, E are LB/RB) or go back.
TEST_F(TextInputTest, UnderCaptureTextKeysAreNotPadButtons) {
    TextInputSetCapture(true);
    for (const PadKey& k : kTextPadKeys) {
        EXPECT_EQ(PadButtons({k.key}), 0u) << k.what << " pressed a pad button while typing";
        EXPECT_EQ(PadButtons({}), 0u);
    }
    for (const PadKey& k : kNavPadKeys)
        EXPECT_EQ(PadButtons({k.key}), 1u << k.button) << k.what << " lost its pad meaning";
    // Together: only the navigation key's button.
    EXPECT_EQ(PadButtons({GLFW_KEY_Q, GLFW_KEY_ENTER, GLFW_KEY_SPACE}), 1u << kPad_X);
    PadButtons({});
    // Esc and Backspace are both B: under capture only Esc is.
    EXPECT_EQ(PadButtons({GLFW_KEY_ESCAPE}), 1u << kPad_Circle);
    EXPECT_EQ(PadButtons({GLFW_KEY_BACKSPACE}), 0u);
    PadButtons({});
    TextInputSetCapture(false);
    for (const PadKey& k : kTextPadKeys) {
        EXPECT_EQ(PadButtons({k.key}), 1u << k.button) << k.what << " after capture";
        PadButtons({});
    }
}

// Backspace on an empty field closes it: the key is still down when capture
// goes off, and must not then go back as the pad's B.
TEST_F(TextInputTest, ATextKeyHeldAcrossCaptureOffIsNotAPadPress) {
    TextInputSetCapture(true);
    EXPECT_EQ(PadButtons({GLFW_KEY_BACKSPACE}), 0u);
    TextInputSetCapture(false);
    EXPECT_EQ(PadButtons({GLFW_KEY_BACKSPACE}), 0u) << "still held from the field: not B";
    EXPECT_EQ(PadButtons({GLFW_KEY_BACKSPACE, GLFW_KEY_ENTER}), 1u << kPad_X);
    EXPECT_EQ(PadButtons({}), 0u); // released
    EXPECT_EQ(PadButtons({GLFW_KEY_BACKSPACE}), 1u << kPad_Circle) << "a new press is B again";
}

TEST_F(TextInputTest, KeyClassification) {
    for (int key : {GLFW_KEY_SPACE, GLFW_KEY_A, GLFW_KEY_Z, GLFW_KEY_0, GLFW_KEY_SLASH,
                    GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_WORLD_2, GLFW_KEY_KP_5, GLFW_KEY_KP_ADD,
                    GLFW_KEY_KP_EQUAL, GLFW_KEY_BACKSPACE, GLFW_KEY_DELETE, GLFW_KEY_HOME,
                    GLFW_KEY_END})
        EXPECT_TRUE(TextInputKeyIsText(key)) << "key " << key;
    for (int key : {GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_ENTER,
                    GLFW_KEY_KP_ENTER, GLFW_KEY_ESCAPE, GLFW_KEY_TAB, GLFW_KEY_F1,
                    GLFW_KEY_LEFT_SHIFT, GLFW_KEY_PAGE_DOWN, GLFW_KEY_INSERT, -1})
        EXPECT_FALSE(TextInputKeyIsText(key)) << "key " << key;
}

// ---------------------------------------------------------------------------
// The GLFW callbacks chain
// ---------------------------------------------------------------------------

struct ChainLog {
    std::vector<unsigned int> chars;
    std::vector<int> keys;    // key
    std::vector<int> actions; // action
    GLFWwindow* window = nullptr;
};
ChainLog gChain;

void PrevChar(GLFWwindow* w, unsigned int cp) {
    gChain.window = w;
    gChain.chars.push_back(cp);
}
void PrevKey(GLFWwindow* w, int key, int, int action, int) {
    gChain.window = w;
    gChain.keys.push_back(key);
    gChain.actions.push_back(action);
}

// Whatever was installed before ours (the game's Keyboard, the fullscreen
// toggle, ImGui's backend when it came first) still gets every event, with
// capture on and with it off.
TEST_F(TextInputTest, CallbacksForwardToTheOnesInstalledBefore) {
    gChain = ChainLog();
    GLFWwindow* window = reinterpret_cast<GLFWwindow*>(0x1234);
    TextInputSetChainForTest(PrevChar, PrevKey);
    for (bool capture : {false, true}) {
        TextInputSetCapture(capture);
        TextInputGlfwKey(window, GLFW_KEY_Q, 24, GLFW_PRESS, 0);
        TextInputGlfwChar(window, 'q');
        TextInputGlfwKey(window, GLFW_KEY_Q, 24, GLFW_RELEASE, 0);
        TextInputGlfwKey(window, GLFW_KEY_F11, 95, GLFW_PRESS, 0);
    }
    EXPECT_EQ(gChain.chars, (std::vector<unsigned int>{'q', 'q'}));
    EXPECT_EQ(gChain.keys,
              (std::vector<int>{GLFW_KEY_Q, GLFW_KEY_Q, GLFW_KEY_F11, GLFW_KEY_Q, GLFW_KEY_Q,
                                GLFW_KEY_F11}));
    EXPECT_EQ(gChain.actions, (std::vector<int>{GLFW_PRESS, GLFW_RELEASE, GLFW_PRESS, GLFW_PRESS,
                                                GLFW_RELEASE, GLFW_PRESS}));
    EXPECT_EQ(gChain.window, window);
    // And ours ran too: the capture-on half queued its 'q'.
    EXPECT_EQ(Typed(), "q");
    // With nothing installed before, the callbacks stand alone.
    TextInputSetChainForTest(nullptr, nullptr);
    TextInputGlfwKey(window, GLFW_KEY_A, 38, GLFW_PRESS, 0);
    TextInputGlfwChar(window, 'a');
    EXPECT_EQ(Typed(), "a");
    EXPECT_EQ(gChain.chars.size(), 2u);
}

} // namespace
