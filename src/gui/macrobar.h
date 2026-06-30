// Quick-send macros + send-history recall for the injection panel.
//
// Self-contained so it can be dropped into the layout with a single connect():
// it owns its macro definitions (persisted via QSettings) and a rolling history
// of recent sends, and emits send() with the bytes + target side.
#pragma once

#include <QByteArray>
#include <QVector>
#include <QWidget>

class QComboBox;
class QHBoxLayout;

namespace aether {

class MacroBar : public QWidget {
    Q_OBJECT

public:
    explicit MacroBar(QWidget *parent = nullptr);

    /// Record a just-sent payload at the top of the recall history.
    void pushHistory(const QByteArray &bytes, bool toDevice);

signals:
    /// Request that @p bytes be transmitted; @p toDevice picks the side.
    void send(const QByteArray &bytes, bool toDevice);

private slots:
    void editMacros();
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
