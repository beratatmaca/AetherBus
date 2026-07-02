#include "gui/widgets/console_panel.hpp"
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTextDocument>
#include <QSplitter>
#include "gui/widgets/byte_inspector_panel.hpp"
#include "core/common/format_codec.hpp"

namespace aether {

ConsolePanel::ConsolePanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 0, 6, 0);
    layout->setSpacing(6);

    m_console = new ConsoleView(this);

    const auto markToolbarButton = [](QPushButton *button, const char *kind) {
        button->setProperty(kind, true);
        button->setCursor(Qt::PointingHandCursor);
    };
    const auto makeToggle = [&](const QString &text, const QString &tooltip) {
        auto *button = new QPushButton(text, this);
        button->setCheckable(true);
        button->setToolTip(tooltip);
        markToolbarButton(button, "toolbarToggle");
        return button;
    };
    const auto makeAction = [&](const QString &text, const QString &tooltip) {
        auto *button = new QPushButton(text, this);
        button->setToolTip(tooltip);
        markToolbarButton(button, "toolbarAction");
        return button;
    };
    const auto makeDivider = [&]() {
        auto *line = new QFrame(this);
        line->setFrameShape(QFrame::VLine);
        line->setObjectName(QStringLiteral("toolbarDivider"));
        return line;
    };
    const auto makeSectionLabel = [&](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setObjectName(QStringLiteral("toolbarSectionLabel"));
        return label;
    };

    // Row 1: View formats + Split settings (optional)
    auto *row1 = new QHBoxLayout();
    row1->setSpacing(6);
    row1->addWidget(makeSectionLabel(QStringLiteral("View")));

    m_hexCheck = makeToggle(QStringLiteral("HEX"), QStringLiteral("Show hexadecimal bytes"));
    m_decCheck = makeToggle(QStringLiteral("DEC"), QStringLiteral("Show decimal byte values"));
    m_binCheck = makeToggle(QStringLiteral("BIN"), QStringLiteral("Show binary byte values"));
    m_asciiCheck = makeToggle(QStringLiteral("ASCII"), QStringLiteral("Show ASCII gutter"));
    m_hexCheck->setChecked(true);
    m_asciiCheck->setChecked(true);

    for (QPushButton *button : {m_hexCheck, m_decCheck, m_binCheck, m_asciiCheck}) {
        button->setProperty("segmentButton", true);
        connect(button, &QPushButton::toggled, this, &ConsolePanel::updateConsoleFormats);
        row1->addWidget(button);
    }

    // Split container (serial split settings)
    m_splitContainer = new QWidget(this);
    auto *splitLayout = new QHBoxLayout(m_splitContainer);
    splitLayout->setContentsMargins(0, 0, 0, 0);
    splitLayout->setSpacing(6);
    splitLayout->addWidget(makeDivider());
    splitLayout->addWidget(makeSectionLabel(QStringLiteral("Split")));

    m_newlineModeBox = new QComboBox(m_splitContainer);
    m_newlineModeBox->addItem(QStringLiteral("CR"), 0);
    m_newlineModeBox->addItem(QStringLiteral("LF"), 1);
    m_newlineModeBox->addItem(QStringLiteral("CR+LF"), 2);
    m_newlineModeBox->addItem(QStringLiteral("Every packet/chunk"), 3);
    m_newlineModeBox->addItem(QStringLiteral("Every N bytes"), 4);
    m_newlineModeBox->addItem(QStringLiteral("Header byte array"), 5);
    m_newlineModeBox->setCurrentIndex(2);
    m_newlineModeBox->setToolTip(
        QStringLiteral("Split incoming streams into lines by:\n"
                       "CR/LF/CR+LF: carriage return, line feed, or both\n"
                       "Every packet/chunk: one line per captured chunk\n"
                       "Every N bytes: fixed byte count per line\n"
                       "Header byte array: split when header pattern is found"));

    m_newlineParamEdit = new QLineEdit(m_splitContainer);
    m_newlineParamEdit->setFixedWidth(100);
    m_newlineParamEdit->setPlaceholderText(QStringLiteral("param…"));

    m_newlineFormatBox = new QComboBox(m_splitContainer);
    m_newlineFormatBox->addItem(QStringLiteral("HEX"));
    m_newlineFormatBox->addItem(QStringLiteral("ASCII"));
    m_newlineFormatBox->addItem(QStringLiteral("DEC"));
    m_newlineFormatBox->addItem(QStringLiteral("BIN"));
    m_newlineFormatBox->setFixedWidth(60);
    m_newlineFormatBox->setToolTip(QStringLiteral("Data format for header pattern input"));
    m_newlineFormatBox->hide();

    connect(m_newlineModeBox, &QComboBox::currentIndexChanged, this, &ConsolePanel::newlineModeChanged);
    connect(m_newlineParamEdit, &QLineEdit::editingFinished, this, &ConsolePanel::newlineModeChanged);
    connect(m_newlineFormatBox, &QComboBox::currentIndexChanged, this, &ConsolePanel::newlineModeChanged);

    splitLayout->addWidget(m_newlineModeBox);
    splitLayout->addWidget(m_newlineFormatBox);
    splitLayout->addWidget(m_newlineParamEdit);
    row1->addWidget(m_splitContainer);

    row1->addStretch(1);
    layout->addLayout(row1);

    // Row 2: Flow controls, stats, action buttons, selection label, search/find
    auto *row2 = new QHBoxLayout();
    row2->setSpacing(6);
    row2->addWidget(makeSectionLabel(QStringLiteral("Flow")));

    m_autoScrollCheck = makeToggle(QStringLiteral("Auto"), QStringLiteral("Automatically scroll to the end of the log"));
    m_autoScrollCheck->setChecked(true);
    connect(m_autoScrollCheck, &QPushButton::toggled, m_console, &ConsoleView::setAutoScroll);

    m_pauseCheck = makeToggle(QStringLiteral("Pause"), QStringLiteral("Suspend UI viewport updates"));
    connect(m_pauseCheck, &QPushButton::toggled, m_console, &ConsoleView::setPaused);

    m_tsCheck = makeToggle(QStringLiteral("Time"), QStringLiteral("Show or hide the timestamp prefix"));
    m_tsCheck->setChecked(true);
    connect(m_tsCheck, &QPushButton::toggled, m_console, &ConsoleView::setShowTimestamps);

    row2->addWidget(m_autoScrollCheck);
    row2->addWidget(m_pauseCheck);
    row2->addWidget(m_tsCheck);

    row2->addWidget(makeDivider());
    m_countsLabel = new QLabel(QStringLiteral("Rx: 0  Tx: 0"), this);
    m_countsLabel->setObjectName(QStringLiteral("consoleCountsLabel"));
    m_countsLabel->setToolTip(QStringLiteral("Cumulative stats. Rates update every second."));
    row2->addWidget(m_countsLabel);

    auto *resetBtn = makeAction(QStringLiteral("Reset"), QStringLiteral("Clear Tx/Rx counters"));
    connect(resetBtn, &QPushButton::clicked, m_console, &ConsoleView::resetCounts);
    row2->addWidget(resetBtn);

    row2->addWidget(makeDivider());
    row2->addWidget(makeSectionLabel(QStringLiteral("Actions")));

    m_clearBtn = makeAction(QStringLiteral("Clear"), QStringLiteral("Clear all text from viewport and raw history cache"));
    connect(m_clearBtn, &QPushButton::clicked, m_console, &ConsoleView::clearConsole);

    m_saveBtn = makeAction(QStringLiteral("Save"), QStringLiteral("Export all currently captured plain text to a file"));
    connect(m_saveBtn, &QPushButton::clicked, this, &ConsolePanel::saveRequested);
    row2->addWidget(m_clearBtn);
    row2->addWidget(m_saveBtn);

    // Extra actions container (log, capture, replay)
    m_extraActionsContainer = new QWidget(this);
    auto *extraActionsLayout = new QHBoxLayout(m_extraActionsContainer);
    extraActionsLayout->setContentsMargins(0, 0, 0, 0);
    extraActionsLayout->setSpacing(6);

    m_logBtn = makeAction(QStringLiteral("Log"), QStringLiteral("Continuously append every line to a file"));
    m_logBtn->setCheckable(true);
    connect(m_logBtn, &QPushButton::clicked, this, &ConsolePanel::logToggled);

    m_captureBtn = makeAction(QStringLiteral("Capture"), QStringLiteral("Record raw traffic to a pcap file"));
    m_captureBtn->setCheckable(true);
    connect(m_captureBtn, &QPushButton::clicked, this, &ConsolePanel::captureToggled);

    m_replayBtn = makeAction(QStringLiteral("Replay"), QStringLiteral("Open and replay a captured pcap file"));
    m_replayBtn->setCheckable(true);
    connect(m_replayBtn, &QPushButton::clicked, this, &ConsolePanel::replayToggled);

    extraActionsLayout->addWidget(m_logBtn);
    extraActionsLayout->addWidget(m_captureBtn);
    extraActionsLayout->addWidget(m_replayBtn);
    row2->addWidget(m_extraActionsContainer);

    m_selLabel = new QLabel(QStringLiteral("Sel: 0"), this);
    m_selLabel->setObjectName(QStringLiteral("consoleSelectionLabel"));
    row2->addWidget(makeDivider());
    row2->addWidget(m_selLabel);

    row2->addStretch(1);
    row2->addWidget(makeSectionLabel(QStringLiteral("Find")));

    auto *searchModeBox = new QComboBox(this);
    searchModeBox->addItems(
        {QStringLiteral("Auto"), QStringLiteral("Text"), QStringLiteral("HEX"), QStringLiteral("DEC"), QStringLiteral("BIN")});
    searchModeBox->setToolTip(QStringLiteral("Interpret search input mode"));
    connect(searchModeBox, &QComboBox::currentIndexChanged, this,
            [this](int index) { m_console->setSearchMode(static_cast<ConsoleView::SearchMode>(index)); });
    row2->addWidget(searchModeBox);

    m_findEdit = new QLineEdit(this);
    m_findEdit->setPlaceholderText(QStringLiteral("find…"));
    m_findEdit->setFixedWidth(160);
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this] { doFind(false); });
    connect(m_findEdit, &QLineEdit::textChanged, m_console, &ConsoleView::highlightSearchText);
    row2->addWidget(m_findEdit);

    auto *findPrevBtn = makeAction(QStringLiteral("◀"), QStringLiteral("Find previous"));
    auto *findNextBtn = makeAction(QStringLiteral("▶"), QStringLiteral("Find next"));
    findPrevBtn->setFixedWidth(32);
    findNextBtn->setFixedWidth(32);
    connect(findPrevBtn, &QPushButton::clicked, this, [this] { doFind(true); });
    connect(findNextBtn, &QPushButton::clicked, this, [this] { doFind(false); });
    row2->addWidget(findPrevBtn);
    row2->addWidget(findNextBtn);

    m_matchCountLabel = new QLabel(this);
    connect(m_console, &ConsoleView::searchMatchCount, m_matchCountLabel, [this](int count) {
        m_matchCountLabel->setText(count > 0 ? QStringLiteral("%1 match%2").arg(count).arg(count == 1 ? QString() : QStringLiteral("es"))
                                             : QString());
    });
    row2->addWidget(m_matchCountLabel);

    layout->addLayout(row2);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setObjectName(QStringLiteral("consoleInspectorSplitter"));
    splitter->addWidget(m_console);

    m_inspector = new ByteInspectorPanel(splitter);
    m_inspector->hide();
    splitter->addWidget(m_inspector);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    layout->addWidget(splitter, 1);

    // Track active selection count
    connect(m_console, &ConsoleView::selectionChars, this,
            [this](int count) { m_selLabel->setText(QStringLiteral("Sel: %1").arg(count)); });
    connect(m_console, &ConsoleView::selectionChars, this, [this](int count) {
        if (count == 0 && m_inspector) {
            m_inspector->setBytes(QByteArray());
            m_inspector->hide();
        }
    });
    connect(m_console, &ConsoleView::selectionChars, this, [this] { onSelectionChanged(); });
}

ConsolePanel::~ConsolePanel() = default;

bool ConsolePanel::isHexChecked() const {
    return m_hexCheck->isChecked();
}
bool ConsolePanel::isDecChecked() const {
    return m_decCheck->isChecked();
}
bool ConsolePanel::isBinChecked() const {
    return m_binCheck->isChecked();
}
bool ConsolePanel::isAsciiChecked() const {
    return m_asciiCheck->isChecked();
}
bool ConsolePanel::isTimeChecked() const {
    return m_tsCheck->isChecked();
}
bool ConsolePanel::isPausedChecked() const {
    return m_pauseCheck->isChecked();
}
bool ConsolePanel::isAutoScrollChecked() const {
    return m_autoScrollCheck->isChecked();
}

void ConsolePanel::setHexChecked(bool checked) {
    m_hexCheck->setChecked(checked);
}
void ConsolePanel::setDecChecked(bool checked) {
    m_decCheck->setChecked(checked);
}
void ConsolePanel::setBinChecked(bool checked) {
    m_binCheck->setChecked(checked);
}
void ConsolePanel::setAsciiChecked(bool checked) {
    m_asciiCheck->setChecked(checked);
}
void ConsolePanel::setTimeChecked(bool checked) {
    m_tsCheck->setChecked(checked);
}
void ConsolePanel::setPausedChecked(bool checked) {
    m_pauseCheck->setChecked(checked);
}
void ConsolePanel::setAutoScrollChecked(bool checked) {
    m_autoScrollCheck->setChecked(checked);
}

void ConsolePanel::setSplitControlsVisible(bool visible) {
    m_splitContainer->setVisible(visible);
}
void ConsolePanel::setExtraActionsVisible(bool visible) {
    m_extraActionsContainer->setVisible(visible);
}
void ConsolePanel::setSelectionLabelVisible(bool visible) {
    m_selLabel->setVisible(visible);
}

void ConsolePanel::setCountsText(const QString &text) {
    m_countsLabel->setText(text);
}
void ConsolePanel::setSelectionText(const QString &text) {
    m_selLabel->setText(text);
}

void ConsolePanel::updateConsoleFormats() {
    m_console->setFormats(m_hexCheck->isChecked(), m_decCheck->isChecked(), m_binCheck->isChecked(), m_asciiCheck->isChecked());
    emit formatChanged();
}

void ConsolePanel::doFind(bool backward) {
    QString text = m_findEdit->text();
    if (text.isEmpty()) {
        return;
    }
    QTextDocument::FindFlags flags;
    if (backward) {
        flags |= QTextDocument::FindBackward;
    }
    if (!m_console->findQuery(text, flags)) {
        if (backward) {
            m_console->moveCursorToEnd();
        } else {
            m_console->moveCursorToStart();
        }
        m_console->findQuery(text, flags);
    }
}

void ConsolePanel::onSelectionChanged() {
    if (!m_inspector) {
        return;
    }

    QString text = m_console->selectedText().trimmed();

    // Remove all bracketed prefixes at the start (timestamps, IDs, direction tags)
    while (text.startsWith(QLatin1Char('['))) {
        int idx = text.indexOf(QLatin1Char(']'));
        if (idx == -1)
            break;
        text = text.mid(idx + 1).trimmed();
    }

    if (text.isEmpty()) {
        m_inspector->setBytes(QByteArray());
        m_inspector->hide();
        return;
    }

    QByteArray bytes;
    // Try structured parses first, then fall back to raw ASCII/UTF-8 bytes.
    if (!codec::parseHexString(text, bytes) && !codec::parseDecString(text, bytes) && !codec::parseBinString(text, bytes)) {
        bytes = text.toUtf8();
    }

    m_inspector->setBytes(bytes);
    m_inspector->setVisible(!bytes.isEmpty());
}

}  // namespace aether
