#pragma once
// ggZip 主窗口：压缩包浏览（目录导航/排序/预览）、解压、智能解压、压缩。
#include <QMainWindow>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "ggzip/Entry.h"
#include "ggzip/IEngine.h"
#include <QPointer>
#include <QImage>

class ArchiveTableModel;
class ProgressRunner;
class PreviewSession;
class QLabel;
class QProgressDialog;
class QTableView;
class QAction;
class QModelIndex;
class QCloseEvent;
class QResizeEvent;
class QDialog;
class QLineEdit;
class QCheckBox;
class QMenu;

namespace ggzip { class IEngine; }

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // 供命令行参数直接打开压缩包
    void openArchive(const QString& path);
    void runShellAction(const QStringList& arguments);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class Op { None, List, Extract, Create, Preview, Test, Edit };

    void startExtract(bool smartDefault, bool selectedOnly = false);
    void startCreate();
    void onOpFinished(bool ok, const QString& error);
    void onDoubleClicked(const QModelIndex& index);
    void openEntryTemp(const ggzip::Entry& e);   // 提取到临时目录后预览/打开
    void goUp();
    void updatePathUi();
    void setBaseStatus(const QString& text);
    void warnEngineMissing();
    void updateActionStates();
    ggzip::ConflictFn conflictResolver();
    void showPreview();
    void startTest();
    bool requestArchivePassword();
    void clearPassword();
    void refreshRecentMenu();
    void saveWindowSettings();
    void startEdit(int operation);
    void runEdit(ggzip::EditOptions options);
    void updateEmptyState();

    std::unique_ptr<ggzip::IEngine> engine_;
    ProgressRunner* runner_ = nullptr;
    ArchiveTableModel* model_ = nullptr;
    QTableView* table_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QWidget* emptyState_ = nullptr;
    QLabel* emptyTitle_ = nullptr;
    QLabel* emptyHint_ = nullptr;
    QAction* actUp_ = nullptr;
    QProgressDialog* progress_ = nullptr;

    QAction* actExtract_ = nullptr;
    QAction* actSmartExtract_ = nullptr;
    QAction* actCreate_ = nullptr;
    QAction* actTest_ = nullptr;
    QAction* actExtractSelected_ = nullptr;
    QAction* actAddMembers_ = nullptr;
    QAction* actAddFolder_ = nullptr;
    QAction* actDeleteMembers_ = nullptr;
    QAction* actRenameMember_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QCheckBox* searchAll_ = nullptr;
    QMenu* recentMenu_ = nullptr;
    QString sessionPassword_;
    bool passwordRequired_ = false;
    ggzip::Entry previewEntry_;
    ggzip::EditOptions pendingEdit_;

    Op currentOp_ = Op::None;
    QString currentArchive_;
    QString baseStatus_;
    std::unique_ptr<PreviewSession> previewSession_;
    std::atomic_bool previewCancel_{ false };
    std::wstring opResult_;                    // 后台线程写入，finished 后 GUI 读取
    ggzip::OperationSummary summary_;
    bool summaryReady_ = false;
    bool closePending_ = false;
    QPointer<QDialog> activeConflict_;
    QImage previewImage_;
    std::vector<ggzip::Entry> pendingEntries_; // 同上
};
