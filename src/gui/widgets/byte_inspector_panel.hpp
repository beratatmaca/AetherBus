#pragma once

#include <QWidget>
#include <QByteArray>

class QTableWidget;
class QLabel;

namespace aether {

class ByteInspectorPanel : public QWidget {
    Q_OBJECT

public:
    explicit ByteInspectorPanel(QWidget *parent = nullptr);
    ~ByteInspectorPanel() override;

    /// Update the inspected bytes and refresh the grid values
    void setBytes(const QByteArray &bytes);

private:
    void setupUi();
    void updateTableValues();

    QByteArray m_bytes;
    QTableWidget *m_table = nullptr;
    QLabel *m_infoLabel = nullptr;
};

} // namespace aether
