#include "preview_fixtures.h"
#include "ggzip/ArchiveVolumes.h"
#include "ggzip/EngineFactory.h"
#include "ggzip/ArchiveError.h"
#include "ggzip/SourceMark.h"
#include "ProcessUtil.h"
#include <functional>
#include <iterator>

namespace fs = std::filesystem;
namespace {
int checks = 0;
int failures = 0;
void check(bool result, const char* label) {
    ++checks;
    if (!result) ++failures;
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
void set16(std::string& value, std::size_t offset, unsigned int number) {
    value.at(offset) = static_cast<char>(number & 255);
    value.at(offset + 1) = static_cast<char>((number >> 8) & 255);
}
void set32(std::string& value, std::size_t offset, unsigned int number) {
    set16(value, offset, number & 65535);
    set16(value, offset + 2, number >> 16);
}
void splitZip(const fs::path& main, const std::string& content) {
    const std::string name = "file.txt";
    previewtest::zip(main, {{name, content}});
    auto data = bytes(main);
    const auto central = 30 + name.size() + content.size();
    const auto end = data.size() - 22;
    const auto cut = 30 + name.size() + content.size() / 2;
    set32(data, central + 42, 4);
    set16(data, end + 4, 1);
    set16(data, end + 6, 1);
    set32(data, end + 16, static_cast<unsigned int>(central - cut));
    auto first = main;
    first.replace_extension(".z01");
    previewtest::write(first, std::string("PK\x07\x08", 4) + data.substr(0, cut));
    previewtest::write(main, data.substr(cut));
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        previewtest::Directory fixture;
        const auto root = fixture.path;
        const auto numberedBase = root / "numbered.7z";
        const auto first = root / "numbered.7z.001";
        const auto second = root / "numbered.7z.002";
        previewtest::write(first, "FIRST");
        previewtest::write(second, "SECOND");
        const auto numbered = ggzip::archiveVolumes(second);
        check(numbered.first == first && numbered.parts.size() == 2 && numbered.split && numbered.baseName == L"numbered",
              "later numbered volume resolves first volume and complete ordered family");
        check(ggzip::archiveVolumes(numberedBase).first == first,
              "missing unsuffixed numbered archive resolves to its existing first volume");
        const auto numericZip = root / "numeric.zip";
        previewtest::write(numericZip.wstring() + L".001", "FIRST");
        check(ggzip::archiveVolumes(numericZip).first == fs::path(numericZip.wstring() + L".001"),
              "numbered ZIP family is resolved before traditional ZIP naming");
        previewtest::write(root / "missing.7z.002", "LATER");
        check(throws([&] { ggzip::archiveVolumes(root / "missing.7z.002"); }), "missing numbered first volume is rejected");
        previewtest::write(root / "numbered.7z.004", "GAP");
        check(throws([&] { ggzip::archiveVolumes(first); }), "missing middle numbered volume is rejected");
        fs::remove(root / "numbered.7z.004");
        previewtest::write(root / "numbered.7z.0001", "DUPLICATE");
        check(throws([&] { ggzip::archiveVolumes(first); }), "duplicate numeric spellings are rejected instead of silently choosing one");
        fs::remove(root / "numbered.7z.0001");
        const auto partFirst = root / L"中文.part01.RAR";
        const auto partLater = root / L"中文.part02.rar";
        previewtest::write(partFirst, "FIRST");
        previewtest::write(partLater, "SECOND");
        const auto part = ggzip::archiveVolumes(partLater);
        check(part.first == partFirst && part.parts.size() == 2 && part.baseName == L"中文",
              "part-number RAR preserves actual spelling and resolves padded first volume");
        fs::remove(partFirst);
        check(throws([&] { ggzip::archiveVolumes(partLater); }), "missing part1 RAR is rejected");
        previewtest::write(partFirst, "FIRST");
        const auto classic = root / "classic.rar";
        previewtest::write(classic, "MAIN");
        previewtest::write(root / "classic.r00", "FIRST");
        previewtest::write(root / "classic.r01", "SECOND");
        const auto old = ggzip::archiveVolumes(root / "classic.r01");
        check(old.first == classic && old.parts.size() == 3 && old.parts[1].extension() == L".r00",
              "traditional RAR starts at the rar primary and snapshots r00/r01");
        fs::create_directory(root / "classic.r02");
        check(throws([&] { ggzip::archiveVolumes(classic); }), "volume-like directory is not accepted as an archive part");
        fs::remove(root / "classic.r02");
        const auto manyRar = root / "many.rar";
        previewtest::write(manyRar, "MAIN");
        for (unsigned int index = 0; index < 100; ++index) {
            const auto suffix = L".r" + std::wstring(index < 10 ? L"0" : L"") + std::to_wstring(index);
            previewtest::write(root / (L"many" + suffix), "PART");
        }
        previewtest::write(root / "many.s00", "PART");
        check(ggzip::archiveVolumes(root / "many.s00").parts.size() == 102,
              "traditional RAR continues from r99 to s00");
        const auto zip = root / "split.zip";
        const std::string content(4096, 'Q');
        splitZip(zip, content);
        const auto zipFirst = root / "split.z01";
        const auto zipFamily = ggzip::archiveVolumes(zipFirst);
        check(zipFamily.first == zip && zipFamily.parts.size() == 2 &&
              zipFamily.parts.front() == zipFirst && zipFamily.parts.back() == zip,
              "traditional ZIP resolves final zip as entry point and includes preceding z01 data");
        previewtest::write(root / "missing-zip.z01", "PART");
        check(throws([&] { ggzip::archiveVolumes(root / "missing-zip.z01"); }), "missing final ZIP primary is rejected");
        const auto manyZip = root / "manyzip.zip";
        previewtest::write(manyZip, "MAIN");
        for (unsigned int index = 1; index <= 100; ++index) {
            const auto suffix = L".z" + std::wstring(index < 10 ? L"0" : L"") + std::to_wstring(index);
            previewtest::write(root / (L"manyzip" + suffix), "PART");
        }
        check(ggzip::archiveVolumes(root / "manyzip.z100").parts.size() == 101,
              "traditional ZIP supports more than 99 volume suffixes");
        previewtest::write(root / "split.rar", "INDEPENDENT");
        check(!ggzip::archiveVolumes(root / "split.rar").split,
              "standalone RAR does not accidentally adopt a same-stem ZIP z01 volume");
        auto engine = ggzip::createEngine();
        std::atomic_bool cancel{false};
        check(engine->list(zipFirst.wstring()).front().path == L"file.txt",
              "bundled engine lists an actual multi-disk ZIP when opened from z01");
        engine->test(zipFirst.wstring(), {}, {}, cancel);
        check(!cancel.load(), "bundled engine verifies actual multi-disk ZIP integrity");
        const auto output = root / "extracted";
        engine->extract(zipFirst.wstring(), output.wstring(), {}, {}, {}, cancel);
        check(bytes(output / "file.txt") == content, "transactional extraction snapshots all actual ZIP disks before decoding");
        std::string preview;
        engine->extractToStream(zipFirst.wstring(), L"file.txt", {},
            [&](std::string_view chunk) { preview.append(chunk); }, cancel);
        check(preview == content, "multi-disk ZIP preview resolves the primary and exact member");
        ggzip::EditOptions edit;
        edit.rename.push_back({L"file.txt", L"renamed.txt"});
        const auto oldZip = bytes(zip);
        check(throws([&] { engine->edit(zip.wstring(), edit, {}, cancel); }) && bytes(zip) == oldZip,
              "multi-disk ZIP is explicitly read-only for editing");
        ggzip::applySourceZone(zipFirst.wstring(), 3);
        const auto markedOutput = root / "marked";
        engine->extract(zip.wstring(), markedOutput.wstring(), {}, {}, {}, cancel);
        check(ggzip::sourceZone((markedOutput / "file.txt").wstring()) == 3,
              "download source zone is propagated from every volume, not only the primary");
        if (argc == 2) {
            const auto batchOutput = root / "batch";
            std::string diagnostic;
            const auto batch = ggzip::proc::run(argv[1],
                {L"batch-extract", zipFirst.wstring(), zip.wstring(), L"-o", batchOutput.wstring()},
                [&](std::string_view chunk) { diagnostic.append(chunk); return true; }, cancel);
            check(batch.exitCode == 0 && bytes(batchOutput / "split" / "file.txt") == content &&
                  std::distance(fs::directory_iterator(batchOutput), fs::directory_iterator{}) == 1,
                  "CLI batch selects each volume family once even when multiple disks are passed");
            if (batch.exitCode != 0) std::fprintf(stderr, "%s\n%s\n", diagnostic.c_str(), batch.stderrTail.c_str());
        }
        fs::remove(zipFirst);
        const auto missingOutput = root / "missing-output";
        check(throws([&] { engine->extract(zip.wstring(), missingOutput.wstring(), {}, {}, {}, cancel); }) &&
              !fs::exists(missingOutput / "file.txt"), "missing ZIP data disk never publishes incomplete content");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
