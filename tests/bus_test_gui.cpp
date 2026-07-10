#include "bus_test.hpp"
#include "gui/widgets/consoleview.hpp"
#include "gui/widgets/macrobar.hpp"
#include "gui/common/theme_controller.hpp"
#include "gui/mainwindow.hpp"
#include <QSignalSpy>

#include <QComboBox>
#include <QTabWidget>
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

    // Text should contain plaintext representation: "41 42 43  |  ABC"
    QString plainText = view.toPlainText();
    QVERIFY(plainText.contains("41 42 43  |  ABC"));

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
    // Should NOT contain "XYZ" or "58 59 5A" in rendering while paused
    QVERIFY(!view.toPlainText().contains("XYZ"));
    QVERIFY(!view.toPlainText().contains("58"));
    // But totals should be updated: m_tx count was 0, now it should be 3 bytes
    QCOMPARE(view.txCount(), static_cast<qint64>(3));

    // Resume and flush, "XYZ" should now be rendered
    view.setPaused(false);
    QMetaObject::invokeMethod(&view, "flush");
    QVERIFY(view.toPlainText().contains("58 59 5A  |  XYZ"));
}

void BusTest::guiSearchModes() {
    ConsoleView view;
    view.setNewlineMode(ConsoleView::NewlineMode::PerChunk, 0);
    view.setFormats(true, false, false, true);  // HEX + ASCII

    CapturedChunk chunk;
    chunk.dir = Direction::Rx;
    chunk.timestampMs = 1000;
    chunk.data = "ABC";  // 0x41 0x42 0x43
    view.appendChunk(chunk);
    QMetaObject::invokeMethod(&view, "flush");

    // Each mode parses the query differently but resolves to the same bytes,
    // matched against whichever columns are visible (HEX here).
    view.setSearchMode(ConsoleView::SearchMode::Hex);
    view.moveCursorToStart();
    QVERIFY(view.findQuery("41 42", 0));

    view.setSearchMode(ConsoleView::SearchMode::Dec);  // 65 66 == 0x41 0x42
    view.moveCursorToStart();
    QVERIFY(view.findQuery("65 66", 0));

    view.setSearchMode(ConsoleView::SearchMode::Bin);  // 01000001 == 0x41
    view.moveCursorToStart();
    QVERIFY(view.findQuery("01000001", 0));

    // Text mode matches the ASCII gutter literally.
    view.setSearchMode(ConsoleView::SearchMode::Text);
    view.moveCursorToStart();
    QVERIFY(view.findQuery("AB", 0));

    // A malformed hex query in explicit HEX mode yields no match (no fallback).
    view.setSearchMode(ConsoleView::SearchMode::Hex);
    view.moveCursorToStart();
    QVERIFY(!view.findQuery("zz", 0));

    // The live match counter is emitted on each highlight pass.
    QSignalSpy countSpy(&view, &ConsoleView::searchMatchCount);
    QVERIFY(countSpy.isValid());
    view.highlightSearchText(QStringLiteral("41"));
    QVERIFY(!countSpy.isEmpty());
    QCOMPARE(countSpy.last().at(0).toInt(), 1);
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
