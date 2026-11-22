#include "Dialogs.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

// ---------- ExtractDialog ----------

ExtractDialog::ExtractDialog(const QString& archivePath, bool smartDefault,
                             QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("解压压缩包");
    setMinimumWidth(460);

    dirEdit_ = new QLineEdit(QFileInfo(archivePath).absolutePath());
    pwEdit_ = new QLineEdit;
    pwEdit_->setEchoMode(QLineEdit::Password);
    pwEdit_->setPlaceholderText("加密包才需要，留空表示无密码");
    smartCheck_ = new QCheckBox("智能解压（单根目录直接解压，否则自动建子文件夹）");
    smartCheck_->setChecked(smartDefault);

    auto* browse = new QPushButton("浏览...");
    connect(browse, &QPushButton::clicked, this, [this] {
        QString dir = QFileDialog::getExistingDirectory(this, "选择解压目标目录",
                                                        dirEdit_->text());
        if (!dir.isEmpty()) dirEdit_->setText(dir);
    });
    auto* dirRow = new QHBoxLayout;
    dirRow->addWidget(dirEdit_, 1);
    dirRow->addWidget(browse);

    auto* form = new QFormLayout;
    form->addRow("源压缩包:", new QLabel(QFileInfo(archivePath).fileName()));
    form->addRow("目标目录:", dirRow);
    form->addRow("密码:", pwEdit_);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(dirEdit_, &QLineEdit::textChanged, this, [buttons](const QString& t) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!t.trimmed().isEmpty());
    });

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(smartCheck_);
    layout->addWidget(buttons);
}

QString ExtractDialog::targetDir() const { return dirEdit_->text().trimmed(); }
QString ExtractDialog::password() const { return pwEdit_->text(); }
bool ExtractDialog::smart() const { return smartCheck_->isChecked(); }

// ---------- CreateDialog ----------

namespace {
const wchar_t* kSaveFilter =
    L"7z (*.7z);;zip (*.zip);;tar (*.tar);;tar.gz (*.tar.gz);;tar.bz2 (*.tar.bz2);;"
    L"tar.xz (*.tar.xz);;gzip (*.gz);;xz (*.xz);;所有文件 (*)";
} // namespace

CreateDialog::CreateDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("新建压缩包");
    setMinimumWidth(520);

    inputList_ = new QListWidget;
    inputList_->setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto* addFilesBtn = new QPushButton("添加文件...");
    auto* addFolderBtn = new QPushButton("添加文件夹...");
    auto* removeBtn = new QPushButton("移除选中");
    connect(addFilesBtn, &QPushButton::clicked, this, &CreateDialog::addFiles);
    connect(addFolderBtn, &QPushButton::clicked, this, &CreateDialog::addFolder);
    connect(removeBtn, &QPushButton::clicked, this, &CreateDialog::removeSelected);
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(addFilesBtn);
    btnRow->addWidget(addFolderBtn);
    btnRow->addWidget(removeBtn);
    btnRow->addStretch(1);

    outEdit_ = new QLineEdit;
    auto* outBrowse = new QPushButton("浏览...");
    connect(outBrowse, &QPushButton::clicked, this, [this] {
        QString p = QFileDialog::getSaveFileName(this, "选择压缩包保存位置",
                                                 outEdit_->text(),
                                                 QString::fromWCharArray(kSaveFilter));
        if (!p.isEmpty()) outEdit_->setText(p);
    });
    auto* outRow = new QHBoxLayout;
    outRow->addWidget(outEdit_, 1);
    outRow->addWidget(outBrowse);

    pwEdit_ = new QLineEdit;
    pwEdit_->setEchoMode(QLineEdit::Password);
    pwEdit_->setPlaceholderText("仅 7z/zip 支持，留空表示不加密");

    levelSpin_ = new QSpinBox;
    levelSpin_->setRange(1, 9);
    levelSpin_->setValue(5);
    levelSpin_->setToolTip("1=最快 9=最小");

    auto* form = new QFormLayout;
    form->addRow("保存为:", outRow);
    form->addRow("密码:", pwEdit_);
    form->addRow("压缩级别:", levelSpin_);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(outEdit_, &QLineEdit::textChanged, this, [buttons](const QString& t) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!t.trimmed().isEmpty());
    });

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("要压缩的文件 / 文件夹:"));
    layout->addWidget(inputList_, 1);
    layout->addLayout(btnRow);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void CreateDialog::addFiles() {
    const QStringList files = QFileDialog::getOpenFileNames(this, "选择文件");
    for (const QString& f : files)
        if (!f.isEmpty()) inputList_->addItem(f);
}

void CreateDialog::addFolder() {
    const QString dir = QFileDialog::getExistingDirectory(this, "选择文件夹");
    if (!dir.isEmpty()) inputList_->addItem(dir + (dir.endsWith('/') ? "*" : "/*"));
}

void CreateDialog::removeSelected() {
    const auto selected = inputList_->selectedItems();
    for (QListWidgetItem* item : selected)
        delete item;
}

QStringList CreateDialog::inputs() const {
    QStringList result;
    for (int i = 0; i < inputList_->count(); ++i)
        result << inputList_->item(i)->text();
    return result;
}
QString CreateDialog::archivePath() const { return outEdit_->text().trimmed(); }
QString CreateDialog::password() const { return pwEdit_->text(); }
int CreateDialog::level() const { return levelSpin_->value(); }
