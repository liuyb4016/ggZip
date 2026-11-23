#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace previewtest {

struct Directory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        (L"ggzip-preview-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));

    Directory() {
        if (!std::filesystem::create_directory(path))
            throw std::runtime_error("Test directory already exists");
    }
    ~Directory() {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    }
};

struct ZipEntry {
    std::string name;
    std::string data;
    std::uint32_t attributes = 0x81a40000;
    bool corrupt = false;
    std::string comment;
};

inline void append16(std::string& data, std::uint16_t value) {
    data.push_back(static_cast<char>(value & 255));
    data.push_back(static_cast<char>(value >> 8));
}

inline void append32(std::string& data, std::uint32_t value) {
    append16(data, static_cast<std::uint16_t>(value & 65535));
    append16(data, static_cast<std::uint16_t>(value >> 16));
}

inline std::uint32_t crc32(const std::string& data) {
    std::uint32_t checksum = 0xffffffff;
    for (const unsigned char byte : data) {
        checksum ^= byte;
        for (int bitIndex = 0; bitIndex < 8; ++bitIndex)
            checksum = (checksum >> 1) ^ ((checksum & 1) ? 0xedb88320 : 0);
    }
    return ~checksum;
}

inline void write(const std::filesystem::path& path, const std::string& data) {
    std::ofstream file(path, std::ios::binary);
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!file) throw std::runtime_error("Unable to write test fixture");
}

inline void zip(const std::filesystem::path& path, const std::vector<ZipEntry>& entries) {
    std::string local;
    std::string central;
    for (const auto& entry : entries) {
        const std::uint32_t offset = static_cast<std::uint32_t>(local.size());
        const std::uint32_t checksum = crc32(entry.data) ^ (entry.corrupt ? 0xffffffff : 0);
        const auto size = static_cast<std::uint32_t>(entry.data.size());
        const auto nameSize = static_cast<std::uint16_t>(entry.name.size());
        append32(local, 0x04034b50);
        for (const int value : { 20, 2048, 0, 0, 33 }) append16(local, static_cast<std::uint16_t>(value));
        append32(local, checksum);
        append32(local, size);
        append32(local, size);
        append16(local, nameSize);
        append16(local, 0);
        local += entry.name;
        local += entry.data;

        append32(central, 0x02014b50);
        for (const int value : { 788, 20, 2048, 0, 0, 33 }) append16(central, static_cast<std::uint16_t>(value));
        append32(central, checksum);
        append32(central, size);
        append32(central, size);
        append16(central, nameSize);
        append16(central, 0);
        append16(central, static_cast<std::uint16_t>(entry.comment.size()));
        append16(central, 0);
        append16(central, 0);
        append32(central, entry.attributes);
        append32(central, offset);
        central += entry.name;
        central += entry.comment;
    }
    const auto centralOffset = static_cast<std::uint32_t>(local.size());
    local += central;
    append32(local, 0x06054b50);
    append16(local, 0);
    append16(local, 0);
    append16(local, static_cast<std::uint16_t>(entries.size()));
    append16(local, static_cast<std::uint16_t>(entries.size()));
    append32(local, static_cast<std::uint32_t>(central.size()));
    append32(local, centralOffset);
    append16(local, 0);
    write(path, local);
}

inline void tarLinks(const std::filesystem::path& path) {
    std::string archive;
    for (const auto& item : std::vector<std::pair<std::string, char>>{
            {"symbolic.txt", '2'}, {"hard.txt", '1'} }) {
        std::string header(512, '\0');
        header.replace(0, item.first.size(), item.first);
        header.replace(100, 7, "0000644");
        header.replace(108, 7, "0000000");
        header.replace(116, 7, "0000000");
        header.replace(124, 11, "00000000000");
        header.replace(136, 11, "00000000000");
        header.replace(148, 8, "        ");
        header[156] = item.second;
        const std::string target = "../../outside.txt";
        header.replace(157, target.size(), target);
        header.replace(257, 5, "ustar");
        header.replace(263, 2, "00");
        unsigned int checksum = 0;
        for (const unsigned char byte : header) checksum += byte;
        char encoded[8]{};
        std::snprintf(encoded, sizeof(encoded), "%06o", checksum);
        header.replace(148, 6, encoded, 6);
        header[154] = '\0';
        header[155] = ' ';
        archive += header;
    }
    archive.append(1024, '\0');
    write(path, archive);
}

}
