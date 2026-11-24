#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <filesystem>
#include <fstream>
#include <cstdio>

namespace fs = std::filesystem;
namespace {
int failures = 0;
void check(bool result, const char* label) {
    std::printf("%s: %s\n", result ? "PASS" : "FAIL", label);
    if (!result) ++failures;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    const auto library = LoadLibraryW(argv[1]);
    if (!library) return 2;
    const auto factoryFunction = reinterpret_cast<HRESULT(WINAPI*)(REFCLSID, REFIID, void**)>(
        GetProcAddress(library, "DllGetClassObject"));
    const auto unload = reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(library, "DllCanUnloadNow"));
    const GUID modern{0x9d0f49ad, 0x69c1, 0x43c3, {0xa7, 0x12, 0xf3, 0xcb, 0x64, 0xf6, 0x92, 0x10}};
    IClassFactory* factory = nullptr;
    IExplorerCommand* root = nullptr;
    check(factoryFunction && SUCCEEDED(factoryFunction(modern, IID_PPV_ARGS(&factory))),
          "modern class factory loads without registry changes");
    if (!factory) return 2;
    check(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&root))), "modern command supports IExplorerCommand");
    factory->Release();
    if (!root) return 2;
    check(unload() == S_FALSE, "modern objects prevent premature DLL unload");
    EXPCMDFLAGS flags{};
    root->GetFlags(&flags);
    check(flags == ECF_HASSUBCOMMANDS, "root exposes subcommands");
    IEnumExplorerCommand* commands = nullptr;
    root->EnumSubCommands(&commands);
    IExplorerCommand* children[5]{};
    ULONG fetched = 0;
    check(commands && commands->Next(5, children, &fetched) == S_OK && fetched == 5,
          "enumerator returns all five extraction and creation commands");
    check(commands->Reset() == S_OK && commands->Skip(4) == S_OK, "enumerator supports reset and skip");
    IEnumExplorerCommand* cloned = nullptr;
    commands->Clone(&cloned);
    IExplorerCommand* last = nullptr;
    check(cloned && cloned->Next(1, &last, nullptr) == S_OK, "enumerator clone preserves its cursor");
    if (last) last->Release();
    if (cloned) cloned->Release();
    const auto directory = fs::temp_directory_path() /
        (L"ggzip-modern-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    if (!fs::create_directory(directory)) return 2;
    const auto archive = directory / L"中文 archive.zip";
    const auto file = directory / "plain.txt";
    const std::vector<fs::path> archives{archive, directory / "archive.part2.rar", directory / "archive.r00",
        directory / "archive.s00", directory / "archive.z01", directory / "archive.z100", directory / "archive.7z.002"};
    for (const auto& path : archives) std::ofstream(path).put('X');
    std::ofstream(file).put('X');
    auto selections = archives;
    selections.push_back(file);
    selections.push_back(directory);
    for (const auto& path : selections) {
        IShellItem* item = nullptr;
        IShellItemArray* selection = nullptr;
        check(SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item))),
              "native shell item fixture created");
        if (!item) continue;
        SHCreateShellItemArrayFromShellItem(item, IID_PPV_ARGS(&selection));
        item->Release();
        if (!selection) continue;
        for (unsigned index = 0; index < 5; ++index) {
            EXPCMDSTATE state{};
            children[index]->GetState(selection, TRUE, &state);
            const bool extract = index < 3;
            check(state == ((path != file && path != directory) == extract ? ECS_ENABLED : ECS_HIDDEN),
                  "subcommand visibility matches archive/file/directory selection");
        }
        selection->Release();
    }
    for (auto* child : children) if (child) child->Release();
    if (commands) commands->Release();
    root->Release();
    check(unload() == S_OK, "all modern COM objects release cleanly");
    fs::remove(file);
    for (const auto& path : archives) fs::remove(path);
    fs::remove(directory);
    FreeLibrary(library);
    CoUninitialize();
    return failures ? 1 : 0;
}
