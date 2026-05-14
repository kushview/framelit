#include <QAudioDevice>
#include <QAudioOutput>
#include <QMediaDevices>
#include "preferencesdialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
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

    // Output size — sizes stored as QVariant(QSize) user data so separators
    // don't disturb index-based lookup.
    auto addSize = [](QComboBox* cb, const QString& label, QSize size) {
        cb->addItem(label, QVariant::fromValue(size));
    };
    auto* sizeCombo = new QComboBox(this);
    // 16:9 landscape
    addSize(sizeCombo, "640\u00d7360",   {640,  360});
    addSize(sizeCombo, "800\u00d7450",   {800,  450});
    addSize(sizeCombo, "1280\u00d7720",  {1280, 720});
    addSize(sizeCombo, "1920\u00d71080", {1920, 1080});
    // 9:16 portrait
    sizeCombo->insertSeparator(sizeCombo->count());
    addSize(sizeCombo, "360\u00d7640",   {360,  640});
    addSize(sizeCombo, "450\u00d7800",   {450,  800});
    addSize(sizeCombo, "720\u00d71280",  {720,  1280});
    addSize(sizeCombo, "1080\u00d71920", {1080, 1920});
    // Common GIF sizes
    sizeCombo->insertSeparator(sizeCombo->count());
    addSize(sizeCombo, "320\u00d7180 (GIF)",  {320, 180});
    addSize(sizeCombo, "480\u00d7270 (GIF)",  {480, 270});
    addSize(sizeCombo, "320\u00d7240 (GIF)",  {320, 240});
    addSize(sizeCombo, "480\u00d7360 (GIF)",  {480, 360});
    // Select current
    for (int i = 0; i < sizeCombo->count(); ++i) {
        if (sizeCombo->itemData(i).value<QSize>() == settings.outputSize) {
            sizeCombo->setCurrentIndex(i);
            break;
        }
    }
    form->addRow("Output size:", sizeCombo);

    // Grow/shrink step
    auto* growStepSpin = new QSpinBox(this);
    growStepSpin->setRange(1, 200);
    growStepSpin->setSuffix(" px");
    growStepSpin->setValue(settings.growStep);
    growStepSpin->setToolTip("Pixels added or removed per grow/shrink hotkey press");
    form->addRow("Grow/shrink step:", growStepSpin);

    // Letterbox vs fill
    auto* letterboxCheck = new QCheckBox("Letterbox (preserve aspect ratio)", this);
    letterboxCheck->setChecked(settings.letterbox);
    letterboxCheck->setToolTip("When checked, black bars fill any aspect-ratio gap. "
                               "When unchecked, the frame is stretched to fill the output.");
    form->addRow("Scaling:", letterboxCheck);

    // Demo mode
    auto* demoCheck = new QCheckBox("Allow border and controls to be captured by external apps", this);
    demoCheck->setChecked(settings.demoMode);
    demoCheck->setToolTip("When checked, the capture border and control bar are visible "
                          "to external screen recorders and capture tools.");
    form->addRow("Demo mode:", demoCheck);

    vlay->addLayout(form);

    // Audio input device selector
    auto* audioInputCombo = new QComboBox(this);
    const auto inputDevices = QMediaDevices::audioInputs();
    QString currentInputId = settings.audioDeviceId;
    int inputIdx = 0, inputSel = 0;
    for (const QAudioDevice& dev : inputDevices) {
        audioInputCombo->addItem(dev.description(), dev.id());
        if (!currentInputId.isEmpty() && dev.id() == currentInputId)
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
        audioOutputCombo->addItem(dev.description(), dev.id());
        if (!currentOutputId.isEmpty() && dev.id() == currentOutputId)
            outputSel = outputIdx;
        ++outputIdx;
    }
    audioOutputCombo->setCurrentIndex(outputSel);
    form->addRow("Audio output device:", audioOutputCombo);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this,
            [this, dirEdit, sizeCombo, growStepSpin, letterboxCheck, demoCheck,
             audioInputCombo, audioOutputCombo,
             savedDir    = settings.outputDir,
             savedSize   = settings.outputSize,
             savedStep   = settings.growStep,
             savedLb     = settings.letterbox,
             savedDemo   = settings.demoMode,
             savedInput  = settings.audioDeviceId,
             savedOutput = settings.audioOutputDeviceId]()
    {
        const QString dir = dirEdit->text();
        if (dir != savedDir)
            emit outputDirChangeRequested(dir);

        const QSize size = sizeCombo->currentData().value<QSize>();
        if (size != savedSize)
            emit outputSizeChangeRequested(size);

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
