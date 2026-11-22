#pragma once
// ggZip 主窗口：压缩包浏览、解压、智能解压、压缩。
#include <QMainWindow>
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
class QCloseEvent;

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
    void warnEngineMissing();
    void updateActionStates();

    std::unique_ptr<ggzip::IEngine> engine_;
    ProgressRunner* runner_ = nullptr;
    ArchiveTableModel* model_ = nullptr;
    QTableView* table_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QProgressDialog* progress_ = nullptr;

    QAction* actExtract_ = nullptr;
    QAction* actSmartExtract_ = nullptr;
    QAction* actCreate_ = nullptr;

    Op currentOp_ = Op::None;
    QString currentArchive_;
    std::wstring opResult_;                    // 后台线程写入，finished 后 GUI 读取
    std::vector<ggzip::Entry> pendingEntries_; // 同上
};
