#pragma once

namespace GameplayTelemetry {
    void Init();            // Check DC3_TEL env var
    void Sample(int frame); // Emit key=value lines to stderr for this frame
    bool IsEnabled();
}
