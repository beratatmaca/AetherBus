#include "gui/widgets/macro_button_bar.hpp"

#include <QLayoutItem>

namespace aether {

MacroButtonBar::MacroButtonBar(QWidget *parent) : QWidget(parent) {
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(6);
}

void MacroButtonBar::rebuildButtons() {
    // Remove and destroy all existing children.
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    m_emptyHint = nullptr;

    const int count = macroCount();
    if (count == 0) {
        m_emptyHint = new QLabel(tr("<i>none yet — click ★ Save as macro</i>"), this);
        m_emptyHint->setEnabled(false);
        m_layout->addWidget(m_emptyHint);
        return;
    }

    for (int i = 0; i < count; ++i) {
        auto *btn = new QPushButton(macroName(i), this);
        btn->setObjectName(macroName(i));
        btn->setProperty("toolbarAction", true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(macroToolTip(i));

        connect(btn, &QPushButton::clicked, this, [this, i] { onMacroTriggered(i); });

        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QPushButton::customContextMenuRequested, this, [this, i, btn](const QPoint &pos) {
            QMenu menu(this);
            buildContextMenu(i, menu);
            if (!menu.isEmpty()) {
                menu.exec(btn->mapToGlobal(pos));
            }
        });

        m_layout->addWidget(btn);
    }
}

bool MacroButtonBar::triggerMacro(int index) {
    if (index < 0 || index >= macroCount()) {
        return false;
    }
    onMacroTriggered(index);
    return true;
}

int MacroButtonBar::indexOfMacro(const QString &name) const {
    const int count = macroCount();
    for (int i = 0; i < count; ++i) {
        if (macroName(i) == name) {
            return i;
        }
    }
    return -1;
}

}  // namespace aether
