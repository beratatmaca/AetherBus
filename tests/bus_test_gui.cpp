#include "bus_test.hpp"
#include "gui/widgets/consoleview.hpp"
#include "gui/widgets/macrobar.hpp"
#include "gui/widgets/collapsible_splitter.hpp"
#include "gui/common/theme_controller.hpp"
#include "gui/mainwindow.hpp"
#include "gui/panels/config_panel.hpp"
#include "gui/panels/can_config_panel.hpp"
#include "gui/dialogs/welcome_tutorial_dialog.hpp"
#include "gui/sessions/session_view.hpp"
#include "gui/control/control_server.hpp"
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include "gui/control/control_bridge.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDockWidget>
#include <QGuiApplication>
#include <QScreen>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QtTest/QtTest>

using namespace aether;

namespace {
bool g_stylesheetParseWarningSeen = false;

void stylesheetWarningCatcher(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    if (type == QtWarningMsg && msg.contains(QStringLiteral("Could not parse application stylesheet"))) {
        g_stylesheetParseWarningSeen = true;
    }
}
}  // namespace

void BusTest::serialConfigSettingsRoundTrip() {
    QSettings in;
    in.beginGroup(QStringLiteral("test_serial_in"));
    in.setValue(QStringLiteral("device"), QStringLiteral("/dev/ttyTEST0"));
    in.setValue(QStringLiteral("baud"), 57600);
    in.setValue(QStringLiteral("dataBits"), 7);
    in.setValue(QStringLiteral("parity"), 1);  // Parity::Even
    in.setValue(QStringLiteral("stopBits"), 2);
    in.setValue(QStringLiteral("flow"), 1);  // FlowControl::RtsCts
    in.setValue(QStringLiteral("symlink"), QStringLiteral("/tmp/aether_test_symlink"));
    in.setValue(QStringLiteral("directMode"), false);
    in.endGroup();

    ConfigPanel panel;
    in.beginGroup(QStringLiteral("test_serial_in"));
    panel.loadSettings(in);
    in.endGroup();

    QSettings out;
    out.beginGroup(QStringLiteral("test_serial_out"));
    panel.saveSettings(out);
    out.endGroup();

    out.beginGroup(QStringLiteral("test_serial_out"));
    QCOMPARE(out.value(QStringLiteral("device")).toString(), QStringLiteral("/dev/ttyTEST0"));
    QCOMPARE(out.value(QStringLiteral("baud")).toInt(), 57600);
    QCOMPARE(out.value(QStringLiteral("dataBits")).toInt(), 7);
    QCOMPARE(out.value(QStringLiteral("parity")).toInt(), 1);
    QCOMPARE(out.value(QStringLiteral("stopBits")).toInt(), 2);
    QCOMPARE(out.value(QStringLiteral("flow")).toInt(), 1);
    QCOMPARE(out.value(QStringLiteral("symlink")).toString(), QStringLiteral("/tmp/aether_test_symlink"));
    QCOMPARE(out.value(QStringLiteral("directMode")).toBool(), false);
    out.endGroup();
}

void BusTest::canConfigSettingsRoundTrip() {
    // Regression test: fdMode/loopback/receiveOwn/errorFrames used to be
    // dropped entirely — only iface/filters were ever persisted.
    QSettings in;
    in.beginGroup(QStringLiteral("test_can_in"));
    in.setValue(QStringLiteral("iface"), QStringLiteral("vcan7"));
    in.setValue(QStringLiteral("fdMode"), false);
    in.setValue(QStringLiteral("loopback"), false);
    in.setValue(QStringLiteral("receiveOwn"), true);
    in.setValue(QStringLiteral("errorFrames"), false);
    in.setValue(QStringLiteral("filters"), QString());
    in.endGroup();

    CanConfigPanel panel;
    in.beginGroup(QStringLiteral("test_can_in"));
    panel.loadSettings(in);
    in.endGroup();

    QSettings out;
    out.beginGroup(QStringLiteral("test_can_out"));
    panel.saveSettings(out);
    out.endGroup();

    out.beginGroup(QStringLiteral("test_can_out"));
    QCOMPARE(out.value(QStringLiteral("iface")).toString(), QStringLiteral("vcan7"));
    QCOMPARE(out.value(QStringLiteral("fdMode")).toBool(), false);
    QCOMPARE(out.value(QStringLiteral("loopback")).toBool(), false);
    QCOMPARE(out.value(QStringLiteral("receiveOwn")).toBool(), true);
    QCOMPARE(out.value(QStringLiteral("errorFrames")).toBool(), false);
    out.endGroup();
}

void BusTest::welcomeTutorialDontShowPersistsOnToggle() {
    // Regression test: the preference used to be written only inside
    // finishTutorial(), which never ran if the dialog was closed via the
    // window's X button or Escape (QDialog::reject()) instead of "Finish".
    QSettings settings;
    settings.setValue(QStringLiteral("ui/show_tutorial"), true);

    auto *dlg = new WelcomeTutorialDialog();
    auto *check = dlg->findChild<QCheckBox *>();
    QVERIFY(check != nullptr);

    check->setChecked(true);
    QCOMPARE(settings.value(QStringLiteral("ui/show_tutorial"), true).toBool(), false);

    dlg->reject();  // simulates the X button / Escape, not "Finish"
    QCOMPARE(settings.value(QStringLiteral("ui/show_tutorial"), true).toBool(), false);

    delete dlg;
}

void BusTest::themeControllerStylesheetParses() {
    // Regression test: this exact category of bug ("Could not parse
    // application stylesheet") has shipped three separate times per
    // CHANGELOG.md history. Catch it at the source instead of by inspection.
    ThemeController theme(qApp);

    QWidget probe;
    probe.show();
    QVERIFY(QTest::qWaitForWindowExposed(&probe));

    QtMessageHandler previous = qInstallMessageHandler(stylesheetWarningCatcher);

    g_stylesheetParseWarningSeen = false;
    theme.setMode(ThemeController::Mode::Dark);
    QCoreApplication::processEvents();
    QVERIFY2(!g_stylesheetParseWarningSeen, "Dark theme stylesheet failed to parse");

    g_stylesheetParseWarningSeen = false;
    theme.setMode(ThemeController::Mode::Light);
    QCoreApplication::processEvents();
    QVERIFY2(!g_stylesheetParseWarningSeen, "Light theme stylesheet failed to parse");

    qInstallMessageHandler(previous);
}

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

void BusTest::guiConsoleViewEmptyClickDoesNotAssert() {
    // Regression test: posFromPoint() used to compute qBound(0, x, m_lines.size() - 1)
    // == qBound(0, x, -1) before checking for the empty-lines case, tripping Qt's
    // "!(P(max) < P(min))" assertion (aborting the whole app) on any click/drag
    // over an empty console — e.g. right at startup or right after Clear Log.
    ConsoleView view;
    view.resize(200, 200);

    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    // Reaching here without aborting is the test.
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
    QSettings().remove(QStringLiteral("sessions"));  // isolate from other tests' persisted workspaces
    MainWindow mainWin;

    // Verify it contains SessionViews
    auto sessions = mainWin.findChildren<SessionView *>();
    // Should have 1 session by default (initial session tab)
    QCOMPARE(sessions.count(), 1);

    // Simulate adding session
    QMetaObject::invokeMethod(&mainWin, "addNewSession");
    sessions = mainWin.findChildren<SessionView *>();
    QCOMPARE(sessions.count(), 2);
}

void BusTest::mainWindowWorkspacePersistenceRoundTrip() {
    QSettings().remove(QStringLiteral("sessions"));  // isolate from other tests' persisted workspaces
    {
        MainWindow w1;  // starts with exactly 1 default Serial session
        QMetaObject::invokeMethod(&w1, "addNewCanSession");
        QCOMPARE(w1.findChildren<SessionView *>().count(), 2);
        QMetaObject::invokeMethod(&w1, "saveWorkspaceState");
    }  // destroyed directly (no closeEvent) — the explicit save above is what counts.

    MainWindow w2;  // constructor calls restoreWorkspaceState() automatically.
    auto sessions = w2.findChildren<SessionView *>();
    QCOMPARE(sessions.count(), 2);

    int serialCount = 0;
    int canCount = 0;
    for (SessionView *s : sessions) {
        if (s->sessionType() == SessionType::Serial) {
            ++serialCount;
        } else if (s->sessionType() == SessionType::Can) {
            ++canCount;
        }
    }
    QCOMPARE(serialCount, 1);
    QCOMPARE(canCount, 1);
}

void BusTest::mainWindowTileGridShape() {
    QSettings().remove(QStringLiteral("sessions"));  // isolate from other tests' persisted workspaces
    MainWindow w;                                    // starts with exactly 1 default Serial session
    QMetaObject::invokeMethod(&w, "addNewCanSession");
    QMetaObject::invokeMethod(&w, "addNewSession");
    QCOMPARE(w.findChildren<SessionView *>().count(), 3);
    QMetaObject::invokeMethod(&w, "tileWorkspace");

    {
        // n=3: cols=ceil(sqrt(3))=2 -> one column of 2 (wrapped in a vertical
        // splitter) plus one single session added directly to the root.
        auto *root = w.findChild<QSplitter *>(QStringLiteral("workspaceGridRoot"));
        QVERIFY(root != nullptr);
        QCOMPARE(root->orientation(), Qt::Horizontal);
        QCOMPARE(root->count(), 2);
        auto columns = root->findChildren<QSplitter *>(QStringLiteral("workspaceGridColumn"));
        QCOMPARE(columns.count(), 1);
        QCOMPARE(columns.first()->orientation(), Qt::Vertical);
        QCOMPARE(columns.first()->count(), 2);
    }

    QMetaObject::invokeMethod(&w, "addNewCanSession");
    QCOMPARE(w.findChildren<SessionView *>().count(), 4);
    QMetaObject::invokeMethod(&w, "tileWorkspace");

    {
        // n=4: cols=2, both columns balanced at 2 rows each (a proper 2x2 grid).
        auto *root = w.findChild<QSplitter *>(QStringLiteral("workspaceGridRoot"));
        QVERIFY(root != nullptr);
        QCOMPARE(root->count(), 2);
        auto columns = root->findChildren<QSplitter *>(QStringLiteral("workspaceGridColumn"));
        QCOMPARE(columns.count(), 2);
        for (QSplitter *col : columns) {
            QCOMPARE(col->orientation(), Qt::Vertical);
            QCOMPARE(col->count(), 2);
        }
    }
}

void BusTest::mainWindowTiledMinimumSizeScales() {
    // Mirrors updateMinimumSizeForTiling()'s formula directly since the
    // offscreen test screen is smaller than the base minimum, so a plain
    // growth check isn't reliable here.
    QSettings().remove(QStringLiteral("sessions"));  // isolate from other tests' persisted workspaces
    MainWindow w;                                    // starts with exactly 1 default Serial session
    const QSize baseMin = w.minimumSize();

    QMetaObject::invokeMethod(&w, "addNewCanSession");
    QMetaObject::invokeMethod(&w, "addNewSession");
    QMetaObject::invokeMethod(&w, "addNewCanSession");
    const auto sessions = w.findChildren<SessionView *>();
    const int n = static_cast<int>(sessions.count());
    QCOMPARE(n, 4);
    QMetaObject::invokeMethod(&w, "tileWorkspace");

    int cols = 1;
    while (cols * cols < n) {
        ++cols;
    }
    const int rows = (n + cols - 1) / cols;
    int tileMinWidth = 0;
    int tileMinHeight = 0;
    for (SessionView *session : sessions) {
        const QSize hint = session->sizeHint();
        tileMinWidth = qMax(tileMinWidth, hint.width());
        tileMinHeight = qMax(tileMinHeight, hint.height());
    }
    tileMinHeight += 28;  // wrapForTile()'s header row (title + close button)

    int wantWidth = cols * tileMinWidth;
    int wantHeight = rows * tileMinHeight;
    const QRect available = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry() : QRect();
    if (available.isValid()) {
        wantWidth = qMin(wantWidth, available.width() - 80);
        wantHeight = qMin(wantHeight, available.height() - 80);
    }
    const QSize expected(qMax(baseMin.width(), wantWidth), qMax(baseMin.height(), wantHeight));

    QCOMPARE(w.minimumSize(), expected);

    QMetaObject::invokeMethod(&w, "resetWorkspaceLayout");
    QCOMPARE(w.minimumSize(), baseMin);
}

void BusTest::mainWindowSessionCloseDestroysWidget() {
    // Regression test: session widgets never had Qt::WA_DeleteOnClose set, so
    // close() (from the tab 'X' or Ctrl+W) only ever hid them — nothing was
    // ever actually removed.
    QSettings().remove(QStringLiteral("sessions"));  // isolate from other tests' persisted workspaces
    MainWindow w;
    QMetaObject::invokeMethod(&w, "addNewSession");
    QCOMPARE(w.findChildren<SessionView *>().count(), 2);

    QMetaObject::invokeMethod(&w, "closeCurrentSession");
    QTest::qWait(10);  // let the scheduled deleteLater() actually run

    QCOMPARE(w.findChildren<SessionView *>().count(), 1);
}

void BusTest::mainWindowTileCloseButtonWorks() {
    // Tiled mode has no tab bar, so each tile gets its own close button
    // (wrapForTile()) as the only visible way to close a session there.
    QSettings().remove(QStringLiteral("sessions"));  // isolate from other tests' persisted workspaces
    MainWindow w;
    QMetaObject::invokeMethod(&w, "addNewSession");
    QCOMPARE(w.findChildren<SessionView *>().count(), 2);
    QMetaObject::invokeMethod(&w, "tileWorkspace");

    auto *root = w.findChild<QSplitter *>(QStringLiteral("workspaceGridRoot"));
    QVERIFY(root != nullptr);
    auto closeButtons = root->findChildren<QPushButton *>(QStringLiteral("tileCloseButton"));
    QCOMPARE(closeButtons.count(), 2);

    closeButtons.first()->click();
    QTest::qWait(10);  // let the scheduled deleteLater() actually run

    QCOMPARE(w.findChildren<SessionView *>().count(), 1);
}

void BusTest::collapsibleSplitterTogglesPane() {
    // Regression/behavior guard: a plain click (not a drag) on a collapsible
    // handle should toggle its neighboring pane between 0 and its last
    // on-screen size, without disturbing normal drag-to-resize elsewhere.
    CollapsibleSplitter splitter(Qt::Horizontal);
    auto *left = new QWidget(&splitter);
    auto *right = new QWidget(&splitter);
    splitter.addWidget(left);
    splitter.addWidget(right);
    splitter.resize(400, 300);
    splitter.show();
    QVERIFY(QTest::qWaitForWindowExposed(&splitter));
    splitter.setSizes({150, 250});
    splitter.setPaneCollapsible(0);

    // Qt normalizes requested sizes against the handle's own width, so it
    // won't necessarily settle on exactly {150, 250} — capture whatever it
    // actually produced and assert the round-trip reproduces *that*, rather
    // than asserting an exact literal that depends on this platform's handle
    // width.
    const int expandedWidth = splitter.sizes()[0];
    QVERIFY(expandedWidth > 0);
    QVERIFY(!splitter.isPaneCollapsed(0));

    QSplitterHandle *handle = splitter.handle(1);
    QVERIFY(handle != nullptr);
    const QPoint center(handle->width() / 2, handle->height() / 2);

    QTest::mouseClick(handle, Qt::LeftButton, Qt::NoModifier, center);
    QVERIFY(splitter.isPaneCollapsed(0));
    QCOMPARE(splitter.sizes()[0], 0);

    QTest::mouseClick(handle, Qt::LeftButton, Qt::NoModifier, center);
    QVERIFY(!splitter.isPaneCollapsed(0));
    QCOMPARE(splitter.sizes()[0], expandedWidth);
}

namespace {
/// Minimal SessionView for exercising ControlServer without a real backend.
class StubControlSession : public SessionView {
public:
    [[nodiscard]] bool isRunning() const override { return true; }
    void stopSession() override {}
    [[nodiscard]] SessionType sessionType() const override { return SessionType::Serial; }
    void saveSettings(QSettings &) const override {}
    void loadSettings(const QSettings &) override {}
    bool sendControl(const QJsonObject &cmd, QString *) override {
        lastData = cmd.value(QStringLiteral("data")).toString();
        return true;
    }
    /// Publicly fire the inherited traffic signal (can't `emit` it from outside).
    void fire(const QVector<CapturedChunk> &chunks) { emit controlTraffic(chunks); }
    QString lastData;
};
}  // namespace

void BusTest::controlServerRoundTrip() {
    // Isolate the control socket into a throwaway XDG_RUNTIME_DIR so this never
    // collides with a real running GUI or a parallel test run.
    QTemporaryDir runtimeDir;
    QVERIFY(runtimeDir.isValid());
    qputenv("XDG_RUNTIME_DIR", runtimeDir.path().toLocal8Bit());

    // Mirror the production topology: bridge on this (GUI) thread, server on a
    // worker thread. Same-thread BlockingQueuedConnection would deadlock.
    ControlBridge bridge;
    StubControlSession stub;
    stub.setControlId(1);
    bridge.registerSession(&stub);

    QThread serverThread;
    ControlServer server;  // no parent — moved to the worker thread
    server.setBridge(&bridge);
    server.moveToThread(&serverThread);
    QObject::connect(&serverThread, &QThread::started, &server, &ControlServer::start);
    QObject::connect(&bridge, &ControlBridge::traffic, &server, &ControlServer::onTraffic);
    serverThread.start();

    QLocalSocket sock;
    // The socket file may not exist the instant the thread starts; retry briefly.
    {
        QElapsedTimer connectTimer;
        connectTimer.start();
        do {
            sock.connectToServer(ControlServer::socketName());
            if (sock.waitForConnected(200)) {
                break;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        } while (connectTimer.elapsed() < 3000);
    }
    QVERIFY2(sock.state() == QLocalSocket::ConnectedState, qPrintable(sock.errorString()));

    // Ensure the worker thread is joined however the test exits.
    struct ThreadJoiner {
        QThread &thread;
        ControlServer &server;
        ~ThreadJoiner() {
            QMetaObject::invokeMethod(&server, "stop", Qt::BlockingQueuedConnection);
            thread.quit();
            thread.wait();
        }
    } joiner{serverThread, server};

    // Pump the shared event loop until a full line is buffered, then return it.
    const auto nextLine = [&]() -> QJsonObject {
        QElapsedTimer timer;
        timer.start();
        while (!sock.canReadLine() && timer.elapsed() < 2000) {
            QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 50);
        }
        return QJsonDocument::fromJson(sock.readLine()).object();
    };
    int nextId = 0;
    // Send a command (auto-tagged with an id) and return its reply, skipping any
    // interleaved async event lines — mirrors the correlation the client does.
    const auto command = [&](QJsonObject o) -> QJsonObject {
        const int rid = ++nextId;
        o.insert(QStringLiteral("id"), rid);
        sock.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
        sock.flush();
        for (;;) {
            const QJsonObject msg = nextLine();
            if (msg.contains(QStringLiteral("event"))) {
                continue;  // hello / chunks / dropped
            }
            // Reply must echo our request id.
            if (msg.value(QStringLiteral("id")).toInt(-1) == rid) {
                return msg;
            }
        }
    };

    // The very first async line is the hello handshake.
    const QJsonObject hello = nextLine();
    QCOMPARE(hello.value(QStringLiteral("event")).toString(), QStringLiteral("hello"));
    QCOMPARE(hello.value(QStringLiteral("protocol")).toInt(), 1);

    // list -> our stub session (and the id is echoed)
    QJsonObject reply = command({{QStringLiteral("cmd"), QStringLiteral("list")}});
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(reply.value(QStringLiteral("id")).toInt(), 1);
    const QJsonArray sessions = reply.value(QStringLiteral("sessions")).toArray();
    QCOMPARE(sessions.size(), 1);
    QCOMPARE(sessions.at(0).toObject().value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(sessions.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("serial"));

    // send -> reaches the session backend
    reply = command({{QStringLiteral("cmd"), QStringLiteral("send")},
                     {QStringLiteral("session"), 1},
                     {QStringLiteral("data"), QStringLiteral("4142")}});
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(stub.lastData, QStringLiteral("4142"));

    // unknown session -> error reply, no crash
    reply = command(
        {{QStringLiteral("cmd"), QStringLiteral("send")}, {QStringLiteral("session"), 99}, {QStringLiteral("data"), QStringLiteral("00")}});
    QVERIFY(!reply.value(QStringLiteral("ok")).toBool());

    // subscribe -> traffic fans out as a batched `chunks` event
    reply = command({{QStringLiteral("cmd"), QStringLiteral("subscribe")}, {QStringLiteral("session"), 1}});
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());

    CapturedChunk chunk;
    chunk.dir = Direction::Rx;
    chunk.timestampMs = 4242;
    chunk.data = QByteArray::fromHex("cafe");
    stub.fire({chunk});

    // Read lines until the chunks event arrives.
    QJsonObject ev;
    {
        QElapsedTimer timer;
        timer.start();
        do {
            ev = nextLine();
        } while (ev.value(QStringLiteral("event")).toString() != QLatin1String("chunks") && timer.elapsed() < 2000);
    }
    QCOMPARE(ev.value(QStringLiteral("event")).toString(), QStringLiteral("chunks"));
    QCOMPARE(ev.value(QStringLiteral("session")).toInt(), 1);
    const QJsonArray evChunks = ev.value(QStringLiteral("chunks")).toArray();
    QCOMPARE(evChunks.size(), 1);
    QCOMPARE(evChunks.at(0).toObject().value(QStringLiteral("dir")).toString(), QStringLiteral("rx"));
    QCOMPARE(evChunks.at(0).toObject().value(QStringLiteral("data")).toString(), QStringLiteral("cafe"));

    sock.disconnectFromServer();
    // joiner stops the server on its thread and joins before returning.
}
