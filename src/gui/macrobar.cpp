#include "gui/macrobar.h"

#include "core/format_codec.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace aether {

namespace {
constexpr int kMaxHistory = 25;
} // namespace

MacroBar::MacroBar(QWidget *parent) : QWidget(parent) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(new QLabel(QStringLiteral("Macros:"), this));
    m_buttonRow = new QHBoxLayout();
    layout->addLayout(m_buttonRow);

    auto *editBtn = new QPushButton(QStringLiteral("Edit…"), this);
    editBtn->setToolTip(QStringLiteral("Define named quick-send hex macros"));
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
        const QString name = settings.value(QStringLiteral("name")).toString();
        const QByteArray hex = settings.value(QStringLiteral("hex")).toString().toLatin1();
        m_macros.append({name, QByteArray::fromHex(hex)});
    }
    settings.endArray();
}

void MacroBar::saveMacros() {
    QSettings settings;
    settings.beginWriteArray(QStringLiteral("macros"));
    for (int i = 0; i < m_macros.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("name"), m_macros.at(i).name);
        settings.setValue(QStringLiteral("hex"), QString::fromLatin1(m_macros.at(i).bytes.toHex()));
    }
    settings.endArray();
}

void MacroBar::rebuildButtons() {
    // Clear existing macro buttons.
    while (QLayoutItem *item = m_buttonRow->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const Macro &macro : m_macros) {
        if (macro.name.isEmpty() || macro.bytes.isEmpty()) {
            continue;
        }
        auto *btn = new QPushButton(macro.name, this);
        btn->setToolTip(codec::toHex(macro.bytes));
        const QByteArray bytes = macro.bytes;
        connect(btn, &QPushButton::clicked, this, [this, bytes] { emit send(bytes, true); });
        m_buttonRow->addWidget(btn);
    }
}

void MacroBar::editMacros() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Edit macros"));
    auto *vbox = new QVBoxLayout(&dialog);
    vbox->addWidget(new QLabel(
        QStringLiteral("One macro per line, as <b>Name = hex bytes</b> (e.g. <tt>Reset = 1B 40</tt>)."),
        &dialog));

    auto *editor = new QPlainTextEdit(&dialog);
    QString text;
    for (const Macro &macro : m_macros) {
        text += QStringLiteral("%1 = %2\n").arg(macro.name, codec::toHex(macro.bytes));
    }
    editor->setPlainText(text);
    vbox->addWidget(editor);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    vbox->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QVector<Macro> parsed;
    const QStringList lines = editor->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq < 0) {
            continue;
        }
        const QString name = line.left(eq).trimmed();
        QByteArray bytes;
        if (name.isEmpty() || !codec::parseHexString(line.mid(eq + 1), bytes) || bytes.isEmpty()) {
            continue; // silently skip malformed lines
        }
        parsed.append({name, bytes});
    }
    m_macros = parsed;
    saveMacros();
    rebuildButtons();
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
    m_historyBox->insertItem(0, QStringLiteral("%1 %2").arg(toDevice ? QStringLiteral("→Dev")
                                                                     : QStringLiteral("→App"),
                                                            codec::toHex(bytes)));
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

} // namespace aether
