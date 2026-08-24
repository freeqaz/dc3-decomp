#include "math/Easing.h"
#include "os\Debug.h"
#include "utl\Licenses.h"

// static like the original (each TU owns its own Licenses object): non-static
// duplicates of ?sLicense@@3VLicenses@@A merge under /FORCE:MULTIPLE and the
// shared object gets constructed once per TU.
static Licenses sLicense("system/src/math/Easing.h", Licenses::kRequirementNotification);

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
