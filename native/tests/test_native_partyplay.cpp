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
