#include "MainWindow.h"
#include "ArchiveTableModel.h"
#include "Dialogs.h"
#include "ProgressRunner.h"
#include "ggzip/EngineFactory.h"
#include "ggzip/IEngine.h"
#include "ggzip/SmartExtract.h"
#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QStatusBar>
#include <QStyle>
#include <QTableView>
#include <QToolBar>
#include <atomic>
#include <functional>

namespace {

std::wstring toW(const QString& s) { return s.toStdWString(); }

const wchar_t* kFilter =
    L"压缩包 (*.zip *.7z *.rar *.tar *.gz *.tgz *.bz2 *.tbz *.xz *.txz *.zst *.tzst "
    L"*.iso *.cab *.wim *.msi *.jar *.apk);;所有文件 (*.*)";

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

    auto* toolBar = addToolBar("主工具栏");
    toolBar->setMovable(false);
    toolBar->addAction(actOpen);
    toolBar->addAction(actExtract_);
    toolBar->addAction(actSmartExtract_);
    toolBar->addAction(actCreate_);

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

    setAcceptDrops(true);
    setWindowTitle("ggZip");
    updateActionStates();
}

MainWindow::~MainWindow() = default;

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

    CreateDialog dlg(this);
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
    const int level = dlg.level();
    runner_->start([this, warc, inputs, wpw, level](const std::atomic_bool& cancel,
                                                    const ProgressRunner::OnProgress& prog) {
        ggzip::CreateOptions opt;
        opt.password = wpw;
        opt.level = level;
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
            statusLabel_->setText(QString("条目: %1    物理大小: %2")
                                      .arg(model_->rowCount())
                                      .arg(ggzHumanSize(QFileInfo(currentArchive_).size())));
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
