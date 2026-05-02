#include "globakhotkeys.hpp"

#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>

#include <QMetaObject>
#include <QtLogging>

namespace sc {

// Virtual key codes from HIToolbox/Events.h (no Carbon import needed)
static constexpr CGKeyCode kKeyCodeEqual = 0x18; // = and + (with shift)
static constexpr CGKeyCode kKeyCodeMinus = 0x1B; // - and _ (with shift)
static constexpr CGKeyCode kKeyCodeF     = 0x03; // F — toggle follow-mouse
static constexpr CGKeyCode kKeyCodeSpace = 0x31; // Space — toggle record
static constexpr CGKeyCode kKeyCode4     = 0x15; // number-row 4 — show capture UI
static constexpr CGKeyCode kKeyCodeKP4   = 0x56; // keypad 4 — show capture UI

static CGEventRef eventTapCallback(CGEventTapProxy /*proxy*/,
                                   CGEventType    type,
                                   CGEventRef     event,
                                   void*          userInfo)
{
    if (type == kCGEventKeyDown) {
        const CGEventFlags flags = CGEventGetFlags(event);
        const bool cmd   = (flags & kCGEventFlagMaskCommand) != 0;
        const bool shift = (flags & kCGEventFlagMaskShift)   != 0;
        const bool alt   = (flags & kCGEventFlagMaskAlternate) != 0;
        const bool ctrl  = (flags & kCGEventFlagMaskControl)  != 0;

        auto keyCode = static_cast<CGKeyCode>(
            CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
        auto* mgr = static_cast<GlobakHotkeys*>(userInfo);

        // Cmd+Shift+= / Cmd+Shift+- — grow/shrink
        if (cmd && shift && !alt && !ctrl) {
            if (keyCode == kKeyCodeEqual)
                QMetaObject::invokeMethod(mgr, "growRequested", Qt::QueuedConnection);
            else if (keyCode == kKeyCodeMinus)
                QMetaObject::invokeMethod(mgr, "shrinkRequested", Qt::QueuedConnection);
            else if (keyCode == kKeyCodeF)
                QMetaObject::invokeMethod(mgr, "followMouseToggleRequested", Qt::QueuedConnection);
            // Cmd+Shift+4 — show capture UI; consume so macOS screenshot tool doesn't open.
            else if (keyCode == kKeyCode4 || keyCode == kKeyCodeKP4) {
                QMetaObject::invokeMethod(mgr, "showUiRequested", Qt::QueuedConnection);
                return nullptr; // consumed
            }
        }
        // Cmd+Space — toggle recording; consume the event so Spotlight doesn't open.
        else if (cmd && !shift && !alt && !ctrl && keyCode == kKeyCodeSpace) {
            QMetaObject::invokeMethod(mgr, "recordToggleRequested", Qt::QueuedConnection);
            return nullptr; // consumed
        }
    }
    return event; // pass all events through — listen-only
}

GlobakHotkeys::GlobakHotkeys(QObject* parent)
    : QObject(parent)
{
    if (!AXIsProcessTrusted()) {
        qWarning("GlobakHotkeys: Accessibility not granted — global hotkeys disabled.");
        return;
    }

    CFMachPortRef tap = CGEventTapCreate(
        kCGHIDEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionDefault,
        CGEventMaskBit(kCGEventKeyDown),
        eventTapCallback,
        this
    );
    if (!tap) {
        qWarning("GlobakHotkeys: CGEventTapCreate failed.");
        return;
    }

    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
    CGEventTapEnable(tap, true);
    CFRelease(source);

    m_tap = tap; // keep alive — released in destructor
}

GlobakHotkeys::~GlobakHotkeys()
{
    if (m_tap) {
        auto tap = static_cast<CFMachPortRef>(m_tap);
        CGEventTapEnable(tap, false);
        CFRelease(tap);
    }
}

} // namespace sc
