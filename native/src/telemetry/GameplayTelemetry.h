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
};

void Init();            // Check DC3_TEL env var
void Sample(int frame); // Emit key=value lines to stderr for this frame
bool IsEnabled();

// Capture current engine state as a snapshot (callable any frame, no interval gating)
Snapshot CaptureSnapshot(int frame);

// Last snapshot captured by Sample() — only updated every sInterval frames
const Snapshot& LastSnapshot();

}
