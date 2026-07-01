#include "gui/sessions/can_session_widget.hpp"

#include "core/can/can_backend.hpp"
#include "core/common/format_codec.hpp"
#include "gui/panels/can_config_panel.hpp"
#include "gui/widgets/consoleview.hpp"
#include "gui/widgets/console_panel.hpp"
#include "gui/panels/can_decoder_panel.hpp"
#include "gui/widgets/statspanel.hpp"

#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QAction>
#include <QSettings>

namespace aether {

CanSessionWidget::CanSessionWidget(QWidget *parent) : SessionView(parent), m_backend(new CanBackend(this)) {
    buildUi();
    rescan();

    connect(m_configPanel, &CanConfigPanel::startCan, this, &CanSessionWidget::startCapture);
    connect(m_configPanel, &CanConfigPanel::stopCan, this, &CanSessionWidget::stopCapture);
    connect(m_configPanel, &CanConfigPanel::rescanRequested, this, &CanSessionWidget::rescan);

    connect(m_backend, &CanBackend::chunkCaptured, m_consolePanel->console(), &ConsoleView::appendChunk);
    connect(m_backend, &CanBackend::chunkCaptured, m_decoderPanel, &CanDecoderPanel::processChunk);
    connect(m_backend, &CanBackend::chunkCaptured, this, &CanSessionWidget::onChunkCaptured);
    connect(m_backend, &CanBackend::started, this, &CanSessionWidget::onStarted);
    connect(m_backend, &CanBackend::stopped, this, &CanSessionWidget::onStopped);
    connect(m_backend, &CanBackend::errorOccurred, this, &CanSessionWidget::onError);
    connect(m_backend, &CanBackend::disconnected, this, &CanSessionWidget::onDisconnected);

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1000);
    connect(m_statsTimer, &QTimer::timeout, this, [this] {
        m_stats.rollRates();
        updateCounts();
    });
    m_statsTimer->start();

    if (!CanBackend::isSupported()) {
        m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>SocketCAN is not available on this platform.</span>"));
    }

    m_consolePanel->console()->setNewlineMode(ConsoleView::NewlineMode::Frame, 0);
    connect(m_consolePanel, &ConsolePanel::formatChanged, this, &CanSessionWidget::applyFormats);
    applyFormats();
    loadMacros();
    rebuildMacroButtons();
}

CanSessionWidget::~CanSessionWidget() {
    if (m_backend) {
        m_backend->disconnect();
        delete m_backend;
        m_backend = nullptr;
    }
}

bool CanSessionWidget::isRunning() const {
    return m_backend && m_backend->isRunning();
}

void CanSessionWidget::stopSession() {
    if (m_backend && m_backend->isRunning()) {
        m_backend->close();
    }
}

void CanSessionWidget::buildUi() {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);

    auto *mainSplitter = new QSplitter(Qt::Horizontal, this);

    auto *leftColumn = new QWidget(mainSplitter);
    leftColumn->setMinimumWidth(320);
    auto *leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    m_configPanel = new CanConfigPanel(leftColumn);
    leftLayout->addWidget(m_configPanel);

    m_statsPanel = new StatsPanel(leftColumn);
    m_statsPanel->setActiveCalculator(&m_stats);
    m_statsPanel->setMinimumHeight(360);
    leftLayout->addWidget(m_statsPanel, 1);

    mainSplitter->addWidget(leftColumn);
    mainSplitter->addWidget(buildConsolePanel(this));

    m_decoderPanel = new CanDecoderPanel(mainSplitter);
    m_decoderPanel->setMinimumWidth(320);
    mainSplitter->addWidget(m_decoderPanel);

    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setStretchFactor(2, 0);
    mainSplitter->setSizes({340, 760, 340});

    outer->addWidget(mainSplitter);
}

QWidget *CanSessionWidget::buildConsolePanel(QWidget *parent) {
    auto *panel = new QWidget(parent);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_consolePanel = new ConsolePanel(panel);
    m_consolePanel->setSplitControlsVisible(false);
    m_consolePanel->setExtraActionsVisible(false);
    m_consolePanel->setSelectionLabelVisible(false);
    layout->addWidget(m_consolePanel, 1);

    // Save handling
    connect(m_consolePanel, &ConsolePanel::saveRequested, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save CAN log"), QStringLiteral("aetherbus_can.txt"),
                                                          QStringLiteral("Text files (*.txt);;All files (*)"));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (file.open(QFile::WriteOnly | QFile::Text)) {
            file.write(m_consolePanel->console()->toPlainText().toUtf8());
        } else {
            m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>Could not write %1</span>").arg(path));
        }
    });

    const auto makeAction = [&](const QString &text, const QString &tooltip) {
        auto *button = new QPushButton(text, panel);
        button->setToolTip(tooltip);
        button->setProperty("toolbarAction", true);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    const auto makeDivider = [&]() {
        auto *line = new QFrame(panel);
        line->setFrameShape(QFrame::VLine);
        line->setObjectName(QStringLiteral("toolbarDivider"));
        return line;
    };
    const auto makeSectionLabel = [&](const QString &text) {
        auto *label = new QLabel(text, panel);
        label->setObjectName(QStringLiteral("toolbarSectionLabel"));
        return label;
    };

    // --- Transmit bar (cansend-style) ---
    auto *txRow = new QHBoxLayout();
    txRow->setSpacing(6);
    txRow->addWidget(makeSectionLabel(QStringLiteral("Transmit")));

    m_txIdEdit = new QLineEdit(panel);
    m_txIdEdit->setPlaceholderText(QStringLiteral("ID (hex)"));
    m_txIdEdit->setFixedWidth(100);
    m_txIdEdit->setToolTip(QStringLiteral("Frame identifier in hexadecimal (e.g. 123 or 18DAF110)"));
    txRow->addWidget(m_txIdEdit);

    m_txFormatBox = new QComboBox(panel);
    m_txFormatBox->addItem(QStringLiteral("HEX"));
    m_txFormatBox->addItem(QStringLiteral("ASCII"));
    m_txFormatBox->addItem(QStringLiteral("DEC"));
    m_txFormatBox->addItem(QStringLiteral("BIN"));
    m_txFormatBox->setFixedWidth(72);
    m_txFormatBox->setToolTip(QStringLiteral("Payload input format"));
    txRow->addWidget(m_txFormatBox);

    m_txDataEdit = new QLineEdit(panel);
    m_txDataEdit->setPlaceholderText(QStringLiteral("payload hex, e.g. DE AD BE EF"));
    m_txDataEdit->setToolTip(QStringLiteral("Up to 8 bytes (classic) or 64 bytes (CAN-FD)"));
    txRow->addWidget(m_txDataEdit, 1);

    connect(m_txFormatBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        switch (idx) {
            case 0:
                m_txDataEdit->setPlaceholderText(QStringLiteral("payload hex, e.g. DE AD BE EF"));
                break;
            case 1:
                m_txDataEdit->setPlaceholderText(QStringLiteral("payload ascii, e.g. hello"));
                break;
            case 2:
                m_txDataEdit->setPlaceholderText(QStringLiteral("payload decimal, e.g. 65 66"));
                break;
            case 3:
                m_txDataEdit->setPlaceholderText(QStringLiteral("payload binary, e.g. 01000001"));
                break;
        }
    });

    m_txEffCheck = new QCheckBox(QStringLiteral("EFF"), panel);
    m_txEffCheck->setToolTip(QStringLiteral("29-bit extended identifier (auto-enabled for ids > 0x7FF)"));
    m_txRtrCheck = new QCheckBox(QStringLiteral("RTR"), panel);
    m_txRtrCheck->setToolTip(QStringLiteral("Remote-transmission-request frame (no payload)"));
    m_txFdCheck = new QCheckBox(QStringLiteral("FD"), panel);
    m_txFdCheck->setToolTip(QStringLiteral("Transmit as a CAN-FD frame"));
    m_txBrsCheck = new QCheckBox(QStringLiteral("BRS"), panel);
    m_txBrsCheck->setToolTip(QStringLiteral("CAN-FD bit-rate switch"));
    for (QCheckBox *box : {m_txEffCheck, m_txRtrCheck, m_txFdCheck, m_txBrsCheck}) {
        txRow->addWidget(box);
    }

    m_txButton = makeAction(QStringLiteral("Send"), QStringLiteral("Transmit the frame onto the bus"));
    connect(m_txButton, &QPushButton::clicked, this, &CanSessionWidget::transmit);
    connect(m_txDataEdit, &QLineEdit::returnPressed, this, &CanSessionWidget::transmit);
    connect(m_txIdEdit, &QLineEdit::returnPressed, this, &CanSessionWidget::transmit);
    txRow->addWidget(m_txButton);

    txRow->addWidget(makeDivider());
    txRow->addWidget(makeSectionLabel(QStringLiteral("History")));
    m_txHistoryBox = new QComboBox(panel);
    m_txHistoryBox->setMinimumWidth(200);
    m_txHistoryBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_txHistoryBox->setToolTip(QStringLiteral("Recall recently transmitted CAN frames"));
    connect(m_txHistoryBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0 || index >= m_txHistory.size())
            return;
        const auto &item = m_txHistory.at(index);
        m_txIdEdit->setText(QString::number(item.id, 16).toUpper());
        m_txDataEdit->setText(item.payload.toHex(' ').toUpper());
        m_txEffCheck->setChecked(item.flags & FrameExtendedId);
        m_txRtrCheck->setChecked(item.flags & FrameRemote);
        m_txFdCheck->setChecked(item.flags & FrameFd);
        m_txBrsCheck->setChecked(item.flags & FrameBitRateSwitch);
    });
    txRow->addWidget(m_txHistoryBox);

    auto *txResendBtn = makeAction(QStringLiteral("Resend"), QStringLiteral("Re-transmit the selected history frame"));
    connect(txResendBtn, &QPushButton::clicked, this, [this] {
        const int idx = m_txHistoryBox->currentIndex();
        if (idx < 0 || idx >= m_txHistory.size())
            return;
        const auto &item = m_txHistory.at(idx);
        m_backend->sendFrame(item.id, item.flags, item.payload);
    });
    txRow->addWidget(txResendBtn);

    layout->addLayout(txRow);

    // --- CAN Macro bar ---
    auto *macroRow = new QHBoxLayout();
    macroRow->setSpacing(6);
    macroRow->addWidget(makeSectionLabel(QStringLiteral("Macros")));

    m_macroContainer = new QWidget(panel);
    m_macroLayout = new QHBoxLayout(m_macroContainer);
    m_macroLayout->setContentsMargins(0, 0, 0, 0);
    m_macroLayout->setSpacing(6);
    macroRow->addWidget(m_macroContainer);

    macroRow->addStretch(1);

    auto *saveMacroBtn =
        makeAction(QStringLiteral("★ Save as macro"), QStringLiteral("Save current transmit fields as a quick-send macro"));
    connect(saveMacroBtn, &QPushButton::clicked, this, &CanSessionWidget::saveCurrentAsMacro);
    macroRow->addWidget(saveMacroBtn);

    layout->addLayout(macroRow);

    connect(m_consolePanel->console(), &ConsoleView::countsChanged, this, [this](qint64, qint64, qint64, qint64) { updateCounts(); });

    return panel;
}

void CanSessionWidget::rescan() {
    m_configPanel->populateInterfaces(CanBackend::listInterfaces());
    m_configPanel->setStatus(QStringLiteral("Interface list refreshed."));
}

void CanSessionWidget::startCapture(const CanConfig &cfg) {
    m_backend->open(cfg);
}

void CanSessionWidget::stopCapture() {
    m_backend->close();
}

void CanSessionWidget::onStarted(const QString &info) {
    m_configPanel->setRunningState(true);
    m_configPanel->setStatus(QStringLiteral("Capturing on <b>%1</b>").arg(info));
    emit sessionTitleChanged(QStringLiteral("[CAN] %1").arg(m_configPanel->iface()));
}

void CanSessionWidget::onStopped() {
    m_configPanel->setRunningState(false);
    m_configPanel->setStatus(QStringLiteral("Stopped."));
    emit sessionTitleChanged(QStringLiteral("CAN Session"));
}

void CanSessionWidget::onError(const QString &message) {
    m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>%1</span>").arg(message));
}

void CanSessionWidget::onDisconnected() {
    m_configPanel->setRunningState(false);
    m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>Interface lost.</span>"));
    emit sessionTitleChanged(QStringLiteral("CAN Session"));
}

void CanSessionWidget::onChunkCaptured(const aether::CapturedChunk &chunk) {
    m_stats.addChunk(chunk);
}

void CanSessionWidget::transmit() {
    bool okId = false;
    const quint32 id = m_txIdEdit->text().trimmed().toUInt(&okId, 16);
    if (!okId) {
        m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>Invalid transmit id.</span>"));
        return;
    }

    QByteArray payload;
    QString error;
    bool okData = codec::encodePayload(m_txFormatBox->currentIndex(), m_txDataEdit->text(), 0, payload, &error);
    if (!okData) {
        m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>%1</span>").arg(error));
        return;
    }

    quint16 flags = 0;
    if (m_txEffCheck->isChecked() || id > 0x7FF) {
        flags |= FrameExtendedId;
    }
    if (m_txRtrCheck->isChecked()) {
        flags |= FrameRemote;
    }
    if (m_txFdCheck->isChecked()) {
        flags |= FrameFd;
    }
    if (m_txBrsCheck->isChecked()) {
        flags |= FrameBitRateSwitch;
    }

    if (!m_backend->sendFrame(id, flags, payload)) {
        m_configPanel->setStatus(m_backend->isRunning()
                                     ? QStringLiteral("<span style='color:#e57373'>Transmit failed (payload too large?).</span>")
                                     : QStringLiteral("<span style='color:#e57373'>Start capture before transmitting.</span>"));
    } else {
        // Add to history
        CanHistoryItem item{id, payload, flags};
        for (int i = m_txHistory.size() - 1; i >= 0; --i) {
            if (m_txHistory.at(i).id == id && m_txHistory.at(i).payload == payload && m_txHistory.at(i).flags == flags) {
                m_txHistory.remove(i);
                m_txHistoryBox->removeItem(i);
            }
        }
        m_txHistory.prepend(item);
        QString display = QStringLiteral("ID: %1 | Data: %2")
                              .arg(QString::number(id, 16).toUpper())
                              .arg(payload.isEmpty() ? QStringLiteral("<empty>") : QString::fromLatin1(payload.toHex(' ').toUpper()));
        m_txHistoryBox->insertItem(0, display);

        m_txHistoryBox->blockSignals(true);
        m_txHistoryBox->setCurrentIndex(0);
        m_txHistoryBox->blockSignals(false);

        constexpr int kMaxHistory = 50;
        while (m_txHistory.size() > kMaxHistory) {
            m_txHistory.removeLast();
            m_txHistoryBox->removeItem(m_txHistoryBox->count() - 1);
        }
    }
}

void CanSessionWidget::applyFormats() {
    m_consolePanel->console()->setFormats(m_consolePanel->isHexChecked(), m_consolePanel->isDecChecked(), m_consolePanel->isBinChecked(),
                                          m_consolePanel->isAsciiChecked());
}

void CanSessionWidget::updateCounts() {
    const auto fmtRate = [](double bytesPerSec) -> QString {
        if (bytesPerSec >= 1024.0) {
            return QStringLiteral("%1 KB/s").arg(bytesPerSec / 1024.0, 0, 'f', 1);
        }
        return QStringLiteral("%1 B/s").arg(static_cast<qint64>(bytesPerSec));
    };
    const QString text = QStringLiteral("Rx: %1 frm (%2)  Tx: %3 frm (%4)")
                             .arg(m_stats.rxChunks())
                             .arg(fmtRate(m_stats.currentRxRate()))
                             .arg(m_stats.txChunks())
                             .arg(fmtRate(m_stats.currentTxRate()));
    m_consolePanel->setCountsText(text);
}

void CanSessionWidget::loadMacros() {
    m_macros.clear();
    QSettings settings;
    const int count = settings.beginReadArray(QStringLiteral("can_macros"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        CanMacro macro;
        macro.name = settings.value(QStringLiteral("name")).toString();
        macro.id = settings.value(QStringLiteral("id")).toUInt();
        macro.payload = settings.value(QStringLiteral("payload")).toByteArray();
        macro.flags = static_cast<quint16>(settings.value(QStringLiteral("flags")).toUInt());
        m_macros.append(macro);
    }
    settings.endArray();
}

void CanSessionWidget::saveMacros() {
    QSettings settings;
    settings.beginWriteArray(QStringLiteral("can_macros"));
    for (int i = 0; i < m_macros.size(); ++i) {
        settings.setArrayIndex(i);
        const auto &macro = m_macros.at(i);
        settings.setValue(QStringLiteral("name"), macro.name);
        settings.setValue(QStringLiteral("id"), macro.id);
        settings.setValue(QStringLiteral("payload"), macro.payload);
        settings.setValue(QStringLiteral("flags"), macro.flags);
    }
    settings.endArray();
}

void CanSessionWidget::rebuildMacroButtons() {
    // Clear layout
    QLayoutItem *child;
    while ((child = m_macroLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }

    if (m_macros.isEmpty()) {
        m_emptyMacroHint = new QLabel(QStringLiteral("<i>none yet — click ★ Save as macro</i>"), m_macroContainer);
        m_macroLayout->addWidget(m_emptyMacroHint);
        return;
    }
    m_emptyMacroHint = nullptr;

    for (int i = 0; i < m_macros.size(); ++i) {
        const auto &macro = m_macros.at(i);
        auto *btn = new QPushButton(macro.name, m_macroContainer);
        btn->setProperty("toolbarAction", true);
        btn->setCursor(Qt::PointingHandCursor);

        QString details =
            QStringLiteral("ID: %1 | Data: %2")
                .arg(QString::number(macro.id, 16).toUpper())
                .arg(macro.payload.isEmpty() ? QStringLiteral("<empty>") : QString::fromLatin1(macro.payload.toHex(' ').toUpper()));
        btn->setToolTip(details);

        connect(btn, &QPushButton::clicked, this, [this, macro] { m_backend->sendFrame(macro.id, macro.flags, macro.payload); });

        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QPushButton::customContextMenuRequested, this, [this, i](const QPoint &pos) {
            auto *senderBtn = qobject_cast<QPushButton *>(sender());
            if (!senderBtn)
                return;
            QMenu menu(this);
            auto *deleteAct = menu.addAction(QStringLiteral("Delete Macro"));
            connect(deleteAct, &QAction::triggered, this, [this, i] {
                if (i >= 0 && i < m_macros.size()) {
                    m_macros.removeAt(i);
                    saveMacros();
                    rebuildMacroButtons();
                }
            });
            menu.exec(senderBtn->mapToGlobal(pos));
        });

        m_macroLayout->addWidget(btn);
    }
}

void CanSessionWidget::saveCurrentAsMacro() {
    bool okId = false;
    const quint32 id = m_txIdEdit->text().trimmed().toUInt(&okId, 16);
    if (!okId) {
        m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>Invalid transmit id.</span>"));
        return;
    }

    QByteArray payload;
    QString error;
    bool okData = codec::encodePayload(m_txFormatBox->currentIndex(), m_txDataEdit->text(), 0, payload, &error);
    if (!okData) {
        m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>%1</span>").arg(error));
        return;
    }

    quint16 flags = 0;
    if (m_txEffCheck->isChecked() || id > 0x7FF) {
        flags |= FrameExtendedId;
    }
    if (m_txRtrCheck->isChecked()) {
        flags |= FrameRemote;
    }
    if (m_txFdCheck->isChecked()) {
        flags |= FrameFd;
    }
    if (m_txBrsCheck->isChecked()) {
        flags |= FrameBitRateSwitch;
    }

    bool nameOk = false;
    QString name = QInputDialog::getText(this, QStringLiteral("Save CAN Macro"), QStringLiteral("Enter a name for this macro:"),
                                         QLineEdit::Normal, QString(), &nameOk);
    if (!nameOk || name.trimmed().isEmpty()) {
        return;
    }

    CanMacro macro{name.trimmed(), id, payload, flags};
    m_macros.append(macro);
    saveMacros();
    rebuildMacroButtons();
}

}  // namespace aether
