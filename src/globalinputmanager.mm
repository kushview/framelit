#include "globalinputmanager.hpp"

#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>

#include <QMetaObject>
#include <QtLogging>

namespace sc {

// Virtual key codes from HIToolbox/Events.h (no Carbon import needed)
static constexpr CGKeyCode kKeyCodeEqual = 0x18; // = and + (with shift)
static constexpr CGKeyCode kKeyCodeMinus = 0x1B; // - and _ (with shift)

static CGEventRef eventTapCallback(CGEventTapProxy /*proxy*/,
                                   CGEventType    type,
                                   CGEventRef     event,
                                   void*          userInfo)
{
    if (type == kCGEventKeyDown) {
        auto keyCode = static_cast<CGKeyCode>(
            CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
        auto* mgr = static_cast<GlobalInputManager*>(userInfo);
        if (keyCode == kKeyCodeEqual)
            QMetaObject::invokeMethod(mgr, "growRequested", Qt::QueuedConnection);
        else if (keyCode == kKeyCodeMinus)
            QMetaObject::invokeMethod(mgr, "shrinkRequested", Qt::QueuedConnection);
    }
    return event; // pass all events through — listen-only
}

GlobalInputManager::GlobalInputManager(QObject* parent)
    : QObject(parent)
{
    if (!AXIsProcessTrusted()) {
        qWarning("GlobalInputManager: Accessibility not granted — global hotkeys disabled.");
        return;
    }

    CFMachPortRef tap = CGEventTapCreate(
        kCGSessionEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionListenOnly,
        CGEventMaskBit(kCGEventKeyDown),
        eventTapCallback,
        this
    );
    if (!tap) {
        qWarning("GlobalInputManager: CGEventTapCreate failed.");
        return;
    }

    CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
    CGEventTapEnable(tap, true);
    CFRelease(source);

    m_tap = tap; // keep alive — released in destructor
}

GlobalInputManager::~GlobalInputManager()
{
    if (m_tap) {
        auto tap = static_cast<CFMachPortRef>(m_tap);
        CGEventTapEnable(tap, false);
        CFRelease(tap);
    }
}

} // namespace sc
