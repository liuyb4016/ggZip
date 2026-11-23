#include "ggzip/SourceMark.h"
#include "ggzip/ArchiveError.h"
#include <windows.h>
#include <algorithm>
#include <sstream>

namespace ggzip {
namespace {

struct Handle {
    HANDLE value;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

}

unsigned sourceZone(const std::wstring& path) {
    const Handle input{CreateFileW((path + L":Zone.Identifier").c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (input.value == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_INVALID_NAME)
            return 0;
        throw ArchiveError("无法读取下载来源标记");
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(input.value, &size) || size.QuadPart < 0 || size.QuadPart > 4096)
        throw ArchiveError("下载来源标记不明确");
    std::string content(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!ReadFile(input.value, content.data(), static_cast<DWORD>(content.size()), &read, nullptr) ||
        read != content.size()) throw ArchiveError("无法读取下载来源标记");
    std::istringstream lines(content);
    std::string line;
    bool transfer = false;
    unsigned zone = 0;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line.front() == '[') transfer = line == "[ZoneTransfer]";
        if (transfer && line.size() == 8 && line.rfind("ZoneId=", 0) == 0 &&
            line.back() >= '0' && line.back() <= '4')
            zone = std::max(zone, static_cast<unsigned>(line.back() - '0'));
    }
    return zone;
}

void applySourceZone(const std::wstring& path, unsigned zone) {
    if (!zone) return;
    if (zone > 4) throw ArchiveError("无效的下载来源区域");
    const Handle output{CreateFileW((path + L":Zone.Identifier").c_str(), GENERIC_WRITE,
        0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (output.value == INVALID_HANDLE_VALUE) throw ArchiveError("无法保留下载来源标记，文件未发布");
    const std::string content = "[ZoneTransfer]\r\nZoneId=" + std::to_string(zone) + "\r\n";
    DWORD written = 0;
    if (!WriteFile(output.value, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) ||
        written != content.size() || !FlushFileBuffers(output.value))
        throw ArchiveError("无法保存下载来源标记，文件未发布");
}

}
