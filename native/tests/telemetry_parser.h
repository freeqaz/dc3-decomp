#pragma once
#include <string>
#include <vector>
#include <unordered_map>

struct TelemetrySample {
    int frame;
    std::unordered_map<std::string, std::string> fields;

    float getFloat(const std::string &key, float def = 0.0f) const;
    int getInt(const std::string &key, int def = 0) const;
    bool getBool(const std::string &key, bool def = false) const;
    std::string getString(const std::string &key, const std::string &def = "") const;
};

// Parse DC3_TEL: lines from output, merge fields by frame number
std::vector<TelemetrySample> ParseTelemetry(const std::string &output);
