import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: win
    width: 880
    height: 640
    minimumWidth: 420
    minimumHeight: 560
    visible: true
    title: "Omacalc"

    readonly property bool darkMode: backend.darkMode
    readonly property color pageColor: backend.themeBackground
    readonly property color inkColor: backend.themeForeground
    readonly property color accentColor: backend.themeAccent
    // Side by side when there is room, stacked on a tall narrow window. Every
    // hardcoded size is expressed at the design size of the current layout
    // and scales with the window from there.
    readonly property bool wide: width >= 700 && width >= height * 1.05
    readonly property real uiScale: wide ? Math.min(width / 880, height / 640)
                                         : Math.min(width / 460, height / 820)
    property real appliedTextScale: backend.textScale
    property bool shift: false
    property bool helpOpen: false

    Connections {
        target: backend

        function onTextScaleChanged() {
            var factor = backend.textScale / win.appliedTextScale;
            win.appliedTextScale = backend.textScale;
            if (win.visibility === Window.Windowed) {
                win.width = Math.round(win.width * factor);
                win.height = Math.round(win.height * factor);
            }
        }

        function onCalculationChanged() {
            if (input.text !== backend.input)
                input.text = backend.input;
        }
    }

    function mixColors(base, tint, amount) {
        return Qt.rgba(
            base.r + (tint.r - base.r) * amount,
            base.g + (tint.g - base.g) * amount,
            base.b + (tint.b - base.b) * amount, 1);
    }
    readonly property color mutedColor: mixColors(pageColor, inkColor, 0.5)
    readonly property color faintColor: mixColors(pageColor, inkColor, 0.16)

    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * uiScale));
    }

    Material.theme: darkMode ? Material.Dark : Material.Light
    Material.accent: backend.themeAccent
    color: pageColor

    // ── Editing ────────────────────────────────────────────────────────────

    // Set right after a successful Enter, so an operator typed next continues
    // from the answer: "+ 3" becomes "ans + 3".
    property bool justEvaluated: false
    property int historyCursor: -1
    property string draft: ""

    function insertText(text) {
        if (justEvaluated && input.text === "" && /^[+\-×÷^!%²³∥]|^⁻¹| mod |^ ∥ /.test(text))
            text = "ans" + text;
        justEvaluated = false;
        if (input.selectedText !== "")
            input.remove(input.selectionStart, input.selectionEnd);
        input.insert(input.cursorPosition, text);
        input.forceActiveFocus();
    }

    // Functions wrap a selection: select "x + 1", press sin, get sin(x + 1).
    function insertFunction(text) {
        if (input.selectedText !== "" && text.endsWith("(")) {
            var selected = input.selectedText;
            input.remove(input.selectionStart, input.selectionEnd);
            input.insert(input.cursorPosition, text + selected + ")");
            input.forceActiveFocus();
            return;
        }
        insertText(text);
    }

    function backspace() {
        justEvaluated = false;
        if (input.selectedText !== "")
            input.remove(input.selectionStart, input.selectionEnd);
        else if (input.cursorPosition > 0)
            input.remove(input.cursorPosition - 1, input.cursorPosition);
        input.forceActiveFocus();
    }

    function clearInput() {
        input.text = "";
        historyCursor = -1;
        input.forceActiveFocus();
    }

    function evaluate() {
        if (backend.evaluate()) {
            justEvaluated = true;
            historyCursor = -1;
        }
        input.forceActiveFocus();
    }

    function recallHistory(step) {
        var entries = backend.history;
        if (entries.length === 0)
            return;
        if (historyCursor === -1)
            draft = input.text;
        var next = Math.max(-1, Math.min(entries.length - 1, historyCursor + step));
        if (next === historyCursor)
            return;
        historyCursor = next;
        input.text = next === -1 ? draft : entries[entries.length - 1 - next].expression;
        input.cursorPosition = input.text.length;
    }

    function cycle(values, current) {
        return values[(values.indexOf(current) + 1) % values.length];
    }

    function press(entry) {
        var action = shift && entry.secondAction !== undefined ? entry.secondAction : entry.action;
        var text = shift && entry.secondInsert !== undefined ? entry.secondInsert : entry.insert;
        if (entry.action !== "shift")
            shift = false;

        if (action === "shift") shift = !shift;
        else if (action === "clear") clearInput();
        else if (action === "backspace") backspace();
        else if (action === "equals") evaluate();
        else insertFunction(text);
    }

    // ── Layout ─────────────────────────────────────────────────────────────

    Item {
        id: face
        anchors.fill: parent
        anchors.margins: win.scaledSize(16)

        RowLayout {
            id: modeBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: win.scaledSize(26)
            spacing: win.scaledSize(18)

            ModeChip {
                options: ["DEG", "RAD"]
                current: backend.angleMode.toUpperCase()
                hint: "Ctrl+D"
                onClicked: backend.angleMode = backend.angleMode === "deg" ? "rad" : "deg"
            }
            ModeChip {
                options: ["NORM", "SCI", "ENG"]
                current: backend.notation.toUpperCase()
                hint: "Ctrl+E"
                onClicked: backend.notation = win.cycle(["norm", "sci", "eng"], backend.notation)
            }
            ModeChip {
                options: ["DEC", "HEX", "BIN", "OCT"]
                current: backend.base.toUpperCase()
                hint: "Ctrl+B"
                onClicked: backend.base = win.cycle(["dec", "hex", "bin", "oct"], backend.base)
            }
            ModeChip {
                options: ["RECT", "POLAR"]
                current: backend.polar ? "POLAR" : "RECT"
                hint: "Ctrl+P"
                onClicked: backend.polar = !backend.polar
            }
            Item { Layout.fillWidth: true }
            ModeChip {
                options: ["?"]
                current: win.helpOpen ? "?" : ""
                hint: "F1"
                onClicked: win.helpOpen = !win.helpOpen
            }
        }

        Item {
            id: displayArea
            anchors.top: modeBar.bottom
            anchors.topMargin: win.scaledSize(8)
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: divider.top
            anchors.bottomMargin: win.scaledSize(12)
            anchors.leftMargin: win.scaledSize(6)
            anchors.rightMargin: win.scaledSize(6)

            ListView {
                id: historyView
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: inputRow.top
                anchors.bottomMargin: win.scaledSize(6)
                clip: true
                verticalLayoutDirection: ListView.BottomToTop
                boundsBehavior: Flickable.StopAtBounds
                model: backend.history.slice().reverse()
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Item {
                    id: row
                    required property var modelData
                    width: historyView.width
                    height: win.scaledSize(26)

                    Rectangle {
                        anchors.fill: parent
                        radius: win.scaledSize(4)
                        color: win.faintColor
                        opacity: expressionArea.containsMouse || resultArea.containsMouse ? 0.5 : 0
                    }

                    Text {
                        id: expressionLabel
                        anchors.left: parent.left
                        anchors.leftMargin: win.scaledSize(4)
                        anchors.right: resultLabel.left
                        anchors.rightMargin: win.scaledSize(16)
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.expression
                        color: win.mutedColor
                        elide: Text.ElideRight
                        font.family: "iA Writer Mono S"
                        font.pixelSize: win.scaledSize(15)

                        MouseArea {
                            id: expressionArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            ToolTip.visible: containsMouse
                            ToolTip.delay: 800
                            ToolTip.text: "Click to edit this expression"
                            onClicked: {
                                input.text = row.modelData.expression;
                                input.cursorPosition = input.text.length;
                                input.forceActiveFocus();
                            }
                        }
                    }

                    Text {
                        id: resultLabel
                        anchors.right: parent.right
                        anchors.rightMargin: win.scaledSize(4)
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, parent.width * 0.65)
                        horizontalAlignment: Text.AlignRight
                        text: "= " + row.modelData.result
                        color: win.inkColor
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: win.scaledSize(10)
                        elide: Text.ElideRight
                        font.family: "iA Writer Mono S"
                        font.pixelSize: win.scaledSize(15)

                        MouseArea {
                            id: resultArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            ToolTip.visible: containsMouse
                            ToolTip.delay: 800
                            ToolTip.text: "Click to insert this result"
                            onClicked: {
                                if (row.modelData.result !== "defined")
                                    win.insertText(row.modelData.result);
                            }
                        }
                    }
                }
            }

            Item {
                id: inputRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: resultText.top
                height: win.scaledSize(36)

                Text {
                    id: prompt
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "›"
                    color: win.accentColor
                    font.family: "iA Writer Mono S"
                    font.pixelSize: win.scaledSize(24)
                }

                TextInput {
                    id: input
                    anchors.left: prompt.right
                    anchors.leftMargin: win.scaledSize(10)
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    focus: true
                    clip: true
                    selectByMouse: true
                    color: win.inkColor
                    selectionColor: backend.themeSelection
                    selectedTextColor: win.inkColor
                    font.family: "iA Writer Mono S"
                    font.pixelSize: win.scaledSize(24)
                    onTextChanged: backend.input = text
                    onTextEdited: win.historyCursor = -1

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        visible: input.text === ""
                        text: "type an expression — F1 for the reference"
                        color: win.faintColor
                        font: input.font
                        elide: Text.ElideRight
                    }

                    Keys.onPressed: function(event) {
                        var ctrl = event.modifiers & (Qt.ControlModifier | Qt.MetaModifier);
                        if (ctrl) {
                            if (event.key === Qt.Key_C && input.selectedText === "") backend.copyResult();
                            else if (event.key === Qt.Key_C) input.copy();
                            else if (event.key === Qt.Key_D) backend.angleMode = backend.angleMode === "deg" ? "rad" : "deg";
                            else if (event.key === Qt.Key_E) backend.notation = win.cycle(["norm", "sci", "eng"], backend.notation);
                            else if (event.key === Qt.Key_B) backend.base = win.cycle(["dec", "hex", "bin", "oct"], backend.base);
                            else if (event.key === Qt.Key_P) backend.polar = !backend.polar;
                            else if (event.key === Qt.Key_L) backend.clearHistory();
                            else if (event.key === Qt.Key_Q) win.close();
                            else return;
                            event.accepted = true;
                            return;
                        }

                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            win.evaluate();
                        } else if (event.key === Qt.Key_F1) {
                            win.helpOpen = !win.helpOpen;
                        } else if (event.key === Qt.Key_Escape) {
                            if (win.helpOpen) win.helpOpen = false;
                            else win.clearInput();
                        } else if (event.key === Qt.Key_Up) {
                            win.recallHistory(1);
                        } else if (event.key === Qt.Key_Down) {
                            win.recallHistory(-1);
                        } else if (event.text === "*") {
                            win.insertText("×");
                        } else if (event.text === "/") {
                            win.insertText("÷");
                        } else if (win.justEvaluated && input.text === ""
                                   && /^[+\-^!%]$/.test(event.text)) {
                            win.insertText(event.text);
                        } else {
                            if (event.text !== "")
                                win.justEvaluated = false;
                            return;
                        }
                        event.accepted = true;
                    }
                }
            }

            Text {
                id: resultText
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: infoRow.top
                height: win.scaledSize(64)
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                text: backend.display
                color: backend.displayIsPreview ? win.mixColors(win.pageColor, win.inkColor, 0.62)
                                                : win.inkColor
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: win.scaledSize(16)
                font.family: "iA Writer Mono S"
                font.pixelSize: win.scaledSize(56)

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    ToolTip.visible: containsMouse
                    ToolTip.delay: 800
                    ToolTip.text: "Click to copy (Ctrl+C)"
                    hoverEnabled: true
                    onClicked: { backend.copyResult(); input.forceActiveFocus(); }
                }
            }

            Item {
                id: infoRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: win.scaledSize(20)

                Text {
                    anchors.left: parent.left
                    anchors.right: alternateText.left
                    anchors.rightMargin: win.scaledSize(12)
                    anchors.verticalCenter: parent.verticalCenter
                    text: backend.status
                    color: win.accentColor
                    elide: Text.ElideRight
                    font.family: "iA Writer Mono S"
                    font.pixelSize: win.scaledSize(14)
                }

                Text {
                    id: alternateText
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, parent.width * (backend.status === "" ? 1 : 0.6))
                    horizontalAlignment: Text.AlignRight
                    text: backend.alternate
                    color: win.mutedColor
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: win.scaledSize(10)
                    elide: Text.ElideRight
                    font.family: "iA Writer Mono S"
                    font.pixelSize: win.scaledSize(14)

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { backend.copyText(backend.alternate); input.forceActiveFocus(); }
                    }
                }
            }
        }

        Rectangle {
            id: divider
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: keypad.top
            anchors.bottomMargin: win.scaledSize(14)
            height: 1
            color: win.faintColor
        }

        GridLayout {
            id: keypad
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Math.round(parent.height * (win.wide ? 0.56 : 0.6))
            flow: win.wide ? GridLayout.LeftToRight : GridLayout.TopToBottom
            columns: win.wide ? 2 : 1
            rows: win.wide ? 1 : 2
            columnSpacing: win.scaledSize(18)
            rowSpacing: win.scaledSize(10)

            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 6
                Layout.preferredHeight: 5
                columns: 6
                rowSpacing: win.scaledSize(7)
                columnSpacing: win.scaledSize(7)

                Repeater {
                    model: [
                        { label: "2nd", action: "shift", name: "Second function" },
                        { label: "sin", insert: "sin(", second: "asin", secondInsert: "asin(" },
                        { label: "cos", insert: "cos(", second: "acos", secondInsert: "acos(" },
                        { label: "tan", insert: "tan(", second: "atan", secondInsert: "atan(" },
                        { label: "(", insert: "(" },
                        { label: ")", insert: ")" },

                        { label: "x²", insert: "²", second: "x³", secondInsert: "³", name: "Square" },
                        { label: "√", insert: "√(", second: "∛", secondInsert: "∛(", name: "Square root" },
                        { label: "xʸ", insert: "^", second: "ʸ√x", secondInsert: "root(", name: "Power" },
                        { label: "ln", insert: "ln(", second: "eˣ", secondInsert: "exp(" },
                        { label: "log", insert: "log(", second: "10ˣ", secondInsert: "10^" },
                        { label: "EE", insert: "E", second: "log₂", secondInsert: "log2(", name: "Exponent" },

                        { label: "x⁻¹", insert: "⁻¹", second: "rnd", secondInsert: "round(", name: "Reciprocal" },
                        { label: "n!", insert: "!", second: "nCr", secondInsert: "nCr(", name: "Factorial" },
                        { label: "π", insert: "π", second: "e", secondInsert: "e" },
                        { label: "j", insert: "j", second: "∠", secondInsert: "∠", name: "Imaginary unit" },
                        { label: "∥", insert: " ∥ ", second: "mod", secondInsert: " mod ", name: "Parallel" },
                        { label: ",", insert: ", " },

                        { label: "sinh", insert: "sinh(", second: "asinh", secondInsert: "asinh(" },
                        { label: "cosh", insert: "cosh(", second: "acosh", secondInsert: "acosh(" },
                        { label: "tanh", insert: "tanh(", second: "atanh", secondInsert: "atanh(" },
                        { label: "abs", insert: "abs(", second: "arg", secondInsert: "arg(" },
                        { label: "°", insert: "°", second: "conj", secondInsert: "conj(", name: "Degrees" },
                        { label: "x", insert: "x", second: "=", secondInsert: " = ", name: "Variable x" },

                        { label: "m", insert: "m", second: "k", secondInsert: "k", name: "Milli" },
                        { label: "µ", insert: "µ", second: "M", secondInsert: "M", name: "Micro" },
                        { label: "n", insert: "n", second: "G", secondInsert: "G", name: "Nano" },
                        { label: "p", insert: "p", second: "T", secondInsert: "T", name: "Pico" },
                        { label: "dB", insert: "db(", second: "undB", secondInsert: "undb(" },
                        { label: "0x", insert: "0x", second: "0b", secondInsert: "0b", name: "Hexadecimal prefix" }
                    ]

                    CalcButton {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: 1
                        label: modelData.label
                        secondLabel: modelData.second === undefined ? "" : modelData.second
                        shifted: win.shift
                        active: modelData.action === "shift" && win.shift
                        kind: "function"
                        accessibleName: modelData.name === undefined ? "" : modelData.name
                        pageColor: win.pageColor
                        inkColor: win.inkColor
                        accentColor: win.accentColor
                        onActivated: win.press(modelData)
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 4
                Layout.preferredHeight: 5
                columns: 4
                rowSpacing: win.scaledSize(7)
                columnSpacing: win.scaledSize(7)

                Repeater {
                    model: [
                        { label: "AC", action: "clear", kind: "number", name: "Clear input" },
                        { label: "", action: "backspace", kind: "number", icon: "backspace", name: "Backspace" },
                        { label: "%", insert: "%", kind: "number", name: "Percent" },
                        { label: "÷", insert: "÷", kind: "operator", name: "Divide" },
                        { label: "7", insert: "7", kind: "number" },
                        { label: "8", insert: "8", kind: "number" },
                        { label: "9", insert: "9", kind: "number" },
                        { label: "×", insert: "×", kind: "operator", name: "Multiply" },
                        { label: "4", insert: "4", kind: "number" },
                        { label: "5", insert: "5", kind: "number" },
                        { label: "6", insert: "6", kind: "number" },
                        { label: "−", insert: "-", kind: "operator", name: "Subtract" },
                        { label: "1", insert: "1", kind: "number" },
                        { label: "2", insert: "2", kind: "number" },
                        { label: "3", insert: "3", kind: "number" },
                        { label: "+", insert: "+", kind: "operator", name: "Add" },
                        { label: "0", insert: "0", kind: "number" },
                        { label: ".", insert: ".", kind: "number", name: "Decimal point" },
                        { label: "ans", insert: "ans", kind: "number", name: "Last answer" },
                        { label: "=", action: "equals", kind: "equals", name: "Equals" }
                    ]

                    CalcButton {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: 1
                        label: modelData.label
                        kind: modelData.kind
                        iconName: modelData.icon === undefined ? "" : modelData.icon
                        accessibleName: modelData.name === undefined ? "" : modelData.name
                        pageColor: win.pageColor
                        inkColor: win.inkColor
                        accentColor: win.accentColor
                        onActivated: win.press(modelData)
                    }
                }
            }
        }

        HelpPanel {
            anchors.fill: parent
            anchors.topMargin: modeBar.height + win.scaledSize(8)
            visible: win.helpOpen
            pageColor: win.pageColor
            inkColor: win.inkColor
            accentColor: win.accentColor
            uiScale: win.uiScale
            onInsertRequested: function(text) {
                win.helpOpen = false;
                win.insertFunction(text);
            }
            onCloseRequested: { win.helpOpen = false; input.forceActiveFocus(); }
        }
    }

    component ModeChip: Text {
        id: chip
        property var options: []
        property string current
        property string hint
        signal clicked()

        textFormat: Text.StyledText
        font.family: "iA Writer Mono S"
        font.pixelSize: win.scaledSize(14)
        text: {
            var parts = [];
            for (var k = 0; k < options.length; ++k) {
                var color = options[k] === current ? win.accentColor : win.mutedColor;
                var label = options[k] === current ? "<b>" + options[k] + "</b>" : options[k];
                parts.push("<font color=\"" + color + "\">" + label + "</font>");
            }
            return parts.join(" ");
        }

        MouseArea {
            anchors.fill: parent
            anchors.margins: -win.scaledSize(4)
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            ToolTip.visible: containsMouse && chip.hint !== ""
            ToolTip.delay: 600
            ToolTip.text: chip.hint
            onClicked: { chip.clicked(); input.forceActiveFocus(); }
        }
    }

    // Remember the last windowed geometry rather than whatever the window
    // happens to measure at teardown: a maximized window reports screen-sized
    // dimensions, and the close sequence hides the window before destruction,
    // so neither the live geometry nor the final visibility can be trusted.
    property rect normalGeometry: Qt.rect(x, y, width, height)
    property bool wasMaximized: false

    function trackNormalGeometry() {
        if (visibility === Window.Windowed)
            normalGeometry = Qt.rect(x, y, width, height);
    }

    onXChanged: trackNormalGeometry()
    onYChanged: trackNormalGeometry()
    onWidthChanged: trackNormalGeometry()
    onHeightChanged: trackNormalGeometry()

    onVisibilityChanged: {
        if (visibility === Window.Maximized || visibility === Window.FullScreen)
            wasMaximized = true;
        else if (visibility === Window.Windowed)
            wasMaximized = false;
    }

    Component.onCompleted: {
        var geometry = backend.windowGeometry();
        if (geometry.valid) {
            x = geometry.x;
            y = geometry.y;
            width = geometry.width;
            height = geometry.height;
            if (geometry.maximized) showMaximized();
        } else {
            // First run: open at the design size, grown by the desktop text scale.
            width = Math.round(880 * backend.textScale);
            height = Math.round(640 * backend.textScale);
        }
        input.text = backend.input;
        input.forceActiveFocus();
    }

    Component.onDestruction: backend.saveWindowGeometry(
        normalGeometry.x, normalGeometry.y,
        normalGeometry.width, normalGeometry.height, wasMaximized)
}
