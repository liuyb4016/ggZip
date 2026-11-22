#include "Dialogs.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QMessageBox>
#include <QScreen>
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

    // 密码库下拉（有保存的密码时才显示）
    try {
        stored_ = ggzip::PasswordStore().load();
    } catch (const std::exception&) {
        stored_.clear();
    }
    if (!stored_.empty()) {
        pwPreset_ = new QComboBox;
        pwPreset_->addItem("手动输入");
        for (const auto& e : stored_)
            pwPreset_->addItem(QString::fromStdWString(e.label));
        connect(pwPreset_, &QComboBox::currentIndexChanged, this, [this](int idx) {
            if (idx > 0 && idx <= (int)stored_.size())
                pwEdit_->setText(QString::fromStdWString(stored_[(size_t)idx - 1].password));
        });
    }

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
    if (pwPreset_) form->addRow("从密码库:", pwPreset_);
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

// ---------- PasswordManagerDialog ----------

PasswordManagerDialog::PasswordManagerDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("密码管理器");
    setMinimumWidth(440);

    list_ = new QListWidget;
    auto* addBtn = new QPushButton("添加...");
    auto* removeBtn = new QPushButton("删除选中");
    connect(addBtn, &QPushButton::clicked, this, &PasswordManagerDialog::addPassword);
    connect(removeBtn, &QPushButton::clicked, this, &PasswordManagerDialog::removeSelected);
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(addBtn);
    btnRow->addWidget(removeBtn);
    btnRow->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        "密码以 DPAPI（当前用户作用域）加密存储。\n标签建议使用压缩包文件名，解压时可在下拉框快速填充。"));
    layout->addWidget(list_, 1);
    layout->addLayout(btnRow);
    layout->addWidget(buttons);
    reload();
}

void PasswordManagerDialog::reload() {
    list_->clear();
    try {
        for (const auto& e : ggzip::PasswordStore().load())
            list_->addItem(QString::fromStdWString(e.label));
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, "ggZip", QString("读取密码库失败: ") + ex.what());
    }
}

void PasswordManagerDialog::addPassword() {
    bool ok = false;
    const QString label = QInputDialog::getText(this, "添加密码", "标签（如压缩包文件名）:",
                                                QLineEdit::Normal, {}, &ok);
    if (!ok || label.trimmed().isEmpty()) return;
    const QString pw = QInputDialog::getText(this, "添加密码", "密码:",
                                             QLineEdit::Password, {}, &ok);
    if (!ok || pw.isEmpty()) return;
    try {
        ggzip::PasswordStore store;
        store.add(label.trimmed().toStdWString(), pw.toStdWString());
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, "ggZip", QString("保存失败: ") + ex.what());
        return;
    }
    reload();
}

void PasswordManagerDialog::removeSelected() {
    QListWidgetItem* item = list_->currentItem();
    if (!item) return;
    try {
        ggzip::PasswordStore store;
        store.remove(item->text().toStdWString());
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, "ggZip", QString("删除失败: ") + ex.what());
        return;
    }
    reload();
}

// ---------- PreviewDialog ----------

PreviewDialog::PreviewDialog(const QPixmap& pixmap, const QString& fileName,
                             QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("预览 — " + fileName);

    QLabel* lbl = new QLabel;
    lbl->setAlignment(Qt::AlignCenter);
    // 适配屏幕可用区域的 80%，等比缩放
    QSize target(900, 700);
    if (const QScreen* scr = screen()) {
        const QSize avail = scr->availableGeometry().size();
        target = QSize(int(avail.width() * 0.8), int(avail.height() * 0.8));
    }
    const QPixmap scaled = pixmap.scaled(target, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
    lbl->setPixmap(scaled);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(lbl);
    layout->addWidget(buttons);
    resize(scaled.width() + 40, scaled.height() + 80);
}

// ---------- CreateDialog ----------

namespace {
const wchar_t* kSaveFilter =
    L"7z (*.7z);;zip (*.zip);;tar (*.tar);;tar.gz (*.tar.gz);;tar.bz2 (*.tar.bz2);;"
    L"tar.xz (*.tar.xz);;gzip (*.gz);;xz (*.xz);;所有文件 (*)";
} // namespace

CreateArchiveDialog::CreateArchiveDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("新建压缩包");
    setMinimumWidth(520);

    inputList_ = new QListWidget;
    inputList_->setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto* addFilesBtn = new QPushButton("添加文件...");
    auto* addFolderBtn = new QPushButton("添加文件夹...");
    auto* removeBtn = new QPushButton("移除选中");
    connect(addFilesBtn, &QPushButton::clicked, this, &CreateArchiveDialog::addFiles);
    connect(addFolderBtn, &QPushButton::clicked, this, &CreateArchiveDialog::addFolder);
    connect(removeBtn, &QPushButton::clicked, this, &CreateArchiveDialog::removeSelected);
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

    // 密码库下拉（有保存的密码时才显示）
    try {
        stored_ = ggzip::PasswordStore().load();
    } catch (const std::exception&) {
        stored_.clear();
    }
    if (!stored_.empty()) {
        pwPreset_ = new QComboBox;
        pwPreset_->addItem("手动输入");
        for (const auto& e : stored_)
            pwPreset_->addItem(QString::fromStdWString(e.label));
        connect(pwPreset_, &QComboBox::currentIndexChanged, this, [this](int idx) {
            if (idx > 0 && idx <= (int)stored_.size())
                pwEdit_->setText(QString::fromStdWString(stored_[(size_t)idx - 1].password));
        });
    }

    levelSpin_ = new QSpinBox;
    levelSpin_->setRange(1, 9);
    levelSpin_->setValue(5);
    levelSpin_->setToolTip("1=最快 9=最小");

    volCombo_ = new QComboBox;
    volCombo_->addItems({ "不分卷", "10m", "50m", "100m", "500m", "1g" });
    volCombo_->setEditable(true);
    volCombo_->setToolTip("分卷大小：每个分卷的最大字节数（如 100m、1g）");

    auto* form = new QFormLayout;
    form->addRow("保存为:", outRow);
    if (pwPreset_) form->addRow("从密码库:", pwPreset_);
    form->addRow("密码:", pwEdit_);
    form->addRow("压缩级别:", levelSpin_);
    form->addRow("分卷大小:", volCombo_);

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

void CreateArchiveDialog::addFiles() {
    const QStringList files = QFileDialog::getOpenFileNames(this, "选择文件");
    for (const QString& f : files)
        if (!f.isEmpty()) inputList_->addItem(f);
}

void CreateArchiveDialog::addFolder() {
    const QString dir = QFileDialog::getExistingDirectory(this, "选择文件夹");
    if (!dir.isEmpty()) inputList_->addItem(dir + (dir.endsWith('/') ? "*" : "/*"));
}

void CreateArchiveDialog::removeSelected() {
    const auto selected = inputList_->selectedItems();
    for (QListWidgetItem* item : selected)
        delete item;
}

QStringList CreateArchiveDialog::inputs() const {
    QStringList result;
    for (int i = 0; i < inputList_->count(); ++i)
        result << inputList_->item(i)->text();
    return result;
}
QString CreateArchiveDialog::archivePath() const { return outEdit_->text().trimmed(); }
QString CreateArchiveDialog::password() const { return pwEdit_->text(); }
int CreateArchiveDialog::level() const { return levelSpin_->value(); }
QString CreateArchiveDialog::volumeSize() const {
    const QString t = volCombo_->currentText().trimmed();
    return (t.isEmpty() || t == "不分卷") ? QString() : t;
}
