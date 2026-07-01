/**
 * @file macrobar.h
 * @brief Quick-send macros and send-history recall for the injection panel.
 */
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QWidget>

class QComboBox;
class QHBoxLayout;
class QLabel;

namespace aether {

/**
 * @brief One-click transmit macros plus a rolling recall history.
 *
 * Self-contained so it drops into a layout with a single @ref send()
 * connection: it owns its macro definitions (named payloads persisted via
 * @c QSettings) and a bounded history of recent sends, re-emitting either on
 * request. Each macro carries the same knobs as the injection panel — input
 * format (HEX/ASCII/DEC/BIN), a line ending, and a target direction — so
 * building a macro is identical to composing a one-off injection.
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

    /**
     * @brief Open the editor with a new row pre-filled from the given input state.
     *
     * Lets other widgets (e.g. the injection panel's "Save as macro" action)
     * seed a macro from whatever the user just composed, without knowing how
     * macros are stored.
     * @param format   Input format index (0=HEX, 1=ASCII, 2=DEC, 3=BIN).
     * @param payload  Raw payload text in that format.
     * @param ending   Line-ending index (0=none, 1=CR, 2=LF, 3=CR+LF).
     * @param toDevice @c true targets the device, @c false the application.
     */
    void addMacroFromState(int format, const QString &payload, int ending, bool toDevice);

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
        int format = 0;   ///< 0=HEX 1=ASCII 2=DEC 3=BIN
        QString payload;  ///< raw user text in @ref format
        int ending = 0;   ///< 0=none 1=CR 2=LF 3=CR+LF
        bool toDevice = true;
    };
    struct HistoryItem {
        QByteArray bytes;
        bool toDevice;
    };

    void loadMacros();
    void saveMacros();
    void rebuildButtons();
    /**
     * @brief Run the table editor over the current macro set.
     * @param prefill     Optional extra row appended pre-filled (nullptr for none).
     * @param selectIndex Row to select/scroll to on open (-1 for the prefill/last row).
     */
    void openEditor(const Macro *prefill, int selectIndex);

    QVector<Macro> m_macros;
    QVector<HistoryItem> m_history;
    QHBoxLayout *m_buttonRow = nullptr;
    QLabel *m_emptyHint = nullptr;
    QComboBox *m_historyBox = nullptr;
};

}  // namespace aether
