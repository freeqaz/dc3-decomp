#pragma once

namespace GameplayTelemetry {

struct Snapshot {
    int frame = 0;
    const char* state = "boot";      // boot, loading, intro, playing, gameover
    const char* screen = "";
    const char* transitionScreen = "";
    bool uiInTransition = false;
    bool gameScreenActive = false;
    bool currentHasWorldPanel = false;
    bool transitionHasWorldPanel = false;
    bool worldPanelLoaded = false;
    int gamePanelLoadState = -1;
    int gameWaitState = -1;
    int gameLoadState = -1;
    bool gameUsesMoveGraph = false;
    bool gamePaused = false;
    bool gameRealTime = false;
    float beat = 0;
    float realSecs = 0;
    float songAnimFrame = 0;
    bool pollEnabled = false;
    bool worldLoaded = false;
    bool worldPresent = false;
    bool venuePresent = false;
    const char* typeDef = "";
    const char* gameStage = "";
    bool hamProvider = false;
    bool mergerDir = false;
    bool clipDir = false;
    bool masterClip = false;
    bool clipPlayerInit = false;
    int charClipLayers = -1;
    bool player0 = false;
    bool player1 = false;
    int clipKeyCount = -1;
    int songAnimKeys = -1;
    int diffProxy = 0;
    int routineLoaded = 0;
    int mergeMoves = -1;
    int p0SongAnim = -99;
    int doSongAnim = -1;
    int nativeSetFrameCount = 0;

    // Move/flashcard validation fields
    bool moveInterpActive = false;  // move prop key track exists and has keys
    int moveKeyCount = 0;           // number of keys in the move prop key track
    float songAnimFrameRate = 0;    // delta of songAnimFrame between telemetry samples
    int activeMoveCount = 0;        // players with a non-null, non-Rest current move

    // HUD merge convergence (T1-T4 invariants)
    bool hudMergeTargetIsHUD = false; // T1: game_hud MergerDir() == world->GetHUD()
    bool hudPanelIsHUD = false;       // T2: DataVariable("hud_panel") == world->GetHUD()
    bool hudHasLeft = false;          // T3a: hud_left findable in merge target
    bool hudHasRight = false;         // T3b: hud_right findable in merge target
    bool hudMDirResolved = false;     // whether game_hud merger's mDir is non-null (vs fallback)
};

void Init();            // Check DC3_TEL env var
void Sample(int frame); // Emit key=value lines to stderr for this frame
bool IsEnabled();

// Capture current engine state as a snapshot (callable any frame, no interval gating)
Snapshot CaptureSnapshot(int frame);

// Last snapshot captured by Sample() — only updated every sInterval frames
const Snapshot& LastSnapshot();

}
