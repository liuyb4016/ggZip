#include "BatchDialog.h"
#include "ggzip/SmartExtract.h"
#include "ggzip/ArchiveVolumes.h"
#include "ggzip/ArchiveError.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QDir>
#include <QThread>

BatchDialog::BatchDialog(ggzip::IEngine& engine, const QStringList& initial, QWidget* parent)
    : QDialog(parent), engine_(engine), inputs_(initial) {
    setWindowTitle(tr("批量压缩/解压"));
    resize(900, 540);
    table_ = new QTableWidget(0, 5);
    table_->setHorizontalHeaderLabels({tr("源"), tr("目标"), tr("状态"), tr("进度"), tr("结果")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    output_ = new QLineEdit(QSettings().value("paths/output", QDir::homePath()).toString());
    password_ = new QLineEdit;
    password_->setEchoMode(QLineEdit::Password);
    mode_ = new QComboBox;
    mode_->addItems({tr("逐包解压到独立目录"), tr("逐项创建 ZIP"), tr("逐项创建 7z")});
    continue_ = new QCheckBox(tr("失败后继续后续任务"));
    auto* form = new QFormLayout;
    form->addRow(tr("模式:"), mode_);
    form->addRow(tr("输出目录:"), output_);
    form->addRow(tr("本次队列密码（不保存）:"), password_);
    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(tr("添加文件..."));
    auto* folder = new QPushButton(tr("添加文件夹..."));
    auto* browse = new QPushButton(tr("浏览..."));
    auto* start = new QPushButton(tr("开始/继续"));
    auto* cancel = new QPushButton(tr("取消选中任务"));
    auto* all = new QPushButton(tr("取消整个队列"));
    buttons->addWidget(add);
    buttons->addWidget(folder);
    buttons->addWidget(browse);
    buttons->addWidget(start);
    buttons->addWidget(cancel);
    buttons->addWidget(all);
    connect(add, &QPushButton::clicked, this, &BatchDialog::addInputs);
    connect(folder, &QPushButton::clicked, this, [this] {
        if (prepared_) return;
        const auto selected = QFileDialog::getExistingDirectory(this, tr("选择文件夹"));
        if (!selected.isEmpty()) inputs_.push_back(selected);
        refresh();
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        if (prepared_) return;
        const auto selected = QFileDialog::getExistingDirectory(this, tr("选择输出目录"), output_->text());
        if (!selected.isEmpty()) output_->setText(selected);
    });
    connect(start, &QPushButton::clicked, this, &BatchDialog::startQueue);
    connect(cancel, &QPushButton::clicked, this, [this] {
        QSet<int> rows;
        bool activeSelected = false;
        for (const auto& index : table_->selectionModel()->selectedRows()) rows.insert(index.row());
        for (const auto row : rows) {
            if (static_cast<std::size_t>(row) < queue_.tasks().size() &&
                queue_.tasks()[static_cast<std::size_t>(row)].state == TaskQueue::State::Running) activeSelected = true;
            queue_.cancel(static_cast<std::size_t>(row));
        }
        if (activeSelected && conflict_) conflict_->reject();
    });
    connect(all, &QPushButton::clicked, this, [this] { queue_.cancelAll(); if (conflict_) conflict_->reject(); });
    connect(&queue_, &TaskQueue::changed, this, &BatchDialog::refresh);
    connect(&queue_, &TaskQueue::idle, this, [this] { if (closing_) QDialog::reject(); });
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("默认串行执行。每个压缩包使用独立目标目录，取消不删除已验证的完整结果。")));
    layout->addLayout(form);
    layout->addWidget(continue_);
    layout->addWidget(table_);
    layout->addLayout(buttons);
    refresh();
}

void BatchDialog::addInputs() {
    if (prepared_) return;
    inputs_.append(QFileDialog::getOpenFileNames(this, tr("选择文件")));
    inputs_.removeDuplicates();
    refresh();
}

ggzip::ConflictDecision BatchDialog::ask(const ggzip::Conflict& conflict) {
    ggzip::ConflictDecision decision;
    QMetaObject::invokeMethod(this, [&] {
        if (closing_) return;
        QMessageBox dialog(QMessageBox::Question, tr("目标已存在"), QString::fromStdWString(conflict.target),
                           QMessageBox::NoButton, this);
        auto* overwrite = dialog.addButton(tr("覆盖"), QMessageBox::AcceptRole);
        overwrite->setEnabled(!conflict.directoryConflict);
        auto* skip = dialog.addButton(tr("跳过"), QMessageBox::ActionRole);
        auto* rename = dialog.addButton(tr("自动重命名"), QMessageBox::ActionRole);
        auto* stop = dialog.addButton(tr("取消任务"), QMessageBox::RejectRole);
        dialog.setDefaultButton(stop);
        auto* all = new QCheckBox(tr("本次同类冲突都这样处理"), &dialog);
        dialog.setCheckBox(all);
        conflict_ = &dialog;
        dialog.exec();
        conflict_.clear();
        decision.applyToAll = all->isChecked();
        if (dialog.clickedButton() == overwrite) decision.policy = ggzip::ConflictPolicy::Overwrite;
        else if (dialog.clickedButton() == skip) decision.policy = ggzip::ConflictPolicy::Skip;
        else if (dialog.clickedButton() == rename) decision.policy = ggzip::ConflictPolicy::Rename;
        else {
            for (std::size_t index = 0; index < queue_.tasks().size(); ++index)
                if (queue_.tasks()[index].state == TaskQueue::State::Running) queue_.cancel(index);
        }
    }, Qt::BlockingQueuedConnection);
    return decision;
}

void BatchDialog::startQueue() {
    if (queue_.busy()) return;
    if (!prepared_) {
        if (inputs_.isEmpty() || output_->text().trimmed().isEmpty()) return;
        QSet<QString> reserved;
        const QString parent = QDir(output_->text()).absolutePath();
        const auto password = password_->text().toStdWString();
        const int mode = mode_->currentIndex();
        QStringList taskInputs;
        QSet<QString> volumeGroups;
        for (const auto& source : inputs_) {
            const QFileInfo info(source);
            if (mode == 0 && !info.isFile()) {
                QMessageBox::warning(this, tr("批量压缩/解压"), tr("解压输入必须为压缩包文件。"));
                return;
            }
            QString canonical = source;
            if (mode == 0) {
                try { canonical = QString::fromStdWString(ggzip::archiveVolumes(source.toStdWString()).first.wstring()); }
                catch (const ggzip::ArchiveError&) {}
                const auto key = QFileInfo(canonical).absoluteFilePath().toCaseFolded();
                if (volumeGroups.contains(key)) continue;
                volumeGroups.insert(key);
            }
            taskInputs.push_back(canonical);
        }
        prepared_ = true;
        for (const auto& source : taskInputs) {
            const QFileInfo info(source);
            QString base = info.completeBaseName();
            if (mode == 0) {
                try { base = QString::fromStdWString(ggzip::archiveVolumes(source.toStdWString()).baseName); }
                catch (const ggzip::ArchiveError&) {}
            }
            if (base.isEmpty()) base = "extracted";
            const QString suffix = mode == 1 ? ".zip" : mode == 2 ? ".7z" : QString();
            QString target = QDir(parent).filePath(base + suffix);
            for (int index = 1; QFileInfo::exists(target) || reserved.contains(target.toCaseFolded()); ++index)
                target = QDir(parent).filePath(base + " (" + QString::number(index) + ")" + suffix);
            reserved.insert(target.toCaseFolded());
            TaskQueue::Task task;
            task.title = info.absoluteFilePath();
            task.target = target;
            const auto resolver = [this](const ggzip::Conflict& conflict) { return ask(conflict); };
            task.work = [this, source = info.absoluteFilePath().toStdWString(), target = target.toStdWString(),
                         mode, password, resolver](const std::atomic_bool& cancel, const ProgressRunner::OnProgress& progress) {
                const auto notify = [&](const ggzip::Progress& value) { progress(value.percent); };
                if (mode == 0) {
                    ggzip::ExtractOptions options;
                    options.password = password;
                    options.conflict = ggzip::ConflictPolicy::Ask;
                    options.resolveConflict = resolver;
                    return engine_.extract(source, target, {}, options, notify, cancel);
                }
                ggzip::CreateOptions options;
                options.password = password;
                options.conflict = ggzip::ConflictPolicy::Ask;
                options.resolveConflict = resolver;
                return engine_.create(target, {source}, options, notify, cancel);
            };
            queue_.add(std::move(task));
        }
        password_->clear();
        password_->setEnabled(false);
        output_->setEnabled(false);
        mode_->setEnabled(false);
    }
    queue_.start(continue_->isChecked());
}

void BatchDialog::refresh() {
    const auto& tasks = queue_.tasks();
    table_->setRowCount(prepared_ ? static_cast<int>(tasks.size()) : inputs_.size());
    const QStringList states{tr("等待"), tr("执行中"), tr("完成"), tr("已取消"), tr("失败")};
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (!prepared_) {
            table_->setItem(row, 0, new QTableWidgetItem(inputs_[row]));
            continue;
        }
        const auto& task = tasks[static_cast<std::size_t>(row)];
        table_->setItem(row, 0, new QTableWidgetItem(task.title));
        table_->setItem(row, 1, new QTableWidgetItem(task.target));
        table_->setItem(row, 2, new QTableWidgetItem(states[static_cast<int>(task.state)]));
        table_->setItem(row, 3, new QTableWidgetItem(QString::number(task.percent) + "%"));
        const QString result = tr("完成 %1，跳过 %2").arg(task.result.completed).arg(task.result.skipped) +
            (task.error.isEmpty() ? QString() : "\n" + task.error);
        table_->setItem(row, 4, new QTableWidgetItem(result));
    }
}

void BatchDialog::closeEvent(QCloseEvent* event) {
    if (queue_.busy()) {
        closing_ = true;
        queue_.cancelAll();
        if (conflict_) conflict_->reject();
        event->ignore();
    } else QDialog::closeEvent(event);
}

void BatchDialog::reject() {
    if (queue_.busy()) {
        closing_ = true;
        queue_.cancelAll();
        if (conflict_) conflict_->reject();
    } else QDialog::reject();
}
