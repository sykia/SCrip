import QtQuick

// Attach to the popup surface, outside its layout and scrollable content.
Item {
    id: gesture
    required property var targetPopup
    property real entranceOffset: 0
    property real headerHeight: targetPopup.topPadding + 48
    property var dismissAction: function() { targetPopup.close() }
    property bool hiding: false
    readonly property Item surface: targetPopup.contentItem ? targetPopup.contentItem.parent : null
    readonly property real offset: motion.value
    readonly property bool dragging: drag.active
    parent: surface
    x: 0; y: 0; width: surface ? surface.width : 0
    height: surface ? surface.height : 0
    z: 10000
    // Preserve any existing transforms installed by the popup style.
    onSurfaceChanged: if (surface) surface.transform = Array.from(surface.transform).concat([shift])
    Translate { id: shift; y: gesture.entranceOffset + motion.value }
    SheetMotion {
        id: motion
        extent: Math.max(gesture.targetPopup.height,
            gesture.surface && gesture.surface.parent ? gesture.surface.parent.height - gesture.surface.y : 0)
        onSettled: closed => {
            if (!closed || gesture.hiding) return
            gesture.dismissAction()
            // A guarded dismiss may show confirmation instead of closing.
            if (!gesture.hiding) value = 0
        }
    }
    Connections {
        target: gesture.targetPopup
        function onAboutToShow() { gesture.hiding = false; motion.stop(); motion.value = 0 }
        function onAboutToHide() { gesture.hiding = true; motion.stop() }
        function onClosed() { motion.stop(); motion.value = 0 }
    }
    PointHandler {
        acceptedDevices: PointerDevice.TouchScreen
        // Touch-only passive observation leaves mouse clicks and wheel input
        // to controls while stopping settling anywhere on the mobile surface.
        onActiveChanged: {
            if (active) motion.stop()
            else if (!drag.active && !motion.running && motion.value > 0 && !gesture.hiding)
                motion.settle(0)
        }
    }
    Item {
        width: gesture.width
        height: Math.min(gesture.targetPopup.height, gesture.headerHeight)
        DragHandler {
            acceptedDevices: PointerDevice.TouchScreen
            id: drag
            target: null
            xAxis.enabled: false
            grabPermissions: PointerHandler.CanTakeOverFromItems | PointerHandler.ApprovesTakeOverByAnything
            property real pressY: 0
            property real startOffset: 0
            property real releaseSpeed: 0
            property double lastMoveTime: 0
            onActiveTranslationChanged: if (active) {
                releaseSpeed = centroid.velocity.y
                lastMoveTime = Date.now()
            }
            onActiveChanged: {
                if (active) {
                    motion.stop()
                    startOffset = motion.value
                    pressY = centroid.scenePosition.y
                    releaseSpeed = 0
                    lastMoveTime = 0
                } else if (!gesture.hiding) {
                    motion.settle(Date.now() - lastMoveTime < 100 ? releaseSpeed : 0)
                }
            }
            onCentroidChanged: if (active) motion.value = Math.max(0,
                Math.min(motion.extent, startOffset + centroid.scenePosition.y - pressY))
            onCanceled: if (!gesture.hiding) motion.settle(0, motion.extent)
        }
    }
}
