#include "ArchiveTableModel.h"
#include <algorithm>

ArchiveTableModel::ArchiveTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void ArchiveTableModel::setEntries(std::vector<ggzip::Entry> entries) {
    beginResetModel();
    // 目录在前，其余按路径升序
    std::stable_sort(entries.begin(), entries.end(),
                     [](const ggzip::Entry& a, const ggzip::Entry& b) {
                         if (a.isDir != b.isDir) return a.isDir > b.isDir;
                         return a.path < b.path;
                     });
    entries_ = std::move(entries);
    endResetModel();
}

void ArchiveTableModel::clear() {
    beginResetModel();
    entries_.clear();
    endResetModel();
}

int ArchiveTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : (int)entries_.size();
}

int ArchiveTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 4;
}

QVariant ArchiveTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= (int)entries_.size() || role != Qt::DisplayRole)
        return {};
    const ggzip::Entry& e = entries_[(size_t)index.row()];
    switch (index.column()) {
        case 0: return QString::fromStdWString(e.path);
        case 1: return e.isDir ? QVariant() : ggzHumanSize(e.size);
        case 2: return e.isDir ? QVariant() : ggzHumanSize(e.packedSize);
        case 3: return e.isDir ? QString("目录") : QString("文件");
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
