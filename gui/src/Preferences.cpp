#include "Preferences.h"
#include "ggzip/PasswordStore.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

ArchivePasswordDialog::ArchivePasswordDialog(const QString& archive, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("压缩包密码"));
    password_ = new QLineEdit;
    password_->setEchoMode(QLineEdit::Password);
    auto* presets = new QComboBox;
    presets->addItem(tr("手动输入"));
    std::vector<ggzip::PasswordEntry> stored;
    try { stored = ggzip::PasswordStore().load(); } catch (const std::exception&) {}
    for (const auto& entry : stored) presets->addItem(QString::fromStdWString(entry.label));
    connect(presets, &QComboBox::currentIndexChanged, this, [this, stored](int index) {
        if (index > 0 && index <= static_cast<int>(stored.size()))
            password_->setText(QString::fromStdWString(stored[static_cast<std::size_t>(index - 1)].password));
    });
    auto* save = new QCheckBox(tr("明确保存到当前用户 DPAPI 密码库"));
    auto* label = new QLineEdit(QFileInfo(archive).fileName());
    label->setEnabled(false);
    connect(save, &QCheckBox::toggled, label, &QWidget::setEnabled);
    auto* form = new QFormLayout;
    form->addRow(tr("压缩包:"), new QLabel(QFileInfo(archive).fileName()));
    form->addRow(tr("密码库:"), presets);
    form->addRow(tr("密码:"), password_);
    form->addRow(tr("保存标签:"), label);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, save, label] {
        if (password_->text().isEmpty()) return;
        if (save->isChecked()) {
            if (label->text().trimmed().isEmpty()) return;
            try { ggzip::PasswordStore().add(label->text().trimmed().toStdWString(), password().toStdWString()); }
            catch (const std::exception& error) {
                QMessageBox::warning(this, tr("保存失败"), QString::fromUtf8(error.what()));
                return;
            }
        }
        accept();
    });
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("输入仅在当前压缩包会话内使用，切换压缩包或关闭后清除。")));
    layout->addLayout(form);
    layout->addWidget(save);
    layout->addWidget(buttons);
}

QString ArchivePasswordDialog::password() const { return password_->text(); }

PreferencesDialog::PreferencesDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("常用设置与预览资源限制"));
    QSettings settings;
    auto* output = new QLineEdit(settings.value("paths/output").toString());
    auto* level = new QSpinBox;
    level->setRange(0, 9);
    level->setValue(settings.value("create/level", 5).toInt());
    auto* bytes = new QSpinBox;
    bytes->setRange(1, 1024);
    bytes->setSuffix(tr(" MiB"));
    bytes->setValue(static_cast<int>(settings.value("preview/maxBytes", 200ull * 1024 * 1024).toULongLong() / (1024 * 1024)));
    auto* pixels = new QSpinBox;
    pixels->setRange(1, 256);
    pixels->setSuffix(tr(" 百万像素"));
    pixels->setValue(static_cast<int>(settings.value("preview/maxPixels", 64000000).toULongLong() / 1000000));
    auto* timeout = new QSpinBox;
    timeout->setRange(1, 300);
    timeout->setSuffix(tr(" 秒"));
    timeout->setValue(static_cast<int>(settings.value("preview/timeoutMs", 30000).toULongLong() / 1000));
    auto* clear = new QPushButton(tr("清除最近打开历史"));
    connect(clear, &QPushButton::clicked, this, [this] {
        QSettings().remove("history/archives");
        QMessageBox::information(this, tr("历史记录"), tr("最近打开历史已清除，密码库不受影响。"));
    });
    auto* form = new QFormLayout;
    form->addRow(tr("常用输出目录:"), output);
    form->addRow(tr("默认压缩级别:"), level);
    form->addRow(tr("预览文件上限:"), bytes);
    form->addRow(tr("图片像素上限:"), pixels);
    form->addRow(tr("预览总超时:"), timeout);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, output, level, bytes, pixels, timeout] {
        QSettings preferences;
        preferences.setValue("paths/output", output->text().trimmed());
        preferences.setValue("create/level", level->value());
        preferences.setValue("preview/maxBytes", static_cast<qulonglong>(bytes->value()) * 1024 * 1024);
        preferences.setValue("preview/maxPixels", static_cast<qulonglong>(pixels->value()) * 1000000);
        preferences.setValue("preview/timeoutMs", static_cast<qulonglong>(timeout->value()) * 1000);
        accept();
    });
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("提高上限会增加资源消耗。图片仍在受内存限制的独立进程中解码。")));
    layout->addLayout(form);
    layout->addWidget(clear);
    layout->addWidget(buttons);
}
