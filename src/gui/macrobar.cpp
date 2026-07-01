#include "gui/macrobar.h"

#include "core/format_codec.h"

#include <QAction>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cstdint>

namespace aether {

namespace {
constexpr int kMaxHistory = 25;

// Editor table columns.
enum Column : std::uint8_t { ColName = 0, ColFormat, ColPayload, ColEnding, ColDirection, ColPreview, ColCount };

const char *formatName(int format) {
    switch (format) {
        case 1:
            return "ASCII";
        case 2:
            return "DEC";
        case 3:
            return "BIN";
        default:
            return "HEX";
    }
}

QComboBox *makeFormatCombo(QWidget *parent) {
    auto *box = new QComboBox(parent);
    box->addItems({QStringLiteral("HEX"), QStringLiteral("ASCII"), QStringLiteral("DEC"), QStringLiteral("BIN")});
    return box;
}

QComboBox *makeEndingCombo(QWidget *parent) {
    auto *box = new QComboBox(parent);
    box->addItems({QStringLiteral("None"), QStringLiteral("CR"), QStringLiteral("LF"), QStringLiteral("CR+LF")});
    return box;
}

QComboBox *makeDirectionCombo(QWidget *parent) {
    auto *box = new QComboBox(parent);
    box->addItems({QStringLiteral("Device"), QStringLiteral("App")});
    return box;
}
}  // namespace

MacroBar::MacroBar(QWidget *parent) : QWidget(parent) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(new QLabel(QStringLiteral("Macros:"), this));
    m_buttonRow = new QHBoxLayout();
    layout->addLayout(m_buttonRow);

    m_emptyHint = new QLabel(QStringLiteral("<i>none yet — click Edit… or ★ Save as macro</i>"), this);
    m_emptyHint->setEnabled(false);
    layout->addWidget(m_emptyHint);

    auto *editBtn = new QPushButton(QStringLiteral("Edit…"), this);
    editBtn->setToolTip(QStringLiteral("Define, reorder and delete quick-send macros"));
    connect(editBtn, &QPushButton::clicked, this, &MacroBar::editMacros);
    layout->addWidget(editBtn);

    layout->addStretch(1);

    layout->addWidget(new QLabel(QStringLiteral("History:"), this));
    m_historyBox = new QComboBox(this);
    m_historyBox->setMinimumWidth(220);
    m_historyBox->setToolTip(QStringLiteral("Recently sent payloads"));
    layout->addWidget(m_historyBox);
    auto *resendBtn = new QPushButton(QStringLiteral("Resend"), this);
    connect(resendBtn, &QPushButton::clicked, this, &MacroBar::resendSelected);
    layout->addWidget(resendBtn);

    loadMacros();
    rebuildButtons();
}

void MacroBar::loadMacros() {
    m_macros.clear();
    QSettings settings;
    const int count = settings.beginReadArray(QStringLiteral("macros"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        Macro macro;
        macro.name = settings.value(QStringLiteral("name")).toString();
        if (settings.contains(QStringLiteral("format"))) {
            // New-format entry: format/payload/ending/direction stored verbatim.
            macro.format = settings.value(QStringLiteral("format")).toInt();
            macro.payload = settings.value(QStringLiteral("payload")).toString();
            macro.ending = settings.value(QStringLiteral("ending")).toInt();
            macro.toDevice = settings.value(QStringLiteral("toDevice"), true).toBool();
        } else {
            // Legacy entry: name + raw hex string, always sent to the device.
            macro.format = static_cast<int>(codec::PayloadFormat::Hex);
            macro.payload = settings.value(QStringLiteral("hex")).toString();
            macro.ending = static_cast<int>(codec::LineEnding::None);
            macro.toDevice = true;
        }
        m_macros.append(macro);
    }
    settings.endArray();
}

void MacroBar::saveMacros() {
    QSettings settings;
    settings.beginWriteArray(QStringLiteral("macros"));
    for (int i = 0; i < m_macros.size(); ++i) {
        settings.setArrayIndex(i);
        const Macro &macro = m_macros.at(i);
        settings.setValue(QStringLiteral("name"), macro.name);
        settings.setValue(QStringLiteral("format"), macro.format);
        settings.setValue(QStringLiteral("payload"), macro.payload);
        settings.setValue(QStringLiteral("ending"), macro.ending);
        settings.setValue(QStringLiteral("toDevice"), macro.toDevice);
        settings.remove(QStringLiteral("hex"));  // drop any legacy key
    }
    settings.endArray();
}

void MacroBar::rebuildButtons() {
    // Clear existing macro buttons.
    while (QLayoutItem *item = m_buttonRow->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    int shown = 0;
    for (int i = 0; i < m_macros.size(); ++i) {
        const Macro &macro = m_macros.at(i);
        if (macro.name.isEmpty()) {
            continue;
        }
        QByteArray bytes;
        QString error;
        const bool valid = codec::encodePayload(macro.format, macro.payload, macro.ending, bytes, &error) && !bytes.isEmpty();

        auto *btn = new QPushButton(macro.name, this);
        const QString dir = macro.toDevice ? QStringLiteral("Device") : QStringLiteral("App");
        if (valid) {
            btn->setToolTip(QStringLiteral("%1 → %2 : %3").arg(QLatin1String(formatName(macro.format)), dir, codec::toHex(bytes)));
            connect(btn, &QPushButton::clicked, this, [this, bytes, macro] { emit send(bytes, macro.toDevice); });
        } else {
            btn->setText(macro.name + QStringLiteral(" (!)"));
            btn->setToolTip(
                QStringLiteral("Invalid payload — %1. Right-click → Edit to fix.").arg(error.isEmpty() ? QStringLiteral("empty") : error));
            connect(btn, &QPushButton::clicked, this, [this, i] { openEditor(nullptr, i); });
        }

        // Right-click actions: edit, delete, or flip the send direction in place.
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QPushButton::customContextMenuRequested, this, [this, i, btn](const QPoint &pos) {
            QMenu menu(this);
            QAction *edit = menu.addAction(QStringLiteral("Edit…"));
            QAction *flip =
                menu.addAction(m_macros.at(i).toDevice ? QStringLiteral("Send to App instead") : QStringLiteral("Send to Device instead"));
            menu.addSeparator();
            QAction *del = menu.addAction(QStringLiteral("Delete"));
            const QAction *chosen = menu.exec(btn->mapToGlobal(pos));
            if (chosen == edit) {
                openEditor(nullptr, i);
            } else if (chosen == flip) {
                m_macros[i].toDevice = !m_macros[i].toDevice;
                saveMacros();
                rebuildButtons();
            } else if (chosen == del) {
                m_macros.remove(i);
                saveMacros();
                rebuildButtons();
            }
        });
        m_buttonRow->addWidget(btn);
        ++shown;
    }
    m_emptyHint->setVisible(shown == 0);
}

void MacroBar::openEditor(const Macro *prefill, int selectIndex) {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit macros"));
    dialog.setWindowIcon(QIcon(QStringLiteral(":/aetherbus/icon.ico")));
    dialog.resize(720, 320);
    auto *vbox = new QVBoxLayout(&dialog);
    vbox->addWidget(
        new QLabel(QStringLiteral("One macro per row. The <b>Preview</b> column shows the exact bytes that will be sent."), &dialog));

    auto *table = new QTableWidget(0, ColCount, &dialog);
    table->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Format"), QStringLiteral("Payload"), QStringLiteral("Ending"),
                                      QStringLiteral("Direction"), QStringLiteral("Preview")});
    table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(ColPayload, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(ColPreview, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);

    bool populating = false;

    // Refresh the read-only preview cell for a row from its current format/payload/ending.
    auto updatePreview = [table](int row) {
        if (row < 0 || row >= table->rowCount()) {
            return;
        }
        auto *fmt = qobject_cast<QComboBox *>(table->cellWidget(row, ColFormat));
        auto *end = qobject_cast<QComboBox *>(table->cellWidget(row, ColEnding));
        auto *preview = table->item(row, ColPreview);
        if (fmt == nullptr || end == nullptr || preview == nullptr) {
            return;
        }
        const QString payload = table->item(row, ColPayload) != nullptr ? table->item(row, ColPayload)->text() : QString();
        if (payload.isEmpty()) {
            preview->setText(QString());
            preview->setForeground(QPalette().color(QPalette::PlaceholderText));
            return;
        }
        QByteArray bytes;
        QString error;
        if (codec::encodePayload(fmt->currentIndex(), payload, end->currentIndex(), bytes, &error) && !bytes.isEmpty()) {
            preview->setText(QStringLiteral("%1  (%2 bytes)").arg(codec::toHex(bytes)).arg(bytes.size()));
            preview->setForeground(QPalette().color(QPalette::Text));
        } else {
            preview->setText(QStringLiteral("✗ %1").arg(error.isEmpty() ? QStringLiteral("empty") : error));
            preview->setForeground(QColor(0xC0, 0x30, 0x30));
        }
    };

    // Append one populated row; combos are cell widgets, name/payload are editable items.
    auto addRow = [&](const Macro &macro) {
        const int row = table->rowCount();
        table->insertRow(row);
        table->setItem(row, ColName, new QTableWidgetItem(macro.name));

        auto *fmt = makeFormatCombo(table);
        fmt->setCurrentIndex(macro.format);
        table->setCellWidget(row, ColFormat, fmt);

        table->setItem(row, ColPayload, new QTableWidgetItem(macro.payload));

        auto *end = makeEndingCombo(table);
        end->setCurrentIndex(macro.ending);
        table->setCellWidget(row, ColEnding, end);

        auto *dir = makeDirectionCombo(table);
        dir->setCurrentIndex(macro.toDevice ? 0 : 1);
        table->setCellWidget(row, ColDirection, dir);

        auto *preview = new QTableWidgetItem();
        preview->setFlags(preview->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, ColPreview, preview);

        connect(fmt, &QComboBox::currentIndexChanged, &dialog, [updatePreview, row] { updatePreview(row); });
        connect(end, &QComboBox::currentIndexChanged, &dialog, [updatePreview, row] { updatePreview(row); });
        if (!populating) {
            updatePreview(row);
        }
    };

    connect(table, &QTableWidget::itemChanged, &dialog, [&populating, updatePreview](QTableWidgetItem *item) {
        if (populating || item == nullptr) {
            return;
        }
        if (item->column() == ColName || item->column() == ColPayload) {
            updatePreview(item->row());
        }
    });

    populating = true;
    for (const Macro &macro : m_macros) {
        addRow(macro);
    }
    if (prefill != nullptr) {
        addRow(*prefill);
    }
    populating = false;
    for (int row = 0; row < table->rowCount(); ++row) {
        updatePreview(row);
    }

    vbox->addWidget(table, 1);

    // Row-management buttons.
    auto *rowBtns = new QHBoxLayout();
    auto *addBtn = new QPushButton(QStringLiteral("+ Add"), &dialog);
    auto *delBtn = new QPushButton(QStringLiteral("Delete"), &dialog);
    auto *upBtn = new QPushButton(QStringLiteral("▲"), &dialog);
    auto *downBtn = new QPushButton(QStringLiteral("▼"), &dialog);
    upBtn->setToolTip(QStringLiteral("Move selected macro up"));
    downBtn->setToolTip(QStringLiteral("Move selected macro down"));
    rowBtns->addWidget(addBtn);
    rowBtns->addWidget(delBtn);
    rowBtns->addWidget(upBtn);
    rowBtns->addWidget(downBtn);
    rowBtns->addStretch(1);
    vbox->addLayout(rowBtns);

    // Reads the whole table back into a Macro vector (source of truth for moves).
    auto snapshot = [table]() {
        QVector<Macro> rows;
        for (int row = 0; row < table->rowCount(); ++row) {
            Macro macro;
            macro.name = table->item(row, ColName) != nullptr ? table->item(row, ColName)->text().trimmed() : QString();
            macro.payload = table->item(row, ColPayload) != nullptr ? table->item(row, ColPayload)->text() : QString();
            if (auto *fmt = qobject_cast<QComboBox *>(table->cellWidget(row, ColFormat))) {
                macro.format = fmt->currentIndex();
            }
            if (auto *end = qobject_cast<QComboBox *>(table->cellWidget(row, ColEnding))) {
                macro.ending = end->currentIndex();
            }
            if (auto *dir = qobject_cast<QComboBox *>(table->cellWidget(row, ColDirection))) {
                macro.toDevice = dir->currentIndex() == 0;
            }
            rows.append(macro);
        }
        return rows;
    };

    // Rebuild the table from a Macro vector, restoring the selection.
    auto rebuild = [&](const QVector<Macro> &rows, int select) {
        populating = true;
        table->setRowCount(0);
        for (const Macro &macro : rows) {
            addRow(macro);
        }
        populating = false;
        for (int row = 0; row < table->rowCount(); ++row) {
            updatePreview(row);
        }
        if (select >= 0 && select < table->rowCount()) {
            table->selectRow(select);
        }
    };

    connect(addBtn, &QPushButton::clicked, &dialog, [&] {
        addRow(Macro{});
        table->selectRow(table->rowCount() - 1);
        table->editItem(table->item(table->rowCount() - 1, ColName));
    });
    connect(delBtn, &QPushButton::clicked, &dialog, [&] {
        const int row = table->currentRow();
        if (row >= 0) {
            QVector<Macro> rows = snapshot();
            rows.remove(row);
            rebuild(rows, qMin(row, static_cast<int>(rows.size()) - 1));
        }
    });
    connect(upBtn, &QPushButton::clicked, &dialog, [&] {
        const int row = table->currentRow();
        if (row > 0) {
            QVector<Macro> rows = snapshot();
            rows.swapItemsAt(row, row - 1);
            rebuild(rows, row - 1);
        }
    });
    connect(downBtn, &QPushButton::clicked, &dialog, [&] {
        const int row = table->currentRow();
        if (row >= 0 && row < table->rowCount() - 1) {
            QVector<Macro> rows = snapshot();
            rows.swapItemsAt(row, row + 1);
            rebuild(rows, row + 1);
        }
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        // Validate before committing: name is required and the payload must encode.
        QVector<Macro> parsed;
        for (const Macro &macro : snapshot()) {
            if (macro.name.isEmpty() && macro.payload.isEmpty()) {
                continue;  // blank row — ignore
            }
            if (macro.name.isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("Edit macros"), QStringLiteral("Every macro needs a name."));
                return;
            }
            QByteArray bytes;
            QString error;
            if (!codec::encodePayload(macro.format, macro.payload, macro.ending, bytes, &error) || bytes.isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("Edit macros"),
                                     QStringLiteral("Macro \"%1\" has an invalid payload: %2")
                                         .arg(macro.name, error.isEmpty() ? QStringLiteral("empty") : error));
                return;
            }
            parsed.append(macro);
        }
        m_macros = parsed;
        dialog.accept();
    });
    vbox->addWidget(buttons);

    if (selectIndex >= 0 && selectIndex < table->rowCount()) {
        table->selectRow(selectIndex);
    } else if (prefill != nullptr && table->rowCount() > 0) {
        table->selectRow(table->rowCount() - 1);
    }

    if (dialog.exec() != QDialog::Accepted) {
        loadMacros();  // discard any in-memory edits by reloading persisted set
        return;
    }
    saveMacros();
    rebuildButtons();
}

void MacroBar::editMacros() {
    openEditor(nullptr, -1);
}

void MacroBar::addMacroFromState(int format, const QString &payload, int ending, bool toDevice) {
    Macro macro;
    macro.name = QStringLiteral("Macro %1").arg(m_macros.size() + 1);
    macro.format = format;
    macro.payload = payload;
    macro.ending = ending;
    macro.toDevice = toDevice;
    openEditor(&macro, -1);
}

void MacroBar::pushHistory(const QByteArray &bytes, bool toDevice) {
    if (bytes.isEmpty()) {
        return;
    }
    const HistoryItem item{bytes, toDevice};
    // Drop any existing identical entry so the newest floats to the top.
    for (int i = static_cast<int>(m_history.size()) - 1; i >= 0; --i) {
        if (m_history.at(i).bytes == bytes && m_history.at(i).toDevice == toDevice) {
            m_history.remove(i);
            m_historyBox->removeItem(i);
        }
    }
    m_history.prepend(item);
    m_historyBox->insertItem(0,
                             QStringLiteral("%1 %2").arg(toDevice ? QStringLiteral("→Dev") : QStringLiteral("→App"), codec::toHex(bytes)));
    m_historyBox->setCurrentIndex(0);
    while (m_history.size() > kMaxHistory) {
        m_history.removeLast();
        m_historyBox->removeItem(m_historyBox->count() - 1);
    }
}

void MacroBar::resendSelected() {
    const int idx = m_historyBox->currentIndex();
    if (idx < 0 || idx >= static_cast<int>(m_history.size())) {
        return;
    }
    const HistoryItem &item = m_history.at(idx);
    emit send(item.bytes, item.toDevice);
}

}  // namespace aether
