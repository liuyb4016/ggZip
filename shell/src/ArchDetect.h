// ggZip Shell 扩展 —— 压缩包扩展名判断（自包含，不依赖 core）
#pragma once

#include <windows.h>
#include <shlwapi.h>
#include <cwctype>
#include <string>

namespace ggshell {

inline std::wstring extOf(const std::wstring& path) {
    const wchar_t* file = PathFindFileNameW(path.c_str());
    const wchar_t* dot = wcsrchr(file, L'.');
    if (dot == nullptr || dot == file) return L"";   // 无扩展名 / .开头的隐藏文件
    std::wstring e(dot + 1);
    for (auto& c : e) c = static_cast<wchar_t>(towlower(c));
    return e;
}

// 7z 引擎能解压的常见格式（菜单据此决定是否显示解压组）
inline bool isArchive(const std::wstring& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    static const wchar_t* kExts[] = {
        L"zip", L"7z", L"rar", L"tar", L"gz", L"bz2", L"xz", L"zst",
        L"tgz", L"tbz2", L"tbz", L"txz", L"tzst",
        L"cab", L"iso", L"wim", L"swm", L"msi", L"dmg", L"arj", L"lzh",
        L"lz4", L"apk", L"jar", L"ipa", L"vhd", L"vhdx", L"cpio",
        L"rpm", L"deb", L"xar", L"hfs", L"fat", L"ntfs", L"squashfs",
    };
    const std::wstring e = extOf(path);
    if (e.empty()) return false;
    if (e.size() == 3 && e[0] >= L'r' && e[0] <= L'z' &&
        e[1] >= L'0' && e[1] <= L'9' && e[2] >= L'0' && e[2] <= L'9') return true;
    if (e.size() > 3 && e[0] == L'z') {
        bool numbered = true;
        for (std::size_t index = 1; index < e.size(); ++index)
            if (e[index] < L'0' || e[index] > L'9') numbered = false;
        if (numbered) return true;
    }
    if (e.size() >= 3) {
        bool numbered = true;
        for (const auto character : e) if (character < L'0' || character > L'9') numbered = false;
        if (numbered) return true;
    }
    for (const wchar_t* k : kExts)
        if (e == k) return true;
    return false;
}

// 取去扩展名的文件名（stem）
inline std::wstring stemOf(const std::wstring& path) {
    const wchar_t* file = PathFindFileNameW(path.c_str());
    const wchar_t* dot = wcsrchr(file, L'.');
    const size_t len = (dot == nullptr || dot == file) ? wcslen(file)
                                                       : static_cast<size_t>(dot - file);
    return std::wstring(file, len);
}

} // namespace ggshell
