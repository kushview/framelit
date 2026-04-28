#include <QtTest>
#include "appcontroller.hpp"

namespace sc {

class CaptureRegionTest : public QObject {
    Q_OBJECT

private slots:
    // dimensionsString -------------------------------------------------------

    void dimensionsString_returnsWidthByHeight()
    {
        CaptureRegion r{ nullptr, QRect(0, 0, 800, 450) };
        QCOMPARE(r.dimensionsString(), QString("800×450"));
    }

    // isValid ----------------------------------------------------------------

    void isValid_falseWhenScreenIsNull()
    {
        CaptureRegion r{ nullptr, QRect(0, 0, 100, 100) };
        QVERIFY(!r.isValid());
    }

    void isValid_falseWhenRectIsEmpty()
    {
        CaptureRegion r{ reinterpret_cast<QScreen*>(1), QRect(0, 0, 0, 0) };
        QVERIFY(!r.isValid());
    }

    // clampedTo — rect already inside bounds ---------------------------------

    void clamp_insideBounds_unchanged()
    {
        QRect bounds(0, 0, 1920, 1080);
        CaptureRegion r{ nullptr, QRect(100, 100, 800, 450) };
        CaptureRegion c = r.clampedTo(bounds);
        QCOMPARE(c.rect, QRect(100, 100, 800, 450));
    }

    // clampedTo — translation cases ------------------------------------------

    void clamp_offLeftEdge_movedRight()
    {
        QRect bounds(0, 0, 1920, 1080);
        CaptureRegion r{ nullptr, QRect(-50, 100, 800, 450) };
        CaptureRegion c = r.clampedTo(bounds);
        QCOMPARE(c.rect.left(), 0);
        QCOMPARE(c.rect.width(), 800); // size unchanged
    }

    void clamp_offRightEdge_movedLeft()
    {
        QRect bounds(0, 0, 1920, 1080);
        CaptureRegion r{ nullptr, QRect(1800, 100, 800, 450) };
        CaptureRegion c = r.clampedTo(bounds);
        QCOMPARE(c.rect.left(), 1920 - 800);
        QCOMPARE(c.rect.width(), 800);
    }

    void clamp_offTopEdge_movedDown()
    {
        QRect bounds(0, 0, 1920, 1080);
        CaptureRegion r{ nullptr, QRect(100, -30, 800, 450) };
        CaptureRegion c = r.clampedTo(bounds);
        QCOMPARE(c.rect.top(), 0);
        QCOMPARE(c.rect.height(), 450);
    }

    void clamp_offBottomEdge_movedUp()
    {
        QRect bounds(0, 0, 1920, 1080);
        CaptureRegion r{ nullptr, QRect(100, 900, 800, 450) };
        CaptureRegion c = r.clampedTo(bounds);
        QCOMPARE(c.rect.top(), 1080 - 450);
        QCOMPARE(c.rect.height(), 450);
    }

    // clampedTo — shrink cases -----------------------------------------------

    void clamp_tooWide_shrunkToScreenWidth()
    {
        QRect bounds(0, 0, 600, 1080);
        CaptureRegion r{ nullptr, QRect(0, 0, 800, 450) };
        CaptureRegion c = r.clampedTo(bounds);
        QCOMPARE(c.rect.width(), 600);
    }

    void clamp_tooTall_shrunkToScreenHeight()
    {
        QRect bounds(0, 0, 1920, 400);
        CaptureRegion r{ nullptr, QRect(0, 0, 800, 450) };
        CaptureRegion c = r.clampedTo(bounds);
        QCOMPARE(c.rect.height(), 400);
    }

    void clamp_exactlyFillsBounds_unchanged()
    {
        QRect bounds(0, 0, 800, 450);
        CaptureRegion r{ nullptr, QRect(0, 0, 800, 450) };
        CaptureRegion c = r.clampedTo(bounds);
        QCOMPARE(c.rect, QRect(0, 0, 800, 450));
    }

    // clampedTo — screen pointer preserved -----------------------------------

    void clamp_screenPointerPreserved()
    {
        QScreen* fakeScreen = reinterpret_cast<QScreen*>(0xDEAD);
        QRect bounds(0, 0, 1920, 1080);
        CaptureRegion r{ fakeScreen, QRect(0, 0, 800, 450) };
        CaptureRegion c = r.clampedTo(bounds);
        QCOMPARE(c.screen, fakeScreen);
    }

    // clampedTo — non-zero origin bounds -------------------------------------

    void clamp_nonZeroOriginBounds_offLeftEdge()
    {
        // Simulates a secondary monitor at x=1920
        QRect bounds(1920, 0, 1920, 1080);
        CaptureRegion r{ nullptr, QRect(1800, 100, 800, 450) }; // starts left of monitor
        CaptureRegion c = r.clampedTo(bounds);
        QCOMPARE(c.rect.left(), 1920);
        QCOMPARE(c.rect.width(), 800);
    }
};

} // namespace sc

QTEST_MAIN(sc::CaptureRegionTest)
#include "capture_region_test.moc"
