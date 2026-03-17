#include "telemetry_parser.h"
#include <sstream>
#include <cstdlib>
#include <map>

float TelemetrySample::getFloat(const std::string &key, float def) const {
    auto it = fields.find(key);
    if (it == fields.end()) return def;
    return std::strtof(it->second.c_str(), nullptr);
}

int TelemetrySample::getInt(const std::string &key, int def) const {
    auto it = fields.find(key);
    if (it == fields.end()) return def;
    return std::atoi(it->second.c_str());
}

bool TelemetrySample::getBool(const std::string &key, bool def) const {
    auto it = fields.find(key);
    if (it == fields.end()) return def;
    return it->second == "1" || it->second == "true";
}

std::string TelemetrySample::getString(const std::string &key, const std::string &def) const {
    auto it = fields.find(key);
    if (it == fields.end()) return def;
    return it->second;
}

std::vector<TelemetrySample> ParseTelemetry(const std::string &output) {
    // Accumulate by frame number (preserves order of first appearance)
    std::map<int, std::unordered_map<std::string, std::string>> byFrame;

    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        // Find DC3_TEL: prefix (may have [XDK] prefix from web logs)
        size_t pos = line.find("DC3_TEL:");
        if (pos == std::string::npos) continue;

        std::string payload = line.substr(pos + 8); // skip "DC3_TEL:"
        std::istringstream pairs(payload);
        std::string token;

        int frame = -1;
        std::unordered_map<std::string, std::string> fields;

        while (pairs >> token) {
            size_t eq = token.find('=');
            if (eq == std::string::npos) continue;
            std::string key = token.substr(0, eq);
            std::string val = token.substr(eq + 1);
            if (key == "frame") {
                frame = std::atoi(val.c_str());
            }
            fields[key] = val;
        }

        if (frame >= 0) {
            auto &merged = byFrame[frame];
            for (auto &kv : fields) {
                merged[kv.first] = kv.second;
            }
        }
    }

    std::vector<TelemetrySample> result;
    result.reserve(byFrame.size());
    for (auto &kv : byFrame) {
        TelemetrySample s;
        s.frame = kv.first;
        s.fields = std::move(kv.second);
        result.push_back(std::move(s));
    }
    return result;
}
