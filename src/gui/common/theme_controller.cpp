#include "gui/common/theme_controller.hpp"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QPalette>

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QStyleHints>
#endif

namespace aether {

namespace {

QString loadThemeStylesheet(bool dark) {
    const QString basename = dark ? QStringLiteral("theme-dark.qss") : QStringLiteral("theme-light.qss");

    QFile resource(QStringLiteral(":/aetherbus/") + basename);
    if (resource.open(QFile::ReadOnly | QFile::Text)) {
        return QString::fromUtf8(resource.readAll());
    }

    const QString filename = QStringLiteral("/assets/") + basename;
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + filename,
        QCoreApplication::applicationDirPath() + QStringLiteral("/..") + filename,
    };
    for (const QString &path : candidates) {
        QFile file(path);
        if (file.exists() && file.open(QFile::ReadOnly | QFile::Text)) {
            return QString::fromUtf8(file.readAll());
        }
    }
    return {};
}

// Build a QPalette that mirrors the active stylesheet. The app is themed via a
// stylesheet, but Qt never derives QPalette from it, so any C++ that reads
// palette() (e.g. table item colours, custom painting) would otherwise see the
// stale default (usually light) palette. Setting a matching palette keeps those
// runtime reads correct in both themes.
QPalette buildPalette(bool dark) {
    QPalette p;
    if (dark) {
        p.setColor(QPalette::Window, QColor(0x12, 0x12, 0x12));
        p.setColor(QPalette::WindowText, QColor(0xe0, 0xe0, 0xe0));
        p.setColor(QPalette::Base, QColor(0x18, 0x18, 0x18));
        p.setColor(QPalette::AlternateBase, QColor(0x1e, 0x1e, 0x1e));
        p.setColor(QPalette::Text, QColor(0xe0, 0xe0, 0xe0));
        p.setColor(QPalette::Button, QColor(0x1e, 0x1e, 0x1e));
        p.setColor(QPalette::ButtonText, QColor(0xe0, 0xe0, 0xe0));
        p.setColor(QPalette::ToolTipBase, QColor(0x1e, 0x1e, 0x1e));
        p.setColor(QPalette::ToolTipText, QColor(0xe0, 0xe0, 0xe0));
        p.setColor(QPalette::PlaceholderText, QColor(0x80, 0x80, 0x80));
        p.setColor(QPalette::BrightText, QColor(0xff, 0x52, 0x52));
        p.setColor(QPalette::Link, QColor(0x21, 0x96, 0xf3));
        p.setColor(QPalette::Highlight, QColor(0x21, 0x96, 0xf3));
        p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x60, 0x60, 0x60));
        p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x60, 0x60, 0x60));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x60, 0x60, 0x60));
    } else {
        p.setColor(QPalette::Window, QColor(0xf5, 0xf5, 0xf5));
        p.setColor(QPalette::WindowText, QColor(0x20, 0x20, 0x20));
        p.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::AlternateBase, QColor(0xec, 0xec, 0xec));
        p.setColor(QPalette::Text, QColor(0x20, 0x20, 0x20));
        p.setColor(QPalette::Button, QColor(0xe8, 0xe8, 0xe8));
        p.setColor(QPalette::ButtonText, QColor(0x20, 0x20, 0x20));
        p.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::ToolTipText, QColor(0x20, 0x20, 0x20));
        p.setColor(QPalette::PlaceholderText, QColor(0x90, 0x90, 0x90));
        p.setColor(QPalette::BrightText, QColor(0xd3, 0x2f, 0x2f));
        p.setColor(QPalette::Link, QColor(0x15, 0x65, 0xc0));
        p.setColor(QPalette::Highlight, QColor(0x21, 0x96, 0xf3));
        p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::Disabled, QPalette::Text, QColor(0xa0, 0xa0, 0xa0));
        p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0xa0, 0xa0, 0xa0));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0xa0, 0xa0, 0xa0));
    }
    return p;
}

}  // namespace

ThemeController::ThemeController(QApplication *app, QObject *parent) : QObject(parent), m_app(app) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // Track live OS scheme changes; only relevant while in System mode.
    connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
        if (m_mode == Mode::System)
            apply();
    });
#endif
    apply();
}

void ThemeController::setMode(Mode mode) {
    m_mode = mode;
    apply();
}

ThemeController::Mode ThemeController::modeFromString(const QString &text) {
    if (text == QLatin1String("light"))
        return Mode::Light;
    if (text == QLatin1String("dark"))
        return Mode::Dark;
    return Mode::System;
}

QString ThemeController::modeToString(Mode mode) {
    switch (mode) {
        case Mode::Light:
            return QStringLiteral("light");
        case Mode::Dark:
            return QStringLiteral("dark");
        case Mode::System:
            break;
    }
    return QStringLiteral("system");
}

bool ThemeController::effectiveDark() const {
    if (m_mode == Mode::Dark)
        return true;
    if (m_mode == Mode::Light)
        return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    // Heuristic fallback for Qt < 6.5: a dark window base implies dark mode.
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
#endif
}

void ThemeController::apply() {
    const bool dark = effectiveDark();
    QApplication::setPalette(buildPalette(dark));
    m_app->setStyleSheet(loadThemeStylesheet(dark));
    emit themeChanged(dark);
}

}  // namespace aether
