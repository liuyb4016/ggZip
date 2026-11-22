#pragma once
// ggZip 主窗口：压缩包浏览（目录导航/排序/预览）、解压、智能解压、压缩。
#include <QMainWindow>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "ggzip/Entry.h"

class ArchiveTableModel;
class ProgressRunner;
class QLabel;
class QProgressDialog;
class QTableView;
class QAction;
class QModelIndex;

namespace ggzip { class IEngine; }

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // 供命令行参数直接打开压缩包
    void openArchive(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    enum class Op { None, List, Extract, Create };

    void startExtract(bool smartDefault);
    void startCreate();
    void onOpFinished(bool ok, const QString& error);
    void onDoubleClicked(const QModelIndex& index);
    void openEntryTemp(const ggzip::Entry& e);   // 提取到临时目录后预览/打开
    void goUp();
    void updatePathUi();
    void setBaseStatus(const QString& text);
    QString previewTempRoot();
    void warnEngineMissing();
    void updateActionStates();

    std::unique_ptr<ggzip::IEngine> engine_;
    ProgressRunner* runner_ = nullptr;
    ArchiveTableModel* model_ = nullptr;
    QTableView* table_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QAction* actUp_ = nullptr;
    QProgressDialog* progress_ = nullptr;

    QAction* actExtract_ = nullptr;
    QAction* actSmartExtract_ = nullptr;
    QAction* actCreate_ = nullptr;

    Op currentOp_ = Op::None;
    QString currentArchive_;
    QString baseStatus_;
    QString previewTempDir_;
    int previewSeq_ = 0;
    std::atomic_bool previewCancel_{ false };
    std::wstring opResult_;                    // 后台线程写入，finished 后 GUI 读取
    std::vector<ggzip::Entry> pendingEntries_; // 同上
};
