#include <QtTest>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include "appcontroller.hpp"

namespace sc {

class RecordingSettingsTest : public QObject {
    Q_OBJECT

private slots:
    // Defaults ---------------------------------------------------------------

    void defaults_fps30()
    {
        RecordingSettings s;
        QCOMPARE(s.fps, 30);
    }

    void defaults_formatGif()
    {
        RecordingSettings s;
        QCOMPARE(s.format, OutputFormat::Gif);
    }

    void defaults_qualityMedium()
    {
        RecordingSettings s;
        QCOMPARE(s.quality, QualityPreset::Medium);
    }

    void defaults_showCursorTrue()
    {
        RecordingSettings s;
        QVERIFY(s.showCursor);
    }

    void defaults_showClicksTrue()
    {
        RecordingSettings s;
        QVERIFY(s.showClicks);
    }

    void defaults_countdownFalse()
    {
        RecordingSettings s;
        QVERIFY(!s.countdown);
    }

    // load from empty QSettings uses defaults --------------------------------

    void load_emptyQSettings_returnsDefaults()
    {
        // Use an in-memory (temp) QSettings scope so we start clean
        QSettings qs("sc_test", "RecordingSettingsTest_emptyDefaults");
        qs.clear();

        RecordingSettings s = RecordingSettings::load(qs);

        QCOMPARE(s.fps, 30);
        QCOMPARE(s.format, OutputFormat::Gif);
        QCOMPARE(s.quality, QualityPreset::Medium);
        QVERIFY(s.showCursor);
        QVERIFY(s.showClicks);
        QVERIFY(!s.countdown);
        QCOMPARE(s.outputDir,
            QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
    }

    // Round-trip -------------------------------------------------------------

    void roundTrip_allFields()
    {
        const QString group = "RecordingSettingsTest_roundTrip";
        {
            RecordingSettings s;
            s.fps        = 15;
            s.format     = OutputFormat::Mp4;
            s.quality    = QualityPreset::High;
            s.showCursor = false;
            s.showClicks = false;
            s.countdown  = true;
            s.outputDir  = "/tmp/test_output";

            QSettings qs("sc_test", group);
            qs.clear();
            s.save(qs);
            qs.sync();
        }
        {
            QSettings qs("sc_test", group);
            RecordingSettings s = RecordingSettings::load(qs);

            QCOMPARE(s.fps,        15);
            QCOMPARE(s.format,     OutputFormat::Mp4);
            QCOMPARE(s.quality,    QualityPreset::High);
            QVERIFY(!s.showCursor);
            QVERIFY(!s.showClicks);
            QVERIFY(s.countdown);
            QCOMPARE(s.outputDir,  QString("/tmp/test_output"));
        }
        // Clean up persisted test data
        QSettings qs("sc_test", group);
        qs.clear();
    }

    void roundTrip_webmFormat()
    {
        const QString group = "RecordingSettingsTest_webm";
        RecordingSettings s;
        s.format = OutputFormat::WebM;

        QSettings qs("sc_test", group);
        qs.clear();
        s.save(qs);
        qs.sync();

        RecordingSettings loaded = RecordingSettings::load(qs);
        QCOMPARE(loaded.format, OutputFormat::WebM);
        qs.clear();
    }

    void roundTrip_qualityLow()
    {
        const QString group = "RecordingSettingsTest_low";
        RecordingSettings s;
        s.quality = QualityPreset::Low;

        QSettings qs("sc_test", group);
        qs.clear();
        s.save(qs);
        qs.sync();

        RecordingSettings loaded = RecordingSettings::load(qs);
        QCOMPARE(loaded.quality, QualityPreset::Low);
        qs.clear();
    }
};

} // namespace sc

QTEST_MAIN(sc::RecordingSettingsTest)
#include "recording_settings_test.moc"
