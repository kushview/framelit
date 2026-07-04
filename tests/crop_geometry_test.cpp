#include <QtTest>

#include "capture/cropgeometry.hpp"
#include "ui/uigeometry.hpp"

namespace sc {

// physicalCropRect is the single source of truth shared by the GIF encoder and
// the streaming (video) strategy, so these tests pin its behavior for both.
class CropGeometryTest : public QObject {
    Q_OBJECT

private slots:
    // With no screen, the frame itself is treated as the logical screen
    // (scale = 1), so the crop is just the region clamped to the frame.
    void noScreen_regionInsideFrame_cropsToRegion()
    {
        CaptureRegion region;
        region.screen = nullptr;
        region.rect   = QRect(10, 20, 100, 50);

        const QRect crop = physicalCropRect(region, QSize(200, 200));
        QCOMPARE(crop, QRect(10, 20, 100, 50));
    }

    // A region that pokes past the frame edge is clamped to the frame bounds.
    void noScreen_regionPartlyOutside_clampsToFrame()
    {
        CaptureRegion region;
        region.screen = nullptr;
        region.rect   = QRect(150, 150, 100, 100); // extends beyond 200×200

        const QRect crop = physicalCropRect(region, QSize(200, 200));
        QCOMPARE(crop, QRect(150, 150, 50, 50));
    }

    // A region fully off-frame yields an empty intersection, so the whole frame
    // is returned (callers then encode the full frame rather than nothing).
    void noScreen_regionFullyOutside_returnsWholeFrame()
    {
        CaptureRegion region;
        region.screen = nullptr;
        region.rect   = QRect(500, 500, 100, 100);

        const QRect crop = physicalCropRect(region, QSize(200, 200));
        QCOMPARE(crop, QRect(0, 0, 200, 200));
    }

    // heightForAspect -------------------------------------------------------

    void heightForAspect_roundsAndClamps()
    {
        // 16:9 → height rounds to nearest, not truncated.
        QCOMPARE(heightForAspect(160, 16.0 / 9.0, 80), 90);
        QCOMPARE(heightForAspect(100, 16.0 / 9.0, 80), 80); // below min → clamped
    }

    void heightForAspect_nonPositiveAspect_returnsClampedWidth()
    {
        QCOMPARE(heightForAspect(120, 0.0, 80), 120);
        QCOMPARE(heightForAspect(40, -1.0, 80), 80);
    }
};

} // namespace sc

QTEST_MAIN(sc::CropGeometryTest)
#include "crop_geometry_test.moc"
