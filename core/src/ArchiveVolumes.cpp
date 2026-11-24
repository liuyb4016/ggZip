#include "ggzip/ArchiveVolumes.h"
#include "ggzip/ArchiveError.h"
#include "ggzip/Diagnostics.h"
#include "ggzip/Text.h"
#include "FileTransaction.h"
#include <algorithm>
#include <cwctype>
#include <map>

namespace fs = std::filesystem;
namespace ggzip {
namespace {

std::wstring folded(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return value;
}

bool number(const std::wstring& text, unsigned int& result) {
    if (text.empty() || text.size() > 6) return false;
    result = 0;
    for (const auto character : text) {
        if (character < L'0' || character > L'9') return false;
        result = result * 10 + static_cast<unsigned int>(character - L'0');
    }
    return true;
}

void ordinary(const fs::path& path) {
    const auto state = files::identity(path);
    if (!state.exists || (state.info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        throw ArchiveError(diagnostic("分卷不是普通文件: ") + utf8(path.wstring()));
}

}

ArchiveVolumeSet archiveVolumes(const fs::path& path) {
    const auto input = fs::absolute(path).lexically_normal();
    const auto parent = input.parent_path();
    const auto filename = folded(input.filename().wstring());
    const auto extension = folded(input.extension().wstring());
    ArchiveVolumeSet result{input, {}, input.stem().wstring(), false};
    enum class Naming { Single, Numbered, PartRar, ClassicRar, SplitZip };
    Naming naming = Naming::Single;
    std::wstring prefix;
    unsigned int selected = 0;
    if (extension.size() >= 4 && number(extension.substr(1), selected)) {
        naming = Naming::Numbered;
        prefix = folded(input.stem().wstring()) + L".";
        result.baseName = input.stem().stem().wstring();
    } else if (!files::identity(input).exists &&
               files::identity(fs::path(input.wstring() + L".001")).exists) {
        naming = Naming::Numbered;
        prefix = filename + L".";
        result.baseName = input.stem().wstring();
    } else if (extension == L".rar") {
        const auto marker = filename.rfind(L".part");
        if (marker != std::wstring::npos &&
            number(filename.substr(marker + 5, filename.size() - marker - 9), selected)) {
            naming = Naming::PartRar;
            prefix = filename.substr(0, marker + 5);
            result.baseName = input.filename().wstring().substr(0, marker);
        } else {
            naming = Naming::ClassicRar;
            prefix = folded(input.stem().wstring()) + L".";
        }
    } else if (extension == L".zip") {
        naming = Naming::SplitZip;
        prefix = folded(input.stem().wstring()) + L".";
    } else if (extension.size() >= 4 && extension[1] == L'z' && number(extension.substr(2), selected)) {
        prefix = folded(input.stem().wstring()) + L".";
        naming = files::identity(parent / (input.stem().wstring() + L".zip")).exists ||
            !files::identity(parent / (input.stem().wstring() + L".rar")).exists
            ? Naming::SplitZip : Naming::ClassicRar;
    } else if (extension.size() == 4 && extension[1] >= L'r' && extension[1] <= L'y' &&
               number(extension.substr(2), selected)) {
        prefix = folded(input.stem().wstring()) + L".";
        naming = Naming::ClassicRar;
    }
    if (naming == Naming::Single) {
        if (files::identity(input).exists) { ordinary(input); result.parts.push_back(input); }
        return result;
    }
    std::map<unsigned int, fs::path> numbered;
    fs::path primary;
    if (!fs::exists(parent)) return result;
    for (const auto& item : fs::directory_iterator(parent)) {
        const auto name = folded(item.path().filename().wstring());
        if (name.rfind(prefix, 0) != 0) continue;
        const auto tail = name.substr(prefix.size());
        unsigned int index = 0;
        bool part = false;
        if (naming == Naming::Numbered) {
            part = tail.size() >= 3 && number(tail, index);
        } else if (naming == Naming::PartRar) {
            part = tail.size() > 4 && tail.substr(tail.size() - 4) == L".rar" &&
                number(tail.substr(0, tail.size() - 4), index);
        } else if (naming == Naming::ClassicRar) {
            if (tail == L"rar") { ordinary(item.path()); primary = item.path(); }
            part = tail.size() == 3 && tail[0] >= L'r' && tail[0] <= L'z' && number(tail.substr(1), index);
            if (part && tail[0] == L'z' && files::identity(parent / (input.stem().wstring() + L".zip")).exists)
                part = false;
            if (part) index += 1 + static_cast<unsigned int>(tail[0] - L'r') * 100;
        } else {
            if (tail == L"zip") { ordinary(item.path()); primary = item.path(); }
            part = tail.size() >= 3 && tail[0] == L'z' && number(tail.substr(1), index);
        }
        if (!part) continue;
        ordinary(item.path());
        if (!index || !numbered.emplace(index, item.path()).second)
            throw ArchiveError(diagnostic("分卷编号重复或无效: ") + utf8(item.path().wstring()));
        if (numbered.size() > 10000) throw ArchiveError(diagnostic("分卷数量超过安全上限"));
    }
    if ((naming == Naming::ClassicRar || naming == Naming::SplitZip) && primary.empty()) {
        result.first = parent / (input.stem().wstring() + (naming == Naming::ClassicRar ? L".rar" : L".zip"));
        throw ArchiveError(diagnostic("缺少分卷主文件: ") + utf8(result.first.wstring()));
    }
    if (numbered.empty()) {
        if (naming == Naming::ClassicRar || naming == Naming::SplitZip) {
            result.first = primary;
            result.parts.push_back(primary);
            return result;
        }
        throw ArchiveError(diagnostic("缺少首个分卷: ") + utf8(input.wstring()));
    }
    unsigned int expected = 1;
    for (const auto& item : numbered) {
        if (item.first != expected++) throw ArchiveError(diagnostic("分卷编号不连续，存在缺失分卷"));
        result.parts.push_back(item.second);
    }
    result.split = true;
    if (naming == Naming::ClassicRar) result.parts.insert(result.parts.begin(), primary);
    if (naming == Naming::SplitZip) result.parts.push_back(primary);
    result.first = naming == Naming::SplitZip ? primary : result.parts.front();
    return result;
}

}
