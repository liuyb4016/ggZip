#pragma once
// ggZip 视觉主题：浅色 / 深色 / 跟随系统。
// header-only，供 GUI 与多个复用 MainWindow/Dialogs 的测试目标共同包含，避免改动各目标源文件列表。
#include <QApplication>
#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QString>
#include <QStyle>
#include <QStyleHints>

namespace ggzipui {

enum class ThemeMode { System, Light, Dark };

inline ThemeMode themeModeFromString(const QString& value) {
    if (value == QLatin1String("light")) return ThemeMode::Light;
    if (value == QLatin1String("dark")) return ThemeMode::Dark;
    return ThemeMode::System;
}

inline ThemeMode configuredTheme() {
    return themeModeFromString(QSettings().value("ui/theme", "system").toString());
}

inline bool systemIsDark() {
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme == Qt::ColorScheme::Dark) return true;
    if (scheme == Qt::ColorScheme::Light) return false;
    const QColor window = QApplication::palette().color(QPalette::Window);
    return window.lightness() < 128;
}

inline bool effectiveDark(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Light:  return false;
        case ThemeMode::Dark:   return true;
        case ThemeMode::System: return systemIsDark();
    }
    return false;
}

struct ThemeColors {
    const char* window;
    const char* surface;
    const char* surfaceAlt;
    const char* border;
    const char* borderStrong;
    const char* text;
    const char* textMuted;
    const char* accent;
    const char* accentHover;
    const char* accentPressed;
    const char* onAccent;
    const char* selectBg;
    const char* hoverBg;
    const char* headerBg;
    const char* inputBg;
    const char* statusBg;
    const char* shadow;
};

inline ThemeColors lightColors() {
    return {
        "#ffffff", "#ffffff", "#f6f8fa", "#e3e8ef", "#cfd8e3",
        "#1f2733", "#6b7684", "#2f6fed", "#245bd1", "#1d4bab",
        "#ffffff", "#e8f0ff", "#f1f4f8", "#f7f9fc", "#ffffff",
        "#fafbfd", "rgba(31,39,51,18)"
    };
}

inline ThemeColors darkColors() {
    return {
        "#1e222b", "#252b36", "#2b323f", "#384152", "#4b566b",
        "#e6ecf5", "#9aa7ba", "#5b8dff", "#4779ef", "#3a67d6",
        "#ffffff", "#2b3c5c", "#323b4a", "#2a303b", "#1b2029",
        "#20252e", "rgba(0,0,0,55)"
    };
}

inline QString buildStyleSheet(const ThemeColors& c) {
    const QString q = QStringLiteral(R"QSS(
QWidget {
    background: %1;
    color: %6;
    font-family: "Segoe UI", "Microsoft YaHei UI", "Microsoft YaHei", sans-serif;
    font-size: 13px;
}
QToolTip {
    background: %2;
    color: %6;
    border: 1px solid %4;
    border-radius: 6px;
    padding: 5px 8px;
}
QMenuBar {
    background: %2;
    border-bottom: 1px solid %4;
    padding: 2px 6px;
}
QMenuBar::item {
    background: transparent;
    padding: 6px 12px;
    border-radius: 5px;
}
QMenuBar::item:selected { background: %13; color: %8; }
QMenuBar::item:pressed  { background: %14; color: %11; }
QMenu {
    background: %2;
    border: 1px solid %4;
    border-radius: 8px;
    padding: 6px 4px;
}
QMenu::item {
    padding: 7px 30px 7px 28px;
    border-radius: 5px;
    margin: 0px 4px;
}
QMenu::item:selected { background: %8; color: %11; }
QMenu::item:disabled { color: %7; }
QMenu::separator { height: 1px; background: %4; margin: 5px 12px; }
QMenu::indicator { width: 16px; height: 16px; }

QToolBar {
    background: %2;
    border: none;
    border-bottom: 1px solid %4;
    padding: 6px 10px;
    spacing: 6px;
}
QToolBar QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 5px 9px;
    color: %6;
}
QToolBar QToolButton:hover    { background: %14; border-color: %4; }
QToolBar QToolButton:pressed  { background: %13; }
QToolBar QToolButton:checked  { background: %13; color: %8; }
QToolBar QToolButton:disabled { color: %7; }
QToolBar::separator {
    width: 1px;
    background: %4;
    margin: 4px 6px;
}

QLabel#archivePath {
    background: %15;
    border: 1px solid %4;
    border-radius: 6px;
    padding: 5px 12px;
    color: %6;
    selection-background-color: %8;
}
QLineEdit#archiveSearch {
    background: %15;
    border: 1px solid %4;
    border-radius: 6px;
    padding: 5px 12px;
    min-width: 180px;
    selection-background-color: %8;
    selection-color: %11;
}
QLineEdit#archiveSearch:focus { border: 1px solid %8; }
QCheckBox#searchWholeArchive { spacing: 6px; padding: 4px 2px; }

QTableView#archiveTable {
    background: %2;
    alternate-background-color: %3;
    border: none;
    gridline-color: %4;
    selection-background-color: %13;
    selection-color: %6;
    outline: none;
}
QTableView#archiveTable::item {
    padding: 4px 10px;
    border-bottom: 1px solid %4;
}
QTableView#archiveTable::item:hover { background: %14; }
QTableView#archiveTable::item:selected { background: %13; color: %6; }
QHeaderView::section {
    background: %16;
    color: %7;
    border: none;
    border-right: 1px solid %4;
    border-bottom: 1px solid %4;
    padding: 8px 10px;
    font-weight: 600;
}
QHeaderView::section:hover { background: %14; color: %6; }
QTableCornerButton::section { background: %16; border: 1px solid %4; }

QPushButton {
    background: %2;
    color: %6;
    border: 1px solid %5;
    border-radius: 6px;
    padding: 7px 16px;
    min-height: 18px;
}
QPushButton:hover    { background: %14; border-color: %8; color: %8; }
QPushButton:pressed  { background: %13; }
QPushButton:disabled { color: %7; background: %3; border-color: %4; }
QPushButton:default {
    background: %8;
    color: %11;
    border-color: %8;
    font-weight: 600;
}
QPushButton:default:hover    { background: %9; border-color: %9; }
QPushButton:default:pressed  { background: %10; }
QPushButton:default:disabled { background: %4; color: %7; border-color: %4; }
QDialogButtonBox QPushButton { min-width: 88px; }

QLineEdit, QSpinBox, QComboBox, QPlainTextEdit {
    background: %15;
    color: %6;
    border: 1px solid %4;
    border-radius: 6px;
    padding: 6px 10px;
    selection-background-color: %8;
    selection-color: %11;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus { border: 1px solid %8; }
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled { color: %7; background: %3; }
QComboBox::drop-down { border: none; width: 26px; }
QComboBox::down-arrow {
    image: none;
    border-left: 5px solid transparent;
    border-right: 5px solid transparent;
    border-top: 6px solid %7;
    margin-right: 10px;
}
QComboBox QAbstractItemView {
    background: %2;
    color: %6;
    border: 1px solid %4;
    border-radius: 6px;
    selection-background-color: %8;
    selection-color: %11;
    padding: 4px;
}
QCheckBox, QRadioButton { spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 16px; height: 16px;
    border: 1px solid %5;
    border-radius: 4px;
    background: %15;
}
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background: %8;
    border-color: %8;
}
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: %8; }

QProgressBar {
    background: %3;
    border: 1px solid %4;
    border-radius: 8px;
    height: 18px;
    text-align: center;
    color: %6;
}
QProgressBar::chunk { background: %8; border-radius: 7px; }

QStatusBar {
    background: %17;
    border-top: 1px solid %4;
    color: %7;
}
QStatusBar QLabel { background: transparent; color: %7; padding: 2px 4px; }

QScrollBar:vertical { background: transparent; width: 13px; margin: 0; }
QScrollBar::handle:vertical { background: %5; border-radius: 6px; min-height: 28px; margin: 2px; }
QScrollBar::handle:vertical:hover { background: %8; }
QScrollBar:horizontal { background: transparent; height: 13px; margin: 0; }
QScrollBar::handle:horizontal { background: %5; border-radius: 6px; min-width: 28px; margin: 2px; }
QScrollBar::handle:horizontal:hover { background: %8; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QListWidget, QTreeView, QTableView {
    border: 1px solid %4;
    border-radius: 6px;
}
QListWidget::item, QTreeView::item { padding: 5px 8px; }
QListWidget::item:selected, QTreeView::item:selected { background: %8; color: %11; }
QListWidget::item:hover, QTreeView::item:hover { background: %14; }

QGroupBox {
    border: 1px solid %4;
    border-radius: 8px;
    margin-top: 12px;
    padding: 12px 12px 8px 12px;
}
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; color: %7; }

QWidget#emptyState { background: %2; }
QLabel#emptyStateTitle {
    background: transparent;
    color: %6;
    font-size: 18px;
    font-weight: 600;
    padding: 2px;
}
QLabel#emptyStateHint {
    background: transparent;
    color: %7;
    font-size: 13px;
    padding: 2px;
}

QDialog { background: %1; }
QMessageBox { background: %1; }
QProgressDialog { background: %1; }
)QSS");

    // 索引与 QSS 中的 %1..%17 一一对应；%12 保留占位（未使用）。
    const QStringList values = {
        c.window, c.surface, c.surfaceAlt, c.border, c.borderStrong,
        c.text, c.textMuted, c.accent, c.accentHover, c.accentPressed,
        c.onAccent, c.selectBg, c.selectBg, c.hoverBg,
        c.inputBg, c.headerBg, c.statusBg
    };
    QString result = q;
    for (int i = values.size(); i >= 1; --i)  // 逆序：避免 %1 先替换掉 %10/%11 的前缀
        result.replace(QStringLiteral("%%1").arg(i), values.at(i - 1));
    return result;
}

inline QPalette buildPalette(bool dark) {
    QPalette p;
    if (dark) {
        const ThemeColors c = darkColors();
        p.setColor(QPalette::Window,          QColor(QLatin1String(c.window)));
        p.setColor(QPalette::WindowText,      QColor(QLatin1String(c.text)));
        p.setColor(QPalette::Base,            QColor(QLatin1String(c.inputBg)));
        p.setColor(QPalette::AlternateBase,   QColor(QLatin1String(c.surfaceAlt)));
        p.setColor(QPalette::Text,            QColor(QLatin1String(c.text)));
        p.setColor(QPalette::Button,          QColor(QLatin1String(c.surface)));
        p.setColor(QPalette::ButtonText,      QColor(QLatin1String(c.text)));
        p.setColor(QPalette::BrightText,      QColor(QLatin1String(c.onAccent)));
        p.setColor(QPalette::Highlight,       QColor(QLatin1String(c.accent)));
        p.setColor(QPalette::HighlightedText, QColor(QLatin1String(c.onAccent)));
        p.setColor(QPalette::Link,            QColor(QLatin1String(c.accent)));
        p.setColor(QPalette::ToolTipBase,     QColor(QLatin1String(c.surface)));
        p.setColor(QPalette::ToolTipText,     QColor(QLatin1String(c.text)));
        p.setColor(QPalette::PlaceholderText, QColor(QLatin1String(c.textMuted)));
        p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(QLatin1String(c.textMuted)));
        p.setColor(QPalette::Disabled, QPalette::Text,       QColor(QLatin1String(c.textMuted)));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(QLatin1String(c.textMuted)));
    } else {
        const ThemeColors c = lightColors();
        p.setColor(QPalette::Window,          QColor(QLatin1String(c.window)));
        p.setColor(QPalette::WindowText,      QColor(QLatin1String(c.text)));
        p.setColor(QPalette::Base,            QColor(QLatin1String(c.inputBg)));
        p.setColor(QPalette::AlternateBase,   QColor(QLatin1String(c.surfaceAlt)));
        p.setColor(QPalette::Text,            QColor(QLatin1String(c.text)));
        p.setColor(QPalette::Button,          QColor(QLatin1String(c.surface)));
        p.setColor(QPalette::ButtonText,      QColor(QLatin1String(c.text)));
        p.setColor(QPalette::BrightText,      QColor(QLatin1String(c.onAccent)));
        p.setColor(QPalette::Highlight,       QColor(QLatin1String(c.accent)));
        p.setColor(QPalette::HighlightedText, QColor(QLatin1String(c.onAccent)));
        p.setColor(QPalette::Link,            QColor(QLatin1String(c.accent)));
        p.setColor(QPalette::ToolTipBase,     QColor(QLatin1String(c.surface)));
        p.setColor(QPalette::ToolTipText,     QColor(QLatin1String(c.text)));
        p.setColor(QPalette::PlaceholderText, QColor(QLatin1String(c.textMuted)));
    }
    return p;
}

// 在 QApplication 构造后、创建窗口前调用；也可在设置变更后即时重新应用。
inline void applyTheme(ThemeMode mode) {
    if (!qApp) return;  // 无 QApplication 实例时（如 QCoreApplication 测试）不应用
    const bool dark = effectiveDark(mode);
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setPalette(buildPalette(dark));
    qApp->setStyleSheet(buildStyleSheet(dark ? darkColors() : lightColors()));
}

inline void applyConfiguredTheme() { applyTheme(configuredTheme()); }

} // namespace ggzipui
