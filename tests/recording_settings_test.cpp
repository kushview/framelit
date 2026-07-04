#include <QtTest>
#include <QDir>
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
        QCOMPARE(s.outputDir,
            QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
    }

    // Round-trip -------------------------------------------------------------

    void roundTrip_allFields()
    {
        const QString group = "RecordingSettingsTest_roundTrip";
        // Use a real, writable dir so load()'s outputDir validation keeps it.
        const QString realDir = QDir::tempPath();
        RecordingSettings s;
        s.fps        = 15;
        s.format     = OutputFormat::Mp4;
        s.quality    = QualityPreset::High;
        s.outputDir  = realDir;
        {
            QSettings qs("sc_test", group);
            qs.clear();
            s.save(qs);
            qs.sync();
        }
        {
            QSettings qs("sc_test", group);
            RecordingSettings loaded = RecordingSettings::load(qs);

            QCOMPARE(loaded.fps,        15);
            QCOMPARE(loaded.format,     OutputFormat::Mp4);
            QCOMPARE(loaded.quality,    QualityPreset::High);
            QCOMPARE(loaded.outputDir,  realDir);
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

    // A stored outputDir that no longer exists (deleted/unmounted) must fall
    // back to Movies on load rather than being kept and failing at record time.
    void load_outputDirMissing_fallsBackToMovies()
    {
        const QString group = "RecordingSettingsTest_badOutputDir";
        {
            QSettings qs("sc_test", group);
            qs.clear();
            qs.setValue("outputDir",
                        QStringLiteral("/nonexistent/framelit/output/dir"));
            qs.sync();
        }
        QSettings qs("sc_test", group);
        RecordingSettings s = RecordingSettings::load(qs);
        QCOMPARE(s.outputDir,
                 QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
        qs.clear();
    }
};

} // namespace sc

QTEST_MAIN(sc::RecordingSettingsTest)
#include "recording_settings_test.moc"
