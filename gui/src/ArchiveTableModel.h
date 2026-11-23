#pragma once
// 压缩包条目的表格模型：支持按目录前缀过滤（进入目录）、列排序。
#include <QAbstractTableModel>
#include <QString>
#include <vector>
#include "ggzip/Entry.h"

// 人类可读的文件大小（GUI 各处共用）
inline QString ggzHumanSize(quint64 n) {
    if (n < 1024) return QString::number(n) + " B";
    if (n < 1024ull * 1024) return QString::number(n / 1024.0, 'f', 1) + " KB";
    if (n < 1024ull * 1024 * 1024)
        return QString::number(n / 1024.0 / 1024.0, 'f', 1) + " MB";
    return QString::number(n / 1024.0 / 1024.0 / 1024.0, 'f', 2) + " GB";
}

class ArchiveTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ArchiveTableModel(QObject* parent = nullptr);

    // 重置全部条目并回到根目录
    void setEntries(std::vector<ggzip::Entry> entries);
    void clear();

    // 进入目录（prefix 以 '/' 结尾）或回到根（空串）
    void setPrefix(const QString& prefix);
    QString prefix() const { return prefix_; }
    QString displayPath() const;  // 面包屑显示文本
    void setSearch(const QString& query, bool wholeArchive);
    bool hasEncryptedEntries() const;

    // 当前可见行对应的条目；越界返回 nullptr
    const ggzip::Entry* entryAt(int row) const;
    int allCount() const { return memberCount_; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;
    void sort(int column, Qt::SortOrder order) override;

private:
    void rebuildView();
    bool lessThan(int aIdx, int bIdx) const;

    std::vector<ggzip::Entry> all_;  // 完整条目
    std::vector<int> view_;          // 当前目录下可见条目的索引（已排序）
    QString prefix_;                 // 当前目录前缀（'' = 根；以 '/' 结尾）
    int sortColumn_ = -1;            // -1 = 默认排序（目录在前，按路径）
    Qt::SortOrder sortOrder_ = Qt::AscendingOrder;
    QString search_;
    bool wholeArchive_ = false;
    int memberCount_ = 0;
};
