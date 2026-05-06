#pragma once

#include "../appcontroller.hpp"

#include <QObject>

class QAction;
class QActionGroup;

namespace sc {

// Central action registry — owns every QAction in the application.
//
// Insert the same QAction* into any number of menus or toolbars.
// Qt propagates setEnabled / setChecked / setText to all of them
// automatically, so no per-surface sync loop is ever needed.
//
// Usage pattern:
//   1. AppController creates Actions and connects its signals to slots.
//   2. AppController calls sync() after any state or settings change.
//   3. UI surfaces (SystemTray, future toolbars) receive Actions* in
//      their constructor and add the shared pointers to their menus.
class Actions : public QObject {
    Q_OBJECT

public:
    explicit Actions(QObject* parent = nullptr);

    // Push current application state into all actions in one call.
    // Qt propagates the property changes to every menu/toolbar that holds them.
    void sync(AppState state,
              const RecordingSettings& settings,
              bool followEnabled,
              bool uiVisible);

    // ---------------------------------------------------------------------------
    // Shared action pointers — add to any menu or toolbar directly.
    // ---------------------------------------------------------------------------

    // Recording lifecycle
    QAction* record      = nullptr;   // start a recording
    QAction* pauseResume = nullptr;   // context-sensitive: "Pause" or "Resume"
    QAction* stop        = nullptr;   // stop and encode

    // Output format — mutually exclusive (backed by QActionGroup)
    QAction* formatGif   = nullptr;   // checkable
    QAction* formatMp4   = nullptr;   // checkable

    // Toggle settings — checkable
    QAction* audio       = nullptr;   // mic audio capture
    QAction* hiDpi       = nullptr;   // 2× output resolution
    QAction* followMouse = nullptr;   // pan capture region after cursor

    // One-shot actions
    QAction* snapAspect  = nullptr;   // snap region to nearest 16:9 / 9:16
    QAction* preferences = nullptr;   // open the Preferences dialog
    QAction* showHide    = nullptr;   // toggle capture UI window visibility
    QAction* quit        = nullptr;   // quit the application

signals:
    void recordRequested();
    void pauseResumeRequested();
    void stopRequested();
    void toggleUiRequested();
    void formatChangeRequested(sc::OutputFormat format);
    void audioChangeRequested(bool on);
    void hiDpiChangeRequested(bool on);
    void followMouseChangeRequested(bool on);
    void snapAspectRequested();
    void preferencesRequested();
    void quitRequested();

private:
    QActionGroup* m_formatGroup = nullptr;
};

} // namespace sc
