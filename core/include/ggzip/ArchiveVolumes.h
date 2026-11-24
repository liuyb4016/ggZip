#pragma once
#include <filesystem>
#include <vector>
#include <string>

namespace ggzip {

struct ArchiveVolumeSet {
    std::filesystem::path first;
    std::vector<std::filesystem::path> parts;
    std::wstring baseName;
    bool split = false;
};

ArchiveVolumeSet archiveVolumes(const std::filesystem::path& path);

}
