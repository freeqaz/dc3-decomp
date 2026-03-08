#include "math/Easing.h"
#include "os/Debug.h"
#include "utl/Licenses.h"

Licenses sLicense("system/src/math/Easing.h", Licenses::kRequirementNotification);

float EaseBounceOut(float t, float, float) {
    MILO_ASSERT(t >= 0 && t <= 1, 0x13);
    if (t < 0.36363637f) {
        return t * t * 7.5625f;
    } else if (t < 0.72727275f) {
        float diff = t - 0.54545456f;
        return diff * diff * 7.5625f + 0.75f;
    } else if (t < 0.9090909090909091) {
        float diff = t - 0.8181818f;
        return diff * diff * 7.5625f + 0.9375f;
    } else {
        float diff = t - 0.95454544f;
        return diff * diff * 7.5625f + 0.984375f;
    }
}

// Elastic easing in: overshoots target with exponentially decaying oscillation
// Physics model: damped harmonic oscillator with phase shift
float EaseElasticIn(float t, float power, float period) {
    MILO_ASSERT(t >= 0 && t <= 1, 0x91);
    if (t > 0 && t < 1.0f) {
        // Default period for oscillation
        if (period <= 0)
            period = 0.45f;

        // Calculate phase shift (s) based on oscillation amplitude (power)
        float s;
        if (power < 1.0f) {
            power = 1.0f;
            s = period * 0.25f;  // Simple phase shift for unit amplitude
        } else {
            // Phase shift derived from amplitude: asin(1/A) * T / (2π)
            // 0.15915494f ≈ 1/(2π)
            s = asin(1.0f / power) * period * 0.15915494f;
        }

        t = t - 1.0f;
        // Exponential growth: 2^(10t) as t goes from -1 to 0
        float amplitude = pow(2.0, t * 10.0f);
        // Sine wave with phase shift, scaled by exponential amplitude and power
        // 6.283185f ≈ 2π for full wave period
        return -(FastSin((t - s) / period * 6.283185f) * amplitude * power);
    }
    return t;  // Boundary cases: t=0 returns 0, t=1 returns 1
}
