#pragma once
// 压缩包条目的表格模型（名称 / 大小 / 压缩后 / 类型）。
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

    void setEntries(std::vector<ggzip::Entry> entries);  // 目录排前，其余按路径排序
    void clear();

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

private:
    std::vector<ggzip::Entry> entries_;
};
