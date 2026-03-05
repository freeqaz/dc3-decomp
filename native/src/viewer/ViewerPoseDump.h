#pragma once

#include <string>
#include <vector>

class ObjectDir;

std::vector<std::string> ParseCommaSeparatedList(const char* csv);

bool WritePoseDumpJson(const char* path,
                       ObjectDir* dir,
                       const std::vector<std::string>& selectedBones,
                       const char* sourceMilo,
                       const char* clipName,
                       float beat);
