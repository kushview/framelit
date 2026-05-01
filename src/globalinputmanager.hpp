#pragma once

#include <QObject>

namespace sc {

// Listens for global +/- keypresses via CGEventTap (macOS only).
// Requires Accessibility permission (System Settings > Privacy > Accessibility).
// If permission is not granted the tap is silently skipped and signals are
// never emitted — the app continues to work without global hotkeys.
class GlobalInputManager : public QObject {
    Q_OBJECT

public:
    explicit GlobalInputManager(QObject* parent = nullptr);
    ~GlobalInputManager() override;

signals:
    void growRequested();
    void shrinkRequested();
    void followMouseToggleRequested();
    void recordToggleRequested();

private:
    void* m_tap = nullptr; // CFMachPortRef — opaque to keep ObjC out of header
};

} // namespace sc
