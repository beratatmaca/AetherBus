#include "gui/dialogs/welcome_tutorial_dialog.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QSettings>
#include <QIcon>
#include <QFile>
#include <QTextBrowser>

namespace aether {

WelcomeTutorialDialog::WelcomeTutorialDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Welcome to AetherBus"));
    setWindowIcon(QIcon(QStringLiteral(":/aetherbus/icon.ico")));
    setFixedSize(580, 480);
    setupUi();
}

void WelcomeTutorialDialog::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 16);
    mainLayout->setSpacing(16);

    m_stack = new QStackedWidget(this);

    // Read Usage.md
    QFile file(QStringLiteral(":/aetherbus/Usage.md"));
    QString markdownContent;
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        markdownContent = QString::fromUtf8(file.readAll());
    }

    // Split slides by horizontal rule "---"
    QStringList slides = markdownContent.split(QStringLiteral("\n---"));
    if (slides.isEmpty()) {
        slides << QStringLiteral("# Welcome\n\nNo documentation found.");
    }

    for (const QString &slideText : slides) {
        auto *browser = new QTextBrowser(this);
        browser->setFrameStyle(QFrame::NoFrame);
        browser->setReadOnly(true);
        browser->setOpenExternalLinks(true);
        // QTextBrowser is a QTextEdit, so leaving color unset would inherit the
        // theme's global "QTextEdit, QPlainTextEdit" rule meant for the
        // hex/ASCII console views (terminal green) — wrong here. Reference the
        // palette directly instead of a hardcoded hex so this is always
        // correct for the active theme (a fixed hex was the earlier bug: fine
        // on dark, nearly invisible on light).
        browser->setStyleSheet(QStringLiteral("background: transparent; color: palette(text); font-size: 11pt;"));
        browser->setMarkdown(slideText.trimmed());
        m_stack->addWidget(browser);
    }

    mainLayout->addWidget(m_stack);

    // Bottom controls row
    auto *bottomRow = new QHBoxLayout();

    m_dontShowCheck = new QCheckBox(tr("Don't show this again"), this);
    connect(m_dontShowCheck, &QCheckBox::toggled, this, [](bool checked) {
        QSettings settings;
        settings.setValue(QStringLiteral("ui/show_tutorial"), !checked);
    });
    bottomRow->addWidget(m_dontShowCheck);

    bottomRow->addStretch();

    m_indicatorLabel = new QLabel(QStringLiteral("1 / %1").arg(m_stack->count()), this);
    m_indicatorLabel->setStyleSheet(QStringLiteral("color: #808080; font-weight: bold;"));
    bottomRow->addWidget(m_indicatorLabel);

    bottomRow->addSpacing(16);

    m_backBtn = new QPushButton(tr("Back"), this);
    m_backBtn->setEnabled(false);
    connect(m_backBtn, &QPushButton::clicked, this, &WelcomeTutorialDialog::prevStep);
    bottomRow->addWidget(m_backBtn);

    m_nextBtn = new QPushButton(tr("Next"), this);
    m_nextBtn->setDefault(true);
    connect(m_nextBtn, &QPushButton::clicked, this, &WelcomeTutorialDialog::nextStep);
    bottomRow->addWidget(m_nextBtn);

    mainLayout->addLayout(bottomRow);
}

void WelcomeTutorialDialog::nextStep() {
    int nextIdx = m_stack->currentIndex() + 1;
    if (nextIdx >= m_stack->count()) {
        finishTutorial();
        return;
    }

    m_stack->setCurrentIndex(nextIdx);
    m_backBtn->setEnabled(true);
    m_indicatorLabel->setText(QStringLiteral("%1 / %2").arg(nextIdx + 1).arg(m_stack->count()));

    if (nextIdx == m_stack->count() - 1) {
        m_nextBtn->setText(tr("Finish"));
    }
}

void WelcomeTutorialDialog::prevStep() {
    int prevIdx = m_stack->currentIndex() - 1;
    if (prevIdx < 0) {
        return;
    }

    m_stack->setCurrentIndex(prevIdx);
    m_nextBtn->setText(tr("Next"));
    m_indicatorLabel->setText(QStringLiteral("%1 / %2").arg(prevIdx + 1).arg(m_stack->count()));

    if (prevIdx == 0) {
        m_backBtn->setEnabled(false);
    }
}

void WelcomeTutorialDialog::finishTutorial() {
    // The checkbox's own toggled handler already persists the preference.
    accept();
}

}  // namespace aether
