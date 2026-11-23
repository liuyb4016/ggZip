#include "ArchiveTableModel.h"
#include <algorithm>
#include <set>

ArchiveTableModel::ArchiveTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void ArchiveTableModel::setEntries(std::vector<ggzip::Entry> entries) {
    memberCount_ = static_cast<int>(entries.size());
    std::set<std::wstring> directories;
    for (const auto& entry : entries) if (entry.isDir) directories.insert(entry.path);
    std::vector<ggzip::Entry> inferred;
    for (const auto& entry : entries) {
        for (auto separator = entry.path.find(L'/'); separator != std::wstring::npos;
             separator = entry.path.find(L'/', separator + 1)) {
            const auto path = entry.path.substr(0, separator + 1);
            if (directories.insert(path).second) {
                ggzip::Entry directory;
                directory.path = path;
                directory.isDir = true;
                inferred.push_back(std::move(directory));
            }
        }
    }
    entries.insert(entries.end(), inferred.begin(), inferred.end());
    all_ = std::move(entries);
    prefix_.clear();
    sortColumn_ = -1;
    rebuildView();
}

void ArchiveTableModel::clear() {
    beginResetModel();
    all_.clear();
    memberCount_ = 0;
    view_.clear();
    prefix_.clear();
    sortColumn_ = -1;
    endResetModel();
}

void ArchiveTableModel::setPrefix(const QString& prefix) {
    prefix_ = prefix;
    rebuildView();
}

void ArchiveTableModel::setSearch(const QString& query, bool wholeArchive) {
    search_ = query.trimmed();
    wholeArchive_ = wholeArchive;
    rebuildView();
}

bool ArchiveTableModel::hasEncryptedEntries() const {
    return std::any_of(all_.begin(), all_.end(), [](const auto& entry) { return entry.encrypted; });
}

QString ArchiveTableModel::displayPath() const {
    return prefix_.isEmpty() ? QStringLiteral("(根目录)") : prefix_;
}

const ggzip::Entry* ArchiveTableModel::entryAt(int row) const {
    if (row < 0 || row >= (int)view_.size()) return nullptr;
    return &all_[(size_t)view_[(size_t)row]];
}

// 只显示 prefix 的直接子项（子目录本身与文件）
void ArchiveTableModel::rebuildView() {
    beginResetModel();
    view_.clear();
    std::wstring wp = prefix_.toStdWString();
    if (!wp.empty() && wp.back() == L'/') wp.pop_back();
    for (size_t i = 0; i < all_.size(); ++i) {
        std::wstring p = all_[i].path;
        if (!p.empty() && p.back() == L'/') p.pop_back();
        if (p.empty()) continue;
        if (!search_.isEmpty() && !QString::fromStdWString(p).contains(search_, Qt::CaseInsensitive)) continue;
        if (search_.isEmpty() || !wholeArchive_) {
            if (p.size() <= wp.size() || p.compare(0, wp.size(), wp) != 0) continue;
            if (!wp.empty() && p[wp.size()] != L'/') continue;
            const size_t restStart = wp.empty() ? 0 : wp.size() + 1;
            if (p.find(L'/', restStart) != std::wstring::npos) continue;
        }
        view_.push_back((int)i);
    }
    std::stable_sort(view_.begin(), view_.end(),
                     [this](int a, int b) { return lessThan(a, b); });
    endResetModel();
}

bool ArchiveTableModel::lessThan(int aIdx, int bIdx) const {
    const ggzip::Entry& A = all_[(size_t)aIdx];
    const ggzip::Entry& B = all_[(size_t)bIdx];
    if (sortColumn_ < 0) {
        if (A.isDir != B.isDir) return A.isDir;  // 目录在前
        return A.path < B.path;
    }
    const bool asc = (sortOrder_ == Qt::AscendingOrder);
    int c = 0;
    switch (sortColumn_) {
        case 0: c = A.path.compare(B.path); break;
        case 1: c = (A.size < B.size) ? -1 : (A.size > B.size ? 1 : 0); break;
        case 2:
            c = (A.packedSize < B.packedSize) ? -1 : (A.packedSize > B.packedSize ? 1 : 0);
            break;
        case 3: c = (A.isDir == B.isDir) ? 0 : (B.isDir ? 1 : -1); break;
        default: break;
    }
    if (c != 0) return asc ? c < 0 : c > 0;
    if (A.isDir != B.isDir) return asc ? A.isDir : B.isDir;  // 同值时目录仍在前
    return A.path < B.path;
}

int ArchiveTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : (int)view_.size();
}

int ArchiveTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 4;
}

QVariant ArchiveTableModel::data(const QModelIndex& index, int role) const {
    if (role != Qt::DisplayRole || !index.isValid()) return {};
    const ggzip::Entry* e = entryAt(index.row());
    if (!e) return {};
    switch (index.column()) {
        case 0: return QString::fromStdWString(e->path);
        case 1: return e->isDir ? QVariant() : ggzHumanSize(e->size);
        case 2: return e->isDir ? QVariant() : ggzHumanSize(e->packedSize);
        case 3: return e->isDir ? QString("目录") : QString("文件");
    }
    return {};
}

QVariant ArchiveTableModel::headerData(int section, Qt::Orientation orientation,
                                       int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return {};
    switch (section) {
        case 0: return "名称";
        case 1: return "大小";
        case 2: return "压缩后";
        case 3: return "类型";
    }
    return {};
}

void ArchiveTableModel::sort(int column, Qt::SortOrder order) {
    sortColumn_ = column;
    sortOrder_ = order;
    rebuildView();
}
