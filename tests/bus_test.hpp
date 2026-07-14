#pragma once

#include <QObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class BusTest : public QObject {
    Q_OBJECT

private slots:
    // Lifecycle
    void initTestCase();
    void cleanupTestCase();

    // QSettings persistence round-trips
    void serialConfigSettingsRoundTrip();
    void canConfigSettingsRoundTrip();
#ifdef AETHER_HAVE_ETHERNET
    void ethernetSessionSettingsRoundTrip();
#endif
    void mainWindowWorkspacePersistenceRoundTrip();
    void mainWindowTileGridShape();
    void mainWindowTiledMinimumSizeScales();
    void mainWindowTileCloseButtonWorks();
    void mainWindowSessionCloseDestroysWidget();
    void collapsibleSplitterTogglesPane();
    void welcomeTutorialDontShowPersistsOnToggle();
    void themeControllerStylesheetParses();

    // Codec Tests
    void hexFormatting();
    void asciiFormatting();
    void asciiEscapedFormatting();
    void binaryAndDecimalFormatting();
    void parseValidHex();
    void parseCompactHexValidAndInvalid();
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
    void guiConsoleViewEmptyClickDoesNotAssert();
    void guiSearchModes();
    void guiMacroBar();
    void guiThemeController();
    void guiMainWindow();
    void controlServerRoundTrip();

    // Ethernet pcap parsing (core-only; no libpcap dependency, always built)
    void ethernetPcapRoundTrip();
    void ethernetPcapRejectsBadMagic();
    void ethernetPcapRejectsWrongLinkType();
    void ethernetPcapRejectsTruncatedRecord();
    void ethernetPcapWriterRoundTrip();

#ifdef AETHER_HAVE_ETHERNET
    // Ethernet Tests (backend + GUI; only built where libpcap is available)
    void ethernetBackendAndParsing();
    void ethernetPacketConstructorIcmp();
    void ethernetPacketConstructorTcp();
    void ethernetPacketConstructorInvalidMacBlocksSend();
    void ethernetPacketModelBasics();
    void ethernetPacketModelEvictsOldest();
    void ethernetPacketModelBatchClampsAndEvicts();
    void ethernetPacketConstructorMacroRoundTrip();
    void ethernetBackendOpenInvalidInterfaceFails();
#endif

private:
    /// Redirects every default-constructed QSettings in this process to an
    /// isolated, throwaway location for the lifetime of the test binary —
    /// see initTestCase().
    QTemporaryDir m_settingsDir;
};
