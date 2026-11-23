#include "ggzip/FileAssociations.h"
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

int failed = 0;
int checked = 0;

void check(bool condition, const char* message) {
    ++checked;
    if (!condition) ++failed;
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", message);
}

void requireSuccess(LSTATUS status) {
    if (status != ERROR_SUCCESS)
        throw std::runtime_error("Registry test setup failed: " + std::to_string(status));
}

struct RegistrySandbox {
    HKEY handle = nullptr;
    std::wstring path = L"Software\\ggZip\\Tests\\Associations-" +
                        std::to_wstring(GetCurrentProcessId()) + L"-" +
                        std::to_wstring(GetTickCount64());

    RegistrySandbox() {
        DWORD disposition = 0;
        requireSuccess(RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0,
                                      KEY_ALL_ACCESS, nullptr, &handle, &disposition));
        if (disposition != REG_CREATED_NEW_KEY) {
            RegCloseKey(handle);
            throw std::runtime_error("Registry sandbox already exists");
        }
        const LSTATUS status = RegOverridePredefKey(HKEY_CURRENT_USER, handle);
        if (status != ERROR_SUCCESS) {
            RegCloseKey(handle);
            RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
            requireSuccess(status);
        }
    }

    ~RegistrySandbox() {
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        RegCloseKey(handle);
        RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    }
};

struct TemporaryFiles {
    fs::path directory = fs::temp_directory_path() /
                         (L"ggzip-associations-" + std::to_wstring(GetCurrentProcessId()) +
                          L"-" + std::to_wstring(GetTickCount64()));
    TemporaryFiles() {
        if (!fs::create_directory(directory))
            throw std::runtime_error("Temporary directory already exists");
    }
    ~TemporaryFiles() {
        std::error_code error;
        fs::remove_all(directory, error);
    }
    std::wstring executable(const std::wstring& name) const {
        const fs::path folder = directory / name;
        fs::create_directory(folder);
        const fs::path path = folder / L"ggzip_gui.exe";
        std::ofstream(path, std::ios::binary) << "test";
        return path.wstring();
    }
};

void writeString(const wchar_t* path, const wchar_t* name, const std::wstring& value) {
    HKEY handle = nullptr;
    requireSuccess(RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0,
                                  KEY_SET_VALUE, nullptr, &handle, nullptr));
    const LSTATUS status = RegSetValueExW(handle, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(handle);
    requireSuccess(status);
}

std::wstring readString(const wchar_t* path, const wchar_t* name = nullptr) {
    wchar_t value[4096]{};
    DWORD bytes = sizeof(value);
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, path, name, RRF_RT_REG_SZ,
                                       nullptr, value, &bytes);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) return {};
    requireSuccess(status);
    return value;
}

bool valueExists(const wchar_t* path, const wchar_t* name) {
    return RegGetValueW(HKEY_CURRENT_USER, path, name, RRF_RT_ANY,
                        nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

}

int wmain() {
    try {
        RegistrySandbox sandbox;
        TemporaryFiles files;
        const std::wstring first = files.executable(L"中文目录 with spaces");
        const std::wstring second = files.executable(L"Another installation");
        constexpr wchar_t openWith[] = L"Software\\Classes\\.zip\\OpenWithProgids";
        constexpr wchar_t commandKey[] = L"Software\\Classes\\ggZip.Archive\\shell\\open\\command";
        constexpr wchar_t userChoice[] =
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.zip\\UserChoice";
        writeString(L"Software\\Classes\\.zip", nullptr, L"Other.Zip");
        writeString(openWith, L"Other.Zip", L"");
        writeString(userChoice, L"ProgId", L"Other.Zip");
        writeString(userChoice, L"Hash", L"unchanged");
        writeString(L"Software\\RegisteredApplications", L"Other", L"Software\\Other\\Capabilities");
        writeString(L"Software\\ggZip", L"GgzPath", L"preserve-cli-path");

        for (const std::wstring& invalid : { std::wstring(), std::wstring(L"relative.exe"),
                                            first + L"\"invalid", first + L".missing" }) {
            bool rejected = false;
            try { ggzip::registerFileAssociations(invalid, false); }
            catch (const std::exception&) { rejected = true; }
            check(rejected, "invalid executable rejected before registration");
        }
        check(readString(commandKey).empty(), "invalid input leaves registration untouched");

        ggzip::registerFileAssociations(first, false);
        check(readString(commandKey) == L"\"" + first + L"\" \"%1\"",
              "Unicode executable and archive arguments are quoted");
        check(readString(L"Software\\Classes\\ggZip.Archive\\DefaultIcon") ==
              L"\"" + first + L"\",0", "archive icon points to GUI resource");
        check(valueExists(openWith, L"ggZip.Archive"), "ZIP open-with candidate registered");
        check(valueExists(L"Software\\Classes\\.7z\\OpenWithProgids", L"ggZip.Archive"),
              "7z open-with candidate registered");
        check(!valueExists(L"Software\\Classes\\.exe\\OpenWithProgids", L"ggZip.Archive"),
              "executable file types excluded");
        check(readString(L"Software\\ggZip\\Capabilities\\FileAssociations", L".rar") ==
              L"ggZip.Archive", "supported types advertised to Windows");
        check(readString(L"Software\\RegisteredApplications", L"ggZip") ==
              L"Software\\ggZip\\Capabilities", "per-user registered application");
        check(readString(L"Software\\Classes\\.zip") == L"Other.Zip",
              "existing extension default is preserved");
        check(readString(userChoice, L"ProgId") == L"Other.Zip" &&
              readString(userChoice, L"Hash") == L"unchanged", "Windows UserChoice is untouched");

        ggzip::registerFileAssociations(first, false);
        check(readString(commandKey) == L"\"" + first + L"\" \"%1\"",
              "registration is idempotent");
        ggzip::unregisterFileAssociations(second, false);
        check(valueExists(openWith, L"ggZip.Archive"), "other installation cannot remove associations");
        ggzip::registerFileAssociations(second, false);
        ggzip::unregisterFileAssociations(first, false);
        check(readString(commandKey) == L"\"" + second + L"\" \"%1\"",
              "old installation uninstall preserves newer registration");
        ggzip::unregisterFileAssociations(second, false);
        check(!valueExists(openWith, L"ggZip.Archive"), "owned open-with value removed");
        check(readString(commandKey).empty(), "owned ProgID removed");
        check(!valueExists(L"Software\\RegisteredApplications", L"ggZip"),
              "registered application removed");
        check(readString(L"Software\\Classes\\Applications\\ggzip_gui.exe\\shell\\open\\command").empty(),
              "owned application command removed");
        check(!valueExists(L"Software\\ggZip\\Capabilities\\FileAssociations", L".rar"),
              "owned capabilities removed");
        check(valueExists(openWith, L"Other.Zip"), "other open-with candidates survive uninstall");
        check(readString(L"Software\\Classes\\.zip") == L"Other.Zip" &&
              readString(userChoice, L"Hash") == L"unchanged", "defaults survive uninstall");
        check(readString(L"Software\\ggZip", L"GgzPath") == L"preserve-cli-path",
              "unrelated ggZip settings survive uninstall");
        check(valueExists(L"Software\\RegisteredApplications", L"Other"),
              "other registered applications survive uninstall");
        ggzip::unregisterFileAssociations(second, false);
        check(readString(commandKey).empty(), "unregistration is idempotent");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    std::printf("%d checks, %d failures\n", checked, failed);
    return failed ? 1 : 0;
}
