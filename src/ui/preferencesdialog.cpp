#include "audioengine.hpp"
#include <QAudioDevice>
#include <QAudioOutput>
#include <QMediaDevices>
#include "preferencesdialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace sc {

PreferencesDialog::PreferencesDialog(const RecordingSettings& settings,
                                     QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Preferences");
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);

    auto* vlay = new QVBoxLayout(this);
    vlay->setSpacing(12);

    auto* form = new QFormLayout;
    auto makeSectionHeading = [this](const QString& text) {
        auto* row = new QWidget(this);
        auto* rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(0, 8, 0, 0);
        rowLay->setSpacing(8);

        auto* label = new QLabel(text, row);
        QFont f = label->font();
        f.setBold(true);
        if (f.pointSizeF() > 0.0)
            f.setPointSizeF(f.pointSizeF() * 0.92);
        label->setFont(f);
        label->setStyleSheet("color: palette(mid);");

        auto* divider = new QFrame(row);
        divider->setFrameShape(QFrame::HLine);
        divider->setFrameShadow(QFrame::Plain);

        rowLay->addWidget(label);
        rowLay->addWidget(divider, 1);
        return row;
    };

    form->addRow(makeSectionHeading("Output"));

    // Output folder
    auto* dirEdit = new QLineEdit(settings.outputDir, this);
    dirEdit->setMinimumWidth(300);
    dirEdit->setReadOnly(true);
    auto* browseBtn = new QPushButton("Browse\u2026", this);
    auto* dirRow = new QHBoxLayout;
    dirRow->addWidget(dirEdit);
    dirRow->addWidget(browseBtn);
    form->addRow("Output folder:", dirRow);

    connect(browseBtn, &QPushButton::clicked, this, [this, dirEdit]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, "Choose Output Folder", dirEdit->text());
        if (!dir.isEmpty())
            dirEdit->setText(dir);
    });

    // Output sizes — GIF and video now use separate stored values.
    // Sizes are stored as QVariant(QSize) user data so separators don't
    // disturb index-based lookup.
    auto addSize = [](QComboBox* cb, const QString& label, QSize size) {
        cb->addItem(label, QVariant::fromValue(size));
    };
    auto populateSizeCombo = [&](QComboBox* cb, const QSize& currentSize) {
        // 16:9 landscape
        addSize(cb, "640\u00d7360",   {640,  360});
        addSize(cb, "800\u00d7450",   {800,  450});
        addSize(cb, "1280\u00d7720",  {1280, 720});
        addSize(cb, "1920\u00d71080", {1920, 1080});
        // 9:16 portrait
        cb->insertSeparator(cb->count());
        addSize(cb, "360\u00d7640",   {360,  640});
        addSize(cb, "450\u00d7800",   {450,  800});
        addSize(cb, "720\u00d71280",  {720,  1280});
        addSize(cb, "1080\u00d71920", {1080, 1920});
        // Common GIF sizes
        cb->insertSeparator(cb->count());
        addSize(cb, "320\u00d7180 (GIF)",  {320, 180});
        addSize(cb, "480\u00d7270 (GIF)",  {480, 270});
        addSize(cb, "320\u00d7240 (GIF)",  {320, 240});
        addSize(cb, "480\u00d7360 (GIF)",  {480, 360});
        for (int i = 0; i < cb->count(); ++i) {
            if (cb->itemData(i).value<QSize>() == currentSize) {
                cb->setCurrentIndex(i);
                break;
            }
        }
    };

    auto* gifSizeCombo = new QComboBox(this);
    auto* mp4SizeCombo = new QComboBox(this);
    populateSizeCombo(gifSizeCombo, settings.gifOutputSize);
    populateSizeCombo(mp4SizeCombo, settings.outputSize);

    form->addRow("GIF size:", gifSizeCombo);
    form->addRow("Video size:", mp4SizeCombo);

    // Output quality
    auto* qualityCombo = new QComboBox(this);
    qualityCombo->addItem("Low", static_cast<int>(QualityPreset::Low));
    qualityCombo->addItem("Medium", static_cast<int>(QualityPreset::Medium));
    qualityCombo->addItem("High", static_cast<int>(QualityPreset::High));
    for (int i = 0; i < qualityCombo->count(); ++i) {
        if (qualityCombo->itemData(i).toInt() == static_cast<int>(settings.quality)) {
            qualityCombo->setCurrentIndex(i);
            break;
        }
    }
    form->addRow("Video quality:", qualityCombo);

    // Letterbox vs fill
    auto* letterboxCheck = new QCheckBox("Letterbox (preserve aspect ratio)", this);
    letterboxCheck->setChecked(settings.letterbox);
    letterboxCheck->setToolTip("When checked, black bars fill any aspect-ratio gap. "
                               "When unchecked, the frame is stretched to fill the output.");
    form->addRow("Video scaling:", letterboxCheck);

    form->addRow(makeSectionHeading("Capture"));

    // Grow/shrink step
    auto* growStepSpin = new QSpinBox(this);
    growStepSpin->setRange(1, 200);
    growStepSpin->setSuffix(" px");
    growStepSpin->setValue(settings.growStep);
    growStepSpin->setToolTip("Pixels added or removed per grow/shrink hotkey press");
    form->addRow("Grow/shrink step:", growStepSpin);

    // Demo mode
    auto* demoCheck = new QCheckBox("Allow border and controls to be captured by external apps", this);
    demoCheck->setChecked(settings.demoMode);
    demoCheck->setToolTip("When checked, the capture border and control bar are visible "
                          "to external screen recorders and capture tools.");
    form->addRow("Demo mode:", demoCheck);

    form->addRow(makeSectionHeading("Audio"));

    vlay->addLayout(form);

    // Audio input device selector
    auto* audioInputCombo = new QComboBox(this);
    const auto inputDevices = QMediaDevices::audioInputs();
    QString currentInputId = settings.audioDeviceId;
    int inputIdx = 0, inputSel = 0;
    for (const QAudioDevice& dev : inputDevices) {
        const QString persisted = audio::encodeDeviceId(dev.id());
        audioInputCombo->addItem(dev.description(), persisted);
        if (!currentInputId.isEmpty() &&
            (currentInputId == persisted || dev.id() == currentInputId.toUtf8()))
            inputSel = inputIdx;
        ++inputIdx;
    }
    audioInputCombo->setCurrentIndex(inputSel);
    form->addRow("Audio input device:", audioInputCombo);

    // Audio output device selector
    auto* audioOutputCombo = new QComboBox(this);
    const auto outputDevices = QMediaDevices::audioOutputs();
    QString currentOutputId = settings.audioOutputDeviceId;
    int outputIdx = 0, outputSel = 0;
    for (const QAudioDevice& dev : outputDevices) {
        const QString persisted = audio::encodeDeviceId(dev.id());
        audioOutputCombo->addItem(dev.description(), persisted);
        if (!currentOutputId.isEmpty() &&
            (currentOutputId == persisted || dev.id() == currentOutputId.toUtf8()))
            outputSel = outputIdx;
        ++outputIdx;
    }
    audioOutputCombo->setCurrentIndex(outputSel);
    form->addRow("Audio output device:", audioOutputCombo);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this,
            [this, dirEdit, gifSizeCombo, mp4SizeCombo, qualityCombo, growStepSpin, letterboxCheck, demoCheck,
             audioInputCombo, audioOutputCombo,
             savedDir    = settings.outputDir,
             savedGifSize = settings.gifOutputSize,
             savedSize   = settings.outputSize,
             savedQuality = settings.quality,
             savedStep   = settings.growStep,
             savedLb     = settings.letterbox,
             savedDemo   = settings.demoMode,
             savedInput  = settings.audioDeviceId,
             savedOutput = settings.audioOutputDeviceId]()
    {
        const QString dir = dirEdit->text();
        if (dir != savedDir)
            emit outputDirChangeRequested(dir);

        const QSize gifSize = gifSizeCombo->currentData().value<QSize>();
        if (gifSize != savedGifSize)
            emit gifOutputSizeChangeRequested(gifSize);

        const QSize size = mp4SizeCombo->currentData().value<QSize>();
        if (size != savedSize)
            emit outputSizeChangeRequested(size);

        const QualityPreset quality = static_cast<QualityPreset>(qualityCombo->currentData().toInt());
        if (quality != savedQuality)
            emit qualityChangeRequested(quality);

        const int step = growStepSpin->value();
        if (step != savedStep)
            emit growStepChangeRequested(step);

        const bool lb = letterboxCheck->isChecked();
        if (lb != savedLb)
            emit letterboxChangeRequested(lb);

        const bool demo = demoCheck->isChecked();
        if (demo != savedDemo)
            emit demoModeChangeRequested(demo);

        const QString inputId = audioInputCombo->currentData().toString();
        if (inputId != savedInput)
            emit audioInputDeviceChangeRequested(inputId);

        const QString outputId = audioOutputCombo->currentData().toString();
        if (outputId != savedOutput)
            emit audioOutputDeviceChangeRequested(outputId);

        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    vlay->addWidget(buttons);
}

} // namespace sc
