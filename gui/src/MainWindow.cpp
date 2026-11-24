#include "MainWindow.h"
#include "ArchiveTableModel.h"
#include "Dialogs.h"
#include "ProgressRunner.h"
#include "PreviewSession.h"
#include "PreviewImage.h"
#include "Preferences.h"
#include "BatchDialog.h"
#include "UpdateDialog.h"
#include "ggzip/ArchiveError.h"
#include "ggzip/OperationError.h"
#include "ggzip/EngineFactory.h"
#include "ggzip/FileAssociations.h"
#include "ggzip/IEngine.h"
#include "ggzip/SmartExtract.h"
#include "ggzip/SourceMark.h"
#include "ggzip/PasswordError.h"
#include "ggzip/Text.h"
#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QFile>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QStatusBar>
#include <QStyle>
#include <QTableView>
#include <QToolBar>
#include <QUrl>
#include <QCheckBox>
#include <QCloseEvent>
#include <QPushButton>
#include <QThread>
#include <QSettings>
#include <QLineEdit>
#include <QDir>
#include <QDirIterator>
#include <QInputDialog>
#include <qt_windows.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <functional>

namespace {

std::wstring toW(const QString& s) { return s.toStdWString(); }

void showMessage(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text) {
    QMessageBox message(icon, title, text, QMessageBox::Ok, parent);
    message.setTextFormat(Qt::PlainText);
    message.exec();
}

const wchar_t* kFilter =
    L"压缩包 (*.zip *.7z *.rar *.tar *.gz *.tgz *.bz2 *.tbz *.xz *.txz *.zst *.tzst "
    L"*.iso *.cab *.wim *.msi *.jar *.apk *.001 *.r[0-9][0-9] *.z[0-9][0-9]*);;所有文件 (*.*)";

bool isImageFile(const QString& name) {
    static const QStringList kExt = { "png", "jpg", "jpeg", "gif",
                                      "bmp", "webp", "ico" };
    return kExt.contains(QFileInfo(name).suffix().toLower());
}

bool isTextFile(const QString& name) {
    static const QStringList kExt = {
        "txt", "md", "log", "ini", "cfg", "json", "xml", "yaml", "yml",
        "csv", "srt", "bat", "ps1", "py", "js", "html", "css",
        "c", "h", "cpp", "hpp", "cs", "def", "rc"
    };
    return kExt.contains(QFileInfo(name).suffix().toLower());
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    engine_ = ggzip::createEngine();

    model_ = new ArchiveTableModel(this);
    table_ = new QTableView(this);
    table_->setModel(model_);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setObjectName("archiveTable");
    table_->setAlternatingRowColors(true);
    table_->setWordWrap(false);
    table_->setSortingEnabled(true);   // 点击表头排序（模型 sort() 覆盖实现）
    setCentralWidget(table_);

    runner_ = new ProgressRunner(this);

    // ---- 菜单 / 工具栏 ----
    QMenu* fileMenu = menuBar()->addMenu(tr("文件(&F)"));

    QAction* actOpen =
        new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("打开压缩包(&O)..."), this);
    actOpen->setShortcut(QKeySequence::Open);
    connect(actOpen, &QAction::triggered, this, [this] {
        QString p = QFileDialog::getOpenFileName(this, tr("打开压缩包"), QString(),
                                                 tr(ggzip::utf8(kFilter).c_str()));
        if (!p.isEmpty()) openArchive(p);
    });
    fileMenu->addAction(actOpen);
    recentMenu_ = fileMenu->addMenu(tr("最近打开"));
    connect(recentMenu_, &QMenu::aboutToShow, this, &MainWindow::refreshRecentMenu);
    refreshRecentMenu();
    fileMenu->addSeparator();

    actExtract_ = new QAction(style()->standardIcon(QStyle::SP_ArrowDown), tr("解压到(&E)..."), this);
    connect(actExtract_, &QAction::triggered, this, [this] { startExtract(false); });

    actSmartExtract_ = new QAction(tr("智能解压到(&S)..."), this);
    actSmartExtract_->setShortcut(QKeySequence("Ctrl+S"));
    connect(actSmartExtract_, &QAction::triggered, this, [this] { startExtract(true); });

    actCreate_ = new QAction(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("新建压缩包(&C)..."), this);
    actCreate_->setShortcut(QKeySequence::Save);
    connect(actCreate_, &QAction::triggered, this, [this] { startCreate(); });

    fileMenu->addAction(actExtract_);
    actExtractSelected_ = fileMenu->addAction(tr("解压选中项…"), this, [this] { startExtract(false, true); });
    actExtractSelected_->setObjectName("extractSelected");
    fileMenu->addAction(actSmartExtract_);
    fileMenu->addAction(actCreate_);
    actTest_ = fileMenu->addAction(tr("测试压缩包完整性…"), this, &MainWindow::startTest);
    fileMenu->addAction(tr("批量压缩/解压…"), this, [this] {
        if (runner_->isRunning()) return;
        BatchDialog dialog(*engine_, {}, this);
        dialog.exec();
    });
    auto* editMenu = menuBar()->addMenu(tr("编辑压缩包"));
    actAddMembers_ = editMenu->addAction(tr("添加到压缩包…"), this, [this] { startEdit(0); });
    actAddFolder_ = editMenu->addAction(tr("添加文件夹..."), this, [this] { startEdit(3); });
    actDeleteMembers_ = editMenu->addAction(tr("删除选中条目…"), this, [this] { startEdit(1); });
    actRenameMember_ = editMenu->addAction(tr("重命名选中条目…"), this, [this] { startEdit(2); });
    fileMenu->addSeparator();

    QAction* actQuit = new QAction(tr("退出(&X)"), this);
    actQuit->setShortcut(QKeySequence::Quit);
    connect(actQuit, &QAction::triggered, this, &MainWindow::close);
    fileMenu->addAction(actQuit);

    QMenu* settingsMenu = menuBar()->addMenu(tr("设置(&O)"));
    QAction* actPwMgr = new QAction(tr("密码管理器(&P)..."), this);
    connect(actPwMgr, &QAction::triggered, this, [this] {
        PasswordManagerDialog dlg(this);
        dlg.exec();
    });
    settingsMenu->addAction(actPwMgr);
    settingsMenu->addAction(tr("常用设置与预览限制…"), this, [this] {
        PreferencesDialog dialog(this);
        dialog.exec();
    });
    settingsMenu->addAction(tr("检查更新…"), this, [this] { UpdateDialog dialog(this); dialog.exec(); });
    QMenu* associationsMenu = settingsMenu->addMenu(tr("文件关联(&A)"));
    associationsMenu->addAction(tr("注册到「打开方式」..."), this, [this] {
        if (QMessageBox::question(this, tr("注册文件关联"),
                tr("将 ggZip 添加到压缩包的「打开方式」和系统默认应用列表。\n"
                "不会修改现有默认应用；选为默认后，压缩包图标和双击打开才会使用 ggZip。\n\n"
                "是否继续？")) != QMessageBox::Yes) return;
        try {
            ggzip::registerFileAssociations(QApplication::applicationFilePath().toStdWString());
            QMessageBox::information(this, tr("文件关联"),
                tr("已注册。请通过「设置 → 文件关联 → 系统默认应用」自行选择 ggZip。"));
        } catch (const std::exception& error) {
            QMessageBox::critical(this, tr("注册失败"), QString::fromUtf8(error.what()));
        }
    });
    associationsMenu->addAction(tr("移除 ggZip 文件关联..."), this, [this] {
        if (QMessageBox::question(this, tr("移除文件关联"),
                tr("仅移除本程序注册的关联，不修改其他程序设置。\n"
                "若 ggZip 已是默认应用，移除后可能需要重新选择默认应用。\n\n是否继续？"))
            != QMessageBox::Yes) return;
        try {
            ggzip::unregisterFileAssociations(QApplication::applicationFilePath().toStdWString());
            QMessageBox::information(this, tr("文件关联"), tr("已完成清理；其他安装副本的关联会保留。"));
        } catch (const std::exception& error) {
            QMessageBox::critical(this, tr("移除失败"), QString::fromUtf8(error.what()));
        }
    });
    associationsMenu->addAction(tr("系统默认应用..."), this, [this] {
        if (!QDesktopServices::openUrl(QUrl("ms-settings:defaultapps")))
            QMessageBox::warning(this, tr("默认应用"), tr("无法打开系统设置，请在 Windows 设置中打开「默认应用」。"));
    });

    auto* toolBar = addToolBar(tr("主工具栏"));
    toolBar->setObjectName("mainToolbar");
    toolBar->setMovable(false);
    toolBar->addAction(actOpen);
    toolBar->addAction(actExtract_);
    toolBar->addAction(actExtractSelected_);
    toolBar->addAction(actSmartExtract_);
    toolBar->addAction(actCreate_);

    // ---- 导航栏（上级目录 + 当前路径）----
    actUp_ = new QAction(style()->standardIcon(QStyle::SP_ArrowBack), tr("上级目录"), this);
    actUp_->setShortcut(QKeySequence("Alt+Up"));
    actUp_->setEnabled(false);
    connect(actUp_, &QAction::triggered, this, &MainWindow::goUp);
    pathLabel_ = new QLabel(tr("(根目录)"));
    pathLabel_->setTextFormat(Qt::PlainText);
    pathLabel_->setObjectName("archivePath");
    auto* navBar = addToolBar(tr("导航"));
    navBar->setObjectName("navigationToolbar");
    navBar->setMovable(false);
    navBar->addAction(actUp_);
    navBar->addWidget(pathLabel_);
    searchEdit_ = new QLineEdit;
    searchEdit_->setObjectName("archiveSearch");
    searchEdit_->setPlaceholderText(tr("按名称搜索（当前目录）"));
    searchEdit_->setClearButtonEnabled(true);
    searchAll_ = new QCheckBox(tr("全包搜索"));
    searchAll_->setObjectName("searchWholeArchive");
    navBar->addWidget(searchEdit_);
    navBar->addWidget(searchAll_);
    auto search = [this] {
        model_->setSearch(searchEdit_->text(), searchAll_->isChecked());
        updateActionStates();
    };
    connect(searchEdit_, &QLineEdit::textChanged, this, search);
    connect(searchAll_, &QCheckBox::toggled, this, search);

    // ---- 状态栏 / 进度对话框 ----
    statusLabel_ = new QLabel(tr("把压缩包拖进窗口，或使用 文件-打开压缩包"));
    statusLabel_->setTextFormat(Qt::PlainText);
    statusBar()->addWidget(statusLabel_, 1);

    progress_ = new QProgressDialog(this);
    progress_->setWindowModality(Qt::WindowModal);
    progress_->setAutoClose(false);
    progress_->setAutoReset(false);
    progress_->setRange(0, 100);
    progress_->setMinimumDuration(300);
    progress_->setLabelText(tr("正在处理…"));
    connect(progress_, &QProgressDialog::canceled, this, [this] {
        runner_->requestCancel();
        if (activeConflict_) activeConflict_->reject();
        progress_->setLabelText(tr("正在取消…"));
    });

    connect(runner_, &ProgressRunner::progress, this, [this](int pct) {
        progress_->setValue(pct);
    });
    connect(runner_, &ProgressRunner::finished, this,
            &MainWindow::onOpFinished);

    // 双击：目录进入 / 文件预览或打开
    connect(table_, &QTableView::doubleClicked, this, &MainWindow::onDoubleClicked);
    // 选中统计
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection&, const QItemSelection&) {
                const QModelIndexList sel = table_->selectionModel()->selectedRows();
                updateActionStates();
                if (sel.isEmpty()) { statusLabel_->setText(baseStatus_); return; }
                quint64 total = 0;
                for (const QModelIndex& r : sel) {
                    if (const ggzip::Entry* e = model_->entryAt(r.row()))
                        total += e->size;
                }
                statusLabel_->setText(tr("已选 %1 项，合计 %2")
                                          .arg(sel.size())
                                          .arg(ggzHumanSize(total)));
            });

    setAcceptDrops(true);
    setWindowTitle("ggZip");
    const QSettings settings;
    resize(920, 620);
    restoreGeometry(settings.value("window/geometry").toByteArray());
    restoreState(settings.value("window/state").toByteArray());
    table_->horizontalHeader()->restoreState(settings.value("window/columns").toByteArray());
    updateActionStates();
}

MainWindow::~MainWindow() {
    runner_->requestCancel();
    runner_->wait();
    clearPassword();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (runner_->isRunning()) {
        closePending_ = true;
        runner_->requestCancel();
        if (activeConflict_) activeConflict_->reject();
        event->ignore();
    } else {
        saveWindowSettings();
        QMainWindow::closeEvent(event);
    }
}

void MainWindow::saveWindowSettings() {
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/state", saveState());
    settings.setValue("window/columns", table_->horizontalHeader()->saveState());
}

void MainWindow::clearPassword() {
    sessionPassword_.detach();
    sessionPassword_.fill(QChar(0));
    sessionPassword_.clear();
}

bool MainWindow::requestArchivePassword() {
    ArchivePasswordDialog dialog(currentArchive_, this);
    if (dialog.exec() != QDialog::Accepted) return false;
    clearPassword();
    sessionPassword_ = dialog.password();
    return true;
}

void MainWindow::refreshRecentMenu() {
    recentMenu_->clear();
    QStringList valid;
    for (const auto& path : QSettings().value("history/archives").toStringList()) {
        if (!QFileInfo(path).isFile() || valid.contains(path)) continue;
        valid.push_back(path);
        if (valid.size() == 10) break;
    }
    QSettings().setValue("history/archives", valid);
    for (const auto& path : valid) recentMenu_->addAction(path, this, [this, path] { openArchive(path); });
    recentMenu_->setEnabled(!valid.isEmpty());
}

ggzip::ConflictFn MainWindow::conflictResolver() {
    return [this](const ggzip::Conflict& conflict) {
        ggzip::ConflictDecision decision;
        auto ask = [&] {
            if (closePending_) return;
            QMessageBox dialog(QMessageBox::Question, tr("目标已存在"),
                tr("源: %1\n目标: %2\n请选择处理方式。")
                    .arg(QString::fromStdWString(conflict.source), QString::fromStdWString(conflict.target)),
                QMessageBox::NoButton, this);
            auto* overwrite = dialog.addButton(tr("覆盖"), QMessageBox::AcceptRole);
            overwrite->setEnabled(!conflict.directoryConflict);
            auto* skip = dialog.addButton(tr("跳过"), QMessageBox::ActionRole);
            auto* rename = dialog.addButton(tr("自动重命名"), QMessageBox::ActionRole);
            auto* cancel = dialog.addButton(tr("取消任务"), QMessageBox::RejectRole);
            dialog.setDefaultButton(cancel);
            auto* all = new QCheckBox(tr("本次同类冲突都这样处理"), &dialog);
            dialog.setCheckBox(all);
            activeConflict_ = &dialog;
            dialog.exec();
            activeConflict_.clear();
            decision.applyToAll = all->isChecked();
            if (dialog.clickedButton() == overwrite) decision.policy = ggzip::ConflictPolicy::Overwrite;
            else if (dialog.clickedButton() == skip) decision.policy = ggzip::ConflictPolicy::Skip;
            else if (dialog.clickedButton() == rename) decision.policy = ggzip::ConflictPolicy::Rename;
            else runner_->requestCancel();
        };
        if (QThread::currentThread() == thread()) ask();
        else QMetaObject::invokeMethod(this, ask, Qt::BlockingQueuedConnection);
        return decision;
    };
}

void MainWindow::openArchive(const QString& path) {
    if (!engine_->available()) { warnEngineMissing(); return; }
    if (runner_->isRunning()) return;

    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (QString::compare(absolute, currentArchive_, Qt::CaseInsensitive) != 0) {
        clearPassword();
        previewSession_.reset();
    }
    currentOp_ = Op::List;
    passwordRequired_ = false;
    currentArchive_ = absolute;
    model_->clear();
    statusLabel_->setText(tr("读取中: %1").arg(path));

    progress_->setLabelText(tr("读取压缩包中…"));
    progress_->setRange(0, 0);
    progress_->show();
    const std::wstring password = toW(sessionPassword_);
    std::wstring wpath = toW(absolute);
    runner_->start([this, wpath, password](const std::atomic_bool& cancel,
                                 const ProgressRunner::OnProgress&) {
        try { engine_->inspect(wpath, &pendingEntries_, {password, true, &cancel}); }
        catch (const ggzip::PasswordError&) { passwordRequired_ = true; throw; }
    });
    updateActionStates();
}

void MainWindow::startExtract(bool smartDefault, bool selectedOnly) {
    if (!engine_->available()) { warnEngineMissing(); return; }
    if (runner_->isRunning()) return;

    QString archive = currentArchive_;
    if (archive.isEmpty()) {
        archive = QFileDialog::getOpenFileName(this, tr("选择要解压的压缩包"), QString(),
                                               tr(ggzip::utf8(kFilter).c_str()));
        if (archive.isEmpty()) return;
    }

    ExtractDialog dlg(archive, smartDefault, this);
    if (archive == currentArchive_) dlg.setPassword(sessionPassword_);
    std::vector<std::wstring> selected;
    if (selectedOnly) {
        for (const auto& index : table_->selectionModel()->selectedRows()) {
            if (const auto* entry = model_->entryAt(index.row())) selected.push_back(entry->path);
        }
        if (selected.empty()) return;
        dlg.setSelectedOnly();
    }
    if (dlg.exec() != QDialog::Accepted) return;
    QSettings().setValue("paths/output", dlg.targetDir());

    currentOp_ = Op::Extract;
    summaryReady_ = false;
    opResult_.clear();
    progress_->setLabelText(tr("解压: %1").arg(QFileInfo(archive).fileName()));
    progress_->setValue(0);
    progress_->show();

    std::wstring warc = toW(archive);
    std::wstring wout = toW(dlg.targetDir());
    std::wstring wpw = toW(dlg.password());
    const bool smart = dlg.smart();
    runner_->start([this, warc, wout, wpw, smart, selected](const std::atomic_bool& cancel,
                                                 const ProgressRunner::OnProgress& prog) {
        ggzip::ExtractOptions opt;
        opt.password = wpw;
        opt.conflict = ggzip::ConflictPolicy::Ask;
        opt.resolveConflict = conflictResolver();
        ggzip::ProgressFn pfn = [&prog](const ggzip::Progress& p) { prog(p.percent); };
        try {
            summary_ = selected.empty() ? ggzip::extractSmart(*engine_, warc, wout, smart, opt, pfn, cancel) :
                engine_->extract(warc, wout, selected, opt, pfn, cancel);
            summaryReady_ = true;
            opResult_ = summary_.destination;
        } catch (const ggzip::OperationError& error) {
            summary_ = error.summary;
            summaryReady_ = true;
            throw;
        }
    });
    updateActionStates();
}

void MainWindow::startCreate() {
    if (!engine_->available()) { warnEngineMissing(); return; }
    if (runner_->isRunning()) return;

    CreateArchiveDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    QSettings().setValue("create/level", dlg.level());

    currentOp_ = Op::Create;
    summaryReady_ = false;
    opResult_.clear();
    progress_->setLabelText(tr("压缩中: %1").arg(QFileInfo(dlg.archivePath()).fileName()));
    progress_->setValue(0);
    progress_->show();

    std::wstring warc = toW(dlg.archivePath());
    std::vector<std::wstring> inputs;
    for (const QString& s : dlg.inputs()) inputs.push_back(toW(s));
    std::wstring wpw = toW(dlg.password());
    std::wstring wvol = toW(dlg.volumeSize());
    const int level = dlg.level();
    runner_->start([this, warc, inputs, wpw, wvol, level](const std::atomic_bool& cancel,
                                                    const ProgressRunner::OnProgress& prog) {
        ggzip::CreateOptions opt;
        opt.password = wpw;
        opt.level = level;
        opt.volumeSize = wvol;
        opt.conflict = ggzip::ConflictPolicy::Ask;
        opt.resolveConflict = conflictResolver();
        ggzip::ProgressFn pfn = [&prog](const ggzip::Progress& p) { prog(p.percent); };
        summary_ = engine_->create(warc, inputs, opt, pfn, cancel);
        summaryReady_ = true;
        opResult_ = summary_.destination;
    });
    updateActionStates();
}

void MainWindow::startTest() {
    if (runner_->isRunning() || currentArchive_.isEmpty()) return;
    if (model_->hasEncryptedEntries() && sessionPassword_.isEmpty() && !requestArchivePassword()) return;
    currentOp_ = Op::Test;
    passwordRequired_ = false;
    progress_->setLabelText(tr("完整性测试中…"));
    progress_->setValue(0);
    progress_->show();
    const std::wstring archive = toW(currentArchive_);
    const std::wstring password = toW(sessionPassword_);
    runner_->start([this, archive, password](const std::atomic_bool& cancel, const ProgressRunner::OnProgress& progress) {
        try { engine_->test(archive, password, [&](const ggzip::Progress& value) { progress(value.percent); }, cancel); }
        catch (const ggzip::PasswordError&) { passwordRequired_ = true; throw; }
    });
    updateActionStates();
}

void MainWindow::runShellAction(const QStringList& arguments) {
    if (!arguments.isEmpty() && arguments[0] == "batch-extract" && !runner_->isRunning()) {
        BatchDialog dialog(*engine_, arguments.mid(1), this);
        dialog.exec();
        return;
    }
    if (arguments.size() < 2 || runner_->isRunning()) return;
    const QString operation = arguments[0];
    const std::wstring archive = toW(arguments[1]);
    std::wstring target = QFileInfo(arguments[1]).absolutePath().toStdWString();
    std::vector<std::wstring> inputs;
    for (qsizetype index = 2; index < arguments.size(); ++index) {
        if (arguments[index] == "-o" && index + 1 < arguments.size()) target = toW(arguments[++index]);
        else inputs.push_back(toW(arguments[index]));
    }
    if (operation != "create" && operation != "extract" && operation != "smart-extract") {
        QMessageBox::warning(this, tr("操作无效"), operation);
        return;
    }
    currentOp_ = operation == "create" ? Op::Create : Op::Extract;
    summaryReady_ = false;
    progress_->setLabelText(tr("执行任务中…"));
    progress_->setValue(0);
    progress_->show();
    runner_->start([this, operation, archive, target, inputs](const std::atomic_bool& cancel,
        const ProgressRunner::OnProgress& progress) {
        const auto notify = [&](const ggzip::Progress& value) { progress(value.percent); };
        try {
            if (operation == "create") {
                ggzip::CreateOptions options;
                options.conflict = ggzip::ConflictPolicy::Ask;
                options.resolveConflict = conflictResolver();
                summary_ = engine_->create(archive, inputs, options, notify, cancel);
            } else {
                ggzip::ExtractOptions options;
                options.conflict = ggzip::ConflictPolicy::Ask;
                options.resolveConflict = conflictResolver();
                summary_ = ggzip::extractSmart(*engine_, archive, target, operation == "smart-extract",
                                               options, notify, cancel);
            }
            summaryReady_ = true;
        } catch (const ggzip::OperationError& error) {
            summary_ = error.summary;
            summaryReady_ = true;
            throw;
        }
    });
}

void MainWindow::onOpFinished(bool ok, const QString& error) {
    progress_->reset();
    progress_->hide();
    progress_->setRange(0, 100);

    const Op op = currentOp_;
    currentOp_ = Op::None;
    updateActionStates();
    if ((op == Op::Extract || op == Op::Create || op == Op::Edit) && summaryReady_) {
        const QString state = summary_.outcome == ggzip::Outcome::Completed ? tr("完成") :
            summary_.outcome == ggzip::Outcome::Cancelled ? tr("已取消，完整结果已保留") : tr("部分失败");
        const QString message = tr("%1：完成 %2 项，覆盖 %3 项，跳过 %4 项，重命名 %5 项\n%6")
            .arg(state).arg(summary_.completed).arg(summary_.overwritten).arg(summary_.skipped)
            .arg(summary_.renamed).arg(QString::fromStdWString(summary_.destination));
        setBaseStatus(message);
        if (!closePending_) showMessage(this, QMessageBox::Information, tr("任务结果"), message +
            (error.isEmpty() ? QString() : "\n" + error));
        if (closePending_) close();
        else if (op == Op::Edit && summary_.outcome == ggzip::Outcome::Completed) openArchive(currentArchive_);
        return;
    }
    if (closePending_) { close(); return; }

    if (!ok) {
        if (passwordRequired_ && !error.isEmpty()) {
            clearPassword();
            if (requestArchivePassword()) {
                if (op == Op::List) openArchive(currentArchive_);
                else if (op == Op::Preview) openEntryTemp(previewEntry_);
                else if (op == Op::Test) startTest();
                else if (op == Op::Edit) runEdit(pendingEdit_);
                return;
            }
            statusLabel_->setText(tr("密码输入已取消"));
            return;
        }
        statusLabel_->setText(error.isEmpty() ? tr("操作已取消") : tr("失败: %1").arg(error));
        if (!error.isEmpty())
            showMessage(this, QMessageBox::Warning, "ggZip", error);
        updateActionStates();
        return;
    }

    switch (op) {
        case Op::Preview:
            showPreview();
            break;
        case Op::Test:
            QMessageBox::information(this, tr("完整性测试"), tr("压缩包完整性测试通过。"));
            break;
        case Op::List: {
            model_->setEntries(std::move(pendingEntries_));
            pendingEntries_.clear();
            QStringList recent = QSettings().value("history/archives").toStringList();
            recent.removeAll(currentArchive_);
            recent.prepend(currentArchive_);
            while (recent.size() > 10) recent.removeLast();
            QSettings().setValue("history/archives", recent);
            refreshRecentMenu();
            const QString name = QFileInfo(currentArchive_).fileName();
            setWindowTitle("ggZip — " + name);
            setBaseStatus(tr("条目: %1    物理大小: %2")
                              .arg((int)model_->allCount())
                              .arg(ggzHumanSize(QFileInfo(currentArchive_).size())));
            updatePathUi();
            updateActionStates();
            break;
        }
        case Op::Extract:
            statusLabel_->setText(tr("解压完成: %1").arg(QString::fromStdWString(opResult_)));
            QMessageBox::information(this, "ggZip",
                                     tr("解压完成:\n%1").arg(QString::fromStdWString(opResult_)));
            break;
        case Op::Create:
            statusLabel_->setText(tr("压缩完成: %1").arg(QString::fromStdWString(opResult_)));
            QMessageBox::information(this, "ggZip",
                                     tr("已创建: %1").arg(QString::fromStdWString(opResult_)));
            break;
        case Op::None:
        case Op::Edit:
            break;
    }
}

void MainWindow::onDoubleClicked(const QModelIndex& index) {
    if (!index.isValid() || runner_->isRunning()) return;
    const ggzip::Entry* e = model_->entryAt(index.row());
    if (!e) return;
    if (e->isDir) {
        searchEdit_->clear();
        model_->setPrefix(QString::fromStdWString(e->path));  // 目录路径以 '/' 结尾
        updatePathUi();
    } else {
        openEntryTemp(*e);
    }
}

void MainWindow::openEntryTemp(const ggzip::Entry& e) {
    if (!engine_->available()) { warnEngineMissing(); return; }
    if (currentArchive_.isEmpty()) return;
    if (runner_->isRunning()) return;
    if (e.encrypted && sessionPassword_.isEmpty() && !requestArchivePassword()) return;
    if (e.size > QSettings().value("preview/maxBytes", 200ull * 1024 * 1024).toULongLong()) {
        QMessageBox::information(this, "ggZip", tr("文件超过当前预览大小限制，请解压后打开，或在设置中明确提高上限。"));
        return;
    }

    if (e.isLink) {
        QMessageBox::warning(this, "ggZip", tr("不允许预览链接或重解析点条目。"));
        return;
    }

    try {
        if (!previewSession_) previewSession_ = std::make_unique<PreviewSession>();
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("预览不可用"), QString::fromUtf8(error.what()));
        return;
    }
    currentOp_ = Op::Preview;
    passwordRequired_ = false;
    previewEntry_ = e;
    opResult_.clear();
    previewImage_ = {};
    progress_->setLabelText(tr("安全提取预览中…"));
    progress_->setValue(0);
    progress_->show();
    const std::wstring archive = toW(currentArchive_);
    const std::wstring member = e.path;
    const std::wstring password = toW(sessionPassword_);
    const auto milliseconds = QSettings().value("preview/timeoutMs", 30000).toULongLong();
    const auto pixels = QSettings().value("preview/maxPixels", 64000000).toULongLong();
    const auto bytes = QSettings().value("preview/maxBytes", 200ull * 1024 * 1024).toULongLong();
    runner_->start([this, archive, member, password, milliseconds, pixels, bytes](const std::atomic_bool& cancel,
        const ProgressRunner::OnProgress&) {
        ggzip::ExtractOptions options;
        options.password = password;
        options.deadlineTick = GetTickCount64() + milliseconds;
        const QString output = previewSession_->extract(member, [&](const ggzip::DataFn& sink) {
            try { engine_->extractToStream(archive, member, options, sink, cancel); }
            catch (const ggzip::PasswordError&) { passwordRequired_ = true; throw; }
        }, bytes);
        try {
            ggzip::applySourceZone(output.toStdWString(), ggzip::sourceZone(archive));
            if (isImageFile(QFileInfo(output).fileName()))
                previewImage_ = decodePreviewImage(output, pixels, options.deadlineTick, cancel);
            opResult_ = output.toStdWString();
        } catch (...) { previewSession_->discard(output); throw; }
    });
    updateActionStates();
}

void MainWindow::showPreview() {
    const QString outPath = QString::fromStdWString(opResult_);
    const QString name = QFileInfo(outPath).fileName();
    if (isImageFile(name)) {
        QPixmap pix = QPixmap::fromImage(previewImage_);
        if (pix.isNull()) {
            QMessageBox::information(this, "ggZip", tr("该文件不是可预览的图片。"));
        } else {
            PreviewDialog dlg(pix, name, this);
            dlg.exec();
        }
    } else if (isTextFile(name)) {
        QFile f(outPath);
        const QByteArray raw =
            f.open(QIODevice::ReadOnly) ? f.read(100 * 1024) : QByteArray();
        f.close();
        if (f.error() != QFileDevice::NoError) {
            QMessageBox::information(this, "ggZip", tr("无法读取该文件内容。"));
        } else {
            TextPreviewDialog dlg(ggzDecodeTextPreview(raw, QFileInfo(outPath).size() > raw.size()), name,
                                  QFileInfo(outPath).size() > (qint64)raw.size(), this);
            dlg.exec();
        }
    } else {
        if (QMessageBox::warning(this, tr("打开压缩包内文件"),
                tr("此文件将交给系统程序打开，可能运行代码或访问外部资源。\n"
                   "仅打开你信任的文件：%1\n是否继续？").arg(name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            previewSession_->discard(outPath);
            return;
        }
        // 用系统默认程序打开；临时文件保留到程序退出时统一清理
        ShellExecuteW(nullptr, L"open", toW(outPath).c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
        return;
    }
    previewSession_->discard(outPath);
}

void MainWindow::goUp() {
    const QString p = model_->prefix();
    if (p.isEmpty()) return;
    QString t = p;
    t.chop(1);  // 去掉结尾 '/'
    const int slash = int(t.lastIndexOf('/'));
    model_->setPrefix(slash < 0 ? QString() : t.left(slash + 1));
    updatePathUi();
}

void MainWindow::updatePathUi() {
    pathLabel_->setText(model_->displayPath());
    actUp_->setEnabled(!model_->prefix().isEmpty());
}

void MainWindow::setBaseStatus(const QString& text) {
    baseStatus_ = text;
    statusLabel_->setText(text);
}

void MainWindow::warnEngineMissing() {
    QMessageBox::warning(this, "ggZip",
                         tr("内置 7-Zip 引擎文件缺失。\n\n"
                         "请重新安装或完整解压 ggZip，保留程序旁的 7z 子目录。\n"
                         "源码开发时可设置 GGZIP_7Z 指向自备的 7z.exe。"));
}

void MainWindow::updateActionStates() {
    const bool hasArchive = !currentArchive_.isEmpty();
    const bool available = hasArchive && !runner_->isRunning();
    actExtract_->setEnabled(available);
    actSmartExtract_->setEnabled(available);
    actExtractSelected_->setEnabled(available && table_->selectionModel()->hasSelection());
    actCreate_->setEnabled(!runner_->isRunning());
    const auto suffix = QFileInfo(currentArchive_).suffix().toLower();
    const bool editable = available && (suffix == "zip" || suffix == "7z");
    const auto selected = table_->selectionModel()->selectedRows().size();
    actAddMembers_->setEnabled(editable);
    actAddFolder_->setEnabled(editable);
    actDeleteMembers_->setEnabled(editable && selected > 0);
    actRenameMember_->setEnabled(editable && selected == 1);
    actTest_->setEnabled(hasArchive && !runner_->isRunning());
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.size() > 1 && !runner_->isRunning()) {
        QStringList archives;
        for (const auto& url : urls) if (url.isLocalFile()) archives.push_back(url.toLocalFile());
        BatchDialog dialog(*engine_, archives, this);
        dialog.exec();
        return;
    }
    for (const QUrl& u : urls) {
        const QString local = u.toLocalFile();
        if (!local.isEmpty()) {
            openArchive(local);
            break;
        }
    }
}

void MainWindow::startEdit(int operation) {
    if (runner_->isRunning() || currentArchive_.isEmpty()) return;
    const auto suffix = QFileInfo(currentArchive_).suffix().toLower();
    if (suffix != "zip" && suffix != "7z") {
        QMessageBox::information(this, tr("编辑压缩包"), tr("仅支持编辑 ZIP/7z，其他格式和分卷保持只读。"));
        return;
    }
    if (model_->hasEncryptedEntries() && sessionPassword_.isEmpty() && !requestArchivePassword()) return;
    ggzip::EditOptions options;
    options.password = toW(sessionPassword_);
    options.conflict = ggzip::ConflictPolicy::Ask;
    options.resolveConflict = conflictResolver();
    const auto selected = table_->selectionModel()->selectedRows();
    if (operation == 1) {
        if (selected.isEmpty()) return;
        if (QMessageBox::question(this, tr("编辑压缩包"),
            tr("是否永久删除选中的 %1 个条目（包含目录后代）？").arg(selected.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        for (const auto& index : selected) if (const auto* entry = model_->entryAt(index.row()))
            options.remove.push_back(entry->path);
    } else if (operation == 2) {
        if (selected.size() != 1) return;
        const auto* entry = model_->entryAt(selected.front().row());
        if (!entry) return;
        bool accepted = false;
        const auto name = QInputDialog::getText(this, tr("编辑压缩包"), tr("新条目路径:"),
            QLineEdit::Normal, QString::fromStdWString(entry->path), &accepted);
        if (!accepted || name.isEmpty()) return;
        options.rename.push_back({entry->path, toW(name)});
    } else if (operation == 3) {
        const auto folder = QFileDialog::getExistingDirectory(this, tr("选择文件夹"));
        if (folder.isEmpty()) return;
        options.add.push_back({toW(folder), toW(model_->prefix() + QFileInfo(folder).fileName())});
    } else {
        const auto files = QFileDialog::getOpenFileNames(this, tr("选择文件"));
        if (files.isEmpty()) return;
        for (const auto& file : files) options.add.push_back({toW(file), toW(model_->prefix() + QFileInfo(file).fileName())});
    }
    pendingEdit_ = options;
    pendingEdit_.password.clear();
    runEdit(std::move(options));
}

void MainWindow::runEdit(ggzip::EditOptions options) {
    options.password = toW(sessionPassword_);
    currentOp_ = Op::Edit;
    passwordRequired_ = false;
    summaryReady_ = false;
    progress_->setLabelText(tr("安全编辑中…"));
    progress_->setValue(0);
    progress_->show();
    const auto archive = toW(currentArchive_);
    runner_->start([this, archive, options](const std::atomic_bool& cancel, const ProgressRunner::OnProgress& progress) {
        try {
            summary_ = engine_->edit(archive, options, [&](const ggzip::Progress& value) { progress(value.percent); }, cancel);
            summaryReady_ = true;
        } catch (const ggzip::PasswordError&) { passwordRequired_ = true; throw; }
        catch (const ggzip::OperationError& error) { summary_ = error.summary; summaryReady_ = true; throw; }
    });
    updateActionStates();
}
