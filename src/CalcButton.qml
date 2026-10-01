import QtQuick

// One keypad button. Numbers sit almost flush with the page, functions and
// operators lift a step lighter, and equals inverts to the ink color. A
// shifted label, when there is one, rides small in the top corner and swaps
// in while 2nd is held.
Rectangle {
    id: control

    property string label
    property string secondLabel
    property bool shifted: false
    property bool active: false
    property string kind: "number" // "number", "function", "operator", "equals"
    property string iconName
    property string accessibleName
    property color pageColor: "#101010"
    property color inkColor: "#eeeeee"
    property color accentColor: "#5584aa"

    signal activated()

    readonly property string shownLabel: shifted && secondLabel !== "" ? secondLabel : label
    readonly property string cornerLabel: shifted ? "" : secondLabel

    Accessible.role: Accessible.Button
    Accessible.name: accessibleName !== "" ? accessibleName : shownLabel
    Accessible.onPressAction: activated()

    function mixColors(base, tint, amount) {
        return Qt.rgba(
            base.r + (tint.r - base.r) * amount,
            base.g + (tint.g - base.g) * amount,
            base.b + (tint.b - base.b) * amount, 1);
    }

    readonly property real restingLift: kind === "operator" ? 0.16 : (kind === "function" ? 0.09 : 0.05)
    readonly property real activeLift: restingLift
        + (hitArea.pressed ? 0.09 : (hitArea.containsMouse ? 0.045 : 0))

    radius: Math.min(12, height * 0.18)
    color: {
        if (kind === "equals")
            return mixColors(inkColor, pageColor, hitArea.pressed ? 0.22 : (hitArea.containsMouse ? 0.1 : 0));
        if (active)
            return mixColors(pageColor, accentColor, 0.55 + (hitArea.containsMouse ? 0.1 : 0));
        return mixColors(pageColor, inkColor, activeLift);
    }
    border.width: kind === "number" ? 1 : 0
    border.color: mixColors(pageColor, inkColor, 0.13)

    readonly property real glyphSize: Math.min(height * 0.40, width * 0.32)

    Text {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: control.cornerLabel !== "" ? control.height * 0.06 : 0
        width: parent.width - 6
        horizontalAlignment: Text.AlignHCenter
        visible: control.iconName === ""
        text: control.shownLabel
        color: control.kind === "equals" ? control.pageColor
             : (control.shifted && control.secondLabel !== "" ? control.accentColor : control.inkColor)
        font.family: "iA Writer Mono S"
        font.pixelSize: Math.max(8, Math.round(control.glyphSize))
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 8
    }

    Text {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: Math.round(control.height * 0.06)
        anchors.rightMargin: Math.round(control.height * 0.12)
        visible: control.cornerLabel !== ""
        text: control.cornerLabel
        color: control.mixColors(control.pageColor, control.accentColor, 0.9)
        font.family: "iA Writer Mono S"
        font.pixelSize: Math.max(7, Math.round(control.glyphSize * 0.48))
    }

    Canvas {
        id: backspaceIcon
        anchors.centerIn: parent
        width: Math.round(control.glyphSize * 1.3)
        height: Math.round(width * 0.72)
        visible: control.iconName === "backspace"

        onPaint: {
            var context = getContext("2d");
            var w = width;
            var h = height;
            var notch = w * 0.28;
            context.clearRect(0, 0, w, h);
            context.strokeStyle = String(control.inkColor);
            context.lineWidth = Math.max(1.4, w * 0.07);
            context.lineCap = "round";
            context.lineJoin = "round";

            // The key cap: a rectangle whose left edge tapers to a point.
            context.beginPath();
            context.moveTo(notch, 1);
            context.lineTo(w - 1, 1);
            context.lineTo(w - 1, h - 1);
            context.lineTo(notch, h - 1);
            context.lineTo(1, h / 2);
            context.closePath();
            context.stroke();

            // The x inside.
            var cx = notch + (w - notch) / 2;
            var cy = h / 2;
            var arm = h * 0.18;
            context.beginPath();
            context.moveTo(cx - arm, cy - arm);
            context.lineTo(cx + arm, cy + arm);
            context.moveTo(cx + arm, cy - arm);
            context.lineTo(cx - arm, cy + arm);
            context.stroke();
        }

        Connections {
            target: control
            function onInkColorChanged() { backspaceIcon.requestPaint(); }
            function onWidthChanged() { backspaceIcon.requestPaint(); }
            function onHeightChanged() { backspaceIcon.requestPaint(); }
        }
    }

    MouseArea {
        id: hitArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: control.activated()
    }
}
