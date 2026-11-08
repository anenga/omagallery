import QtQuick
import QtQuick.Controls.Basic

// One-click touch-ups for a picture, saved as a copy beside the original.
//
// Each option is a toggle; the preview shows them combined, exactly as the
// writer will apply them (crop first, then levels). "Before" shows the
// untouched picture for comparison. The original is never modified.
// AI Uprez is too slow to preview live: it runs, locally through Real-ESRGAN,
// only when the copy is saved.
Item {
    id: root

    property string path: ""
    property string fileName: ""

    property bool smartCrop: true
    property bool autoFix: true
    property bool uprez: false
    property int uprezFactor: 2
    property bool uprezAvailable: false
    property real progress: -1
    property bool showBefore: false
    property bool saving: false
    property int jobGeneration: -1
    property string errorText: ""
    property string previewProblem: ""
    // Whether this file can be replaced in its own format (not a raw, not
    // animated, writable); otherwise only a copy is offered.
    property bool replaceable: false
    property bool replacing: false

    // Matches TouchUp::Step in src/edit/TouchUp.h.
    readonly property int steps: (smartCrop ? 1 : 0) | (autoFix ? 2 : 0)
    readonly property int shownSteps: showBefore ? 0 : steps
    readonly property var previewSource: root.visible && root.path !== ""
        ? "image://edit/0.0.0.0." + root.shownSteps + encodeURIComponent(root.path)
        : ""
    readonly property int factor: uprez && uprezAvailable ? uprezFactor : 0
    readonly property bool canSave: !root.saving && !ImageEdit.busy
                                    && (root.steps !== 0 || root.factor !== 0)
                                    && preview.status === Image.Ready

    // |replaced| when the touched-up picture took the original's place and
    // the original went to the Trash.
    signal saved(string outputPath, bool replaced)

    function shade(base, amount) {
        return Qt.rgba(base.r, base.g, base.b, amount)
    }

    function open(filePath, name) {
        jobGeneration = ImageEdit.nextJobId()
        previewProblem = ImageEdit.previewProblem(filePath)
        replaceable = ImageEdit.canReplace(filePath)
        replacing = false
        path = filePath
        fileName = name
        smartCrop = true
        autoFix = true
        uprez = false
        uprezAvailable = ImageEdit.upscalerAvailable()
        progress = -1
        showBefore = false
        errorText = ""
        saving = false
        visible = true
        forceActiveFocus()
    }

    function close() {
        jobGeneration = -1
        visible = false
    }

    function save(replace) {
        if (!root.canSave || (replace && !root.replaceable)) return
        saving = true
        replacing = replace === true
        errorText = ""
        progress = root.factor > 0 ? 0 : -1
        ImageEdit.saveTouchUp(root.path, root.steps, root.factor, root.jobGeneration, root.replacing)
    }

    visible: false
    anchors.fill: parent
    focus: visible

    Connections {
        target: ImageEdit
        function onSaved(outputPath, jobId) {
            if (jobId !== root.jobGeneration || !root.visible) return
            root.saving = false
            root.saved(outputPath, root.replacing)
            root.close()
        }
        function onFailed(message, jobId) {
            if (jobId !== root.jobGeneration || !root.visible) return
            root.saving = false
            root.progress = -1
            root.errorText = message
        }
        function onTouchUpProgress(fraction, jobId) {
            if (jobId !== root.jobGeneration) return
            root.progress = fraction
        }
    }

    WheelHandler {
        target: null
        enabled: root.visible
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function (event) { event.accepted = true }
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.68)
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            preventStealing: true
            onClicked: function (mouse) {
                if (mouse.button === Qt.LeftButton && !root.saving) root.close()
            }
        }
    }

    // One toggle card: a title, a line of explanation and a check.
    component OptionCard: Rectangle {
        id: card
        property string title: ""
        property string detail: ""
        property bool checked: false
        property bool available: true
        // Room kept clear on the right for controls placed inside the card.
        property real trailingRoom: 0
        signal toggled()

        implicitHeight: 58
        radius: Theme.cornerRadius > 0 ? Math.min(Theme.cornerRadius, 8) : 4
        color: card.checked && card.available
               ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
               : Qt.rgba(Theme.darkerBackground.r, Theme.darkerBackground.g, Theme.darkerBackground.b, 0.6)
        border.width: 1
        border.color: card.checked && card.available
                      ? Theme.accent
                      : Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b,
                                cardHover.hovered && card.available ? 0.32 : 0.16)
        opacity: card.available ? 1 : 0.5
        activeFocusOnTab: card.available
        Accessible.role: Accessible.CheckBox
        Accessible.name: card.title
        Accessible.description: card.detail
        Accessible.checkable: true
        Accessible.checked: card.checked
        Accessible.onToggleAction: if (card.available) card.toggled()
        Accessible.onPressAction: if (card.available) card.toggled()
        Keys.onSpacePressed: if (card.available) card.toggled()

        HoverHandler { id: cardHover; cursorShape: card.available ? Qt.PointingHandCursor : Qt.ArrowCursor }
        // A MouseArea rather than a TapHandler, so a button inside the card
        // takes its own press without also toggling the card.
        MouseArea {
            anchors.fill: parent
            enabled: card.available
            onClicked: card.toggled()
        }

        Rectangle {
            id: check
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 18
            height: 18
            radius: 4
            color: card.checked && card.available ? Theme.accent : "transparent"
            border.width: 1.5
            border.color: card.checked && card.available
                          ? Theme.accent
                          : Qt.rgba(Theme.foreground.r, Theme.foreground.g, Theme.foreground.b, 0.4)
            Text {
                anchors.centerIn: parent
                visible: card.checked && card.available
                text: "✓"
                font.pixelSize: 12
                font.weight: Font.Bold
                color: Theme.background
            }
        }

        Column {
            anchors.left: check.right
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 12 + card.trailingRoom
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                width: parent.width
                text: card.title
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: Theme.brightForeground
            }
            Text {
                width: parent.width
                text: card.detail
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: 11
                color: Theme.mutedText
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            visible: card.activeFocus
            color: "transparent"
            radius: card.radius + 3
            border.width: 2
            border.color: Theme.accent
        }
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(1080, root.width - 40)
        height: Math.min(820, root.height - 40)
        radius: Theme.cornerRadius > 0 ? Theme.cornerRadius : 4
        color: root.shade(Theme.background, 0.97)
        border.width: 1
        border.color: root.shade(Theme.foreground, 0.20)

        readonly property bool compact: width < 640

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            preventStealing: true
        }

        Item {
            id: head
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 54

            Icon {
                id: titleIcon
                anchors.left: parent.left
                anchors.leftMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                name: "sparkles"
                color: Theme.accent
                size: 18
            }
            Text {
                id: title
                anchors.left: titleIcon.right
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: "Touch up"
                font.family: Theme.fontFamily
                font.pixelSize: 15
                font.weight: Font.DemiBold
                color: Theme.brightForeground
            }
            Text {
                anchors.left: title.right
                anchors.leftMargin: 16
                anchors.right: parent.right
                anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideMiddle
                text: root.fileName
                font.family: Theme.fontFamily
                font.pixelSize: 11
                color: Theme.mutedText
            }
        }

        Rectangle {
            id: stage
            anchors.top: head.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: controls.top
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.bottomMargin: 14
            color: root.shade(Theme.darkerBackground, 0.6)
            radius: Theme.cornerRadius > 0 ? Theme.cornerRadius : 3
            clip: true

            Image {
                id: preview
                objectName: "touchUpPreview"
                anchors.fill: parent
                anchors.margins: 12
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
                mipmap: true
                cache: true
                sourceSize: Qt.size(Math.max(320, Math.round(width * Screen.devicePixelRatio)),
                                    Math.max(320, Math.round(height * Screen.devicePixelRatio)))
                source: root.previewSource
                opacity: status === Image.Ready ? 1 : 0.35
                Behavior on opacity { NumberAnimation { duration: 140 } }
            }

            Text {
                anchors.centerIn: parent
                visible: preview.status !== Image.Ready
                text: preview.status === Image.Error
                      ? (root.previewProblem !== "" ? root.previewProblem : "Could not read this file")
                      : "Working…"
                font.family: Theme.fontFamily
                font.pixelSize: 12
                color: preview.status === Image.Error ? Theme.red : Theme.mutedText
            }

            // Which side of the comparison is on screen.
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 12
                visible: preview.status === Image.Ready
                width: badge.implicitWidth + 16
                height: 22
                radius: 11
                color: Qt.rgba(0, 0, 0, 0.55)
                Text {
                    id: badge
                    anchors.centerIn: parent
                    text: root.showBefore || root.steps === 0 ? "Before" : "After"
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    color: "white"
                }
            }
        }

        Item {
            id: controls
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.bottomMargin: 16
            height: options.height + 14 + footer.height

            Grid {
                id: options
                width: parent.width
                columns: panel.compact ? 1 : 3
                spacing: 10
                readonly property real cardWidth: (width - spacing * (columns - 1)) / columns

                OptionCard {
                    objectName: "touchUpSmartCrop"
                    width: options.cardWidth
                    title: "Smart Crop"
                    detail: "Remove the border a photo scanner leaves"
                    checked: root.smartCrop
                    onToggled: { root.smartCrop = !root.smartCrop; root.showBefore = false; root.errorText = "" }
                }
                OptionCard {
                    objectName: "touchUpAutoFix"
                    width: options.cardWidth
                    title: "Auto Fix"
                    detail: "Balance color, contrast and brightness"
                    checked: root.autoFix
                    onToggled: { root.autoFix = !root.autoFix; root.showBefore = false; root.errorText = "" }
                }
                OptionCard {
                    objectName: "touchUpUprez"
                    width: options.cardWidth
                    title: root.uprez && root.uprezAvailable ? "AI Uprez " + root.uprezFactor + "×" : "AI Uprez"
                    detail: root.uprezAvailable
                            ? "Real-ESRGAN, on this computer"
                            : "Install " + ImageEdit.upscalerPackage() + " to turn this on"
                    checked: root.uprez
                    available: root.uprezAvailable
                    onToggled: { root.uprez = !root.uprez; root.errorText = "" }
                    trailingRoom: factorRow.visible ? factorRow.width + 8 : 0

                    Row {
                        id: factorRow
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.uprez && root.uprezAvailable
                        spacing: 4
                        PillButton {
                            objectName: "touchUpUprez2"
                            label: "2×"
                            active: root.uprezFactor === 2
                            onClicked: root.uprezFactor = 2
                        }
                        PillButton {
                            objectName: "touchUpUprez4"
                            label: "4×"
                            active: root.uprezFactor === 4
                            onClicked: root.uprezFactor = 4
                        }
                    }
                }
            }

            Item {
                id: footer
                anchors.top: options.bottom
                anchors.topMargin: 14
                width: parent.width
                height: 26

                Row {
                    id: compare
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    PillButton {
                        objectName: "touchUpBefore"
                        label: "Before"
                        toolTip: "Show the original"
                        shortcut: "Space"
                        active: root.showBefore || root.steps === 0
                        onClicked: root.showBefore = true
                    }
                    PillButton {
                        objectName: "touchUpAfter"
                        label: "After"
                        active: !root.showBefore && root.steps !== 0
                        onClicked: root.showBefore = false
                    }
                }

                Text {
                    anchors.left: compare.right
                    anchors.leftMargin: 14
                    anchors.right: actions.left
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: root.errorText !== "" ? root.errorText
                          : root.saving && root.progress >= 0
                            ? "Upscaling… " + Math.round(root.progress * 100) + "%"
                          : root.saving ? "Saving…"
                          : root.steps === 0 && root.factor === 0 ? "Choose a touch-up"
                          : root.factor > 0 ? "Uprez runs when you save; the original stays as it is"
                          : "Saved as a copy; the original stays as it is"
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    color: root.errorText !== "" ? Theme.red : Theme.mutedText
                }

                Row {
                    id: actions
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    PillButton {
                        label: root.saving ? "Close" : "Cancel"
                        toolTip: root.saving ? "Saving continues after closing" : ""
                        onClicked: root.close()
                    }
                    PillButton {
                        objectName: "touchUpReplace"
                        visible: root.replaceable
                        label: root.saving && root.replacing ? "Replacing…"
                               : panel.compact ? "Replace" : "Save & replace"
                        toolTip: "Replace the original with this; the original goes to the Trash"
                        onClicked: root.save(true)
                    }
                    PillButton {
                        objectName: "touchUpSave"
                        label: root.saving && !root.replacing ? "Saving…" : panel.compact ? "Copy" : "Save a copy"
                        toolTip: "Save a touched-up copy beside the original"
                        active: root.canSave
                        onClicked: root.save(false)
                    }
                }
            }
        }
    }

    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Escape) {
            if (!root.saving) root.close()
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root.save(false)
            event.accepted = true
        } else if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            root.showBefore = true
            event.accepted = true
        }
    }
    Keys.onReleased: function (event) {
        if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            root.showBefore = false
            event.accepted = true
        }
    }
}
