#include "gui/theme_controller.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QPalette>

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QStyleHints>
#endif

namespace aether {

namespace {

QString loadThemeStylesheet(bool dark) {
    const QString filename = dark ? QStringLiteral("/assets/theme-dark.qss") : QStringLiteral("/assets/theme-light.qss");
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
    const QString style = loadThemeStylesheet(effectiveDark());
    m_app->setStyleSheet(style);
}

}  // namespace aether
