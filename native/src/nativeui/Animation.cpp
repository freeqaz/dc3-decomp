#include "nativeui/Animation.h"

#include <algorithm>
#include <cmath>

namespace nativeui {

    float Ease(Easing easing, float progress) {
        const float t = std::min(1.0f, std::max(0.0f, progress));
        switch (easing) {
        case Easing::EaseOut: {
            const float inverse = 1.0f - t;
            return 1.0f - inverse * inverse * inverse;
        }
        case Easing::EaseInOut:
            if (t < 0.5f) {
                return 4.0f * t * t * t;
            } else {
                const float inverse = 1.0f - t;
                return 1.0f - 4.0f * inverse * inverse * inverse;
            }
        case Easing::Linear:
            break;
        }
        return t;
    }

    void Tween::Snap(float value) {
        from_ = to_ = value_ = value;
        duration_ = elapsed_ = 0.0f;
    }

    void Tween::Start(float target, float seconds, Easing easing) {
        from_ = value_;
        to_ = target;
        duration_ = std::max(0.0f, seconds);
        elapsed_ = 0.0f;
        easing_ = easing;
        if (duration_ == 0.0f) {
            value_ = to_;
        }
    }

    void Tween::Advance(float deltaSeconds) {
        if (AtRest() || deltaSeconds <= 0.0f) {
            return;
        }
        elapsed_ += deltaSeconds;
        if (elapsed_ >= duration_) {
            value_ = to_; // exactly, not within rounding of it
            return;
        }
        value_ = from_ + (to_ - from_) * Ease(easing_, elapsed_ / duration_);
    }

    void Spring::Snap(float value) {
        value_ = target_ = value;
        velocity_ = 0.0f;
    }

    void Spring::Advance(float deltaSeconds, const SpringParams &params) {
        if (AtRest() || deltaSeconds <= 0.0f) {
            return;
        }
        // Closed-form critically damped motion, so the result does not depend on
        // how the elapsed time was cut into steps:
        //   x(t) = (x0 + (v0 + w x0) t) e^(-w t),  x measured from the target.
        const float w = params.angularFrequency;
        const float x0 = value_ - target_;
        const float c = velocity_ + w * x0;
        const float decay = std::exp(-w * deltaSeconds);
        const float x = (x0 + c * deltaSeconds) * decay;
        const float v = (c - w * (x0 + c * deltaSeconds)) * decay;

        if (std::fabs(x) <= params.restDistance
            && std::fabs(v) <= params.restDistance * w) {
            value_ = target_;
            velocity_ = 0.0f;
            return;
        }
        value_ = target_ + x;
        velocity_ = v;
    }

}
