// Animation: values that move over time.
//
// Rules:
//  - Time is seconds, handed in as a delta. Nothing counts frames, and any
//    split of the same total time ends in the same place.
//  - An animation lands on its target exactly, and says so: AtRest() is true
//    only when the value equals the target and nothing is moving.
//  - These are plain values. A widget owns the ones it needs and advances them.
//
// Tween: a fixed-duration move with an easing curve, for one-shot transitions.
// Spring: a critically damped spring, for things that chase a target that can
// change mid-flight (scroll position, a highlight following the selection). It
// never overshoots from rest and keeps its velocity when retargeted.
#pragma once

namespace nativeui {

    enum class Easing {
        Linear,
        EaseOut, // fast start, gentle stop
        EaseInOut // gentle start and stop
    };

    // Maps progress 0..1 to eased progress 0..1; 0 and 1 map to themselves exactly.
    float Ease(Easing easing, float progress);

    class Tween {
    public:
        Tween() = default;
        explicit Tween(float value) : from_(value), to_(value), value_(value) {}

        // Jump to `value` and stop.
        void Snap(float value);
        // Move from the current value to `target` over `seconds`.
        void Start(float target, float seconds, Easing easing);
        void Advance(float deltaSeconds);

        float Value() const { return value_; }
        float Target() const { return to_; }
        bool AtRest() const { return value_ == to_; }

    private:
        float from_ = 0.0f;
        float to_ = 0.0f;
        float value_ = 0.0f;
        float duration_ = 0.0f;
        float elapsed_ = 0.0f;
        Easing easing_ = Easing::Linear;
    };

    // How a spring behaves; lives in the theme.
    struct SpringParams {
        // Stiffness as an angular frequency in radians per second. The value is
        // within 1% of its target after about 6.6 / angularFrequency seconds.
        float angularFrequency = 20.0f;
        // Closer than this to the target (and slow enough), the spring snaps onto
        // it and rests. In the units of the animated value.
        float restDistance = 0.01f;
    };

    class Spring {
    public:
        Spring() = default;
        explicit Spring(float value) : value_(value), target_(value) {}

        // Jump to `value` and stop.
        void Snap(float value);
        void SetTarget(float target) { target_ = target; }
        void Advance(float deltaSeconds, const SpringParams &params);

        float Value() const { return value_; }
        float Target() const { return target_; }
        float Velocity() const { return velocity_; }
        bool AtRest() const { return value_ == target_ && velocity_ == 0.0f; }

    private:
        float value_ = 0.0f;
        float target_ = 0.0f;
        float velocity_ = 0.0f;
    };

}
