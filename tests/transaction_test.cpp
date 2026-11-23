#include "preview_fixtures.h"
#include "ggzip/EngineFactory.h"
#include "ggzip/OperationError.h"
#include "FileTransaction.h"
#include "ggzip/SourceMark.h"
#include <cstdio>
#include <functional>
#include <iterator>

namespace fs = std::filesystem;
namespace {
int checked = 0;
int failed = 0;
void check(bool result, const char* label) {
    ++checked;
    if (!result) ++failed;
    std::printf("%s: %s\n", result ? "PASS" : "FAIL", label);
}
bool throws(const std::function<void()>& action) {
    try { action(); } catch (const std::exception&) { return true; }
    return false;
}
std::string bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}
}

int wmain() {
    try {
        previewtest::Directory fixture;
        auto engine = ggzip::createEngine();
        std::atomic_bool cancel{false};
        const fs::path input = fixture.path / L"中文 文件.txt";
        previewtest::write(input, "ORIGINAL DATA");
        const fs::path archive = fixture.path / "archive.zip";
        const auto first = engine->create(archive.wstring(), {input.wstring()}, {}, {}, cancel);
        check(first.completed == 1 && fs::exists(archive), "new archive committed");
        const std::string oldArchive = bytes(archive);
        check(throws([&] { engine->create(archive.wstring(), {input.wstring()}, {}, {}, cancel); }) &&
              bytes(archive) == oldArchive, "default conflict rejects without changing archive");
        ggzip::CreateOptions option;
        option.conflict = ggzip::ConflictPolicy::Skip;
        check(engine->create(archive.wstring(), {input.wstring()}, option, {}, cancel).skipped == 1 &&
              bytes(archive) == oldArchive, "explicit skip preserves archive");
        option.conflict = ggzip::ConflictPolicy::Rename;
        const auto renamed = engine->create(archive.wstring(), {input.wstring()}, option, {}, cancel);
        check(renamed.renamed == 1 && renamed.destination != archive.wstring() &&
              bytes(archive) == oldArchive, "explicit rename preserves old archive");
        previewtest::write(input, "REPLACEMENT");
        option.conflict = ggzip::ConflictPolicy::Overwrite;
        const auto replaced = engine->create(archive.wstring(), {input.wstring()}, option, {}, cancel);
        check(replaced.overwritten == 1 && bytes(archive) != oldArchive, "authorized replacement committed");
        const std::string goodArchive = bytes(archive);
        check(throws([&] {
            engine->create(archive.wstring(), {(fixture.path / "absent.txt").wstring()}, option, {}, cancel);
        }) && bytes(archive) == goodArchive, "creation failure preserves original archive");
        cancel.store(true);
        check(engine->create(archive.wstring(), {input.wstring()}, option, {}, cancel).outcome ==
              ggzip::Outcome::Cancelled && bytes(archive) == goodArchive, "creation cancellation preserves original");
        cancel.store(false);
        const fs::path volume = fixture.path / "volume.7z";
        const fs::path binary = fixture.path / "random.bin";
        std::string data(262144, '\0');
        std::uint32_t random = 17;
        for (auto& byte : data) { random = random * 1664525 + 1013904223; byte = static_cast<char>(random >> 24); }
        previewtest::write(binary, data);
        ggzip::CreateOptions split;
        split.volumeSize = L"64k";
        const auto volumes = engine->create(volume.wstring(), {binary.wstring()}, split, {}, cancel);
        check(volumes.completed > 1 && fs::exists(volume.wstring() + L".002"), "new volume family committed");
        check(engine->list(volume.wstring()).size() == 1 &&
              engine->list(volume.wstring()).front().path == L"random.bin",
              "split listing contains actual members, not the volume wrapper");
        const auto splitOutput = fixture.path / "split-output";
        engine->extract(volume.wstring(), splitOutput.wstring(), {}, {}, {}, cancel);
        check(bytes(splitOutput / binary.filename()) == data, "split extraction verifies actual members");
        const auto secondVolumeOutput = fixture.path / "second-volume-output";
        engine->extract(volume.wstring() + L".002", secondVolumeOutput.wstring(), {}, {}, {}, cancel);
        check(bytes(secondVolumeOutput / binary.filename()) == data, "opening a later numbered volume resolves the first");
        std::string splitPreview;
        engine->extractToStream(volume.wstring(), L"random.bin", {},
            [&](std::string_view chunk) { splitPreview.append(chunk); }, cancel);
        check(splitPreview == data, "split preview selects the exact actual member");
        const std::string firstVolume = bytes(volume.wstring() + L".001");
        split.conflict = ggzip::ConflictPolicy::Overwrite;
        check(throws([&] {
            engine->create(volume.wstring(), {(fixture.path / "absent").wstring()}, split, {}, cancel);
        }) && bytes(volume.wstring() + L".001") == firstVolume, "failed split creation leaves old volumes intact");
        split.volumeSize = L"1m";
        const auto fewer = engine->create(volume.wstring(), {binary.wstring()}, split, {}, cancel);
        check(fewer.completed == 1 && !fs::exists(volume.wstring() + L".002"),
              "successful volume family replacement removes old extra volumes");

        const fs::path output = fixture.path / "output";
        fs::create_directory(output);
        const fs::path existing = output / input.filename();
        previewtest::write(existing, "KEEP");
        check(throws([&] { engine->extract(archive.wstring(), output.wstring(), {}, {}, {}, cancel); }) &&
              bytes(existing) == "KEEP", "default extraction rejects conflicts");
        ggzip::ExtractOptions extraction;
        extraction.conflict = ggzip::ConflictPolicy::Skip;
        check(engine->extract(archive.wstring(), output.wstring(), {}, extraction, {}, cancel).skipped == 1 &&
              bytes(existing) == "KEEP", "extract skip retains existing bytes");
        extraction.conflict = ggzip::ConflictPolicy::Rename;
        const auto newName = engine->extract(archive.wstring(), output.wstring(), {}, extraction, {}, cancel);
        check(newName.renamed == 1 && bytes(existing) == "KEEP" &&
              bytes(newName.paths.front()) == "REPLACEMENT", "extract rename publishes a distinct complete file");
        extraction.conflict = ggzip::ConflictPolicy::Overwrite;
        const auto overwritten = engine->extract(archive.wstring(), output.wstring(), {}, extraction, {}, cancel);
        check(overwritten.overwritten == 1 && bytes(existing) == "REPLACEMENT", "extract authorized overwrite");
        const fs::path damaged = fixture.path / "damaged.zip";
        previewtest::zip(damaged, {{"broken.txt", "BROKEN", 0x81a40000, true}});
        check(throws([&] { engine->extract(damaged.wstring(), output.wstring(), {}, extraction, {}, cancel); }) &&
              !fs::exists(output / "broken.txt"), "CRC failure does not publish a damaged file");
        const fs::path unsafe = fixture.path / "unsafe.zip";
        previewtest::zip(unsafe, {{"../escape.txt", "ESCAPE"}});
        check(throws([&] { engine->extract(unsafe.wstring(), output.wstring(), {}, extraction, {}, cancel); }) &&
              !fs::exists(fixture.path / "escape.txt"), "unsafe archive rejected before decoding");
        extraction.conflict = ggzip::ConflictPolicy::Ask;
        int prompts = 0;
        extraction.resolveConflict = [&](const ggzip::Conflict&) {
            ++prompts;
            return ggzip::ConflictDecision{ggzip::ConflictPolicy::Skip, true};
        };
        check(engine->extract(archive.wstring(), output.wstring(), {}, extraction, {}, cancel).skipped == 1 &&
              prompts == 1, "explicit conflict resolver controls GUI policy");
        const auto nested = fixture.path / "nested.zip";
        previewtest::zip(nested, {{"dir/first.txt", "FIRST"}, {"dir/sub/second.txt", "SECOND"},
                                 {"other.txt", "OTHER"}});
        const auto nestedOutput = fixture.path / "nested-output";
        fs::create_directory(nestedOutput);
        previewtest::write(nestedOutput / "dir", "KEEP DIRECTORY CONFLICT");
        extraction.conflict = ggzip::ConflictPolicy::Skip;
        const auto skippedTree = engine->extract(nested.wstring(), nestedOutput.wstring(), {}, extraction, {}, cancel);
        check(skippedTree.skipped == 2 && bytes(nestedOutput / "dir") == "KEEP DIRECTORY CONFLICT" &&
              bytes(nestedOutput / "other.txt") == "OTHER", "implicit directory conflict skips whole subtree");
        extraction.conflict = ggzip::ConflictPolicy::Rename;
        const auto renamedTree = engine->extract(nested.wstring(), nestedOutput.wstring(), {L"dir/"}, extraction, {}, cancel);
        check(renamedTree.completed == 2 && bytes(nestedOutput / "dir (1)/sub/second.txt") == "SECOND" &&
              bytes(nestedOutput / "dir") == "KEEP DIRECTORY CONFLICT", "directory rename maps every descendant");
        const auto selectedOutput = fixture.path / "selected-output";
        engine->extract(nested.wstring(), selectedOutput.wstring(), {L"dir/sub/"}, {}, {}, cancel);
        check(bytes(selectedOutput / "dir/sub/second.txt") == "SECOND" &&
              !fs::exists(selectedOutput / "dir/first.txt") && !fs::exists(selectedOutput / "other.txt"),
              "literal selected extraction preserves structure and excludes other members");
        const auto collision = fixture.path / "collision.zip";
        previewtest::zip(collision, {{"same.txt", "NEW"}, {"same (1).txt", "NAMED"}});
        const auto collisionOutput = fixture.path / "collision-output";
        fs::create_directory(collisionOutput);
        previewtest::write(collisionOutput / "same.txt", "KEEP");
        engine->extract(collision.wstring(), collisionOutput.wstring(), {}, extraction, {}, cancel);
        check(bytes(collisionOutput / "same (1).txt") == "NAMED" &&
              bytes(collisionOutput / "same (2).txt") == "NEW", "automatic names cannot collide with later archive members");
        const auto mixed = fixture.path / "mixed.zip";
        previewtest::zip(mixed, {{"a-new.txt", "NEW"}, {"z-existing.txt", "REPLACE"}});
        const auto mixedOutput = fixture.path / "mixed-output";
        fs::create_directory(mixedOutput);
        previewtest::write(mixedOutput / "z-existing.txt", "KEEP");
        check(throws([&] { engine->extract(mixed.wstring(), mixedOutput.wstring(), {}, {}, {}, cancel); }) &&
              !fs::exists(mixedOutput / "a-new.txt"), "reject preflight publishes no earlier new files");
        ggzip::applySourceZone(nested.wstring(), 3);
        const auto markedOutput = fixture.path / "marked-output";
        engine->extract(nested.wstring(), markedOutput.wstring(), {}, {}, {}, cancel);
        check(ggzip::sourceZone((markedOutput / "dir/sub/second.txt").wstring()) == 3,
              "download provenance is preserved on extracted files");
        const auto selfOutput = fixture.path / "self-output";
        fs::create_directory(selfOutput);
        previewtest::write(selfOutput / "data.txt", "PAYLOAD");
        const auto selfArchive = selfOutput / "inside.zip";
        engine->create(selfArchive.wstring(), {selfOutput.wstring() + L"\\*"}, {}, {}, cancel);
        check(engine->list(selfArchive.wstring()).size() == 1,
              "creation inside source tree excludes its private staging directory");
        {
            ggzip::files::Workspace workspace(fixture.path);
            const fs::path staged = workspace.root / "new.txt";
            previewtest::write(staged, "NEW");
            workspace.adopt(staged);
            const fs::path target = fixture.path / "rollback.txt";
            previewtest::write(target, "OLD");
            const auto expected = ggzip::files::identity(target);
            check(throws([&] {
                ggzip::files::commitFamily(workspace,
                    {{staged, target}, {workspace.root / "missing", fixture.path / "second.txt"}},
                    {target}, {expected});
            }) && bytes(target) == "OLD", "mid-commit failure restores the original file");
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    std::printf("%d checks, %d failures\n", checked, failed);
    return failed ? 1 : 0;
}
