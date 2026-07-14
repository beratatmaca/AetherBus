#pragma once

#include <QDialog>

class QStackedWidget;
class QPushButton;
class QCheckBox;
class QLabel;

namespace aether {

/**
 * @brief First-run tutorial dialog that pages through the slides of the bundled
 * Usage.md, with a "don't show this again" preference.
 */
class WelcomeTutorialDialog : public QDialog {
    Q_OBJECT

public:
    explicit WelcomeTutorialDialog(QWidget *parent = nullptr);
    ~WelcomeTutorialDialog() override = default;

private slots:
    void nextStep();
    void prevStep();
    void finishTutorial();

private:
    void setupUi();
    QWidget *createWelcomeSlide();
    QWidget *createDockingSlide();
    QWidget *createConstructorSlide();

    QStackedWidget *m_stack = nullptr;
    QPushButton *m_backBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QCheckBox *m_dontShowCheck = nullptr;
    QLabel *m_indicatorLabel = nullptr;
};

} // namespace aether
