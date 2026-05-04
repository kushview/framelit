#include "controlbar.hpp"
#include "capturewindow.hpp"

#include <QApplication>
#include <QAudioDevice>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QTimer>

#ifdef Q_OS_MACOS
#include "../platform/macos_window.h"
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace sc {

static constexpr int kBarHeight  = 36;
static constexpr int kBarMargin  = 4; // gap between capture rect and bar

ControlBar::ControlBar(CaptureWindow* captureWindow, QWidget* parent)
    : QWidget(parent)
    , m_captureWindow(captureWindow)
{
    setWindowFlags(Qt::FramelessWindowHint
                 | Qt::WindowStaysOnTopHint
                 | Qt::Tool);

    setFixedHeight(kBarHeight);

    // Dark background via stylesheet
    setStyleSheet("QWidget { background-color: #1e2029; color: #e2e8f0; }"
                  "QPushButton { padding: 2px 10px; border-radius: 3px; background-color: #334155; color: #e2e8f0; }"
                  "QPushButton:hover { background-color: #475569; }"
                  "QPushButton#recordBtn { background-color: #dc2626; }"
                  "QPushButton#recordBtn:hover { background-color: #ef4444; }");

    buildUi();

    // Poll the capture window geometry every 16ms (~60fps).
    // Simpler and more reliable than any signal/event timing approach.
    m_snapTimer = new QTimer(this);
    connect(m_snapTimer, &QTimer::timeout, this, [this]() {
        if (m_captureWindow)
            snapToRegion(m_captureWindow->geometry());
    });
    m_snapTimer->start(16);

#ifdef Q_OS_WIN
    QTimer::singleShot(0, this, [hwnd = reinterpret_cast<HWND>(winId())]() {
        SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    });
#endif
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void ControlBar::buildUi()
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(6);

    layout->addStretch();

    m_formatButton = new QPushButton("GIF", this);
    m_formatButton->setToolTip("Toggle output format: GIF or MP4");
    m_formatButton->setStyleSheet(
        "QPushButton { color: #94a3b8; border: 1px solid #334155; border-radius: 3px; padding: 2px 8px; background: transparent; }"
        "QPushButton:hover { border-color: #64748b; color: #e2e8f0; }");
    connect(m_formatButton, &QPushButton::clicked, this, [this]() {
        if (m_state != AppState::Idle)
            return;
        m_format = (m_format == OutputFormat::Gif) ? OutputFormat::Mp4 : OutputFormat::Gif;
        m_formatButton->setText(m_format == OutputFormat::Gif ? "GIF" : "MP4");
        const bool isVideo = (m_format != OutputFormat::Gif);
        m_audioButton->setVisible(isVideo);
        emit formatChangeRequested(m_format);
    });
    layout->addWidget(m_formatButton);

    m_audioButton = new QPushButton("🎙", this);
    m_audioButton->setCheckable(true);
    m_audioButton->setChecked(false);
    m_audioButton->setToolTip("Toggle microphone audio recording");
    m_audioButton->setVisible(false); // hidden when GIF is selected
    m_audioButton->setStyleSheet(
        "QPushButton { color: #64748b; border: 1px solid #334155; border-radius: 3px; padding: 2px 6px; background: transparent; }"
        "QPushButton:checked { color: #e2e8f0; border-color: #60a5fa; }"
        "QPushButton:hover { border-color: #64748b; }");
    connect(m_audioButton, &QPushButton::toggled, this, [this](bool on) {
        m_captureAudio = on;
        m_audioDeviceCombo->setVisible(on);
        emit audioChangeRequested(on);
    });
    layout->addWidget(m_audioButton);

    m_audioDeviceCombo = new QComboBox(this);
    m_audioDeviceCombo->setVisible(false);
    m_audioDeviceCombo->setToolTip("Audio input device");
    m_audioDeviceCombo->setStyleSheet(
        "QComboBox { color: #e2e8f0; background: #1e2029; border: 1px solid #334155;"
        " border-radius: 3px; padding: 1px 6px; font-size: 11px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #1e2029; color: #e2e8f0;"
        " selection-background-color: #334155; }");
    // Populate with available input devices; first entry = system default.
    const auto devices = QMediaDevices::audioInputs();
    if (devices.isEmpty()) {
        m_audioDeviceCombo->addItem("(no input devices)", QString{});
    } else {
        const QAudioDevice defaultDev = QMediaDevices::defaultAudioInput();
        m_audioDeviceCombo->addItem(
            "Default (" + defaultDev.description() + ")",
            defaultDev.id());
        for (const QAudioDevice& dev : devices) {
            if (dev.id() != defaultDev.id())
                m_audioDeviceCombo->addItem(dev.description(), dev.id());
        }
    }
    connect(m_audioDeviceCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        emit audioDeviceChangeRequested(m_audioDeviceCombo->itemData(idx).toString());
    });
    layout->addWidget(m_audioDeviceCombo);

    m_recordButton = new QPushButton("Record", this);
    m_recordButton->setObjectName("recordBtn");
    connect(m_recordButton, &QPushButton::clicked, this, &ControlBar::startRequested);
    layout->addWidget(m_recordButton);

    m_pauseButton = new QPushButton("Pause", this);
    m_pauseButton->setVisible(false);
    connect(m_pauseButton, &QPushButton::clicked, this, [this]() {
        if (m_state == AppState::Recording)
            emit pauseRequested();
        else if (m_state == AppState::Paused)
            emit resumeRequested();
    });
    layout->addWidget(m_pauseButton);

    m_stopButton = new QPushButton("Stop", this);
    m_stopButton->setVisible(false);
    connect(m_stopButton, &QPushButton::clicked, this, &ControlBar::stopRequested);
    layout->addWidget(m_stopButton);

    m_hiDpiButton = new QPushButton("2×", this);
    m_hiDpiButton->setCheckable(true);
    m_hiDpiButton->setChecked(false);
    m_hiDpiButton->setToolTip("HiDPI: output at 1600×900 instead of 800×450");
    m_hiDpiButton->setStyleSheet(
        "QPushButton { color: #94a3b8; border: 1px solid #334155; border-radius: 3px; padding: 2px 6px; background: transparent; font-size: 11px; }"
        "QPushButton:checked { color: #e2e8f0; border-color: #60a5fa; }"
        "QPushButton:hover { border-color: #64748b; }");
    connect(m_hiDpiButton, &QPushButton::toggled, this, [this](bool on) {
        m_hiDpi = on;
        emit hiDpiChangeRequested(on);
    });
    layout->addWidget(m_hiDpiButton);

    m_followMouseButton = new QPushButton("⊕", this);
    m_followMouseButton->setCheckable(true);
    m_followMouseButton->setChecked(false);
    m_followMouseButton->setToolTip("Follow mouse: pan capture region when cursor nears an edge");
    m_followMouseButton->setStyleSheet(
        "QPushButton { color: #94a3b8; border: 1px solid #334155; border-radius: 3px; padding: 2px 6px; background: transparent; font-size: 11px; }"
        "QPushButton:checked { color: #e2e8f0; border-color: #60a5fa; }"
        "QPushButton:hover { border-color: #64748b; }");
    connect(m_followMouseButton, &QPushButton::toggled, this, [this](bool on) {
        m_followMouse = on;
        emit followMouseChangeRequested(on);
    });
    layout->addWidget(m_followMouseButton);

    m_snapButton = new QPushButton("16:9", this);
    m_snapButton->setToolTip("Snap capture region to 16:9 (or 9:16)");
    m_snapButton->setStyleSheet(
        "QPushButton { color: #94a3b8; border: 1px solid #334155; border-radius: 3px;"
        " padding: 2px 6px; background: transparent; font-size: 11px; }"
        "QPushButton:hover { border-color: #64748b; color: #e2e8f0; }"
        "QPushButton:disabled { color: #475569; }");
    connect(m_snapButton, &QPushButton::clicked, this, &ControlBar::snapAspectRequested);
    layout->addWidget(m_snapButton);

    m_settingsButton = new QPushButton("⚙", this);
    m_settingsButton->setFixedWidth(28);
    m_settingsButton->setToolTip("Preferences");
    m_settingsButton->setStyleSheet(
        "QPushButton { color: #94a3b8; border: none; background: transparent; font-size: 14px; }"
        "QPushButton:hover { color: #e2e8f0; }"
        "QPushButton:disabled { color: #475569; }");
    connect(m_settingsButton, &QPushButton::clicked, this, [this]() {
        auto* dlg = new QDialog(this);
        dlg->setWindowTitle("Preferences");
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowFlags(dlg->windowFlags() | Qt::WindowStaysOnTopHint);

        auto* vlay = new QVBoxLayout(dlg);
        vlay->setSpacing(12);

        auto* form = new QFormLayout;

        // Output folder
        auto* dirEdit = new QLineEdit(m_outputDir, dlg);
        dirEdit->setMinimumWidth(300);
        dirEdit->setReadOnly(true);
        auto* browseBtn = new QPushButton("Browse\u2026", dlg);
        auto* dirRow = new QHBoxLayout;
        dirRow->addWidget(dirEdit);
        dirRow->addWidget(browseBtn);
        form->addRow("Output folder:", dirRow);

        // Output size — sizes stored as QVariant(QSize) user data so separators
        // don't disturb index-based lookup.
        auto addSize = [](QComboBox* cb, const QString& label, QSize size) {
            cb->addItem(label, QVariant::fromValue(size));
        };
        auto* sizeCombo = new QComboBox(dlg);
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
            if (sizeCombo->itemData(i).value<QSize>() == m_outputSize) {
                sizeCombo->setCurrentIndex(i);
                break;
            }
        }
        form->addRow("Output size:", sizeCombo);

        // Grow/shrink step
        auto* growStepSpin = new QSpinBox(dlg);
        growStepSpin->setRange(1, 200);
        growStepSpin->setSuffix(" px");
        growStepSpin->setValue(m_growStep);
        growStepSpin->setToolTip("Pixels added or removed per grow/shrink hotkey press");
        form->addRow("Grow/shrink step:", growStepSpin);

        // Letterbox vs fill
        auto* letterboxCheck = new QCheckBox("Letterbox (preserve aspect ratio)", dlg);
        letterboxCheck->setChecked(m_letterbox);
        letterboxCheck->setToolTip("When checked, black bars fill any aspect-ratio gap. When unchecked, the frame is stretched to fill the output.");
        form->addRow("Scaling:", letterboxCheck);

        // Demo mode
        auto* demoCheck = new QCheckBox("Allow border and controls to be captured by external apps", dlg);
        demoCheck->setChecked(m_demoMode);
        demoCheck->setToolTip("When checked, the capture border and control bar are visible to external screen recorders and capture tools.");
        form->addRow("Demo mode:", demoCheck);

        vlay->addLayout(form);

        connect(browseBtn, &QPushButton::clicked, dlg, [dirEdit, dlg]() {
            const QString dir = QFileDialog::getExistingDirectory(
                dlg, "Choose Output Folder", dirEdit->text());
            if (!dir.isEmpty())
                dirEdit->setText(dir);
        });

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
        connect(buttons, &QDialogButtonBox::accepted, dlg, [this, dlg, dirEdit, sizeCombo, growStepSpin, letterboxCheck, demoCheck]() {
            const QString dir = dirEdit->text();
            if (dir != m_outputDir) {
                m_outputDir = dir;
                emit outputDirChangeRequested(m_outputDir);
            }
            const QSize size = sizeCombo->currentData().value<QSize>();
            if (size != m_outputSize) {
                m_outputSize = size;
                emit outputSizeChangeRequested(m_outputSize);
            }
            const int step = growStepSpin->value();
            if (step != m_growStep) {
                m_growStep = step;
                emit growStepChangeRequested(m_growStep);
            }
            const bool lb = letterboxCheck->isChecked();
            if (lb != m_letterbox) {
                m_letterbox = lb;
                emit letterboxChangeRequested(m_letterbox);
            }
            const bool demo = demoCheck->isChecked();
            if (demo != m_demoMode) {
                m_demoMode = demo;
                emit demoModeChangeRequested(m_demoMode);
            }
            dlg->accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
        vlay->addWidget(buttons);

        dlg->exec();
    });
    layout->addWidget(m_settingsButton);

    // Resize grip — a small visual indicator at the right edge of the bar.
    // Hit zone is kGripSize px wide; cursor changes on hover.
    auto* grip = new QLabel("⊿", this);
    grip->setFixedWidth(kGripSize);
    grip->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grip->setStyleSheet(QString("color: #475569; font-size: %1px;").arg(kBarHeight - 10));
    grip->setCursor(Qt::SizeFDiagCursor);
    grip->setAttribute(Qt::WA_TransparentForMouseEvents); // bar handles the events
    layout->addWidget(grip);
    layout->setContentsMargins(8, 0, 0, 0); // remove right margin; grip provides it
}

void ControlBar::setHiDpi(bool hiDpi)
{
    m_hiDpi = hiDpi;
    if (m_hiDpiButton)
        m_hiDpiButton->setChecked(hiDpi);
}

void ControlBar::setCaptureAudio(bool on)
{
    m_captureAudio = on;
    if (m_audioButton)
        m_audioButton->setChecked(on);
    if (m_audioDeviceCombo)
        m_audioDeviceCombo->setVisible(on && m_format != OutputFormat::Gif);
}

void ControlBar::setFollowMouse(bool enabled)
{
    m_followMouse = enabled;
    if (m_followMouseButton)
        m_followMouseButton->setChecked(enabled);
}

void ControlBar::setLetterbox(bool letterbox)
{
    m_letterbox = letterbox;
}

void ControlBar::setDemoMode(bool on)
{
    m_demoMode = on;
}

void ControlBar::setOutputDir(const QString& dir)
{
    m_outputDir = dir;
}

void ControlBar::setOutputSize(QSize size)
{
    m_outputSize = size;
}

void ControlBar::setGrowStep(int step)
{
    m_growStep = step;
}

void ControlBar::setFormat(sc::OutputFormat format)
{
    m_format = format;
    const bool isVideo = (format != OutputFormat::Gif);
    m_formatButton->setText(isVideo ? "MP4" : "GIF");
    m_audioButton->setVisible(isVideo);
    m_audioDeviceCombo->setVisible(isVideo && m_captureAudio);
}

void ControlBar::setAudioDeviceId(const QString& id)
{
    if (!m_audioDeviceCombo)
        return;
    if (id.isEmpty()) {
        m_audioDeviceCombo->setCurrentIndex(0);
        return;
    }
    for (int i = 0; i < m_audioDeviceCombo->count(); ++i) {
        if (m_audioDeviceCombo->itemData(i).toString() == id) {
            m_audioDeviceCombo->setCurrentIndex(i);
            return;
        }
    }
    // ID not found (device removed) — fall back to system default.
    m_audioDeviceCombo->setCurrentIndex(0);
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

void ControlBar::snapToRegion(const QRect& captureRect)
{
    // Try to place below; flip above if near the bottom of the screen.
    // Use captureRect.y() + captureRect.height() rather than bottom()
    // because QRect::bottom() == top() + height() - 1 in Qt.
    QScreen* screen = QApplication::primaryScreen();
    int screenBottom = screen ? screen->availableGeometry().bottom() : 9999;

    int barY = captureRect.y() + captureRect.height() + kBarMargin;
    if (barY + kBarHeight > screenBottom)
        barY = captureRect.y() - kBarMargin - kBarHeight;

    // Use move() + resize() separately — on macOS this produces more
    // reliable immediate native window repositioning than setGeometry()
    // when called from inside another window's mouse event handler.
    move(captureRect.x(), barY);
    resize(captureRect.width(), kBarHeight);
    repaint(); // flush immediately, don't wait for next display cycle
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void ControlBar::onStateChanged(sc::AppState state)
{
    m_state = state;
#ifdef Q_OS_MACOS
    // Mirror the capture window's level change: NSStatusWindowLevel (25) while
    // recording so the control bar can't be buried when another app activates.
    const int level = (state == AppState::Recording) ? 25 /*NSStatusWindowLevel*/
                                                      : 3  /*NSFloatingWindowLevel*/;
    setNSWindowLevel(reinterpret_cast<void*>(winId()), level);
#endif
    updateUiForState(state);
}

void ControlBar::onRegionChanged(const sc::CaptureRegion& region)
{
    m_snapButton->setText(region.rect.width() >= region.rect.height() ? "16:9" : "9:16");
    snapToRegion(region.rect);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void ControlBar::updateUiForState(AppState state)
{
    switch (state) {
    case AppState::Idle:
        m_recordButton->setVisible(true);
        m_pauseButton->setVisible(false);
        m_stopButton->setVisible(false);
        m_formatButton->setEnabled(true);
        m_audioButton->setEnabled(true);
        m_audioDeviceCombo->setEnabled(true);
        m_snapButton->setEnabled(true);
        break;

    case AppState::Recording:
        m_recordButton->setVisible(false);
        m_pauseButton->setText("Pause");
        m_pauseButton->setVisible(true);
        m_stopButton->setVisible(true);
        m_formatButton->setEnabled(false);
        m_audioButton->setEnabled(false);
        m_audioDeviceCombo->setEnabled(false);
        m_snapButton->setEnabled(false);
        break;

    case AppState::Paused:
        m_pauseButton->setText("Resume");
        break;

    case AppState::Processing:
        m_recordButton->setVisible(false);
        m_pauseButton->setVisible(false);
        m_stopButton->setVisible(false);
        m_formatButton->setEnabled(false);
        m_audioButton->setEnabled(false);
        m_audioDeviceCombo->setEnabled(false);
        m_snapButton->setEnabled(false);
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Drag the whole apparatus by clicking the bar background;
// resize the capture window by dragging the bottom-right grip zone.
// ---------------------------------------------------------------------------

bool ControlBar::isInGripZone(const QPoint& localPos) const
{
    return localPos.x() >= width() - kGripSize;
}

void ControlBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_captureWindow) {
        if (isInGripZone(event->pos())) {
            m_resizing          = true;
            m_dragStart         = event->globalPosition().toPoint();
            m_captureRectAtPress = m_captureWindow->geometry();
        } else {
            m_dragging      = true;
            m_dragStart     = event->globalPosition().toPoint();
            m_captureOrigin = m_captureWindow->pos();
        }
    }
    QWidget::mousePressEvent(event);
}

void ControlBar::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_captureWindow) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (m_resizing) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStart;
        QRect r = m_captureRectAtPress;

        int newW = qMax(CaptureWindow::kMinDimension, r.width()  + delta.x());
        int newH = qMax(CaptureWindow::kMinDimension, r.height() + delta.y());

        // If the capture window has an aspect lock (recording), derive height
        // from width so the zoom stays proportional.
        double aspect = m_captureWindow->lockedAspect();
        if (aspect > 0.0)
            newH = qMax(CaptureWindow::kMinDimension, int(newW / aspect));

        r.setWidth(newW);
        r.setHeight(newH);
        m_captureWindow->setGeometry(r);
    } else if (m_dragging) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStart;
        m_captureWindow->move(m_captureOrigin + delta);
    } else {
        // Cursor feedback on hover
        setCursor(isInGripZone(event->pos()) ? Qt::SizeFDiagCursor
                                             : Qt::ArrowCursor);
    }
    QWidget::mouseMoveEvent(event);
}

void ControlBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging  = false;
        m_resizing  = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void ControlBar::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
#ifdef Q_OS_MACOS
    WId wid = winId();
    QTimer::singleShot(0, this, [wid]() {
        excludeWindowFromScreenCapture(reinterpret_cast<void*>(wid));
        setWindowHidesOnDeactivate(reinterpret_cast<void*>(wid), false);
    });
#endif
}

} // namespace sc
