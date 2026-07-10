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
    void statsCalculatorPerId();

    // CAN Backend Tests (skipped when no vcan interface is present)
    void canConfigValidate();
    void canBackendCapturesFrame();
    void canBackendTransmitsFrame();
    void canBackendFdFrame();
    void canDbcDecoding();
    void canBackendQueryBitrate();
    void canPcapCaptureAndReplay();
    void canRtrFrameLogging();

    // GUI Tests
    void guiConsoleView();
    void guiSearchModes();
    void guiMacroBar();
    void guiThemeController();
    void guiMainWindow();

    // Ethernet pcap parsing (core-only; no libpcap dependency, always built)
    void ethernetPcapRoundTrip();
    void ethernetPcapRejectsBadMagic();
    void ethernetPcapRejectsWrongLinkType();
    void ethernetPcapRejectsTruncatedRecord();

#ifdef AETHER_HAVE_ETHERNET
    // Ethernet Tests (backend + GUI; only built where libpcap is available)
    void ethernetBackendAndParsing();
    void ethernetPacketConstructorIcmp();
    void ethernetPacketConstructorTcpWarns();
    void ethernetPacketConstructorInvalidMacBlocksSend();
    void ethernetPacketModelBasics();
    void ethernetPacketModelEvictsOldest();
    void ethernetPacketConstructorMacroRoundTrip();
    void ethernetBackendOpenInvalidInterfaceFails();
#endif
};
