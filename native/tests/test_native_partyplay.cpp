// Native party-mode lane (branch native-partyplay, 2026-09-30).
//
// HttpInputTest: POST /api/input/press must reach the UI as a pad-0 button.
//
// Before this lane the endpoint answered {"ok":true} and did nothing: the
// press was queued in HttpServer::mImmediateButtons, and the only drain,
// ConsumeHttpButtons(), is called from the engine's JoypadPoll under
// DC3_HTTP_SERVER -- which libmilo-engine.a is compiled WITHOUT (the macro is
// defined on the dc3-native target only).  `objdump -d dc3-native` counted 0
// calls to ConsumeHttpButtons.  Measured by the 2026-09-30 assert harvest: no
// effect on main_screen or the results screens, immediate or delayed.
//
// The engine drives the route to main_screen with idle-multiuser's sibling
// idle-main.txt (title confirm, then nothing); the test then presses confirm
// over HTTP.  main_screen's first item is `gameplay` -> choose_mode_screen,
// so reaching choose_mode_screen proves the press arrived.  Presses are
// retried (bounded): a HamNavList drops input while its enter animation runs,
// exactly as the image does.
//
// Gated by DC3_DTA_FLOW_TESTS=1 (drives the real engine as a subprocess).

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

std::string BinaryDir() {
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0)
        return ".";
    buf[len] = '\0';
    std::string p(buf);
    size_t slash = p.rfind('/');
    return slash != std::string::npos ? p.substr(0, slash) : ".";
}

bool Exists(const std::string &p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

struct HttpRun {
    std::string output;
    std::string setupError;
    std::string driver; // the driver's own verdict lines
};

// Boot to main_screen, then POST confirm until the screen changes (at most
// `tries` presses, 2 s apart).  Everything is bounded: the engine is killed by
// PID (never by pattern), and `timeout` caps the whole run.
HttpRun RunHttpPressOnMain(int port, int tries) {
    HttpRun r;
    std::string bin = BinaryDir() + "/dc3-native";
    std::string script = BinaryDir() + "/../../scripts/dc3-input-flows/idle-main.txt";
    if (!Exists(bin)) {
        r.setupError = "dc3-native does not exist at " + bin +
            " (scripts/native_test.sh builds it)";
        return r;
    }
    if (!Exists(script)) {
        r.setupError = "input flow script missing: " + script;
        return r;
    }
    char logTmpl[] = "/tmp/dc3_http_input_XXXXXX";
    int fd = mkstemp(logTmpl);
    if (fd < 0) {
        r.setupError = "mkstemp failed";
        return r;
    }
    close(fd);
    std::string log = logTmpl;
    std::ostringstream sh;
    sh << "cd " << BinaryDir() << " && "
       << "( MILO_HEADLESS=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1"
       << " DC3_HTTP=1 DC3_HTTP_PORT=" << port << " MILO_MAX_FRAMES=0"
       << " MILO_INPUT_SCRIPT=" << script
       << " timeout 240 ./dc3-native > " << log << " 2>&1 & echo $! > " << log << ".pid ); "
       << "pid=$(cat " << log << ".pid); api=http://127.0.0.1:" << port << "/api; "
       << "scr() { curl -s -m 5 $api/screen | sed -n 's/.*\"screen\":\"\\([^\"]*\\)\".*/\\1/p'; }; "
       << "end=$((SECONDS+150)); "
       << "until [ \"$(scr)\" = main_screen ] || [ $SECONDS -ge $end ] || ! kill -0 $pid 2>/dev/null;"
       << " do sleep 1; done; "
       << "echo \"DRIVER: at=$(scr)\"; sleep 2; "
       << "for i in $(seq 1 " << tries << "); do "
       << "  s=$(scr); [ \"$s\" != main_screen ] && break; "
       << "  curl -s -m 5 -X POST -d '{\"button\":\"confirm\"}' $api/input/press > /dev/null; "
       << "  echo \"DRIVER: press $i\"; sleep 2; "
       << "done; "
       << "echo \"DRIVER: final=$(scr)\"; "
       << "kill $pid 2>/dev/null; sleep 1; kill -9 $pid 2>/dev/null; "
       << "cat " << log << "; rm -f " << log << " " << log << ".pid";
    FILE *p = popen(sh.str().c_str(), "r");
    if (!p) {
        r.setupError = "popen failed";
        return r;
    }
    char buf[4096];
    while (fgets(buf, sizeof buf, p)) {
        r.output += buf;
        if (!strncmp(buf, "DRIVER:", 7))
            r.driver += buf;
    }
    pclose(p);
    return r;
}

} // namespace

class HttpInputTest : public ::testing::Test {
protected:
    static HttpRun sRun;
    static bool sRan;
    static void SetUpTestSuite() {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            return;
        sRun = RunHttpPressOnMain(9000 + (getpid() % 400), 8);
        sRan = true;
    }
    void SetUp() override {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
        if (!sRan)
            GTEST_SKIP() << "engine did not run";
        if (!sRun.setupError.empty())
            GTEST_FAIL() << sRun.setupError;
    }
    bool Has(const char *s) const { return sRun.output.find(s) != std::string::npos; }
};
HttpRun HttpInputTest::sRun;
bool HttpInputTest::sRan = false;

TEST_F(HttpInputTest, PressReachesTheUIAsAPad0Button) {
    ASSERT_TRUE(Has("Screen 'main_screen' Enter"))
        << "precondition: the route never reached main_screen\n" << sRun.driver;
    ASSERT_TRUE(Has("DRIVER: press 1"))
        << "precondition: the driver never pressed\n" << sRun.driver;
    EXPECT_TRUE(Has("Screen 'choose_mode_screen' Enter"))
        << "POST /api/input/press confirm on main_screen never reached the UI "
           "(the queued bits are never drained into a ButtonDownMsg)\n"
        << sRun.driver;
    EXPECT_FALSE(Has("Caught SIGSEGV"));
}

// ===========================================================================
// Owner-control holders across the native delete cascade
// ===========================================================================
//
// The image deletes a dir's objects with `delete`, and ~Object runs
// ReplaceRefs(nullptr): every ref pointing at the dying object gets
// ObjRef::Replace(nullptr).  For an OWNER-CONTROL holder that call is the only
// way the owner hears about it -- ObjOwnerPtr::Replace and an
// ObjPtrList<kObjListOwnerControl>::Node::Replace both forward to
// mOwner->Replace(ref, nullptr), and the owner keeps shadow state in step:
//   * TypeProps::Replace (ReplaceObject) nulls the property DataNode that held
//     the object;
//   * RndEnviron::Replace re-points mAmbientFogOwner at the env itself.
// Native ObjectDir::DeleteObjects instead nullifies refs with NullifyAllRefs
// (Phase 0, and ~Object during a cascade), which bypasses Replace on purpose
// (re-entrancy).  The ref was nulled, the owner never heard, and:
//   * a property still held the freed object -- party mode's round-3 crash:
//     char_objects.dta's cached `vo_bank` property on a HamCharacter kept its
//     old character_vo dir after an outfit reload deleted it, and
//     world_objects.dta's play_character_vo called {$vo_bank ...} on it;
//   * an environ's fog owner read NULL -- RndEnviron::FogEnable dereferenced it
//     on every UI draw of party_mode_signin_screen;
//   * a group kept a NULL child (RndGroup::Replace erases the node): a
//     sound_group's get_group_children handed ui_objects.dta's `shuffle` a
//     null $elem (`$elem = <null> not function or object`, 8x per party song).

#include "test_helpers.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "rndobj/Env.h"
#include "rndobj/Group.h"

class OwnerControlCascadeTest : public EngineTestFixture {};

// Control: outside a cascade (ReplaceRefs) the property is nulled.  Passes
// before and after the fix.
TEST_F(OwnerControlCascadeTest, TypePropsObjectValueIsNulledByPlainDelete) {
    // A property with no PropSync lands in the owner's TypeProps, as
    // char_objects.dta's `vo_bank` does on a HamCharacter.
    Hmx::Object *owner = Hmx::Object::New<Hmx::Object>();
    Hmx::Object *value = Hmx::Object::New<Hmx::Object>();
    owner->SetProperty(Symbol("vo_bank"), DataNode(value));
    ASSERT_EQ(owner->Property(Symbol("vo_bank"), false)->UncheckedObj(), value);
    delete value;
    const DataNode *n = owner->Property(Symbol("vo_bank"), false);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->UncheckedObj(), nullptr);
    delete owner;
}

TEST_F(OwnerControlCascadeTest, TypePropsObjectValueIsNulledByTheDirCascade) {
    Hmx::Object *owner = Hmx::Object::New<Hmx::Object>();
    ObjectDir *dir = Hmx::Object::New<ObjectDir>();
    dir->SetName("typeprops_cascade_dir", ObjectDir::Main());
    Hmx::Object *value = Hmx::Object::New<Hmx::Object>();
    value->SetName("character_vo", dir);
    owner->SetProperty(Symbol("vo_bank"), DataNode(value));
    ASSERT_EQ(owner->Property(Symbol("vo_bank"), false)->UncheckedObj(), value);
    delete dir; // native three-phase cascade frees `value`
    const DataNode *n = owner->Property(Symbol("vo_bank"), false);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->UncheckedObj(), nullptr)
        << "the property still holds the object the cascade freed "
           "(TypeProps::Replace never ran)";
    delete owner;
}

// Control for the environ: outside a cascade the fog owner falls back to self.
TEST_F(OwnerControlCascadeTest, EnvironFogOwnerFallsBackToSelfOnPlainDelete) {
    RndEnviron *env = Hmx::Object::New<RndEnviron>();
    RndEnviron *owner = Hmx::Object::New<RndEnviron>();
    env->SetProperty(Symbol("ambient_fog_owner"), DataNode(owner));
    ASSERT_EQ(env->AmbientFogOwner(), owner);
    delete owner;
    EXPECT_EQ(env->AmbientFogOwner(), env);
    delete env;
}

TEST_F(OwnerControlCascadeTest, EnvironFogOwnerFallsBackToSelfInTheDirCascade) {
    RndEnviron *env = Hmx::Object::New<RndEnviron>();
    ObjectDir *dir = Hmx::Object::New<ObjectDir>();
    dir->SetName("env_cascade_dir", ObjectDir::Main());
    RndEnviron *owner = Hmx::Object::New<RndEnviron>();
    owner->SetName("fog_owner.env", dir);
    env->SetProperty(Symbol("ambient_fog_owner"), DataNode(owner));
    ASSERT_EQ(env->AmbientFogOwner(), owner);
    delete dir;
    EXPECT_EQ(env->AmbientFogOwner(), env)
        << "the fog owner was nulled instead of re-pointed at the env "
           "(RndEnviron::Replace never ran); FogEnable() would dereference NULL";
    delete env;
}

// Control: outside a cascade the child's node is erased (RndGroup::Replace).
TEST_F(OwnerControlCascadeTest, GroupDropsADeletedChildOnPlainDelete) {
    RndGroup *group = Hmx::Object::New<RndGroup>();
    Hmx::Object *child = Hmx::Object::New<Hmx::Object>();
    group->AddObject(child);
    ASSERT_EQ(group->Objects().size(), 1);
    delete child;
    EXPECT_EQ(group->Objects().size(), 0);
    delete group;
}

TEST_F(OwnerControlCascadeTest, GroupDropsADeletedChildInTheDirCascade) {
    RndGroup *group = Hmx::Object::New<RndGroup>();
    ObjectDir *dir = Hmx::Object::New<ObjectDir>();
    dir->SetName("group_cascade_dir", ObjectDir::Main());
    Hmx::Object *child = Hmx::Object::New<Hmx::Object>();
    child->SetName("child.snd", dir);
    group->AddObject(child);
    ASSERT_EQ(group->Objects().size(), 1);
    delete dir;
    EXPECT_EQ(group->Objects().size(), 0)
        << "the group kept a NULL child node (RndGroup::Replace never ran)";
    for (ObjPtrList<Hmx::Object>::iterator it = group->Objects().begin();
         it != group->Objects().end(); ++it) {
        EXPECT_NE(*it, nullptr) << "null child left in the group";
    }
    delete group;
}

// ===========================================================================
// Make Your Move: FreestyleMoveRecorder recording must allocate the take
// ===========================================================================
//
// Party mode's `bustamove` event (Make Your Move) crashed natively the moment
// its recording phase started: BustAMovePanel::Poll (kBAMState_Recording)
// calls FreestyleMoveRecorder::GetScore, which reads
// mTakes[mCurrentTakeIndex].mFrames[frameIdx] -- and natively mFrames was
// NULL, because StartRecording / StartRecordingDancerTake / StopRecording
// were emptied under HX_NATIVE ("touch depth frame allocation").  The image's
// StartRecording (100% matched) is FreestyleMove::Init(mMaxFrames), which
// allocates the take; nothing in it touches a camera.  SIGSEGV in
// DancerSkeleton::Set <- GetScore, frames=0x0.

#include "hamobj/FreestyleMoveRecorder.h"

namespace {
void RecordThenScore() {
    FreestyleMoveRecorder rec;
    rec.StartRecording();                 // BustAMovePanel: kBAMState_Recording
    rec.GetScore((const BaseSkeleton *)nullptr, 1, 0.0f, false);
    rec.StartRecordingDancerTake();
    rec.StopRecording();
    _exit(0);
}
} // namespace

class MakeYourMoveRecorderTest : public EngineTestFixture {};

TEST_F(MakeYourMoveRecorderTest, ScoringARecordingReadsAnAllocatedTake) {
    GTEST_FLAG_SET(death_test_style, "threadsafe"); // see test_object_lifetime.cpp
    ASSERT_EXIT(RecordThenScore(), ::testing::ExitedWithCode(0), "")
        << "GetScore after StartRecording read an unallocated take "
           "(StartRecording did not run FreestyleMove::Init)";
}
