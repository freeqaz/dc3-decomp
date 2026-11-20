// Song select on native: Recently Played, sort by artist, section jumps, the
// remembered sort and the classic switch (docs/native/SONG_SELECT.md).
//
// Two kinds of test:
//
//   * NativeSongSelectStateTest -- the file song select keeps beside the
//     saves ($DC3_SAVE_DIR/song_select.txt), driven against a scratch path.
//     No engine, runs everywhere.
//   * SongSelect*FlowTest (further down) -- the real screens in a child
//     dc3-native, driven over the HTTP debug server: buttons go in through
//     /api/input/press, and what the list holds, where the highlight is and
//     what the rows show come back through /api/dta/eval.  Gated by
//     DC3_DTA_FLOW_TESTS=1 like every other flow test (needs the game assets).
//     No GPU: nothing here reads a screenshot.

#include <gtest/gtest.h>

#include "engine_http_port.h"
#include "platform/ExecutablePath.h"
#include "platform/SongSelectState_Native.h"
#include "seed_global_options.h"
#include "test_tmp.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string ReadFile(const std::string &path) {
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

void WriteFile(const std::string &path, const std::string &text) {
    std::ofstream out(path);
    out << text;
}

std::vector<std::string> Songs(std::initializer_list<const char *> names) {
    return std::vector<std::string>(names.begin(), names.end());
}

} // namespace

// The newest song is first, and a song played again moves up rather than
// appearing twice.
TEST(NativeSongSelectStateTest, HistoryIsNewestFirstWithoutDuplicates) {
    ScopedTestDir dir("dc3-songselect");
    ASSERT_TRUE(dir.ok());
    NativeSongSelectState state(dir.path() + "/song_select.txt");
    state.NotePlayed("ymca");
    state.NotePlayed("starships");
    state.NotePlayed("macarena");
    EXPECT_EQ(state.History(), Songs({ "macarena", "starships", "ymca" }));
    state.NotePlayed("ymca");
    EXPECT_EQ(state.History(), Songs({ "ymca", "macarena", "starships" }));
}

// What one launch wrote is what the next one reads: this is "the history
// survives a relaunch" at the level of the file.
TEST(NativeSongSelectStateTest, HistoryAndSortSurviveAReload) {
    ScopedTestDir dir("dc3-songselect");
    ASSERT_TRUE(dir.ok());
    std::string path = dir.path() + "/song_select.txt";
    {
        NativeSongSelectState state(path);
        state.NotePlayed("ymca");
        state.NotePlayed("starships");
        state.SetSort("by_artist");
    }
    NativeSongSelectState reloaded(path);
    reloaded.Load();
    EXPECT_EQ(reloaded.History(), Songs({ "starships", "ymca" }));
    EXPECT_EQ(reloaded.Sort(), "by_artist");
    EXPECT_EQ(ReadFile(path),
              "dc3-song-select 1\nsort by_artist\nplayed starships\nplayed ymca\n");
}

TEST(NativeSongSelectStateTest, HistoryIsCapped) {
    ScopedTestDir dir("dc3-songselect");
    ASSERT_TRUE(dir.ok());
    std::string path = dir.path() + "/song_select.txt";
    NativeSongSelectState state(path);
    for (int i = 0; i < NativeSongSelectState::kMaxHistory + 10; i++) {
        state.NotePlayed("song" + std::to_string(i));
    }
    ASSERT_EQ((int)state.History().size(), (int)NativeSongSelectState::kMaxHistory);
    EXPECT_EQ(state.History().front(),
              "song" + std::to_string(NativeSongSelectState::kMaxHistory + 9));
    NativeSongSelectState reloaded(path);
    reloaded.Load();
    EXPECT_EQ(reloaded.History(), state.History());
}

// No file, a file that is not ours, and lines nobody wrote: all an empty or
// partial state, never a crash and never a row with a made-up name.
TEST(NativeSongSelectStateTest, MissingAndForeignFilesAreAnEmptyState) {
    ScopedTestDir dir("dc3-songselect");
    ASSERT_TRUE(dir.ok());
    std::string path = dir.path() + "/song_select.txt";

    NativeSongSelectState missing(path);
    missing.Load();
    EXPECT_TRUE(missing.History().empty());
    EXPECT_TRUE(missing.Sort().empty());

    WriteFile(path, "something else\nplayed ymca\n");
    NativeSongSelectState foreign(path);
    foreign.Load();
    EXPECT_TRUE(foreign.History().empty()) << "a file without our first line is not read";

    WriteFile(path,
              "dc3-song-select 1\n"
              "played ymca\n"
              "volume 11\n"            // a key a newer build might write
              "played\n"               // no name
              "played ymca\n"          // twice
              "\n"
              "played starships\n"
              "sort by_song\n");
    NativeSongSelectState mixed(path);
    mixed.Load();
    EXPECT_EQ(mixed.History(), Songs({ "ymca", "starships" }));
    EXPECT_EQ(mixed.Sort(), "by_song");
}

// A save dir that cannot be written loses the history on exit, not the
// session: the state in memory still answers.
TEST(NativeSongSelectStateTest, UnwritablePathKeepsTheStateInMemory) {
    NativeSongSelectState state("/nonexistent-dc3-dir/song_select.txt");
    state.NotePlayed("ymca");
    state.SetSort("by_artist");
    EXPECT_EQ(state.History(), Songs({ "ymca" }));
    EXPECT_EQ(state.Sort(), "by_artist");
    EXPECT_FALSE(state.Save());
}

// ===========================================================================
// Flow tests: the real song select in a child dc3-native
// ===========================================================================

namespace {

std::string BinaryDir() {
    char buf[4096];
    ssize_t len = ReadExecutablePath(buf, sizeof(buf) - 1);
    if (len <= 0) return ".";
    buf[len] = '\0';
    std::string path(buf);
    size_t slash = path.rfind('/');
    return slash != std::string::npos ? path.substr(0, slash) : ".";
}

bool Exists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

struct EngineRun {
    std::string setupError;
    std::string output; // the driver's lines, then the engine's log
    std::string driver; // the DRIVER: lines only
};

// A persistence root for a child engine: cache/ holding the global options of
// a console that has seen the move tutorial (so a Perform song starts without
// one), and an empty saves/.  It lives until this process exits, so a second
// engine can boot against it -- that is what a relaunch is here.
std::string MakeRoot(std::string &error) {
    std::string root = MakeTestTempDir("dc3-songselect");
    if (root.empty()) {
        error = "could not create a temporary persistence root";
        return root;
    }
    mkdir((root + "/cache").c_str(), 0755);
    mkdir((root + "/saves").c_str(), 0755);
    if (!SeedGlobalOptions(root, kTutorialGeneral)) {
        error = "could not write the seeded global options under " + root;
    }
    return root;
}

// The driver's shell functions.  Every wait is bounded, and the wait after a
// button is counted in engine FRAMES (/api/frame/wait), not seconds: a headless
// engine steps UI time by 1/30 s a frame, so a frame count is the same amount
// of menu animation on a loaded machine as on an idle one.
//
//   scr                       the current screen
//   ev DTA                    POST /api/dta/eval
//   settle FRAMES             let that many frames go by
//   press BUTTON [FRAMES=45]  a pad-0 button, then settle
//   pace FPS                  cap the frame rate (0: uncapped)
//   waitfor SECONDS SCREEN... until one of the screens is up
//   nav SCREEN [SECONDS=6]    confirm until SCREEN is up: again when the
//                             screen has not changed (the press landed inside
//                             a list's enter animation, or the screen takes
//                             several confirms), at most four times
//   expand                    open a collapsed list (Back), if it is collapsed
//   st TAG                    DRIVER: TAG sort= hm= sel= top= n= bar= tok=
//   rows TAG                  DRIVER: TAG KIND TOKEN;KIND TOKEN;...
//   artists TAG               DRIVER: TAG ROW|ARTIST;...  (the song rows)
//   vis TAG                   DRIVER: TAG ROW|SLOT|TEXT;;...  (what the rows
//                             on screen show: HamNavList::NativeVisibleText)
//   file TAG                  DRIVER: TAG <song_select.txt, | for newline>
//   sb                        GET /api/songbrowser/state (the song browser)
//   sel                       the song browser's selected song
//   typ TEXT [FRAMES=60]      type TEXT (POST /api/input/text), then settle
//   play                      from a highlighted song: the whole Perform song
//                             and back to song select (needs DC3_FAST_TIME)
const char *const kDriverFunctions = R"SH(
L='{song_select_panel find right_hand.hnl}';
scr() { curl -s -m 5 $api/screen | sed -n 's/.*"screen":"\([^"]*\)".*/\1/p'; };
ev() { curl -s -m 30 -X POST --data-binary "$1" $api/dta/eval; };
frame() { curl -s -m 5 $api/frame | sed -n 's/.*"frame":\([0-9]*\).*/\1/p'; };
settle() { f=$(frame); [ -n "$f" ] && curl -s -m 70 "$api/frame/wait/$((f+$1))" > /dev/null; };
pace() { curl -s -m 5 -X PUT "$api/pace?fps=$1" > /dev/null; };
press() { curl -s -m 10 -X POST --data-binary "{\"button\":\"$1\"}" $api/input/press > /dev/null; settle ${2:-45}; };
waitfor() { end=$((SECONDS+$1)); shift; while [ $SECONDS -lt $end ] && kill -0 $pid 2>/dev/null; do
  s=$(scr); for w in "$@"; do [ "$s" = "$w" ] && return 0; done; sleep 0.2; done; return 1; };
nav() { from=$(scr); n=0; while [ $n -lt 4 ]; do press confirm 60; waitfor ${2:-6} "$1" && return 0;
  [ "$(scr)" = "$from" ] || break; n=$((n+1)); done; waitfor 90 "$1"; };
expand() { if ev '{song_offer_provider get_header_mode}' | grep -q '"value":1}'; then press cancel 150; fi; };
st() { echo "DRIVER: $1 $(ev "{sprint \"sort=\" {song_offer_provider get_current_sort_name} \" hm=\" {song_offer_provider get_header_mode} \" sel=\" {$L get_selected} \" top=\" {$L get_top_index} \" n=\" {song_offer_provider num_data} \" bar=\" {song_select_panel get tertiary_token} \" tok=\" {song_offer_provider get_token {$L get_selected}}}")"; };
rows() { echo "DRIVER: $1 $(ev '{do ($s "") {foreach_int $i 0 {song_offer_provider num_data} {set $s {sprint $s {cond ({song_offer_provider data_is $i song} "song") ({song_offer_provider data_is $i header} "header") (TRUE "function")} " " {song_offer_provider get_token $i} ";"}}} $s}')"; };
artists() { echo "DRIVER: $1 $(ev '{do ($s "") {foreach_int $i 0 {song_offer_provider num_data} {if {song_offer_provider data_is $i song} {set $s {sprint $s $i "|" {song_mgr get_artist_name_from_short_name {song_offer_provider get_token $i}} ";"}}}} $s}')"; };
vis() { echo "DRIVER: $1 $(ev "{song_offer_provider visible_text $L}")"; };
file() { echo "DRIVER: $1 $(tr '\n' '|' < $saves/song_select.txt 2>/dev/null)"; };
sb() { curl -s -m 10 $api/songbrowser/state; };
sel() { sb | sed -n 's/.*"selected_song":"\([^"]*\)".*/\1/p'; };
typ() { curl -s -m 10 -X POST --data-binary "{\"text\":\"$1\"}" $api/input/text > /dev/null; settle ${2:-60}; };
play() { press confirm 60; waitfor 30 multiuser_screen || return 1; settle 120; nav game_screen 3 || return 1;
  pace 0; waitfor 300 perform_final_results_screen; reached=$?; pace 240; [ $reached = 0 ] || return 1; settle 240;
  nav perform_complete_screen || return 1; settle 240; nav song_select_screen 30 || return 1; settle 300; };
)SH";

// main_screen -> choose_mode_screen (Perform is its first row) -> song select.
const char *const kToSongSelect =
    "nav choose_mode_screen; settle 90; nav song_select_screen; "
    "echo \"DRIVER: at=$(scr)\"; settle 240; ";

std::string Times(int count, const char *command) {
    return "for i in $(seq " + std::to_string(count) + "); do " + command + "; done; ";
}

// One dc3-native against the persistence root `root`, headless and without a
// renderer, disc songs only.  Boots to main_screen with idle-main.txt (title
// confirm, then nothing: every later button is the driver's), then runs
// `drive`, a shell fragment using the functions above.  `runName` names this
// run's directory under the root (the port file and the log).  The engine is
// killed by PID, never by pattern.
EngineRun RunEngine(const std::string &root, const char *runName, const std::string &drive,
                    const std::string &extraEnv = "") {
    EngineRun run;
    std::string bin = BinaryDir() + "/dc3-native";
    std::string script = BinaryDir() + "/../../scripts/dc3-input-flows/idle-main.txt";
    if (!Exists(bin)) {
        run.setupError =
            "dc3-native does not exist at " + bin + " (scripts/native_test.sh builds it)";
        return run;
    }
    if (!Exists(script)) {
        run.setupError = "input flow script missing: " + script;
        return run;
    }
    std::string dir = root + "/" + runName;
    mkdir(dir.c_str(), 0755);
    std::string log = dir + "/engine.log";
    std::ostringstream sh;
    sh << "t=$(command -v timeout || command -v gtimeout); cd " << BinaryDir() << " && "
       << "( MILO_HEADLESS=1 MILO_NORENDER=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1"
       << " DC3_DLC=0" << EngineHttpPortShellEnv(dir) << " MILO_MAX_FRAMES=0"
       << " DC3_CACHE_DIR=" << root << "/cache DC3_SAVE_DIR=" << root << "/saves"
       << " MILO_INPUT_SCRIPT=" << script << extraEnv
       << " ${t:+$t 600} ./dc3-native > " << log << " 2>&1 & echo $! > " << log << ".pid ); "
       << "pid=$(cat " << log << ".pid); " << EngineHttpApiShell(dir, "pid", 180)
       << "saves=" << root << "/saves; " << kDriverFunctions
       << "waitfor 180 main_screen; echo \"DRIVER: main=$(scr)\"; settle 120; pace 240; "
       << drive
       << "kill $pid 2>/dev/null; sleep 1; kill -9 $pid 2>/dev/null; "
       << "cat " << log << "; rm -f " << log << " " << log << ".pid";
    FILE *pipe = popen(sh.str().c_str(), "r");
    if (!pipe) {
        run.setupError = "popen failed";
        return run;
    }
    std::string line;
    char buf[4096];
    while (fgets(buf, sizeof buf, pipe)) {
        line += buf;
        if (line.empty() || line[line.size() - 1] != '\n') continue; // a long line: more to come
        run.output += line;
        if (line.compare(0, 7, "DRIVER:") == 0) run.driver += line;
        line.clear();
    }
    run.output += line;
    pclose(pipe);
    // A passing run prints nothing; this shows what it was judged on.
    if (getenv("DC3_TEST_SHOW_DRIVER")) fputs(run.driver.c_str(), stderr);
    return run;
}

// The line the driver wrote for `tag`, after "DRIVER: tag "; empty if none.
std::string DriverLine(const EngineRun &run, const std::string &tag) {
    std::string prefix = "DRIVER: " + tag + " ";
    size_t at = 0;
    while (at < run.driver.size()) {
        size_t end = run.driver.find('\n', at);
        if (end == std::string::npos) end = run.driver.size();
        if (run.driver.compare(at, prefix.size(), prefix) == 0) {
            return run.driver.substr(at + prefix.size(), end - at - prefix.size());
        }
        at = end + 1;
    }
    return "";
}

// The "value" of the /api/dta/eval answer in `json`: a string unescaped, a
// number as its digits.  Empty when there is none (an eval that failed).
std::string JsonValue(const std::string &json) {
    size_t at = json.find("\"value\":");
    if (at == std::string::npos) return "";
    at += 8;
    std::string value;
    if (at < json.size() && json[at] == '"') {
        for (size_t i = at + 1; i < json.size() && json[i] != '"'; i++) {
            if (json[i] != '\\' || i + 1 >= json.size()) {
                value += json[i];
                continue;
            }
            char escaped = json[++i];
            if (escaped == 'n') value += '\n';
            else if (escaped == 't') value += '\t';
            else if (escaped == 'u' && i + 4 < json.size()) {
                value += (char)strtol(json.substr(i + 1, 4).c_str(), nullptr, 16);
                i += 4;
            } else value += escaped;
        }
    } else {
        while (at < json.size() && json[at] != ',' && json[at] != '}') value += json[at++];
    }
    return value;
}

std::string DriverValue(const EngineRun &run, const std::string &tag) {
    return JsonValue(DriverLine(run, tag));
}

// sprint writes a symbol with a space in it between single quotes.
std::string Unquoted(const std::string &text) {
    if (text.size() >= 2 && text[0] == '\'' && text[text.size() - 1] == '\'') {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

std::vector<std::string> Split(const std::string &text, const std::string &separator) {
    std::vector<std::string> parts;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find(separator, at);
        if (end == std::string::npos) end = text.size();
        if (end > at) parts.push_back(text.substr(at, end - at));
        at = end + separator.size();
    }
    return parts;
}

// What `st TAG` wrote.
struct ListState {
    bool ok = false;
    std::string sort;
    int headerMode = -1;
    int selected = -1;
    int top = -1;
    int numRows = -1;
    std::string helpBar; // "(by_song by_artist by_location)"
    std::string token;   // the selected row's
};

std::string Field(const std::string &text, const char *key, const char *nextKey) {
    size_t at = text.find(key);
    if (at == std::string::npos) return "";
    at += strlen(key);
    size_t end = nextKey ? text.find(nextKey, at) : std::string::npos;
    if (end == std::string::npos) end = text.size();
    return text.substr(at, end - at);
}

ListState State(const EngineRun &run, const std::string &tag) {
    ListState state;
    std::string text = DriverValue(run, tag);
    if (text.compare(0, 5, "sort=") != 0) return state;
    state.sort = Field(text, "sort=", " hm=");
    state.headerMode = atoi(Field(text, " hm=", " sel=").c_str());
    state.selected = atoi(Field(text, " sel=", " top=").c_str());
    state.top = atoi(Field(text, " top=", " n=").c_str());
    state.numRows = atoi(Field(text, " n=", " bar=").c_str());
    state.helpBar = Field(text, " bar=", " tok=");
    state.token = Unquoted(Field(text, " tok=", nullptr));
    state.ok = true;
    return state;
}

// What `rows TAG` wrote, one "KIND TOKEN" per row.
std::vector<std::string> Rows(const EngineRun &run, const std::string &tag) {
    std::vector<std::string> rows = Split(DriverValue(run, tag), ";");
    for (size_t i = 0; i < rows.size(); i++) {
        size_t space = rows[i].find(' ');
        if (space != std::string::npos) {
            rows[i] = rows[i].substr(0, space + 1) + Unquoted(rows[i].substr(space + 1));
        }
    }
    return rows;
}

// native/tests/data/song_select/<name>: the image's list, dumped before any of
// this existed.  '#' lines are the file's own account of where it came from.
std::vector<std::string> ClassicRows(const char *sort) {
    std::vector<std::string> rows;
    std::ifstream in(
        BinaryDir() + "/../tests/data/song_select/classic_rows_" + sort + ".txt");
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line[0] != '#') rows.push_back(line);
    }
    return rows;
}

bool IsHeader(const std::string &row) { return row.compare(0, 7, "header ") == 0; }
bool IsSong(const std::string &row) { return row.compare(0, 5, "song ") == 0; }
std::string TokenOf(const std::string &row) { return row.substr(row.find(' ') + 1); }

int CountRows(const std::vector<std::string> &rows, const std::string &row) {
    return (int)std::count(rows.begin(), rows.end(), row);
}

// Where the sections of a list start: every header row, and row 0 when the
// list opens with rows that have no header (Playlists, Random Song).  Worked
// out from the dumped rows, not asked of the engine.
std::vector<int> SectionStarts(const std::vector<std::string> &rows) {
    std::vector<int> starts;
    for (size_t i = 0; i < rows.size(); i++) {
        if (IsHeader(rows[i])) starts.push_back((int)i);
    }
    if (starts.empty() || starts.front() > 0) starts.insert(starts.begin(), 0);
    return starts;
}

// Right: the first row of the next section; in the last section, stay.
int RightOf(const std::vector<int> &starts, int selected) {
    for (size_t i = 0; i < starts.size(); i++) {
        if (starts[i] > selected) return starts[i];
    }
    return selected;
}

// Left: the first row of the section before the one `selected` is in; in the
// first section, that section's first row.
int LeftOf(const std::vector<int> &starts, int selected) {
    int current = 0;
    for (size_t i = 0; i < starts.size(); i++) {
        if (starts[i] <= selected) current = (int)i;
    }
    return starts[current > 0 ? current - 1 : 0];
}

// The highlight is one of the five rows the list shows.
const int kRowsOnScreen = 5;
bool OnScreen(const ListState &state) {
    return state.top <= state.selected && state.selected < state.top + kRowsOnScreen;
}

std::string Tail(const std::string &text, size_t bytes = 4000) {
    return text.substr(text.size() > bytes ? text.size() - bytes : 0);
}

// Shared by the three suites: skip without the gate, fail on a setup error,
// and fail every test of a run that never reached song select with one
// message that says so (instead of a dozen about rows that are not there).
class SongSelectFlowBase : public ::testing::Test {
protected:
    void RequireRun(bool ran, const std::string &error, const EngineRun &run) {
        if (!getenv("DC3_DTA_FLOW_TESTS")) {
            GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
        }
        if (!ran) GTEST_SKIP() << "engine did not run";
        ASSERT_TRUE(error.empty()) << error;
        ASSERT_TRUE(run.setupError.empty()) << run.setupError;
        ASSERT_NE(run.driver.find("DRIVER: at=song_select_screen"), std::string::npos)
            << "precondition: the driver never reached song select\n"
            << run.driver << "\n--- tail ---\n"
            << Tail(run.output);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// SongSelectFlowTest: no history.  The sorts, the artist sort's rows and
// labels, and the section and page buttons.
// ---------------------------------------------------------------------------

class SongSelectFlowTest : public SongSelectFlowBase {
protected:
    static EngineRun sRun;
    static std::string sError;
    static bool sRan;

    static void SetUpTestSuite() {
        if (!getenv("DC3_DTA_FLOW_TESTS")) return;
        sRan = true;
        std::string root = MakeRoot(sError);
        if (!sError.empty()) return;
        std::ostringstream d;
        // Another list first: choose_mode's.  Its provider is not song
        // select's, so the four buttons must leave it alone.
        d << "nav choose_mode_screen; settle 90; "
          << "C='{{choose_mode_panel find right_hand.hnl} get_selected}'; "
          << "echo \"DRIVER: other0 $(scr) $(ev \"$C\")\"; "
          << "press right; echo \"DRIVER: other1 $(scr) $(ev \"$C\")\"; "
          << "press rb; echo \"DRIVER: other2 $(scr) $(ev \"$C\")\"; "
          << "press left; echo \"DRIVER: other3 $(scr) $(ev \"$C\")\"; "
          << "press lb; echo \"DRIVER: other4 $(scr) $(ev \"$C\")\"; "
          << "nav song_select_screen; echo \"DRIVER: at=$(scr)\"; settle 240; "
          // Sorted by difficulty, as the image opens it.
          << "st enter; rows d_rows; vis d_vis; "
          << "press down; press down; st j0; "
          << "press right; st j1; press right; st j2; "
          << "press down; press left; st j3; "
          << "press left; st j4; press left; st j5; press left; st j6; "
          << Times(14, "press right 30") << "st j7; press right; st j8; "
          << "press lb; st p1; press rb; st p2; press rb; st p3; press rb; st p4; "
          // The list switched off, as the panel switches it off under one of
          // its overlays; then on again.
          << "ev \"{$L disable_navigation}\" > /dev/null; "
          << "press left; st off1; press lb; st off2; press up; st off3; "
          << "ev \"{$L enable_navigation}\" > /dev/null; press left; st on1; "
          // The sort button: by title.  It leaves the list collapsed.
          << "press y 150; st s1; expand; rows s_rows; vis s_vis; "
          // By artist: collapsed first, then opened.
          << "press y 150; st s2; file file_artist; rows a_collapsed; "
          << Times(14, "press left 30") << "st h0; "
          << "press right; st h1; press right; st h2; press right; st h3; press left; st h4; "
          << Times(14, "press right 30") << "st h5; press right; st h6; "
          << Times(14, "press left 30") << "expand; st a0; rows a_rows; artists a_artists; vis a_vis; "
          << "press right; st a1; press right; st a2; press down; press down; st a3; vis a_vis2; "
          << "press left; st a4; "
          // By source, then by difficulty again: four sorts.
          << "press y 150; st s3; expand; rows l_rows; "
          << "press y 150; st s4; file file_end; ";
        sRun = RunEngine(root, "run", d.str());
    }

    // The checks, grouped into the TESTs at the end of this suite: ctest runs
    // each TEST in its own process, so each one costs the suite's engine runs.
    static void NoHistoryMeansTheImagesList();
    static void SortButtonCyclesFourSortsAndSavesTheChoice();
    static void ArtistSortIsInArtistOrderUnderLetterHeaders();
    static void ArtistSortNamesTheArtistOnEachRow();
    static void LeftRightMoveBySection();
    static void LeftRightMoveBySectionInACollapsedList();
    static void LeftRightMoveBySectionUnderTheArtistSort();
    static void ShoulderButtonsMoveAScreenful();
    static void ASwitchedOffListIgnoresTheButtons();
    static void OtherListsIgnoreTheButtons();

    void SetUp() override { RequireRun(sRan, sError, sRun); }

    static ListState St(const char *tag) { return State(sRun, tag); }
};
EngineRun SongSelectFlowTest::sRun;
std::string SongSelectFlowTest::sError;
bool SongSelectFlowTest::sRan = false;

// With nothing played there is no Recently Played section: the three sorts of
// the image hold exactly the image's rows.
void SongSelectFlowTest::NoHistoryMeansTheImagesList() {
    EXPECT_EQ(St("enter").sort, "by_difficulty");
    EXPECT_EQ(Rows(sRun, "d_rows"), ClassicRows("by_difficulty"));
    EXPECT_EQ(Rows(sRun, "s_rows"), ClassicRows("by_song"));
    EXPECT_EQ(Rows(sRun, "l_rows"), ClassicRows("by_location"));
    EXPECT_FALSE(ClassicRows("by_difficulty").empty()) << "the fixture did not load";
    EXPECT_EQ(sRun.driver.find("recently_played"), std::string::npos) << sRun.driver;
}

// The sort button goes round four sorts, the help bar names the current one
// first, and each choice is written to song_select.txt as it is made.
void SongSelectFlowTest::SortButtonCyclesFourSortsAndSavesTheChoice() {
    EXPECT_EQ(St("s1").sort, "by_song");
    EXPECT_EQ(St("s2").sort, "by_artist");
    EXPECT_EQ(St("s3").sort, "by_location");
    EXPECT_EQ(St("s4").sort, "by_difficulty");
    // Three names fit on the bar: the current sort and the next two.
    EXPECT_EQ(St("enter").helpBar, "(by_difficulty by_song by_artist)");
    EXPECT_EQ(St("s1").helpBar, "(by_song by_artist by_location)");
    EXPECT_EQ(St("s2").helpBar, "(by_artist by_location by_difficulty)");
    EXPECT_EQ(St("s3").helpBar, "(by_location by_difficulty by_song)");
    EXPECT_EQ(DriverLine(sRun, "file_artist"), "dc3-song-select 1|sort by_artist|");
    EXPECT_EQ(DriverLine(sRun, "file_end"), "dc3-song-select 1|sort by_difficulty|");
}

namespace {

// 0 for an artist that does not start with a letter, 1..26 for A..Z, after a
// leading "The ".  Written here from the rule, not taken from ArtistCmp.
std::string ArtistKey(std::string artist) {
    for (size_t i = 0; i < artist.size(); i++) artist[i] = (char)tolower((unsigned char)artist[i]);
    if (artist.compare(0, 4, "the ") == 0) artist = artist.substr(4);
    return artist;
}

int LetterRank(const std::string &text) {
    unsigned char first = text.empty() ? 0 : (unsigned char)text[0];
    return isalpha(first) ? toupper(first) - 'A' + 1 : 0;
}

// "M" -> 13..13, "A - C" -> 1..3, "123 - A" -> 0..1.
bool LabelRange(const std::string &label, int &first, int &last) {
    std::vector<std::string> ends = Split(label, " - ");
    if (ends.empty() || ends.size() > 2) return false;
    for (size_t i = 0; i < ends.size(); i++) {
        if (ends[i] != "123" && !(ends[i].size() == 1 && isupper((unsigned char)ends[i][0]))) {
            return false;
        }
    }
    first = ends.front() == "123" ? 0 : LetterRank(ends.front());
    last = ends.back() == "123" ? 0 : LetterRank(ends.back());
    return first <= last;
}

} // namespace

// Rows in artist order under headers that say which letters they hold, the
// same headers collapsed and opened.
void SongSelectFlowTest::ArtistSortIsInArtistOrderUnderLetterHeaders() {
    std::vector<std::string> rows = Rows(sRun, "a_rows");
    ASSERT_GT(rows.size(), 10u) << sRun.driver;
    EXPECT_EQ(rows[0], "function playlists");
    EXPECT_EQ(rows[1], "function random_song");

    // The same songs as the title sort, each once.
    std::vector<std::string> songs, titleSongs;
    for (size_t i = 0; i < rows.size(); i++) {
        if (IsSong(rows[i])) songs.push_back(rows[i]);
    }
    std::vector<std::string> titleRows = ClassicRows("by_song");
    for (size_t i = 0; i < titleRows.size(); i++) {
        if (IsSong(titleRows[i])) titleSongs.push_back(titleRows[i]);
    }
    std::sort(songs.begin(), songs.end());
    std::sort(titleSongs.begin(), titleSongs.end());
    EXPECT_EQ(songs, titleSongs);

    std::map<int, std::string> artistOfRow;
    std::vector<std::string> pairs = Split(DriverValue(sRun, "a_artists"), ";");
    for (size_t i = 0; i < pairs.size(); i++) {
        size_t bar = pairs[i].find('|');
        ASSERT_NE(bar, std::string::npos) << pairs[i];
        artistOfRow[atoi(pairs[i].c_str())] = Unquoted(pairs[i].substr(bar + 1));
    }
    ASSERT_EQ(artistOfRow.size(), songs.size());

    std::string previousKey;
    int sectionFirst = -1, sectionLast = -1, sectionSongs = 0, headers = 0;
    int lowestSeen = 99, highestSeen = -1;
    for (size_t i = 2; i < rows.size(); i++) {
        if (IsHeader(rows[i])) {
            if (headers > 0) {
                EXPECT_GE(sectionSongs, 4) << "a section of fewer than four songs before row " << i;
                EXPECT_EQ(lowestSeen, sectionFirst) << "header before row " << i << " starts too early";
                EXPECT_EQ(highestSeen, sectionLast) << "header before row " << i << " ends too late";
            }
            ASSERT_TRUE(LabelRange(TokenOf(rows[i]), sectionFirst, sectionLast))
                << "not a letter or a range of letters: " << rows[i];
            headers++;
            sectionSongs = 0;
            lowestSeen = 99;
            highestSeen = -1;
            continue;
        }
        ASSERT_TRUE(IsSong(rows[i])) << rows[i];
        ASSERT_GT(headers, 0) << "a song above the first header";
        std::string key = ArtistKey(artistOfRow[(int)i]);
        int rank = LetterRank(key);
        EXPECT_GE(rank, sectionFirst) << rows[i] << " (" << artistOfRow[(int)i] << ")";
        EXPECT_LE(rank, sectionLast) << rows[i] << " (" << artistOfRow[(int)i] << ")";
        if (!previousKey.empty()) {
            // Digits and symbols before letters, then by name.
            bool inOrder = LetterRank(previousKey) < rank
                || (LetterRank(previousKey) == rank && previousKey <= key);
            EXPECT_TRUE(inOrder) << "'" << previousKey << "' is above '" << key << "' at row " << i;
        }
        previousKey = key;
        lowestSeen = std::min(lowestSeen, rank);
        highestSeen = std::max(highestSeen, rank);
        sectionSongs++;
    }
    EXPECT_GE(headers, 3) << "the artist sort should have several sections";

    // Collapsed, the list is the two function rows and those same headers.
    std::vector<std::string> expectedCollapsed(rows.begin(), rows.begin() + 2);
    for (size_t i = 2; i < rows.size(); i++) {
        if (IsHeader(rows[i])) expectedCollapsed.push_back(rows[i]);
    }
    EXPECT_EQ(St("s2").headerMode, 1);
    EXPECT_EQ(Rows(sRun, "a_collapsed"), expectedCollapsed);
}

namespace {

// ROW -> SLOT -> TEXT of what `vis TAG` wrote.
std::map<int, std::map<std::string, std::string> > Visible(const EngineRun &run,
                                                            const std::string &tag) {
    std::map<int, std::map<std::string, std::string> > shown;
    std::vector<std::string> labels = Split(DriverValue(run, tag), ";;");
    for (size_t i = 0; i < labels.size(); i++) {
        size_t first = labels[i].find('|');
        size_t second = labels[i].find('|', first + 1);
        if (first == std::string::npos || second == std::string::npos) continue;
        shown[atoi(labels[i].c_str())][labels[i].substr(first + 1, second - first - 1)] =
            labels[i].substr(second + 1);
    }
    return shown;
}

} // namespace

// Under the artist sort every song row on screen names its artist, in the
// row's own text; under the other sorts no row does.
void SongSelectFlowTest::ArtistSortNamesTheArtistOnEachRow() {
    std::map<int, std::string> artistOfRow;
    std::vector<std::string> pairs = Split(DriverValue(sRun, "a_artists"), ";");
    for (size_t i = 0; i < pairs.size(); i++) {
        size_t bar = pairs[i].find('|');
        if (bar != std::string::npos) {
            artistOfRow[atoi(pairs[i].c_str())] = Unquoted(pairs[i].substr(bar + 1));
        }
    }
    const char *const tags[] = { "a_vis", "a_vis2" };
    int songRowsSeen = 0;
    for (int t = 0; t < 2; t++) {
        std::map<int, std::map<std::string, std::string> > shown = Visible(sRun, tags[t]);
        ASSERT_FALSE(shown.empty()) << tags[t] << ": nothing on screen\n" << sRun.driver;
        for (std::map<int, std::map<std::string, std::string> >::iterator row = shown.begin();
             row != shown.end(); ++row) {
            if (!row->second.count("song")) continue; // a header or a function row
            songRowsSeen++;
            ASSERT_TRUE(artistOfRow.count(row->first)) << tags[t] << " row " << row->first;
            EXPECT_EQ(row->second["header_text"], artistOfRow[row->first])
                << tags[t] << " row " << row->first << " (" << row->second["song"] << ")";
        }
    }
    EXPECT_GE(songRowsSeen, 4) << "too few song rows on screen to say anything";

    const char *const otherTags[] = { "d_vis", "s_vis" };
    for (int t = 0; t < 2; t++) {
        std::string text = DriverValue(sRun, otherTags[t]);
        EXPECT_NE(text.find("|song|"), std::string::npos) << otherTags[t] << ": " << text;
        EXPECT_EQ(text.find("|header_text|"), std::string::npos) << otherTags[t] << ": " << text;
    }
}

// Left and Right, sorted by difficulty with every section open: from inside a
// section, across two, back, to the top and past it, to the last section and
// past it.
void SongSelectFlowTest::LeftRightMoveBySection() {
    std::vector<int> starts = SectionStarts(Rows(sRun, "d_rows"));
    ASSERT_GE(starts.size(), 4u) << sRun.driver;
    ListState enter = St("enter"), j0 = St("j0");
    ASSERT_TRUE(j0.ok) << sRun.driver;
    ASSERT_EQ(j0.selected, enter.selected + 2) << "precondition: two Downs";
    int row = j0.selected;

    row = RightOf(starts, row);
    EXPECT_EQ(St("j1").selected, row) << "Right from inside a section";
    EXPECT_EQ(St("j1").top, row) << "the section is brought to the top of the list";
    EXPECT_TRUE(IsHeader(Rows(sRun, "d_rows")[St("j1").selected]));
    row = RightOf(starts, row);
    EXPECT_EQ(St("j2").selected, row) << "Right from a header";
    row = LeftOf(starts, row + 1); // Down, then Left
    EXPECT_EQ(St("j3").selected, row) << "Left from the first song of a section";
    EXPECT_LT(St("j3").selected, St("j2").selected);
    row = LeftOf(starts, row);
    EXPECT_EQ(St("j4").selected, row);
    row = LeftOf(starts, row);
    EXPECT_EQ(St("j5").selected, row);
    EXPECT_EQ(St("j5").selected, 0) << "the rows above the first header are a section";
    EXPECT_EQ(St("j6").selected, 0) << "Left at the top stays";
    EXPECT_EQ(St("j7").selected, starts.back()) << "Right, until the last section";
    EXPECT_EQ(St("j8").selected, starts.back()) << "Right in the last section stays";
    const char *const tags[] = { "j1", "j2", "j3", "j4", "j5", "j6", "j7", "j8" };
    for (int i = 0; i < 8; i++) {
        EXPECT_TRUE(OnScreen(St(tags[i]))) << tags[i] << ": selected " << St(tags[i]).selected
                                           << ", top " << St(tags[i]).top;
        EXPECT_EQ(St(tags[i]).headerMode, 0) << tags[i];
    }
}

// In a collapsed list every header is a section, and so are the rows above
// the first one.
void SongSelectFlowTest::LeftRightMoveBySectionInACollapsedList() {
    std::vector<std::string> rows = Rows(sRun, "a_collapsed");
    std::vector<int> starts = SectionStarts(rows);
    ASSERT_GE(starts.size(), 4u) << sRun.driver;
    ASSERT_EQ(St("h0").headerMode, 1) << "precondition: the list is collapsed\n" << sRun.driver;
    EXPECT_EQ(St("h0").selected, 0) << "Left, until the top";
    EXPECT_EQ(St("h1").selected, starts[1]);
    EXPECT_EQ(St("h1").selected, 2) << "over both function rows to the first header";
    EXPECT_EQ(St("h2").selected, 3);
    EXPECT_EQ(St("h3").selected, 4);
    EXPECT_EQ(St("h4").selected, 3);
    EXPECT_EQ(St("h5").selected, (int)rows.size() - 1) << "Right, until the last header";
    EXPECT_EQ(St("h6").selected, (int)rows.size() - 1) << "Right on the last header stays";
    const char *const tags[] = { "h0", "h1", "h2", "h3", "h4", "h5", "h6" };
    for (int i = 0; i < 7; i++) {
        EXPECT_TRUE(OnScreen(St(tags[i]))) << tags[i] << ": selected " << St(tags[i]).selected
                                           << ", top " << St(tags[i]).top;
        EXPECT_EQ(St(tags[i]).headerMode, 1) << tags[i] << ": a jump must not open the list";
    }
}

// The same buttons in a second sort, opened: by artist.
void SongSelectFlowTest::LeftRightMoveBySectionUnderTheArtistSort() {
    std::vector<std::string> rows = Rows(sRun, "a_rows");
    std::vector<int> starts = SectionStarts(rows);
    ASSERT_GE(starts.size(), 4u) << sRun.driver;
    ASSERT_EQ(St("a0").headerMode, 0) << "precondition: the list is open\n" << sRun.driver;
    ASSERT_EQ(St("a0").selected, 0) << "precondition: Left, until the top, before it was opened";
    EXPECT_EQ(St("a1").selected, starts[1]);
    EXPECT_EQ(St("a2").selected, starts[2]);
    EXPECT_EQ(St("a2").top, starts[2]);
    ASSERT_EQ(St("a3").selected, starts[2] + 2) << "precondition: two Downs";
    EXPECT_EQ(St("a4").selected, starts[1]) << "Left from inside a section: the one before";
    EXPECT_TRUE(OnScreen(St("a4")));
}

// LB and RB move the highlight five rows, stopping at the ends.
void SongSelectFlowTest::ShoulderButtonsMoveAScreenful() {
    ListState j8 = St("j8");
    ASSERT_TRUE(j8.ok) << sRun.driver;
    int last = j8.numRows - 1;
    ASSERT_GT(j8.selected, kRowsOnScreen);
    EXPECT_EQ(St("p1").selected, j8.selected - kRowsOnScreen) << "LB";
    EXPECT_EQ(St("p2").selected, j8.selected) << "RB";
    EXPECT_EQ(St("p3").selected, std::min(last, j8.selected + kRowsOnScreen)) << "RB";
    EXPECT_EQ(St("p4").selected, std::min(last, j8.selected + 2 * kRowsOnScreen)) << "RB";
    EXPECT_EQ(St("p4").selected, last) << "precondition: the list ends within two pages";
    const char *const tags[] = { "p1", "p2", "p3", "p4" };
    for (int i = 0; i < 4; i++) {
        EXPECT_TRUE(OnScreen(St(tags[i]))) << tags[i] << ": selected " << St(tags[i]).selected
                                           << ", top " << St(tags[i]).top;
    }
}

// The buttons come after the list's own conditions for taking a button at
// all: switched off (disable_navigation, which song select does while one of
// its overlays is up) it takes none of them, like Up.  Switched on again, Left
// moves.
void SongSelectFlowTest::ASwitchedOffListIgnoresTheButtons() {
    std::vector<int> starts = SectionStarts(Rows(sRun, "d_rows"));
    ListState p4 = St("p4");
    ASSERT_TRUE(p4.ok) << sRun.driver;
    const char *const tags[] = { "off1", "off2", "off3" };
    const char *const buttons[] = { "Left", "LB", "Up" };
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(St(tags[i]).ok) << tags[i] << "\n" << sRun.driver;
        EXPECT_EQ(St(tags[i]).selected, p4.selected) << buttons[i] << " on a switched-off list";
        EXPECT_EQ(St(tags[i]).top, p4.top) << buttons[i] << " on a switched-off list";
    }
    EXPECT_EQ(St("on1").selected, LeftOf(starts, p4.selected))
        << "the control: switched on again, Left moves";
    EXPECT_LT(St("on1").selected, p4.selected);
}

// A list whose provider is not song select's keeps its highlight and its
// screen through each of the four buttons.  Read after every press: a list
// that went down on Right and back up on Left would look untouched at the end.
void SongSelectFlowTest::OtherListsIgnoreTheButtons() {
    std::string before = DriverLine(sRun, "other0");
    ASSERT_EQ(before.compare(0, 19, "choose_mode_screen "), 0) << sRun.driver;
    ASSERT_FALSE(JsonValue(before).empty()) << before;
    const char *const tags[] = { "other1", "other2", "other3", "other4" };
    const char *const buttons[] = { "Right", "RB", "Left", "LB" };
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(DriverLine(sRun, tags[i]), before) << "after " << buttons[i];
    }
}

// With nothing played: the image's three lists, the fourth sort in its place
// in the cycle, and the choice written down.
TEST_F(SongSelectFlowTest, SortsAndTheirLists) {
    NoHistoryMeansTheImagesList();
    SortButtonCyclesFourSortsAndSavesTheChoice();
}

TEST_F(SongSelectFlowTest, ArtistSort) {
    ArtistSortIsInArtistOrderUnderLetterHeaders();
    ArtistSortNamesTheArtistOnEachRow();
}

TEST_F(SongSelectFlowTest, SectionAndPageButtons) {
    LeftRightMoveBySection();
    LeftRightMoveBySectionInACollapsedList();
    LeftRightMoveBySectionUnderTheArtistSort();
    ShoulderButtonsMoveAScreenful();
    ASwitchedOffListIgnoresTheButtons();
    OtherListsIgnoreTheButtons();
}

// ---------------------------------------------------------------------------
// SongSelectHistoryFlowTest: two songs, a relaunch.
//
// Launch 1, sorted by title: the first song is played to the end through the
// menus and back to song select; the second only as far as gameplay, where
// the engine is killed.  Launch 2 is another process on the same save dir.
// ---------------------------------------------------------------------------

class SongSelectHistoryFlowTest : public SongSelectFlowBase {
protected:
    static EngineRun sFirst;
    static EngineRun sSecond;
    static std::string sError;
    static bool sRan;
    static std::string sSongOne; // played to the end
    static std::string sSongTwo; // abandoned in gameplay: the newest

    static void SetUpTestSuite() {
        if (!getenv("DC3_DTA_FLOW_TESTS")) return;
        sRan = true;
        std::string root = MakeRoot(sError);
        if (!sError.empty()) return;
        std::ostringstream d;
        d << kToSongSelect
          << "st enter; press y 150; expand; st sorted; "
          << "press down; st pick1; "
          << "play; echo \"DRIVER: back1=$(scr)\"; st back1; rows rows1; vis vis1; "
          << "press down; st pick2; "
          << "press confirm 60; waitfor 30 multiuser_screen; settle 120; nav game_screen 3; "
          << "echo \"DRIVER: game2=$(scr)\"; file file1; ";
        sFirst = RunEngine(root, "run1", d.str(), " DC3_FAST_TIME=1");
        sSongOne = State(sFirst, "pick1").token;
        sSongTwo = State(sFirst, "pick2").token;
        if (sSongOne.empty() || sSongTwo.empty()) return; // the tests say what is missing

        std::ostringstream e;
        e << kToSongSelect << "st enter; rows rows; vis vis; "
          << "echo \"DRIVER: lookup $(ev '{sprint \"data=\" {song_offer_provider data_index "
          << sSongTwo << "} \" first=\" {song_offer_provider first_data_index " << sSongTwo
          << "} \" header=\" {song_offer_provider get_header_symbol_from_child_symbol "
          << sSongTwo << "} \" section=\" {song_offer_provider first_data_index recently_played}}')\"; "
          // Round the sorts, opening each.
          << "press y 150; st s1; rows collapsed1; expand; rows rows_s1; "
          << "press y 150; st s2; expand; rows rows_s2; "
          << "press y 150; st s3; expand; rows rows_s3; "
          << "press y 150; st s4; expand; rows rows_s4; "
          // To the newest Recently Played row with the buttons: the top, the
          // next section, one row down.  Then pick it.
          << Times(14, "press left 30") << "st top; press right; st section; press down; st row; "
          << "press confirm 60; waitfor 30 multiuser_screen; "
          << "echo \"DRIVER: picked $(scr) $(ev '{meta_performer get_song}')\"; ";
        sSecond = RunEngine(root, "run2", e.str());
    }

    // The checks, grouped into the TESTs at the end of this suite: ctest runs
    // each TEST in its own process, so each one costs the suite's engine runs.
    static void PlayedSongIsOnTopAndTheHighlightComesBackToIt();
    static void ReachingGameplayCountsAndIsWrittenAtOnce();
    static void HistoryAndSortSurviveARelaunch();
    static void RecentlyPlayedIsOnTopOfEverySort();
    static void ASongNameMeansItsOwnRow();
    static void RecentlyPlayedRowSelectsThatSong();

    void SetUp() override {
        RequireRun(sRan, sError, sFirst);
        ASSERT_FALSE(sSongOne.empty() || sSongTwo.empty())
            << "precondition: launch 1 never highlighted two songs\n"
            << sFirst.driver << "\n--- tail ---\n"
            << Tail(sFirst.output);
        ASSERT_NE(sSongOne, sSongTwo);
    }

    // The relaunch is its own precondition, for the checks that read it.
    static bool RelaunchReachedSongSelect() {
        return sSecond.setupError.empty()
            && sSecond.driver.find("DRIVER: at=song_select_screen") != std::string::npos;
    }
};
EngineRun SongSelectHistoryFlowTest::sFirst;
EngineRun SongSelectHistoryFlowTest::sSecond;
std::string SongSelectHistoryFlowTest::sError;
bool SongSelectHistoryFlowTest::sRan = false;
std::string SongSelectHistoryFlowTest::sSongOne;
std::string SongSelectHistoryFlowTest::sSongTwo;

// Back from a song, the song is the first row of a new Recently Played
// section, and the highlight is where it was left: on the song's own row,
// which is now three rows further down.
void SongSelectHistoryFlowTest::PlayedSongIsOnTopAndTheHighlightComesBackToIt() {
    ASSERT_NE(sFirst.driver.find("DRIVER: back1=song_select_screen"), std::string::npos)
        << "precondition: the first song never came back to song select\n"
        << sFirst.driver << "\n--- tail ---\n"
        << Tail(sFirst.output);
    std::vector<std::string> rows = Rows(sFirst, "rows1");
    ASSERT_GT(rows.size(), 8u);
    EXPECT_EQ(rows[2], "header recently_played");
    EXPECT_EQ(rows[3], "song " + sSongOne);
    EXPECT_TRUE(IsHeader(rows[4])) << "one song played, one row: " << rows[4];
    EXPECT_EQ(CountRows(rows, "song " + sSongOne), 2) << "its own row is still in the list";

    ListState pick = State(sFirst, "pick1"), back = State(sFirst, "back1");
    EXPECT_EQ(back.sort, "by_song") << "the sort is kept while the game runs";
    EXPECT_EQ(back.headerMode, 0);
    EXPECT_EQ(back.token, sSongOne);
    EXPECT_EQ(back.selected, pick.selected + 2) << "the song's own row, under the new section";
    EXPECT_EQ(back.selected - back.top, pick.selected - pick.top) << "at the height it was";

    // And the section says what it is.
    std::map<int, std::map<std::string, std::string> > shown = Visible(sFirst, "vis1");
    if (shown.count(2)) EXPECT_EQ(shown[2]["sort_header"], "Recently Played");
}

// Reaching gameplay is what counts -- the second song was never finished --
// and the history is on disk by then, not at exit.
void SongSelectHistoryFlowTest::ReachingGameplayCountsAndIsWrittenAtOnce() {
    ASSERT_NE(sFirst.driver.find("DRIVER: game2=game_screen"), std::string::npos)
        << "precondition: the second song never reached gameplay\n"
        << sFirst.driver << "\n--- tail ---\n"
        << Tail(sFirst.output);
    EXPECT_EQ(DriverLine(sFirst, "file1"),
              "dc3-song-select 1|sort by_song|played " + sSongTwo + "|played " + sSongOne + "|");
}

// The next launch opens in the sort that was chosen, with both songs on top,
// newest first.
void SongSelectHistoryFlowTest::HistoryAndSortSurviveARelaunch() {
    ASSERT_TRUE(RelaunchReachedSongSelect())
        << "precondition: the relaunch never reached song select\n"
        << sSecond.setupError << sSecond.driver << "\n--- tail ---\n"
        << Tail(sSecond.output);
    std::vector<std::string> rows = Rows(sSecond, "rows");
    ASSERT_GT(rows.size(), 8u) << sSecond.driver;
    EXPECT_EQ(State(sSecond, "enter").sort, "by_song");
    EXPECT_EQ(rows[2], "header recently_played");
    EXPECT_EQ(rows[3], "song " + sSongTwo);
    EXPECT_EQ(rows[4], "song " + sSongOne);
    EXPECT_TRUE(IsHeader(rows[5])) << rows[5];
    std::map<int, std::map<std::string, std::string> > shown = Visible(sSecond, "vis");
    ASSERT_TRUE(shown.count(2)) << DriverValue(sSecond, "vis");
    EXPECT_EQ(shown[2]["sort_header"], "Recently Played");
    EXPECT_EQ(shown[2]["song_count"], "(2)");
}

// In all four sorts, collapsed and open; and below the section each of the
// image's sorts is the image's list, untouched.
void SongSelectHistoryFlowTest::RecentlyPlayedIsOnTopOfEverySort() {
    ASSERT_TRUE(RelaunchReachedSongSelect())
        << "precondition: the relaunch never reached song select\n"
        << sSecond.setupError << sSecond.driver << "\n--- tail ---\n"
        << Tail(sSecond.output);
    const char *const sorts[] = { "by_artist", "by_location", "by_difficulty", "by_song" };
    for (int i = 0; i < 4; i++) {
        std::string n = std::to_string(i + 1);
        EXPECT_EQ(State(sSecond, "s" + n).sort, sorts[i]);
        std::vector<std::string> rows = Rows(sSecond, "rows_s" + n);
        ASSERT_GT(rows.size(), 8u) << sorts[i] << "\n" << sSecond.driver;
        EXPECT_EQ(rows[2], "header recently_played") << sorts[i];
        EXPECT_EQ(rows[3], "song " + sSongTwo) << sorts[i];
        EXPECT_EQ(rows[4], "song " + sSongOne) << sorts[i];
        EXPECT_TRUE(IsHeader(rows[5])) << sorts[i] << ": " << rows[5];
        if (strcmp(sorts[i], "by_artist") != 0) {
            std::vector<std::string> classic = ClassicRows(sorts[i]);
            std::vector<std::string> without(rows.begin(), rows.begin() + 2);
            without.insert(without.end(), rows.begin() + 5, rows.end());
            EXPECT_EQ(without, classic) << sorts[i];
        }
    }
    std::vector<std::string> collapsed = Rows(sSecond, "collapsed1");
    ASSERT_GT(collapsed.size(), 3u);
    EXPECT_EQ(State(sSecond, "s1").headerMode, 1);
    EXPECT_EQ(collapsed[2], "header recently_played");
    EXPECT_TRUE(IsHeader(collapsed[3])) << "collapsed, the section is one row: " << collapsed[3];
}

// The song has two rows now.  Its name means its own row -- for a script, for
// voice ("select <song>"), for the header it sits under -- never the copy.
void SongSelectHistoryFlowTest::ASongNameMeansItsOwnRow() {
    ASSERT_TRUE(RelaunchReachedSongSelect())
        << "precondition: the relaunch never reached song select\n"
        << sSecond.setupError << sSecond.driver << "\n--- tail ---\n"
        << Tail(sSecond.output);
    std::vector<std::string> rows = Rows(sSecond, "rows");
    ASSERT_GT(rows.size(), 8u) << sSecond.driver;
    ASSERT_EQ(rows[3], "song " + sSongTwo) << "precondition: the copy is row 3";
    int ownRow = -1;
    std::string ownHeader;
    for (size_t i = 5; i < rows.size(); i++) {
        if (IsHeader(rows[i])) ownHeader = TokenOf(rows[i]);
        if (rows[i] == "song " + sSongTwo) {
            ownRow = (int)i;
            break;
        }
    }
    ASSERT_GT(ownRow, 5) << "precondition: the song's own row is in the list";
    std::string lookup = DriverValue(sSecond, "lookup");
    EXPECT_EQ(atoi(Field(lookup, "data=", " first=").c_str()), ownRow) << lookup;
    EXPECT_EQ(atoi(Field(lookup, " first=", " header=").c_str()), ownRow) << lookup;
    EXPECT_EQ(Unquoted(Field(lookup, " header=", " section=")), ownHeader) << lookup;
    EXPECT_EQ(atoi(Field(lookup, " section=", nullptr).c_str()), 2) << lookup;
}

// Picking a Recently Played row picks that song.
void SongSelectHistoryFlowTest::RecentlyPlayedRowSelectsThatSong() {
    ASSERT_TRUE(RelaunchReachedSongSelect())
        << "precondition: the relaunch never reached song select\n"
        << sSecond.setupError << sSecond.driver << "\n--- tail ---\n"
        << Tail(sSecond.output);
    ASSERT_EQ(State(sSecond, "top").selected, 0) << sSecond.driver;
    ASSERT_EQ(State(sSecond, "section").selected, 2) << "Right from the top: the section's header";
    ListState row = State(sSecond, "row");
    ASSERT_EQ(row.selected, 3);
    ASSERT_EQ(row.token, sSongTwo) << "precondition: the newest Recently Played row is highlighted";
    std::string picked = DriverLine(sSecond, "picked");
    ASSERT_EQ(picked.compare(0, 17, "multiuser_screen "), 0)
        << "confirm on the row did not go on to the next screen: " << picked;
    EXPECT_EQ(JsonValue(picked), sSongTwo);
}

TEST_F(SongSelectHistoryFlowTest, PlayedSongsGoOnTopAndSurviveARelaunch) {
    PlayedSongIsOnTopAndTheHighlightComesBackToIt();
    ReachingGameplayCountsAndIsWrittenAtOnce();
    HistoryAndSortSurviveARelaunch();
}

TEST_F(SongSelectHistoryFlowTest, RecentlyPlayedRowsAreSecondRowsOfTheirSongs) {
    RecentlyPlayedIsOnTopOfEverySort();
    ASongNameMeansItsOwnRow();
    RecentlyPlayedRowSelectsThatSong();
}

// ---------------------------------------------------------------------------
// SongSelectClassicFlowTest: DC3_CLASSIC_SONG_SELECT=1, with a save dir that
// holds a history and a remembered sort.
// ---------------------------------------------------------------------------

class SongSelectClassicFlowTest : public SongSelectFlowBase {
protected:
    static EngineRun sRun;
    static std::string sError;
    static bool sRan;
    static const char *const kSavedFile;

    static void SetUpTestSuite() {
        if (!getenv("DC3_DTA_FLOW_TESTS")) return;
        sRan = true;
        std::string root = MakeRoot(sError);
        if (!sError.empty()) return;
        WriteFile(root + "/saves/song_select.txt", kSavedFile);
        std::ostringstream d;
        d << kToSongSelect << "st enter; rows rows0; "
          << "press right; st k1; press left; st k2; press rb; st k3; press lb; st k4; "
          << "press down; st k5; "
          << "press y 150; st s1; file file_s1; expand; rows rows1; "
          << "press y 150; st s2; expand; rows rows2; "
          << "press y 150; st s3; file file_end; ";
        sRun = RunEngine(root, "run", d.str(), " DC3_CLASSIC_SONG_SELECT=1");
    }

    // The checks, grouped into the TESTs at the end of this suite: ctest runs
    // each TEST in its own process, so each one costs the suite's engine runs.
    static void ListIsTheImagesInEverySort();
    static void HistoryAndRememberedSortAreIgnored();
    static void SectionAndPageButtonsAreInert();

    void SetUp() override { RequireRun(sRan, sError, sRun); }
};
EngineRun SongSelectClassicFlowTest::sRun;
std::string SongSelectClassicFlowTest::sError;
bool SongSelectClassicFlowTest::sRan = false;
const char *const SongSelectClassicFlowTest::kSavedFile =
    "dc3-song-select 1\nsort by_location\nplayed ymca\nplayed starships\n";

// The list is the image's, row for row, in each of the image's three sorts --
// and there are three: the third press of the sort button is back at the first.
void SongSelectClassicFlowTest::ListIsTheImagesInEverySort() {
    EXPECT_EQ(State(sRun, "enter").sort, "by_difficulty");
    EXPECT_EQ(Rows(sRun, "rows0"), ClassicRows("by_difficulty"));
    EXPECT_EQ(State(sRun, "s1").sort, "by_song");
    EXPECT_EQ(Rows(sRun, "rows1"), ClassicRows("by_song"));
    EXPECT_EQ(State(sRun, "s2").sort, "by_location");
    EXPECT_EQ(Rows(sRun, "rows2"), ClassicRows("by_location"));
    EXPECT_EQ(State(sRun, "s3").sort, "by_difficulty");
    EXPECT_GT(ClassicRows("by_difficulty").size(), 40u) << "the fixture did not load";
    EXPECT_EQ(State(sRun, "enter").helpBar, "(by_difficulty by_song by_location)");
    EXPECT_EQ(State(sRun, "s1").helpBar, "(by_song by_location by_difficulty)");
    EXPECT_EQ(State(sRun, "s2").helpBar, "(by_location by_difficulty by_song)");
}

// The history and the sort in the save dir are neither shown nor used, and
// the sort presses do not write the file.
void SongSelectClassicFlowTest::HistoryAndRememberedSortAreIgnored() {
    EXPECT_EQ(State(sRun, "enter").sort, "by_difficulty") << "the file says by_location";
    EXPECT_EQ(sRun.driver.find("recently_played"), std::string::npos) << sRun.driver;
    std::string file = kSavedFile;
    std::replace(file.begin(), file.end(), '\n', '|');
    // After the first press as well as the last: the third press is back at
    // the sort the run started in.
    EXPECT_EQ(DriverLine(sRun, "file_s1"), file);
    EXPECT_EQ(DriverLine(sRun, "file_end"), file);
}

// Left, Right, LB and RB do nothing, as in the image; Down still does.
void SongSelectClassicFlowTest::SectionAndPageButtonsAreInert() {
    ListState enter = State(sRun, "enter");
    ASSERT_TRUE(enter.ok) << sRun.driver;
    const char *const tags[] = { "k1", "k2", "k3", "k4" };
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(State(sRun, tags[i]).selected, enter.selected) << tags[i];
        EXPECT_EQ(State(sRun, tags[i]).top, enter.top) << tags[i];
    }
    EXPECT_EQ(State(sRun, "k5").selected, enter.selected + 1)
        << "the control: a button the image handles does move the highlight";
}

TEST_F(SongSelectClassicFlowTest, ClassicSwitchRestoresTheImagesSongSelect) {
    ListIsTheImagesInEverySort();
    HistoryAndRememberedSortAreIgnored();
    SectionAndPageButtonsAreInert();
}

// ===========================================================================
// The natively drawn song browser in song select
// (native/src/platform/SongBrowserPanel_Native.cpp, docs/native/SONG_SELECT.md
// "The song browser").
//
// Every run in this file has MILO_INPUT_SCRIPT set, which is what pins the
// classic list for the suites above; the suites below ask for the browser by
// name (DC3_SONG_BROWSER=1 or =auto).
// ===========================================================================

namespace {

// The value of "key" in the browser's state answer: a string without its
// quotes, a number or a bool as written.  Empty when the key is not there.
std::string Sb(const std::string &json, const std::string &key) {
    size_t at = json.find("\"" + key + "\":");
    if (at == std::string::npos) return "";
    at += key.size() + 3;
    if (at >= json.size()) return "";
    if (json[at] == '"') {
        size_t end = json.find('"', at + 1);
        return end == std::string::npos ? "" : json.substr(at + 1, end - at - 1);
    }
    size_t end = json.find_first_of(",}", at);
    return json.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

// "SCREEN SONG" of a `echo "DRIVER: tag $(scr) $(ev '{meta_performer get_song}')"` line.
std::string Picked(const EngineRun &run, const std::string &tag) {
    std::string line = DriverLine(run, tag);
    size_t space = line.find(' ');
    if (space == std::string::npos) return line;
    return line.substr(0, space) + " " + JsonValue(line.substr(space + 1));
}

const char *const kBrowserSong = "boomboompow";
// Two songs played, so there is a Recently Played shelf with the test's song
// on its first card.
const char *const kBrowserHistory = "dc3-song-select 1\nplayed boomboompow\nplayed ymca\n";
const char *const kPerformerSong = "$(ev '{meta_performer get_song}')";
const char *const kClassicToken = "$(ev '{song_select_panel get_selected_song_token}')";

} // namespace

// One engine with DC3_SONG_BROWSER=1 and a history: arrive, pick from the
// shelf, come back, pick from the list, come back, search and pick from the
// results, come back, clear the query, change the sort, leave with Back.
class SongBrowserInSongSelectTest : public SongSelectFlowBase {
protected:
    static EngineRun sRun;
    static EngineRun sClassic;
    static std::string sError;
    static bool sRan;

    static void SetUpTestSuite() {
        if (!getenv("DC3_DTA_FLOW_TESTS")) return;
        sRan = true;
        std::string root = MakeRoot(sError);
        if (!sError.empty()) return;
        WriteFile(root + "/saves/song_select.txt", kBrowserHistory);
        const std::string song = kBrowserSong;
        const std::string toSong = "n=0; while [ \"$(sel)\" != " + song
            + " ] && [ $n -lt 20 ]; do press down 30; n=$((n+1)); done; ";
        const std::string comeBack =
            "press cancel 60; waitfor 30 song_select_screen; settle 240; ";
        std::ostringstream d;
        d << kToSongSelect
          << "echo \"DRIVER: arrive $(sb)\"; echo \"DRIVER: arrive_classic " << kClassicToken << "\"; "
          << "echo \"DRIVER: arrive_info $(ev '{song_info_panel showing}') $(ev '{song_select_panel showing}')\"; "
          // From the shelf.
          << "press confirm 60; waitfor 30 multiuser_screen; "
          << "echo \"DRIVER: shelf_pick $(scr) " << kPerformerSong << "\"; echo \"DRIVER: away $(sb)\"; "
          << comeBack << "echo \"DRIVER: return1 $(sb)\"; "
          // From the list.
          << "press down 30; " << toSong << "echo \"DRIVER: list_at $(sb)\"; "
          << "echo \"DRIVER: list_classic " << kClassicToken << "\"; "
          << "press confirm 60; waitfor 30 multiuser_screen; "
          << "echo \"DRIVER: list_pick $(scr) " << kPerformerSong << "\"; "
          << comeBack << "echo \"DRIVER: return2 $(sb)\"; "
          // From search results.
          << "typ boom; echo \"DRIVER: typed $(sb)\"; press down 30; " << toSong
          << "echo \"DRIVER: search_at $(sb)\"; "
          << "press confirm 60; waitfor 30 multiuser_screen; "
          << "echo \"DRIVER: search_pick $(scr) " << kPerformerSong << "\"; "
          << comeBack << "echo \"DRIVER: return3 $(sb)\"; "
          // Back with a query clears it; the sort; Back without one leaves.
          << "press cancel 60; echo \"DRIVER: cleared $(scr) $(sb)\"; "
          << "press rb 60; echo \"DRIVER: sorted $(sb)\"; file file_sorted; "
          << "press cancel 60; waitfor 30 choose_mode_screen; echo \"DRIVER: back $(scr) $(sb)\"; ";
        sRun = RunEngine(root, "browser", d.str(), " DC3_SONG_BROWSER=1");

        // The reference: the same song picked on the classic list with A.
        std::string classicRoot = MakeRoot(sError);
        if (!sError.empty()) return;
        WriteFile(classicRoot + "/saves/song_select.txt", kBrowserHistory);
        std::ostringstream c;
        c << kToSongSelect << "echo \"DRIVER: state $(sb)\"; "
          << "ev '{song_select_panel highlight_right_list_item {song_offer_provider "
          << "first_data_index " << song << "}}' > /dev/null; settle 90; st at; "
          << "press confirm 60; waitfor 30 multiuser_screen; "
          << "echo \"DRIVER: pick $(scr) " << kPerformerSong << "\"; ";
        sClassic = RunEngine(classicRoot, "classic", c.str(), " DC3_SONG_BROWSER=0");
    }

    void Require() {
        RequireRun(sRan, sError, sRun);
        if (IsSkipped() || HasFatalFailure()) return;
        ASSERT_EQ(Sb(DriverLine(sRun, "arrive"), "available"), "true")
            << "no browser state\n" << sRun.driver << "\n--- tail ---\n" << Tail(sRun.output);
    }
};

EngineRun SongBrowserInSongSelectTest::sRun;
EngineRun SongBrowserInSongSelectTest::sClassic;
std::string SongBrowserInSongSelectTest::sError;
bool SongBrowserInSongSelectTest::sRan = false;

TEST_F(SongBrowserInSongSelectTest, TheBrowserIsTheListOnArrivalWithAPad) {
    Require();
    if (IsSkipped() || HasFatalFailure()) return;
    const std::string arrive = DriverLine(sRun, "arrive");
    EXPECT_EQ(Sb(arrive, "shown"), "true") << arrive;
    EXPECT_EQ(Sb(arrive, "focus"), "recents") << "a history: the shelf has focus\n" << arrive;
    EXPECT_EQ(Sb(arrive, "selected_song"), kBrowserSong) << arrive;
    EXPECT_GT(atoi(Sb(arrive, "songs").c_str()), 10) << arrive;
    EXPECT_GT(atoi(Sb(arrive, "rows").c_str()), 10) << arrive;
    EXPECT_EQ(Sb(arrive, "text_capture"), "true") << "typing anywhere searches\n" << arrive;
    // The classic list is underneath, on the same song, and is not shown.
    EXPECT_EQ(DriverValue(sRun, "arrive_classic"), kBrowserSong)
        << "the classic list's highlight follows the browser";
    const std::string info = DriverLine(sRun, "arrive_info");
    EXPECT_NE(info.find("\"value\":0"), std::string::npos)
        << "song_info_panel is not shown under the browser: " << info;
}

TEST_F(SongBrowserInSongSelectTest, APickFromShelfListOrSearchIsTheClassicPick) {
    Require();
    if (IsSkipped() || HasFatalFailure()) return;
    ASSERT_NE(sClassic.driver.find("DRIVER: at=song_select_screen"), std::string::npos)
        << "the classic reference never reached song select\n" << sClassic.driver;
    EXPECT_EQ(Sb(DriverLine(sClassic, "state"), "shown"), "false");
    ASSERT_EQ(State(sClassic, "at").token, kBrowserSong) << DriverLine(sClassic, "at");
    const std::string classic = Picked(sClassic, "pick");
    ASSERT_EQ(classic, std::string("multiuser_screen ") + kBrowserSong)
        << "the reference: A on the classic list\n" << sClassic.driver;
    EXPECT_EQ(Picked(sRun, "shelf_pick"), classic) << "picked from Recently Played";
    EXPECT_EQ(Sb(DriverLine(sRun, "list_at"), "focus"), "list") << DriverLine(sRun, "list_at");
    EXPECT_EQ(DriverValue(sRun, "list_classic"), kBrowserSong)
        << "the classic list is on the browser's song before the pick";
    EXPECT_EQ(Picked(sRun, "list_pick"), classic) << "picked from the list";
    EXPECT_EQ(Sb(DriverLine(sRun, "search_at"), "query"), "boom") << DriverLine(sRun, "search_at");
    EXPECT_EQ(Picked(sRun, "search_pick"), classic) << "picked from search results";
    // Off the screen the browser is gone and the keyboard is the game's again.
    const std::string away = DriverLine(sRun, "away");
    EXPECT_EQ(Sb(away, "shown"), "false") << away;
    EXPECT_EQ(Sb(away, "text_capture"), "false") << away;
}

TEST_F(SongBrowserInSongSelectTest, TypingFiltersBackClearsThenLeavesAndTheStateComesBack) {
    Require();
    if (IsSkipped() || HasFatalFailure()) return;
    const std::string arrive = DriverLine(sRun, "arrive");
    // Typing filters.
    const std::string typed = DriverLine(sRun, "typed");
    EXPECT_EQ(Sb(typed, "query"), "boom") << typed;
    EXPECT_EQ(Sb(typed, "focus"), "search") << typed;
    EXPECT_GT(atoi(Sb(typed, "rows").c_str()), 0) << typed;
    EXPECT_LT(atoi(Sb(typed, "rows").c_str()), atoi(Sb(arrive, "rows").c_str())) << typed;
    EXPECT_EQ(Sb(typed, "selected_song"), kBrowserSong) << typed;
    // The state survives leaving and returning: three visits.
    const std::string return1 = DriverLine(sRun, "return1");
    EXPECT_EQ(Sb(return1, "shown"), "true") << return1;
    EXPECT_EQ(Sb(return1, "focus"), "recents") << return1;
    EXPECT_EQ(Sb(return1, "selected_song"), kBrowserSong) << return1;
    const std::string return2 = DriverLine(sRun, "return2");
    EXPECT_EQ(Sb(return2, "focus"), "list") << return2;
    EXPECT_EQ(Sb(return2, "list_song"), kBrowserSong) << return2;
    const std::string return3 = DriverLine(sRun, "return3");
    EXPECT_EQ(Sb(return3, "query"), "boom") << return3;
    EXPECT_EQ(Sb(return3, "selected_song"), kBrowserSong) << return3;
    // Back with a query clears it and stays; Back without one leaves.
    const std::string cleared = DriverLine(sRun, "cleared");
    EXPECT_EQ(cleared.compare(0, 19, "song_select_screen "), 0) << cleared;
    EXPECT_EQ(Sb(cleared, "query"), "") << cleared;
    EXPECT_EQ(Sb(cleared, "shown"), "true") << cleared;
    const std::string back = DriverLine(sRun, "back");
    EXPECT_EQ(back.compare(0, 19, "choose_mode_screen "), 0)
        << "Back leaves by the panel's own on_back\n" << back;
    EXPECT_EQ(Sb(back, "shown"), "false") << back;
    EXPECT_EQ(Sb(back, "text_capture"), "false") << back;
    // The sort is remembered where song select remembers its own.
    EXPECT_EQ(Sb(DriverLine(sRun, "sorted"), "sort"), "artist") << DriverLine(sRun, "sorted");
    EXPECT_NE(DriverLine(sRun, "file_sorted").find("sort by_artist|"), std::string::npos)
        << DriverLine(sRun, "file_sorted");
}

// The switches: one engine each.
class SongBrowserSwitchFlowTest : public SongSelectFlowBase {
protected:
    // To song select, the browser's state, one Down, the classic list's row.
    EngineRun Visit(const std::string &env) {
        std::string error;
        std::string root = MakeRoot(error);
        EXPECT_TRUE(error.empty()) << error;
        std::ostringstream d;
        d << kToSongSelect << "echo \"DRIVER: state $(sb)\"; st before; press down; st after; "
          << "echo \"DRIVER: state2 $(sb)\"; ";
        return RunEngine(root, "run", d.str(), env);
    }

    void ExpectClassic(const EngineRun &run, const char *why) {
        RequireRun(true, "", run);
        if (IsSkipped() || HasFatalFailure()) return;
        const std::string state = DriverLine(run, "state");
        EXPECT_EQ(Sb(state, "shown"), "false") << state;
        EXPECT_EQ(Sb(state, "text_capture"), "false") << state;
        EXPECT_NE(Sb(state, "why").find(why), std::string::npos) << state;
        EXPECT_EQ(State(run, "after").selected, State(run, "before").selected + 1)
            << "Down moves the classic list\n" << run.driver;
        EXPECT_EQ(Sb(DriverLine(run, "state2"), "shown"), "false");
    }
};

TEST_F(SongBrowserSwitchFlowTest, SwitchOffIsTheClassicList) {
    if (!getenv("DC3_DTA_FLOW_TESTS")) GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable";
    ExpectClassic(Visit(" DC3_SONG_BROWSER=0"), "DC3_SONG_BROWSER=0");
}

TEST_F(SongBrowserSwitchFlowTest, ClassicSongSelectSwitchIsTheClassicList) {
    if (!getenv("DC3_DTA_FLOW_TESTS")) GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable";
    ExpectClassic(
        Visit(" DC3_SONG_BROWSER=1 DC3_CLASSIC_SONG_SELECT=1"), "DC3_CLASSIC_SONG_SELECT=1"
    );
}

// THE pin for every run written against the classic list: an input script
// and no DC3_SONG_BROWSER.
TEST_F(SongBrowserSwitchFlowTest, AScriptedRunThatDoesNotAskGetsTheClassicList) {
    if (!getenv("DC3_DTA_FLOW_TESTS")) GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable";
    ExpectClassic(Visit(" DC3_SONG_BROWSER="), "MILO_INPUT_SCRIPT");
}

// Playlists: the browser belongs to song_select_panel's pick-one-song visits.
// The playlist screens are other panels, and stay the image's.
TEST_F(SongBrowserSwitchFlowTest, PlaylistScreensKeepTheClassicList) {
    if (!getenv("DC3_DTA_FLOW_TESTS")) GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable";
    std::string error;
    std::string root = MakeRoot(error);
    ASSERT_TRUE(error.empty()) << error;
    std::ostringstream d;
    d << kToSongSelect << "echo \"DRIVER: state $(sb)\"; "
      << "ev '{song_select_panel move_on_quickplay_playlist}' > /dev/null; settle 240; "
      << "n=0; while [ \"$(scr)\" != song_select_playlist_screen ] && [ $n -lt 10 ]; do "
      << "press confirm 120; settle 120; n=$((n+1)); done; "
      << "echo \"DRIVER: playlists $(scr) $(sb)\"; "
      << "echo \"DRIVER: mode $(ev '{gamemode get song_select_mode}')\"; "
      << "press confirm 120; settle 240; echo \"DRIVER: next $(scr) $(sb)\"; ";
    EngineRun run = RunEngine(root, "run", d.str(), " DC3_SONG_BROWSER=1");
    RequireRun(true, "", run);
    if (IsSkipped() || HasFatalFailure()) return;
    ASSERT_EQ(Sb(DriverLine(run, "state"), "shown"), "true")
        << "precondition: the browser is on for this run\n" << run.driver;
    const std::string playlists = DriverLine(run, "playlists");
    ASSERT_EQ(playlists.compare(0, 28, "song_select_playlist_screen "), 0) << run.driver;
    EXPECT_EQ(DriverValue(run, "mode"), "song_select_playlist");
    EXPECT_EQ(Sb(playlists, "shown"), "false") << playlists;
    EXPECT_EQ(Sb(playlists, "text_capture"), "false") << playlists;
    const std::string next = DriverLine(run, "next");
    EXPECT_EQ(Sb(next, "shown"), "false") << next;
}

// auto, under the image's own controller-mode rules (faithful) with a short
// timeout: the dummy skeleton is a tracked body, so once the pad has been
// quiet the menu is the gesture player's and the classic list comes back --
// on the browser's song.  A press brings the browser back.
TEST_F(SongBrowserSwitchFlowTest, AutoGivesWayToGesturesAndReturnsOnAPress) {
    if (!getenv("DC3_DTA_FLOW_TESTS")) GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable";
    std::string error;
    std::string root = MakeRoot(error);
    ASSERT_TRUE(error.empty()) << error;
    std::ostringstream d;
    d << kToSongSelect << "press down 30; press down 30; echo \"DRIVER: pad $(sb)\"; "
      // The controller-mode timeout is wall time; the browser's 2 s are UI time.
      << "n=0; while sb | grep -q '\"shown\":true' && [ $n -lt 40 ]; do sleep 0.25; n=$((n+1)); done; "
      << "settle 30; echo \"DRIVER: gestures $(sb)\"; "
      << "echo \"DRIVER: gestures_classic " << kClassicToken << "\"; "
      << "echo \"DRIVER: gestures_draw $(ev '{song_info_panel showing}')\"; "
      << "press down 30; echo \"DRIVER: press1 $(sb)\"; press down 30; echo \"DRIVER: press2 $(sb)\"; ";
    EngineRun run = RunEngine(
        root, "run", d.str(),
        " DC3_SONG_BROWSER=auto DC3_CONTROLLER_MODE=faithful DC3_CONTROLLER_MODE_TIMEOUT_MS=1500"
    );
    RequireRun(true, "", run);
    if (IsSkipped() || HasFatalFailure()) return;
    const std::string pad = DriverLine(run, "pad");
    ASSERT_EQ(Sb(pad, "shown"), "true") << "auto shows the browser while the pad is in use\n" << pad;
    EXPECT_EQ(Sb(pad, "mode"), "auto") << pad;
    const std::string song = Sb(pad, "selected_song");
    ASSERT_FALSE(song.empty()) << pad;
    const std::string gestures = DriverLine(run, "gestures");
    EXPECT_EQ(Sb(gestures, "shown"), "false") << gestures;
    EXPECT_EQ(Sb(gestures, "controller_mode"), "false") << gestures;
    EXPECT_NE(Sb(gestures, "why").find("gestures"), std::string::npos) << gestures;
    EXPECT_EQ(Sb(gestures, "text_capture"), "false")
        << "the keyboard is the game's again the moment the browser hides\n" << gestures;
    EXPECT_EQ(DriverValue(run, "gestures_classic"), song)
        << "the classic list comes back on the browser's song";
    EXPECT_NE(DriverLine(run, "gestures_draw").find("\"value\":1"), std::string::npos)
        << "song_info_panel is shown again with the classic list";
    const std::string press1 = DriverLine(run, "press1");
    EXPECT_EQ(Sb(press1, "shown"), "true") << "a press brings the browser back\n" << press1;
    EXPECT_EQ(Sb(press1, "selected_song"), song)
        << "that press only woke the pad (the game takes it to enter controller mode)\n" << press1;
    const std::string press2 = DriverLine(run, "press2");
    EXPECT_NE(Sb(press2, "selected_song"), song) << "the next one moves\n" << press2;
}
