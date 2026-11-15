import QtQuick

// Start from the current value when a popup reverses direction mid-transition.
Transition {
    id: transition
    property int duration: 240
    property bool leaving: false
    ParallelAnimation {
        NumberAnimation {
            property: "opacity"
            to: transition.leaving ? 0 : 1
            duration: transition.duration * (transition.leaving ? 0.65 : 1)
            easing.type: transition.leaving ? Easing.OutCubic : Easing.OutCubic
        }
        NumberAnimation {
            property: "scale"
            to: transition.leaving ? 0.985 : 1
            duration: transition.duration * (transition.leaving ? 0.65 : 1)
            easing.type: transition.leaving ? Easing.OutCubic : Easing.OutCubic
        }
    }
}
