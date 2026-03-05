#pragma once

#include <vector>

class ObjectDir;
class Character;
class CharClip;
class CharServoBone;
class CharFaceServo;
class CharEyes;
class CharPollable;
class CharLipSyncDriver;
class RndAnimatable;
struct ViewerConfig;

// ============================================================================
// BlinkState — procedural blink timer state machine
// ============================================================================
struct BlinkState {
    float timer = 2.0f;
    float phase = 0.0f;
    static constexpr float kDuration = 0.15f;

    void Advance(float dt);
    float Weight() const;
};

// ============================================================================
// AnimState — prop/TransAnim frame-based animation
// ============================================================================
struct AnimState {
    bool  paused = false;
    float speed = 1.0f;
    float currentFrame = 0.0f;
    float startFrame = 0.0f;
    float endFrame = 0.0f;
    bool  hasAnimation = false;
    double lastTime = 0.0;
    int   animCount = 0;
    std::vector<RndAnimatable*> animatables;

    void ScanScene(ObjectDir* dir, const ViewerConfig& cfg);
};

extern AnimState gAnim;

// ============================================================================
// CharAnimState — character clip-based animation (beat-based)
// ============================================================================
struct CharAnimState {
    bool           active = false;
    Character*     character = nullptr;
    CharClip*      clip = nullptr;
    CharServoBone* servo = nullptr;
    CharFaceServo* faceServo = nullptr;
    CharEyes*      eyes = nullptr;
    CharLipSyncDriver* lipDriver = nullptr;
    BlinkState     blink;
    float          lastBeat = 0.0f;
    float          lastSeconds = 0.0f;
    std::vector<CharPollable*> pollables;

    void CollectPollables();
    void AdvanceBeat(float targetSeconds, float targetBeat, float bpm);
    void DirectPose(float beat, float bpm);
    void PollFace();
};

// ============================================================================
// PoseMeshesWithFacing — facing-aware clip pose
// ============================================================================
void PoseMeshesWithFacing(CharClip* clip, Character* chr, float beat);
void ResetFacingCache();
