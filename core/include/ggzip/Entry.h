#pragma once
// 压缩包内条目与元信息。
#include <cstdint>
#include <string>
#include "Format.h"

namespace ggzip {

struct Entry {
    std::wstring path;        // 包内相对路径，'/' 分隔；目录以 '/' 结尾
    std::uint64_t size = 0;
    std::uint64_t packedSize = 0;
    bool isDir = false;
    bool encrypted = false;
    std::uint32_t crc32 = 0;
    std::string modified;     // 原始时间字符串，如 "2024-01-01 10:00:00"
    std::string method;
};

struct ArchiveInfo {
    Format format = Format::Unknown;
    std::string typeName;     // 7z 报告的类型名
    std::uint64_t physicalSize = 0;
};

} // namespace ggzip
