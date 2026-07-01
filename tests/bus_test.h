#pragma once

#include <QObject>
#include <QtTest/QtTest>

class BusTest : public QObject {
    Q_OBJECT

private slots:
    // Lifecycle
    void initTestCase();
    void cleanupTestCase();

    // Codec Tests
    void hexFormatting();
    void asciiFormatting();
    void asciiEscapedFormatting();
    void binaryAndDecimalFormatting();
    void parseValidHex();
    void parseRejectsGarbage();
    void parseDecAndBin();
    void encodePayloadFormatsAndEndings();

    // PtyProxy Tests
    void proxyStartsAndExposesSlave();
    void injectionTagsDirection();
    void loopbackForwardsBothDirections();
    void flowControlConfigOpens();
    void statsCountForwardedBytes();
    void mirrorsSlaveBaudToDevice();
    void pcapCaptureWritesRecords();
    void captureReplayRoundTrip();
    void writeQueueDropsWhenPeerStalls();
    void crashCleanupUnlinksSymlink();

    // Stats Calculator Tests
    void statsCalculatorBasics();

    // GUI Tests
    void guiConsoleView();
    void guiSearchModes();
    void guiMacroBar();
    void guiThemeController();
    void guiMainWindow();
};
