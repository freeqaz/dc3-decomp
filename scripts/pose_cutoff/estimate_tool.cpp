// estimate_tool -- run Dc3PoseEstimate offline over a dc3-pose-fixture v1 file
// (np-cutoff, PLAN H-91).  Build: scripts/pose_cutoff/build_estimate_tool.py OUT
// estimate_tool MODE GAIN IN OUT   (MODE off|tracker|kinematic)
#include "platform/PoseEstimate_Native.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
using namespace Dc3PoseEstimate;
static JointConfidence Q(float c) { return c < 0.3f ? kConfidenceNotTracked : (c < 0.6f ? kConfidenceInferred : kConfidenceTracked); }
int main(int argc, char **argv) {
    if (argc != 5) return 2;
    Estimator e; e.SetMode(ParseMode(argv[1])); e.SetGain((float)atof(argv[2])); e.Reset();
    std::ifstream in(argv[3]); FILE *out = fopen(argv[4], "w");
    std::string line; double lastT = -1e9; int n = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') { fprintf(out, "%s\n", line.c_str()); continue; }
        std::istringstream ss(line); double t; int tr; ss >> t >> tr;
        if (tr != 1) { fprintf(out, "%s\n", line.c_str()); continue; }
        Person p; p.valid = true; p.trackId = 1;
        for (int j = 0; j < kNumJoints; j++) { float x, y, z, c; ss >> x >> y >> z >> c; p.joints[j].Set(x, y, z); p.confidence[j] = Q(c); }
        float dt = lastT < -1e8 ? 1.0f / 30.0f : (float)(t - lastT); lastT = t;
        e.Apply(0, p, dt, true); n++;
        fprintf(out, "%.4f 1", t);
        for (int j = 0; j < kNumJoints; j++) fprintf(out, " %.5f %.5f %.5f %.3f", p.joints[j].x, p.joints[j].y, p.joints[j].z, p.confidence[j] == kConfidenceTracked ? 1.0f : (p.confidence[j] == kConfidenceInferred ? 0.45f : 0.0f));
        fprintf(out, "\n");
    }
    fclose(out); fprintf(stderr, "%s: %d frames, %u joints estimated\n", argv[4], n, e.Estimated());
    return 0;
}
