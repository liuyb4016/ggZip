#include "preview_fixtures.h"
#include "ggzip/EngineFactory.h"
#include "ggzip/OperationError.h"
#include <functional>
#include <iterator>
#include <algorithm>

namespace fs = std::filesystem;
namespace {
int failures = 0;
int checks = 0;
void check(bool result, const char* label) {
    ++checks;
    std::printf("%s: %s\n", result ? "PASS" : "FAIL", label);
    if (!result) ++failures;
}
bool throws(const std::function<void()>& action) {
    try { action(); } catch (const std::exception&) { return true; }
    return false;
}
std::string bytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}
}

int wmain() {
    try {
        previewtest::Directory temporary;
        auto engine = ggzip::createEngine();
        std::atomic_bool cancel{false};
        const auto source = temporary.path / "source";
        fs::create_directories(source / L"目录");
        previewtest::write(source / L"目录/中文.txt", "UNCHANGED");
        previewtest::write(source / "remove.txt", "REMOVE");
        previewtest::write(source / "keep.txt", "KEEP");
        const auto input = temporary.path / "new.txt";
        previewtest::write(input, "ADDED");
        for (const auto& extension : {L".zip", L".7z"}) {
            const auto archive = temporary.path / (std::wstring(L"editable") + extension);
            ggzip::CreateOptions creation;
            if (std::wstring(extension) == L".7z") creation.password = L"密码";
            engine->create(archive.wstring(), {source.wstring() + L"\\*"}, creation, {}, cancel);
            ggzip::EditOptions edit;
            edit.password = creation.password;
            edit.remove = {L"remove.txt"};
            edit.rename = {{L"目录", L"新目录"}};
            edit.add = {{input.wstring(), L"nested/added.txt"}};
            check(engine->edit(archive.wstring(), edit, {}, cancel).completed == 1,
                  "ZIP/7z edit commits one verified transaction");
            ggzip::ExtractOptions extraction;
            extraction.password = edit.password;
            const auto output = temporary.path / (std::wstring(L"edited-output") + extension);
            engine->extract(archive.wstring(), output.wstring(), {}, extraction, {}, cancel);
            check(bytes(output / L"新目录/中文.txt") == "UNCHANGED" && bytes(output / "keep.txt") == "KEEP",
                  "renamed directories and untouched members preserve bytes");
            check(!fs::exists(output / "remove.txt") && !fs::exists(output / L"目录") &&
                  bytes(output / "nested/added.txt") == "ADDED", "delete and exact nested addition take effect");
            const auto original = bytes(archive);
            edit = {};
            edit.password = creation.password;
            edit.rename = {{L"keep.txt", L"nested/added.txt"}};
            check(throws([&] { engine->edit(archive.wstring(), edit, {}, cancel); }) &&
                  bytes(archive) == original, "rename collision preserves original archive");
            edit.rename.clear();
            edit.add = {{(temporary.path / "absent").wstring(), L"missing.txt"}};
            check(throws([&] { engine->edit(archive.wstring(), edit, {}, cancel); }) &&
                  bytes(archive) == original, "failed addition never modifies original");
            edit.add = {{input.wstring(), L"../escape.txt"}};
            check(throws([&] { engine->edit(archive.wstring(), edit, {}, cancel); }) &&
                  bytes(archive) == original, "unsafe member destination is rejected");
            edit.add = {{input.wstring(), L"keep.txt"}};
            check(throws([&] { engine->edit(archive.wstring(), edit, {}, cancel); }) &&
                  bytes(archive) == original, "addition defaults to conflict rejection");
            edit.conflict = ggzip::ConflictPolicy::Skip;
            check(engine->edit(archive.wstring(), edit, {}, cancel).skipped == 1, "addition skip is explicit");
            edit.conflict = ggzip::ConflictPolicy::Rename;
            check(engine->edit(archive.wstring(), edit, {}, cancel).renamed == 1, "addition can auto-rename");
            const auto beforeCancel = bytes(archive);
            cancel.store(true);
            check(engine->edit(archive.wstring(), edit, {}, cancel).outcome == ggzip::Outcome::Cancelled &&
                  bytes(archive) == beforeCancel, "cancelled edit preserves original bytes");
            cancel.store(false);
            edit.conflict = ggzip::ConflictPolicy::Overwrite;
            check(engine->edit(archive.wstring(), edit, {}, cancel).overwritten == 1, "explicit member replacement is transactional");
            edit = {};
            edit.password = creation.password;
            edit.add = {{input.wstring(), L"@literal.txt"}, {input.wstring(), L"-option.txt"}};
            check(engine->edit(archive.wstring(), edit, {}, cancel).completed == 1,
                  "addition targets cannot inject list files or command options");
            const auto folder = temporary.path / (std::wstring(L"folder") + extension);
            fs::create_directories(folder / "empty");
            previewtest::write(folder / "inside.txt", "DIRECTORY DATA");
            edit.add = {{folder.wstring(), L"added-folder"}};
            check(engine->edit(archive.wstring(), edit, {}, cancel).completed == 1,
                  "recursive directory additions preserve empty directories");
            const auto directoryOutput = temporary.path / (std::wstring(L"directory-output") + extension);
            engine->extract(archive.wstring(), directoryOutput.wstring(), {L"added-folder"}, extraction, {}, cancel);
            check(fs::is_directory(directoryOutput / "added-folder/empty") &&
                  bytes(directoryOutput / "added-folder/inside.txt") == "DIRECTORY DATA",
                  "added directory contents and empty descendants roundtrip");
        }
        check(throws([&] { engine->edit((temporary.path / "readonly.rar").wstring(), {}, {}, cancel); }),
              "other formats remain read-only");
        const auto tree = temporary.path / "containing-tree";
        fs::create_directory(tree);
        previewtest::write(tree / "payload.txt", "PAYLOAD");
        const auto insideArchive = tree / "inside.zip";
        engine->create(insideArchive.wstring(), {(tree / "payload.txt").wstring()}, {}, {}, cancel);
        ggzip::EditOptions directoryEdit;
        directoryEdit.add = {{tree.wstring(), L"folder"}};
        engine->edit(insideArchive.wstring(), directoryEdit, {}, cancel);
        const auto members = engine->list(insideArchive.wstring());
        check(std::none_of(members.begin(), members.end(), [](const auto& entry) {
            return entry.path.find(L"ggZip-job-") != std::wstring::npos || entry.path == L"folder/inside.zip";
        }), "editing inside an input tree excludes private staging and the archive itself");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
