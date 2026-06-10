// StubTrace implementation (roadmap N.2). See StubTrace.h.

#include "StubTrace.h"

#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <string>

namespace dc3 {

namespace {
// Read DC3_STUB_TRACE once at load time so HX_STUB_TRACE works from the very
// first stub hit (including during static init) with no explicit init call.
bool ReadStubTraceEnv() {
    const char* env = std::getenv("DC3_STUB_TRACE");
    return env != nullptr && env[0] != '\0' && env[0] != '0';
}
}  // namespace

bool gStubTraceEnabled = ReadStubTraceEnv();

namespace {

// Guards the table. Stub hits are rare relative to real work, and tracing is an
// opt-in debugging mode, so a plain mutex is more than fast enough and keeps the
// table iteration (ToJson) trivially correct. Keyed by the stub's literal name
// pointer's string value (string_view keys would dangle; we copy the name).
std::mutex& TableMutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<std::string, uint64_t>& Table() {
    static std::unordered_map<std::string, uint64_t> t;
    return t;
}

// JSON-escape the limited set that can appear in a C++ symbol name. Symbol names
// here are mangled identifiers, so only '"' and '\\' realistically matter, but
// we escape control chars defensively.
void AppendEscaped(std::string& out, const char* s) {
    for (const char* p = s; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
}

}  // namespace

bool StubTraceInit() {
    // gStubTraceEnabled is already set from the environment at load time
    // (ReadStubTraceEnv). Re-read so a test or a late setenv() still takes effect.
    gStubTraceEnabled = ReadStubTraceEnv();
    return gStubTraceEnabled;
}

void StubTraceHit(const char* name) {
    // gStubTraceEnabled is checked by the macro before we get here; recheck so a
    // direct call is also safe.
    if (!gStubTraceEnabled || name == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(TableMutex());
    ++Table()[name];
}

std::string StubTraceDump::ToJson(uint64_t* total, uint64_t* distinct) {
    std::vector<std::pair<std::string, uint64_t>> rows;
    uint64_t sum = 0;
    {
        std::lock_guard<std::mutex> lock(TableMutex());
        rows.reserve(Table().size());
        for (const auto& kv : Table()) {
            rows.emplace_back(kv.first, kv.second);
            sum += kv.second;
        }
    }
    std::sort(rows.begin(), rows.end(),
              [](const std::pair<std::string, uint64_t>& a,
                 const std::pair<std::string, uint64_t>& b) {
                  if (a.second != b.second) return a.second > b.second;
                  return a.first < b.first;  // stable, name-ordered tiebreak
              });

    std::string out = "[";
    bool first = true;
    for (const auto& r : rows) {
        if (!first) out += ",";
        first = false;
        out += "{\"name\":\"";
        AppendEscaped(out, r.first.c_str());
        out += "\",\"count\":";
        char buf[32];
        snprintf(buf, sizeof(buf), "%llu", (unsigned long long)r.second);
        out += buf;
        out += "}";
    }
    out += "]";

    if (total) *total = sum;
    if (distinct) *distinct = (uint64_t)rows.size();
    return out;
}

}  // namespace dc3
