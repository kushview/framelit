#pragma once

#include "../appcontroller.hpp"

#include <QDialog>

namespace sc {

// Self-contained preferences dialog. Constructed with the current settings;
// emits fine-grained change signals on accept (only for values that changed).
class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(const RecordingSettings& settings,
                               QWidget* parent = nullptr);

signals:
    void outputDirChangeRequested(QString dir);
    void outputSizeChangeRequested(QSize size);
    void growStepChangeRequested(int step);
    void letterboxChangeRequested(bool letterbox);
    void demoModeChangeRequested(bool on);
};

} // namespace sc
