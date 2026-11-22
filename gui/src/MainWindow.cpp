#include "MainWindow.h"
#include "ArchiveTableModel.h"
#include "Dialogs.h"
#include "ProgressRunner.h"
#include "ggzip/ArchiveError.h"
#include "ggzip/EngineFactory.h"
#include "ggzip/IEngine.h"
#include "ggzip/SmartExtract.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
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
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTableView>
#include <QToolBar>
#include <qt_windows.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <functional>

namespace {

std::wstring toW(const QString& s) { return s.toStdWString(); }

const wchar_t* kFilter =
    L"压缩包 (*.zip *.7z *.rar *.tar *.gz *.tgz *.bz2 *.tbz *.xz *.txz *.zst *.tzst "
    L"*.iso *.cab *.wim *.msi *.jar *.apk);;所有文件 (*.*)";

bool isImageFile(const QString& name) {
    static const QStringList kExt = { "png", "jpg", "jpeg", "gif",
                                      "bmp", "webp", "ico" };
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
    table_->setAlternatingRowColors(true);
    table_->setWordWrap(false);
    table_->setSortingEnabled(true);   // 点击表头排序（模型 sort() 覆盖实现）
    setCentralWidget(table_);

    runner_ = new ProgressRunner(this);

    // ---- 菜单 / 工具栏 ----
    QMenu* fileMenu = menuBar()->addMenu("文件(&F)");

    QAction* actOpen =
        new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), "打开压缩包(&O)...", this);
    actOpen->setShortcut(QKeySequence::Open);
    connect(actOpen, &QAction::triggered, this, [this] {
        QString p = QFileDialog::getOpenFileName(this, "打开压缩包", QString(),
                                                 QString::fromWCharArray(kFilter));
        if (!p.isEmpty()) openArchive(p);
    });
    fileMenu->addAction(actOpen);
    fileMenu->addSeparator();

    actExtract_ = new QAction(style()->standardIcon(QStyle::SP_ArrowDown), "解压到(&E)...", this);
    connect(actExtract_, &QAction::triggered, this, [this] { startExtract(false); });

    actSmartExtract_ = new QAction("智能解压到(&S)...", this);
    actSmartExtract_->setShortcut(QKeySequence("Ctrl+S"));
    connect(actSmartExtract_, &QAction::triggered, this, [this] { startExtract(true); });

    actCreate_ = new QAction(style()->standardIcon(QStyle::SP_DialogSaveButton), "新建压缩包(&C)...", this);
    actCreate_->setShortcut(QKeySequence::Save);
    connect(actCreate_, &QAction::triggered, this, [this] { startCreate(); });

    fileMenu->addAction(actExtract_);
    fileMenu->addAction(actSmartExtract_);
    fileMenu->addAction(actCreate_);
    fileMenu->addSeparator();

    QAction* actQuit = new QAction("退出(&X)", this);
    actQuit->setShortcut(QKeySequence::Quit);
    connect(actQuit, &QAction::triggered, this, &MainWindow::close);
    fileMenu->addAction(actQuit);

    QMenu* settingsMenu = menuBar()->addMenu("设置(&O)");
    QAction* actPwMgr = new QAction("密码管理器(&P)...", this);
    connect(actPwMgr, &QAction::triggered, this, [this] {
        PasswordManagerDialog dlg(this);
        dlg.exec();
    });
    settingsMenu->addAction(actPwMgr);

    auto* toolBar = addToolBar("主工具栏");
    toolBar->setMovable(false);
    toolBar->addAction(actOpen);
    toolBar->addAction(actExtract_);
    toolBar->addAction(actSmartExtract_);
    toolBar->addAction(actCreate_);

    // ---- 导航栏（上级目录 + 当前路径）----
    actUp_ = new QAction(style()->standardIcon(QStyle::SP_ArrowBack), "上级目录", this);
    actUp_->setShortcut(QKeySequence("Alt+Up"));
    actUp_->setEnabled(false);
    connect(actUp_, &QAction::triggered, this, &MainWindow::goUp);
    pathLabel_ = new QLabel("(根目录)");
    auto* navBar = addToolBar("导航");
    navBar->setMovable(false);
    navBar->addAction(actUp_);
    navBar->addWidget(pathLabel_);

    // ---- 状态栏 / 进度对话框 ----
    statusLabel_ = new QLabel("把压缩包拖进窗口，或使用 文件-打开压缩包");
    statusBar()->addWidget(statusLabel_, 1);

    progress_ = new QProgressDialog(this);
    progress_->setWindowModality(Qt::WindowModal);
    progress_->setAutoClose(false);
    progress_->setAutoReset(false);
    progress_->setRange(0, 100);
    progress_->setMinimumDuration(300);
    progress_->setLabelText("正在处理…");
    connect(progress_, &QProgressDialog::canceled, this, [this] {
        runner_->requestCancel();
        progress_->setLabelText("正在取消…");
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
                if (sel.isEmpty()) { statusLabel_->setText(baseStatus_); return; }
                quint64 total = 0;
                for (const QModelIndex& r : sel) {
                    if (const ggzip::Entry* e = model_->entryAt(r.row()))
                        total += e->size;
                }
                statusLabel_->setText(QString("已选 %1 项，合计 %2")
                                          .arg(sel.size())
                                          .arg(ggzHumanSize(total)));
            });

    setAcceptDrops(true);
    setWindowTitle("ggZip");
    updateActionStates();
}

MainWindow::~MainWindow() {
    if (!previewTempDir_.isEmpty())
        QDir(previewTempDir_).removeRecursively();
}

void MainWindow::openArchive(const QString& path) {
    if (!engine_->available()) { warnEngineMissing(); return; }
    if (runner_->isRunning()) return;

    currentOp_ = Op::List;
    currentArchive_ = path;
    model_->clear();
    statusLabel_->setText("读取中: " + path);

    std::wstring wpath = toW(path);
    runner_->start([this, wpath](const std::atomic_bool&,
                                 const ProgressRunner::OnProgress&) {
        engine_->inspect(wpath, &pendingEntries_);
    });
}

void MainWindow::startExtract(bool smartDefault) {
    if (!engine_->available()) { warnEngineMissing(); return; }
    if (runner_->isRunning()) return;

    QString archive = currentArchive_;
    if (archive.isEmpty()) {
        archive = QFileDialog::getOpenFileName(this, "选择要解压的压缩包", QString(),
                                               QString::fromWCharArray(kFilter));
        if (archive.isEmpty()) return;
    }

    ExtractDialog dlg(archive, smartDefault, this);
    if (dlg.exec() != QDialog::Accepted) return;

    currentOp_ = Op::Extract;
    opResult_.clear();
    progress_->setLabelText("解压: " + QFileInfo(archive).fileName());
    progress_->setValue(0);
    progress_->show();

    std::wstring warc = toW(archive);
    std::wstring wout = toW(dlg.targetDir());
    std::wstring wpw = toW(dlg.password());
    const bool smart = dlg.smart();
    runner_->start([this, warc, wout, wpw, smart](const std::atomic_bool& cancel,
                                                 const ProgressRunner::OnProgress& prog) {
        ggzip::ExtractOptions opt;
        opt.password = wpw;
        ggzip::ProgressFn pfn = [&prog](const ggzip::Progress& p) { prog(p.percent); };
        opResult_ = ggzip::extractSmart(*engine_, warc, wout, smart, opt, pfn, cancel);
    });
}

void MainWindow::startCreate() {
    if (!engine_->available()) { warnEngineMissing(); return; }
    if (runner_->isRunning()) return;

    CreateArchiveDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    currentOp_ = Op::Create;
    opResult_.clear();
    progress_->setLabelText("压缩中: " + QFileInfo(dlg.archivePath()).fileName());
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
        ggzip::ProgressFn pfn = [&prog](const ggzip::Progress& p) { prog(p.percent); };
        engine_->create(warc, inputs, opt, pfn, cancel);
        opResult_ = warc;
    });
}

void MainWindow::onOpFinished(bool ok, const QString& error) {
    progress_->reset();
    progress_->hide();

    const Op op = currentOp_;
    currentOp_ = Op::None;

    if (!ok) {
        statusLabel_->setText(error.isEmpty() ? "操作已取消" : ("失败: " + error));
        if (!error.isEmpty())
            QMessageBox::warning(this, "ggZip", error);
        updateActionStates();
        return;
    }

    switch (op) {
        case Op::List: {
            model_->setEntries(std::move(pendingEntries_));
            pendingEntries_.clear();
            const QString name = QFileInfo(currentArchive_).fileName();
            setWindowTitle("ggZip — " + name);
            setBaseStatus(QString("条目: %1    物理大小: %2")
                              .arg((int)model_->allCount())
                              .arg(ggzHumanSize(QFileInfo(currentArchive_).size())));
            updatePathUi();
            updateActionStates();
            break;
        }
        case Op::Extract:
            statusLabel_->setText("解压完成: " + QString::fromStdWString(opResult_));
            QMessageBox::information(this, "ggZip",
                                     "解压完成:\n" + QString::fromStdWString(opResult_));
            break;
        case Op::Create:
            statusLabel_->setText("压缩完成: " + QString::fromStdWString(opResult_));
            QMessageBox::information(this, "ggZip",
                                     "已创建: " + QString::fromStdWString(opResult_));
            break;
        case Op::None:
            break;
    }
}

void MainWindow::onDoubleClicked(const QModelIndex& index) {
    if (!index.isValid() || runner_->isRunning()) return;
    const ggzip::Entry* e = model_->entryAt(index.row());
    if (!e) return;
    if (e->isDir) {
        model_->setPrefix(QString::fromStdWString(e->path));  // 目录路径以 '/' 结尾
        updatePathUi();
    } else {
        openEntryTemp(*e);
    }
}

// 提取单个条目到临时目录：图片弹预览窗口，其他文件交给系统默认程序打开
void MainWindow::openEntryTemp(const ggzip::Entry& e) {
    if (!engine_->available()) { warnEngineMissing(); return; }
    if (currentArchive_.isEmpty()) return;
    if (e.size > 200ull * 1024 * 1024) {
        QMessageBox::information(this, "ggZip", "文件过大（>200MB），请直接解压后打开。");
        return;
    }

    const QString relPath = QString::fromStdWString(e.path);
    const QString name = QFileInfo(relPath).fileName();
    const QString outPath = previewTempRoot() + "/" + relPath;

    // 7z 的 include 匹配依赖存储分隔符：先试原生 '\'，失败再试 '/'
    std::wstring nativePath = e.path;
    std::replace(nativePath.begin(), nativePath.end(), L'/', L'\\');

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    try {
        const std::wstring warc = toW(currentArchive_);
        const std::wstring wout = toW(previewTempDir_);
        ggzip::ExtractOptions opt;
        try {
            engine_->extract(warc, wout, { nativePath }, opt, {}, previewCancel_);
        } catch (const ggzip::ArchiveError&) {
            engine_->extract(warc, wout, { e.path }, opt, {}, previewCancel_);
        }
    } catch (const std::exception& ex) {
        err = QString::fromUtf8(ex.what());
    }
    QApplication::restoreOverrideCursor();

    if (!QFile::exists(outPath)) {
        QMessageBox::warning(this, "ggZip", "无法提取该文件用于预览。\n" + err);
        return;
    }

    if (isImageFile(name)) {
        QPixmap pix(outPath);
        if (pix.isNull()) {
            QMessageBox::information(this, "ggZip", "该文件不是可预览的图片。");
        } else {
            PreviewDialog dlg(pix, name, this);
            dlg.exec();
            QFile::remove(outPath);
        }
    } else {
        // 用系统默认程序打开；临时文件保留到程序退出时统一清理
        ShellExecuteW(nullptr, L"open", toW(outPath).c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
    }
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

QString MainWindow::previewTempRoot() {
    if (previewTempDir_.isEmpty()) {
        previewTempDir_ = QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                          "/ggZip_preview";
    }
    QDir().mkpath(previewTempDir_);
    return previewTempDir_;
}

void MainWindow::warnEngineMissing() {
    QMessageBox::warning(this, "ggZip",
                         "未找到 7z.exe 压缩引擎。\n\n"
                         "请安装 7-Zip（https://www.7-zip.org），或将 7z.exe 放到 "
                         "ggZip 程序同目录，或设置环境变量 GGZIP_7Z。");
}

void MainWindow::updateActionStates() {
    const bool hasArchive = !currentArchive_.isEmpty();
    actExtract_->setEnabled(hasArchive);
    actSmartExtract_->setEnabled(hasArchive);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& u : urls) {
        const QString local = u.toLocalFile();
        if (!local.isEmpty()) {
            openArchive(local);
            break;
        }
    }
}
