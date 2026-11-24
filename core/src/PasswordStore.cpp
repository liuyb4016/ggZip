#include "ggzip/PasswordStore.h"
#include "ggzip/ArchiveError.h"
#include "ggzip/Diagnostics.h"
#include "ggzip/Text.h"
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace ggzip {
namespace {

constexpr wchar_t kDirName[] = L"ggZip";
constexpr wchar_t kFileName[] = L"passwords.dat";

void putU32(std::string& s, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        s.push_back(char((v >> (8 * i)) & 0xFF));
}

std::uint32_t getU32(const std::string& s, size_t& pos) {
    if (pos + 4 > s.size()) throw ArchiveError(diagnostic("密码库文件格式损坏"));
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= (std::uint32_t)(unsigned char)s[pos + i] << (8 * i);
    pos += 4;
    return v;
}

std::string getStr(const std::string& s, size_t& pos) {
    std::uint32_t n = getU32(s, pos);
    if (pos + n > s.size()) throw ArchiveError(diagnostic("密码库文件格式损坏"));
    std::string out = s.substr(pos, n);
    pos += n;
    return out;
}

std::string serialize(const std::vector<PasswordEntry>& es) {
    std::string out;
    putU32(out, 1);            // 版本
    putU32(out, (std::uint32_t)es.size());
    for (const auto& e : es) {
        std::string lu = utf8FromWide(e.label);
        std::string pu = utf8FromWide(e.password);
        putU32(out, (std::uint32_t)lu.size());
        out += lu;
        putU32(out, (std::uint32_t)pu.size());
        out += pu;
    }
    return out;
}

std::vector<PasswordEntry> deserialize(const std::string& s) {
    std::vector<PasswordEntry> es;
    size_t pos = 0;
    getU32(s, pos);                       // 版本（暂不校验，向前兼容）
    std::uint32_t count = getU32(s, pos);
    es.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        PasswordEntry e;
        e.label = wideFromUtf8(getStr(s, pos));
        e.password = wideFromUtf8(getStr(s, pos));
        es.push_back(std::move(e));
    }
    return es;
}

// DPAPI 当前用户作用域加解密
std::vector<BYTE> dpapiProtect(const std::string& plain) {
    DATA_BLOB in;
    in.pbData = (BYTE*)plain.data();
    in.cbData = (DWORD)plain.size();
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"ggZip passwords", nullptr, nullptr, nullptr, 0, &out))
        throw ArchiveError(diagnostic("DPAPI 加密失败 (Win32 错误码 ") + std::to_string(GetLastError()) + ")");
    std::vector<BYTE> v(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return v;
}

std::string dpapiUnprotect(const std::vector<BYTE>& blob) {
    DATA_BLOB in;
    in.cbData = (DWORD)blob.size();
    in.pbData = (BYTE*)blob.data();
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
        throw ArchiveError(diagnostic("密码库解密失败：可能由其他用户账户创建或文件已损坏"));
    std::string s((char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return s;
}

} // namespace

PasswordStore::PasswordStore(std::wstring baseDir) {
    if (baseDir.empty()) {
        std::wstring appdata;
        if (const wchar_t* env = _wgetenv(L"APPDATA"); env && *env) appdata = env;
        if (appdata.empty())
            throw ArchiveError(diagnostic("无法定位 APPDATA 目录，密码库不可用"));
        baseDir = appdata + L"\\" + kDirName;
    }
    dir_ = std::move(baseDir);
}

std::wstring PasswordStore::storeFilePath() const {
    return dir_ + L"\\" + kFileName;
}

std::vector<PasswordEntry> PasswordStore::load() const {
    const std::wstring path = storeFilePath();
    std::error_code ec;
    if (!fs::exists(fs::path(path), ec)) return {};

    std::ifstream f(fs::path(path), std::ios::binary);
    if (!f) return {};
    std::string blob((std::istreambuf_iterator<char>(f)), {});
    if (blob.empty()) return {};

    std::vector<BYTE> bytes(blob.begin(), blob.end());
    return deserialize(dpapiUnprotect(bytes));
}

void PasswordStore::save(const std::vector<PasswordEntry>& entries) const {
    std::error_code ec;
    fs::create_directories(fs::path(dir_), ec);
    std::vector<BYTE> blob = dpapiProtect(serialize(entries));
    std::ofstream f(fs::path(storeFilePath()), std::ios::binary | std::ios::trunc);
    if (!f) throw ArchiveError(diagnostic("无法写入密码库文件: ") + utf8(storeFilePath()));
    f.write((const char*)blob.data(), (std::streamsize)blob.size());
}

void PasswordStore::add(const std::wstring& label, const std::wstring& password) const {
    if (label.empty()) throw ArchiveError(diagnostic("密码条目标签不能为空"));
    auto es = load();
    bool replaced = false;
    for (auto& e : es) {
        if (e.label == label) { e.password = password; replaced = true; break; }
    }
    if (!replaced) es.push_back({ label, password });
    save(es);
}

bool PasswordStore::remove(const std::wstring& label) const {
    auto es = load();
    auto it = std::find_if(es.begin(), es.end(),
                           [&](const PasswordEntry& e) { return e.label == label; });
    if (it == es.end()) return false;
    es.erase(it);
    save(es);
    return true;
}

std::optional<std::wstring> PasswordStore::find(const std::wstring& label) const {
    for (const auto& e : load())
        if (e.label == label) return e.password;
    return std::nullopt;
}

} // namespace ggzip
