#include "ViewerPoseDump.h"

#include "obj/Dir.h"
#include "obj/ObjPtr_p.h"
#include "rndobj/Trans.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

std::vector<std::string> ParseCommaSeparatedList(const char* csv) {
    std::vector<std::string> out;
    if (!csv || !csv[0]) return out;
    const char* cur = csv;
    while (*cur) {
        while (*cur == ' ' || *cur == '\t' || *cur == ',') cur++;
        if (!*cur) break;
        const char* start = cur;
        while (*cur && *cur != ',') cur++;
        const char* end = cur;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
        if (end > start) out.emplace_back(start, (size_t)(end - start));
        if (*cur == ',') cur++;
    }
    return out;
}

static bool PoseDumpBoneSelected(const std::vector<std::string>& selected, const char* boneName) {
    if (selected.empty()) return true;
    for (const auto& b : selected) {
        if (b == boneName) return true;
    }
    return false;
}

static void WriteJsonEscaped(FILE* f, const char* s) {
    fputc('"', f);
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        unsigned char c = *p;
        if (c == '"' || c == '\\') {
            fputc('\\', f);
            fputc(c, f);
        } else if (c == '\n') {
            fputs("\\n", f);
        } else if (c == '\r') {
            fputs("\\r", f);
        } else if (c == '\t') {
            fputs("\\t", f);
        } else if (c < 0x20) {
            fprintf(f, "\\u%04x", (unsigned)c);
        } else {
            fputc(c, f);
        }
    }
    fputc('"', f);
}

bool WritePoseDumpJson(const char* path,
                       ObjectDir* dir,
                       const std::vector<std::string>& selectedBones,
                       const char* sourceMilo,
                       const char* clipName,
                       float beat) {
    if (!path || !dir) return false;

    std::vector<RndTransformable*> bones;
    ObjDirItr<RndTransformable> it(dir, true);
    for (; it; ++it) {
        if (PoseDumpBoneSelected(selectedBones, it->Name())) {
            bones.push_back(it);
        }
    }
    std::sort(bones.begin(), bones.end(), [](const RndTransformable* a, const RndTransformable* b) {
        return strcmp(a->Name(), b->Name()) < 0;
    });

    // Force all bones to recompute world transforms from their parent chain.
    // After rendering warmup, some non-animated bones (hair, jiggle, breast)
    // may have stale/garbage WorldXfm values cached from before PoseMeshes ran.
    // Re-dirtying and resolving ensures the dump reflects the current pose.
    for (RndTransformable* b : bones) {
        b->SetLocalXfm(b->LocalXfm());
    }

    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Error: cannot write pose dump '%s'\n", path);
        return false;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"source_milo\": ");
    WriteJsonEscaped(f, sourceMilo ? sourceMilo : "");
    fprintf(f, ",\n  \"clip\": ");
    WriteJsonEscaped(f, clipName ? clipName : "");
    fprintf(f, ",\n  \"beat\": %.7f,\n", beat);
    fprintf(f, "  \"bone_count\": %zu,\n", bones.size());
    fprintf(f, "  \"bones\": [\n");

    for (size_t i = 0; i < bones.size(); i++) {
        RndTransformable* b = bones[i];
        const Transform& l = b->LocalXfm();
        const Transform& w = b->WorldXfm();
        fprintf(f, "    {\n");
        fprintf(f, "      \"name\": ");
        WriteJsonEscaped(f, b->Name());
        fprintf(f, ",\n");
        fprintf(f, "      \"local\": {\n");
        fprintf(f, "        \"pos\": [%.9g, %.9g, %.9g],\n", l.v.x, l.v.y, l.v.z);
        fprintf(f, "        \"m\": [[%.9g, %.9g, %.9g], [%.9g, %.9g, %.9g], [%.9g, %.9g, %.9g]]\n",
                l.m.x.x, l.m.x.y, l.m.x.z,
                l.m.y.x, l.m.y.y, l.m.y.z,
                l.m.z.x, l.m.z.y, l.m.z.z);
        fprintf(f, "      },\n");
        fprintf(f, "      \"world\": {\n");
        fprintf(f, "        \"pos\": [%.9g, %.9g, %.9g],\n", w.v.x, w.v.y, w.v.z);
        fprintf(f, "        \"m\": [[%.9g, %.9g, %.9g], [%.9g, %.9g, %.9g], [%.9g, %.9g, %.9g]]\n",
                w.m.x.x, w.m.x.y, w.m.x.z,
                w.m.y.x, w.m.y.y, w.m.y.z,
                w.m.z.x, w.m.z.y, w.m.z.z);
        fprintf(f, "      }\n");
        fprintf(f, "    }%s\n", (i + 1 < bones.size()) ? "," : "");
    }

    fprintf(f, "  ]\n}\n");
    fclose(f);
    return true;
}
