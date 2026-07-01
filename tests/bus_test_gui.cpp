#include "bus_test.h"
#include "gui/consoleview.h"
#include "gui/macrobar.h"
#include "gui/theme_controller.h"
#include "gui/mainwindow.h"

#include <QComboBox>
#include <QTabWidget>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace aether;

void BusTest::guiConsoleView() {
    ConsoleView view;
    view.setNewlineMode(ConsoleView::NewlineMode::PerChunk, 0);
    view.setFormats(true, false, false, true);  // Hex + ASCII

    CapturedChunk chunk1;
    chunk1.dir = Direction::Rx;
    chunk1.timestampMs = 1000;
    chunk1.data = "ABC";
    view.appendChunk(chunk1);

    // Force synchronous flush to avoid relying on timer loops in tests
    QMetaObject::invokeMethod(&view, "flush");

    // Text should contain plaintext representation: "41/A 42/B 43/C"
    QString plainText = view.toPlainText();
    QVERIFY(plainText.contains("41/A 42/B 43/C"));

    // Test search query (findQuery)
    view.moveCursorToStart();
    QVERIFY(view.findQuery("42", 0));  // Match middle hex token "42"

    // Test pause behavior
    view.setPaused(true);
    CapturedChunk chunk2;
    chunk2.dir = Direction::Tx;
    chunk2.timestampMs = 2000;
    chunk2.data = "XYZ";
    view.appendChunk(chunk2);

    QMetaObject::invokeMethod(&view, "flush");
    // Should NOT contain "XYZ" or "58/X" in rendering while paused
    QVERIFY(!view.toPlainText().contains("XYZ"));
    QVERIFY(!view.toPlainText().contains("58/X"));
    // But totals should be updated: m_tx count was 0, now it should be 3 bytes
    QCOMPARE(view.txCount(), static_cast<qint64>(3));

    // Resume and flush, "XYZ" should now be rendered
    view.setPaused(false);
    QMetaObject::invokeMethod(&view, "flush");
    QVERIFY(view.toPlainText().contains("58/X 59/Y 5A/Z"));
}

void BusTest::guiMacroBar() {
    MacroBar bar;
    QSignalSpy spy(&bar, &MacroBar::send);
    QVERIFY(spy.isValid());

    // Test history recall
    const QByteArray bytes = "macro_payload";
    bar.pushHistory(bytes, true);

    auto *historyBox = bar.findChild<QComboBox *>();
    QVERIFY(historyBox != nullptr);
    QCOMPARE(historyBox->count(), 1);

    // Trigger action
    historyBox->setCurrentIndex(0);
    QMetaObject::invokeMethod(&bar, "resendSelected");

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toByteArray(), bytes);
    QCOMPARE(spy.first().at(1).toBool(), true);
}

void BusTest::guiThemeController() {
    ThemeController theme(qApp);
    theme.setMode(ThemeController::Mode::Dark);
    QCOMPARE(theme.mode(), ThemeController::Mode::Dark);

    theme.setMode(ThemeController::Mode::Light);
    QCOMPARE(theme.mode(), ThemeController::Mode::Light);
}

void BusTest::guiMainWindow() {
    MainWindow mainWin;

    // Verify it contains a QTabWidget
    auto *tabWidget = mainWin.findChild<QTabWidget *>();
    QVERIFY(tabWidget != nullptr);

    // Should have 1 tab by default (initial session tab)
    QCOMPARE(tabWidget->count(), 1);

    // Simulate adding session
    QMetaObject::invokeMethod(&mainWin, "addNewSession");
    QCOMPARE(tabWidget->count(), 2);
}
