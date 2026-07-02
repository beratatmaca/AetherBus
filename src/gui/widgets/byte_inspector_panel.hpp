#pragma once

#include <QByteArray>
#include <QWidget>

class QLabel;

namespace aether {

/**
 * @brief Compact, auto-sizing decoder for a selected run of bytes.
 *
 * Renders the selection as a small monospace block showing the numeric
 * interpretations that fit the selection length (integer and float widths the
 * byte count allows). Hex/ASCII/binary are omitted because the console already
 * shows them. Little- and big-endian values are shown side by side, labelled so
 * the reader knows which is which. Intended to appear beneath the console while a
 * selection exists and hide when it clears.
 */
class ByteInspectorPanel : public QWidget {
    Q_OBJECT

public:
    explicit ByteInspectorPanel(QWidget *parent = nullptr);
    ~ByteInspectorPanel() override;

    /// Update the inspected bytes and refresh the rendered values.
    void setBytes(const QByteArray &bytes);

private:
    void setupUi();
    [[nodiscard]] QString buildText() const;

    QByteArray m_bytes;
    QLabel *m_content = nullptr;
};

}  // namespace aether
