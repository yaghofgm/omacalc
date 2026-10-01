import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The reference sheet: syntax, keyboard, every builtin function and
// constant, and whatever the user has defined. Clicking a name inserts it.
Rectangle {
    id: panel

    property color pageColor
    property color inkColor
    property color accentColor
    property real uiScale: 1

    signal insertRequested(string text)
    signal closeRequested()

    function mixColors(base, tint, amount) {
        return Qt.rgba(
            base.r + (tint.r - base.r) * amount,
            base.g + (tint.g - base.g) * amount,
            base.b + (tint.b - base.b) * amount, 1);
    }
    function scaled(pixels) { return Math.max(1, Math.round(pixels * uiScale)); }

    readonly property color mutedColor: mixColors(pageColor, inkColor, 0.55)
    readonly property int columnCount: width > scaled(760) ? 2 : 1

    color: pageColor
    // Swallow clicks so they don't reach the keypad underneath.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: function(wheel) { wheel.accepted = false; } }

    Text {
        id: closeButton
        anchors.top: parent.top
        anchors.right: parent.right
        text: "close ✕"
        color: panel.mutedColor
        font.family: "iA Writer Mono S"
        font.pixelSize: panel.scaled(14)
        z: 2
        MouseArea {
            anchors.fill: parent
            anchors.margins: -panel.scaled(6)
            cursorShape: Qt.PointingHandCursor
            onClicked: panel.closeRequested()
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.topMargin: panel.scaled(4)
        contentWidth: width
        contentHeight: content.implicitHeight + panel.scaled(24)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: content
            width: flick.width - panel.scaled(16)
            spacing: panel.scaled(6)

            component Heading: Text {
                Layout.topMargin: panel.scaled(12)
                color: panel.accentColor
                font.family: "iA Writer Mono S"
                font.pixelSize: panel.scaled(15)
                font.bold: true
            }

            component Paragraph: Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.StyledText
                color: panel.inkColor
                lineHeight: 1.25
                font.family: "iA Writer Mono S"
                font.pixelSize: panel.scaled(13)
            }

            component Entries: GridLayout {
                id: grid
                property var entries: []
                property string insertSuffix: ""
                Layout.fillWidth: true
                columns: panel.columnCount
                columnSpacing: panel.scaled(24)
                rowSpacing: panel.scaled(3)

                Repeater {
                    model: grid.entries
                    Item {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        implicitHeight: entryName.implicitHeight + panel.scaled(2)

                        Text {
                            id: entryName
                            width: panel.scaled(110)
                            text: modelData.name
                            color: panel.inkColor
                            elide: Text.ElideRight
                            font.family: "iA Writer Mono S"
                            font.pixelSize: panel.scaled(13)
                            font.bold: true
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: panel.insertRequested(
                                    modelData.name.indexOf("(") >= 0
                                        ? modelData.name.substring(0, modelData.name.indexOf("(") + 1)
                                        : modelData.name + grid.insertSuffix)
                            }
                        }
                        Text {
                            anchors.left: entryName.right
                            anchors.leftMargin: panel.scaled(10)
                            anchors.right: parent.right
                            text: modelData.description + (modelData.value ? "  = " + modelData.value : "")
                            color: panel.mutedColor
                            elide: Text.ElideRight
                            font.family: "iA Writer Mono S"
                            font.pixelSize: panel.scaled(13)
                        }
                    }
                }
            }

            Heading { text: "Syntax"; Layout.topMargin: 0 }
            Paragraph {
                text: "Type expressions directly; the result previews as you type and <b>Enter</b> commits it. "
                    + "<b>+ − × ÷ ^</b> with the usual precedence; <b>**</b> also means power. "
                    + "Implicit multiplication binds tighter than × and ÷: <b>2πf</b>, <b>3(4 + 5)</b>, <b>1/2π</b> = 1/(2π). "
                    + "Unclosed parentheses close themselves at the end.<br>"
                    + "<b>SI prefixes</b> attach to numbers: <b>4.7k 10u 2.2µ 100n 5p 1M 3G 2T 1f 5m</b>. "
                    + "<b>1.5e-3</b> or <b>1.5E-3</b> for exponents.<br>"
                    + "<b>a ∥ b</b> or <b>a || b</b>: parallel combination (resistors, inductors): <b>10k ∥ 4.7k</b>.<br>"
                    + "<b>Complex numbers</b>: <b>3 + 4j</b> (or i), polar <b>5∠53.13</b> (type <b>@</b> for ∠); "
                    + "angles follow DEG/RAD. <b>30°</b> is always degrees.<br>"
                    + "Postfix: <b>n!</b>, <b>x²</b>, <b>x³</b>, <b>x⁻¹</b>, <b>50%</b> (= 0.5). <b>7 % 3</b> or <b>7 mod 3</b> is modulo.<br>"
                    + "Integers: <b>0xFF 0b1010 0o17</b>, bitwise <b>& | xor ~ &lt;&lt; &gt;&gt;</b>.<br>"
                    + "Variables: <b>R1 = 10k</b>. Functions: <b>f(x) = 3x² + 1</b>, <b>fc(r, c) = 1/(2π r c)</b>. "
                    + "<b>ans</b> is the last result; starting with an operator continues from it."
            }

            Heading { text: "Keyboard" }
            Paragraph {
                text: "<b>Enter</b> calculate · <b>Esc</b> clear · <b>↑ ↓</b> recall history · "
                    + "<b>Ctrl+C</b> copy result (or selection) · <b>Ctrl+D</b> DEG/RAD · "
                    + "<b>Ctrl+E</b> NORM/SCI/ENG · <b>Ctrl+B</b> DEC/HEX/BIN/OCT · <b>Ctrl+P</b> RECT/POLAR · "
                    + "<b>Ctrl+L</b> clear history · <b>F1</b> this reference · <b>Ctrl+Q</b> quit.<br>"
                    + "Click a history expression to edit it, a history result to insert it, "
                    + "the big result to copy it. Select text and press a function key to wrap it."
            }

            Heading {
                text: "Your definitions"
                visible: backend.userDefinitions.length > 0
            }
            Entries {
                visible: backend.userDefinitions.length > 0
                entries: backend.userDefinitions
            }
            Text {
                visible: backend.userDefinitions.length > 0
                text: "forget all definitions"
                color: panel.mutedColor
                font.family: "iA Writer Mono S"
                font.pixelSize: panel.scaled(12)
                font.underline: true
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: backend.forgetDefinitions()
                }
            }

            Heading { text: "Functions" }
            Entries {
                entries: backend.functionReference
                insertSuffix: "("
            }

            Heading { text: "Constants" }
            Paragraph {
                text: "π, e, i/j, τ and φ are fixed; physical constants give way to a variable of the same name, so <b>R = 10k</b> just works."
                color: panel.mutedColor
            }
            Entries { entries: backend.constantReference }
        }
    }
}
