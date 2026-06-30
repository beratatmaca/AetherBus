#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QDateTime>
#include "../core/bus_protocol.h"

class FrameTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit FrameTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void addFrame(const AetherFrame& frame);
    void clear();
    AetherFrame getFrame(int row) const;

private:
    QList<AetherFrame> m_frames;
    qint64 m_startTime;
};
