/**
 * @file macrobar.h
 * @brief Quick-send macros and send-history recall for the injection panel.
 */
#pragma once

#include <QByteArray>
#include <QVector>
#include <QWidget>

class QComboBox;
class QHBoxLayout;

namespace aether {

/**
 * @brief One-click transmit macros plus a rolling recall history.
 *
 * Self-contained so it drops into a layout with a single @ref send()
 * connection: it owns its macro definitions (named hex payloads persisted via
 * @c QSettings) and a bounded history of recent sends, re-emitting either on
 * request.
 */
class MacroBar : public QWidget {
    Q_OBJECT

public:
    /** @brief Construct the bar and load saved macros. @param parent Optional parent. */
    explicit MacroBar(QWidget *parent = nullptr);

    /**
     * @brief Record a just-sent payload at the top of the recall history.
     * @param bytes    The payload that was sent (ignored if empty).
     * @param toDevice @c true if it went to the device, @c false to the app.
     */
    void pushHistory(const QByteArray &bytes, bool toDevice);

signals:
    /**
     * @brief Request that a payload be transmitted.
     * @param bytes    Bytes to send.
     * @param toDevice @c true targets the device, @c false the application.
     */
    void send(const QByteArray &bytes, bool toDevice);

private slots:
    /** @brief Open the editor dialog to define/replace the macro set. */
    void editMacros();
    /** @brief Re-emit @ref send() for the currently selected history entry. */
    void resendSelected();

private:
    struct Macro {
        QString name;
        QByteArray bytes;
    };
    struct HistoryItem {
        QByteArray bytes;
        bool toDevice;
    };

    void loadMacros();
    void saveMacros();
    void rebuildButtons();

    QVector<Macro> m_macros;
    QVector<HistoryItem> m_history;
    QHBoxLayout *m_buttonRow = nullptr;
    QComboBox *m_historyBox = nullptr;
};

} // namespace aether
