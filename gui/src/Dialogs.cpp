#include "Dialogs.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QScreen>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QStringDecoder>
#include <QSettings>
#include <qt_windows.h>

// ---------- ExtractDialog ----------

ExtractDialog::ExtractDialog(const QString& archivePath, bool smartDefault,
                             QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("解压压缩包"));
    setMinimumWidth(460);

    dirEdit_ = new QLineEdit(QFileInfo(archivePath).absolutePath());
    dirEdit_->setObjectName("extractTarget");
    const QString savedOutput = QSettings().value("paths/output").toString();
    if (!savedOutput.isEmpty()) dirEdit_->setText(savedOutput);
    pwEdit_ = new QLineEdit;
    pwEdit_->setEchoMode(QLineEdit::Password);
    pwEdit_->setPlaceholderText(tr("加密包才需要，留空表示无密码"));
    smartCheck_ = new QCheckBox(tr("智能解压（单根目录直接解压，否则自动建子文件夹）"));
    smartCheck_->setChecked(smartDefault);

    // 密码库下拉（有保存的密码时才显示）
    try {
        stored_ = ggzip::PasswordStore().load();
    } catch (const std::exception&) {
        stored_.clear();
    }
    if (!stored_.empty()) {
        pwPreset_ = new QComboBox;
        pwPreset_->addItem(tr("手动输入"));
        for (const auto& e : stored_)
            pwPreset_->addItem(QString::fromStdWString(e.label));
        connect(pwPreset_, &QComboBox::currentIndexChanged, this, [this](int idx) {
            if (idx > 0 && idx <= (int)stored_.size())
                pwEdit_->setText(QString::fromStdWString(stored_[(size_t)idx - 1].password));
        });
    }

    auto* browse = new QPushButton(tr("浏览..."));
    connect(browse, &QPushButton::clicked, this, [this] {
        QString dir = QFileDialog::getExistingDirectory(this, tr("选择解压目标目录"),
                                                        dirEdit_->text());
        if (!dir.isEmpty()) dirEdit_->setText(dir);
    });
    auto* dirRow = new QHBoxLayout;
    dirRow->addWidget(dirEdit_, 1);
    dirRow->addWidget(browse);

    auto* form = new QFormLayout;
    form->addRow(tr("源压缩包:"), new QLabel(QFileInfo(archivePath).fileName()));
    form->addRow(tr("目标目录:"), dirRow);
    if (pwPreset_) form->addRow(tr("从密码库:"), pwPreset_);
    form->addRow(tr("密码:"), pwEdit_);

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
void ExtractDialog::setPassword(const QString& password) { pwEdit_->setText(password); }
void ExtractDialog::setSelectedOnly() { smartCheck_->setChecked(false); smartCheck_->setEnabled(false); }

// ---------- PasswordManagerDialog ----------

PasswordManagerDialog::PasswordManagerDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("密码管理器"));
    setMinimumWidth(440);

    list_ = new QListWidget;
    auto* addBtn = new QPushButton(tr("添加..."));
    auto* removeBtn = new QPushButton(tr("删除选中"));
    connect(addBtn, &QPushButton::clicked, this, &PasswordManagerDialog::addPassword);
    connect(removeBtn, &QPushButton::clicked, this, &PasswordManagerDialog::removeSelected);
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(addBtn);
    btnRow->addWidget(removeBtn);
    btnRow->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr(
        "密码以 DPAPI（当前用户作用域）加密存储。\n标签建议使用压缩包文件名，解压时可在下拉框快速填充。")));
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
        QMessageBox::warning(this, "ggZip", tr("读取密码库失败: %1").arg(QString::fromUtf8(ex.what())));
    }
}

void PasswordManagerDialog::addPassword() {
    bool ok = false;
    const QString label = QInputDialog::getText(this, tr("添加密码"), tr("标签（如压缩包文件名）:"),
                                                QLineEdit::Normal, {}, &ok);
    if (!ok || label.trimmed().isEmpty()) return;
    const QString pw = QInputDialog::getText(this, tr("添加密码"), tr("密码:"),
                                             QLineEdit::Password, {}, &ok);
    if (!ok || pw.isEmpty()) return;
    try {
        ggzip::PasswordStore store;
        store.add(label.trimmed().toStdWString(), pw.toStdWString());
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, "ggZip", tr("保存失败: %1").arg(QString::fromUtf8(ex.what())));
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
        QMessageBox::warning(this, "ggZip", tr("删除失败: %1").arg(QString::fromUtf8(ex.what())));
        return;
    }
    reload();
}

// ---------- PreviewDialog ----------

PreviewDialog::PreviewDialog(const QPixmap& pixmap, const QString& fileName,
                             QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("预览 — %1").arg(fileName));

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

// ---------- TextPreviewDialog ----------

// BOM / UTF-8 / 系统编码（GBK）自动识别
QString ggzDecodeTextPreview(const QByteArray& raw, bool truncated) {
    auto utf8Prefix = [&](const QByteArray& bytes) {
        QString result;
        for (int trim = 0; trim <= (truncated ? 3 : 0) && trim <= bytes.size(); ++trim) {
            if (trim) {
                const auto lead = static_cast<unsigned char>(bytes[bytes.size() - trim]);
                const int expected = lead >= 0xc2 && lead <= 0xdf ? 2 :
                    lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
                if (trim >= expected) continue;
                bool continuation = true;
                for (qsizetype index = bytes.size() - trim + 1; index < bytes.size(); ++index) {
                    const auto byte = static_cast<unsigned char>(bytes[index]);
                    if (byte < 0x80 || byte > 0xbf) continuation = false;
                }
                if (!continuation) continue;
            }
            QStringDecoder decoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
            result = decoder(bytes.first(bytes.size() - trim));
            if (!decoder.hasError()) return result;
        }
        return QString();
    };
    if (raw.startsWith("\xEF\xBB\xBF")) return utf8Prefix(raw.mid(3));
    if (raw.startsWith("\xFF\xFE") || raw.startsWith("\xFE\xFF")) {
        QStringDecoder d(QStringConverter::Utf16, QStringConverter::Flag::Stateless);
        return d(truncated && raw.size() % 2 ? raw.first(raw.size() - 1) : raw);
    }
    const QString utf8 = utf8Prefix(raw);
    if (!utf8.isNull()) return utf8;
    for (int trim = 0; trim <= (truncated ? 1 : 0); ++trim) {
        const int length = static_cast<int>(raw.size()) - trim;
        if (length <= 0) break;
        const int count = MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, raw.constData(), length, nullptr, 0);
        if (!count) continue;
        std::wstring decoded(static_cast<std::size_t>(count), L'\0');
        if (MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, raw.constData(), length, decoded.data(), count))
            return QString::fromStdWString(decoded);
    }
    QStringDecoder dsys(QStringConverter::System, QStringConverter::Flag::Stateless);
    const QString sys = dsys(raw);
    return sys;
}

TextPreviewDialog::TextPreviewDialog(const QString& text, const QString& fileName,
                                     bool truncated, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("预览 — %1").arg(fileName));
    resize(860, 640);

    auto* view = new QPlainTextEdit;
    view->setPlainText(text);
    view->setReadOnly(true);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::TypeWriter);
    mono.setPointSize(10);
    view->setFont(mono);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    if (truncated)
        layout->addWidget(new QLabel(tr("内容过长，仅显示前 100KB（完整内容请解压后查看）。")));
    layout->addWidget(view, 1);
    layout->addWidget(buttons);
}

// ---------- CreateDialog ----------

namespace {
const wchar_t* kSaveFilter =
    L"7z (*.7z);;zip (*.zip);;自解压包 (*.exe);;tar (*.tar);;tar.gz (*.tar.gz);;"
    L"tar.bz2 (*.tar.bz2);;tar.xz (*.tar.xz);;gzip (*.gz);;xz (*.xz);;所有文件 (*)";
} // namespace

CreateArchiveDialog::CreateArchiveDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("新建压缩包"));
    setMinimumWidth(520);

    inputList_ = new QListWidget;
    inputList_->setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto* addFilesBtn = new QPushButton(tr("添加文件..."));
    auto* addFolderBtn = new QPushButton(tr("添加文件夹..."));
    auto* removeBtn = new QPushButton(tr("移除选中"));
    connect(addFilesBtn, &QPushButton::clicked, this, &CreateArchiveDialog::addFiles);
    connect(addFolderBtn, &QPushButton::clicked, this, &CreateArchiveDialog::addFolder);
    connect(removeBtn, &QPushButton::clicked, this, &CreateArchiveDialog::removeSelected);
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(addFilesBtn);
    btnRow->addWidget(addFolderBtn);
    btnRow->addWidget(removeBtn);
    btnRow->addStretch(1);

    outEdit_ = new QLineEdit;
    auto* outBrowse = new QPushButton(tr("浏览..."));
    connect(outBrowse, &QPushButton::clicked, this, [this] {
        QString p = QFileDialog::getSaveFileName(this, tr("选择压缩包保存位置"),
                                                 outEdit_->text(),
                                                 tr(QString::fromWCharArray(kSaveFilter).toUtf8().constData()), nullptr,
                                                 QFileDialog::DontConfirmOverwrite);
        if (!p.isEmpty()) outEdit_->setText(p);
    });
    auto* outRow = new QHBoxLayout;
    outRow->addWidget(outEdit_, 1);
    outRow->addWidget(outBrowse);

    pwEdit_ = new QLineEdit;
    pwEdit_->setEchoMode(QLineEdit::Password);
    pwEdit_->setPlaceholderText(tr("仅 7z/zip 支持，留空表示不加密"));

    // 密码库下拉（有保存的密码时才显示）
    try {
        stored_ = ggzip::PasswordStore().load();
    } catch (const std::exception&) {
        stored_.clear();
    }
    if (!stored_.empty()) {
        pwPreset_ = new QComboBox;
        pwPreset_->addItem(tr("手动输入"));
        for (const auto& e : stored_)
            pwPreset_->addItem(QString::fromStdWString(e.label));
        connect(pwPreset_, &QComboBox::currentIndexChanged, this, [this](int idx) {
            if (idx > 0 && idx <= (int)stored_.size())
                pwEdit_->setText(QString::fromStdWString(stored_[(size_t)idx - 1].password));
        });
    }

    levelSpin_ = new QSpinBox;
    levelSpin_->setRange(0, 9);
    levelSpin_->setValue(QSettings().value("create/level", 5).toInt());
    levelSpin_->setToolTip(tr("1=最快 9=最小"));

    volCombo_ = new QComboBox;
    volCombo_->addItems({ tr("不分卷"), "10m", "50m", "100m", "500m", "1g" });
    volCombo_->setEditable(true);
    volCombo_->setToolTip(tr("分卷大小：每个分卷的最大字节数（如 100m、1g）"));

    auto* form = new QFormLayout;
    form->addRow(tr("保存为:"), outRow);
    if (pwPreset_) form->addRow(tr("从密码库:"), pwPreset_);
    form->addRow(tr("密码:"), pwEdit_);
    form->addRow(tr("压缩级别:"), levelSpin_);
    form->addRow(tr("分卷大小:"), volCombo_);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(outEdit_, &QLineEdit::textChanged, this, [buttons](const QString& t) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!t.trimmed().isEmpty());
    });

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("要压缩的文件 / 文件夹:")));
    layout->addWidget(inputList_, 1);
    layout->addLayout(btnRow);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void CreateArchiveDialog::addFiles() {
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("选择文件"));
    for (const QString& f : files)
        if (!f.isEmpty()) inputList_->addItem(f);
}

void CreateArchiveDialog::addFolder() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("选择文件夹"));
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
    return (t.isEmpty() || t == volCombo_->itemText(0)) ? QString() : t;
}
