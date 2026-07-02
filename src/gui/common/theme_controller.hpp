#pragma once

#include <QObject>
#include <QString>

class QApplication;

namespace aether {

/**
 * @brief Applies the application stylesheet and follows the OS colour scheme.
 *
 * Three modes are supported: System (track the OS light/dark setting),
 * Light, and Dark.
 */
class ThemeController : public QObject {
    Q_OBJECT
public:
    /** @brief Theme selection mode. */
    enum class Mode : quint8 { System, Light, Dark };

    /**
     * @brief Construct and apply the initial theme.
     * @param app The application whose stylesheet is driven.
     * @param parent Optional QObject parent.
     */
    explicit ThemeController(QApplication *app, QObject *parent = nullptr);

    /** @return The current selection mode. */
    Mode mode() const { return m_mode; }

    /** @brief Set the selection mode and re-apply the stylesheet. */
    void setMode(Mode mode);

    /** @brief Parse a persisted mode string ("light"/"dark"/anything else). */
    static Mode modeFromString(const QString &text);
    /** @brief Serialise a mode to a stable string for persistence. */
    static QString modeToString(Mode mode);

signals:
    /**
     * @brief Emitted after the theme is (re)applied.
     * @param dark @c true when the newly applied theme is dark.
     *
     * Custom-painted widgets that cannot rely on the stylesheet can connect to
     * this to repaint with theme-appropriate colours.
     */
    void themeChanged(bool dark);

private:
    void apply();
    bool effectiveDark() const;

    QApplication *m_app;
    Mode m_mode = Mode::System;
};

}  // namespace aether
