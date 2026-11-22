#pragma once
// 压缩格式识别与扩展名映射。
#include <string>
#include <string_view>

namespace ggzip {

enum class Format {
    Unknown, Zip, SevenZ, Tar, GZip, BZip2, XZ, Zstd, Wim, Rar, Cab, Iso
};

// 7z -slt 输出的 Type 字段 -> Format
inline Format formatFromTypeName(std::string_view t) {
    if (t == "zip") return Format::Zip;
    if (t == "7z") return Format::SevenZ;
    if (t == "tar") return Format::Tar;
    if (t == "gzip" || t == "gz") return Format::GZip;
    if (t == "bzip2" || t == "bz2") return Format::BZip2;
    if (t == "xz") return Format::XZ;
    if (t == "zstd") return Format::Zstd;
    if (t == "wim" || t == "swm") return Format::Wim;
    if (t == "rar" || t == "rar5") return Format::Rar;
    if (t == "cab") return Format::Cab;
    if (t == "iso") return Format::Iso;
    return Format::Unknown;
}

// Format -> 展示名
inline const char* formatName(Format f) {
    switch (f) {
        case Format::Zip:    return "zip";
        case Format::SevenZ: return "7z";
        case Format::Tar:    return "tar";
        case Format::GZip:   return "gzip";
        case Format::BZip2:  return "bzip2";
        case Format::XZ:     return "xz";
        case Format::Zstd:   return "zstd";
        case Format::Wim:    return "wim";
        case Format::Rar:    return "rar";
        case Format::Cab:    return "cab";
        case Format::Iso:    return "iso";
        default:             return "unknown";
    }
}

// 取小写扩展名（含点），如 ".tar.gz"
std::wstring lowerExtOf(const std::wstring& path);

// 创建压缩包时：扩展名 -> 7z 的 -t 类型值；不支持返回空串。
// .tar.gz/.tgz 等复合扩展返回空串，由引擎按 "先打 tar 再套外层" 两段式处理。
std::wstring archiveTypeFromExt(const std::wstring& path);

// 是否为 tar.* 两层复合格式（.tar.gz/.tgz/.tar.bz2/.tbz2/.tar.xz/.txz/.tar.zst/.tzst）
bool isTarWrappedExt(const std::wstring& path);

} // namespace ggzip
