import QtQuick
import QtQuick.Controls.Basic

// A modal confirm, themed rather than borrowed from the platform dialog set.
//
// Callers describe the captured targets and distinguish recoverable Trash
// from explicit permanent deletion.
Item {
    id: root

    property string title: ""
    property string detail: ""
    property string confirmLabel: "Confirm"
    // An optional "Don't ask again" style check, cleared on each open. Read
    // |checked| in onAccepted.
    property string checkLabel: ""
    property bool checked: false

    signal accepted()

    function shade(base, amount) {
        return Qt.rgba(base.r, base.g, base.b, amount)
    }

    function open() {
        checked = false
        visible = true
        forceActiveFocus()
    }

    function close() {
        visible = false
    }

    visible: false
    anchors.fill: parent
    focus: visible

    // Own wheel input for the whole modal so it cannot scroll the library
    // underneath, as Settings does.
    WheelHandler {
        target: null
        enabled: root.visible
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function (event) { event.accepted = true }
    }

    // Scrim. Swallows clicks so nothing behind it can be triggered by accident.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.55)

        // A MouseArea rather than a TapHandler: a TapHandler only takes a
        // passive grab, so a tap on a control inside the card also arrived
        // here and closed the sheet. Every button is accepted so a right
        // click cannot fall through to a tile behind the scrim.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            preventStealing: true
            onClicked: function (mouse) {
                if (mouse.button === Qt.LeftButton) {
                    root.close()
                }
            }
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(400, root.width - 60)
        height: column.implicitHeight + 40
        radius: Theme.cornerRadius > 0 ? Theme.cornerRadius : 4
        color: root.shade(Theme.background, 0.98)
        border.width: 1
        border.color: root.shade(Theme.foreground, 0.20)

        // Clicks inside the card must not reach the scrim behind it.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            preventStealing: true
        }

        Column {
            id: column
            anchors.centerIn: parent
            width: parent.width - 40
            spacing: 8

            Text {
                width: parent.width
                text: root.title
                font.family: Theme.fontFamily
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: Theme.brightForeground
                wrapMode: Text.WordWrap
            }

            Flickable {
                id: details
                objectName: "confirmationDetails"
                width: parent.width
                height: Math.min(detailText.implicitHeight, Math.max(60, root.height - 180))
                contentWidth: width
                contentHeight: detailText.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                visible: root.detail !== ""
                ScrollBar.vertical: ScrollBar { width: 6; policy: ScrollBar.AsNeeded }
                Text {
                    id: detailText
                    width: parent.width - 12
                    text: root.detail
                    textFormat: Text.PlainText
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    color: Theme.mutedText
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
            }

            Item {
                id: checkRow
                objectName: "confirmationCheck"
                visible: root.checkLabel !== ""
                width: parent.width
                height: 28
                activeFocusOnTab: visible
                Accessible.role: Accessible.CheckBox
                Accessible.name: root.checkLabel
                Accessible.checkable: true
                Accessible.checked: root.checked
                Accessible.onToggleAction: root.checked = !root.checked
                Accessible.onPressAction: root.checked = !root.checked
                Keys.onSpacePressed: root.checked = !root.checked

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.checked = !root.checked
                }
                Rectangle {
                    id: box
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16
                    height: 16
                    radius: 3
                    color: root.checked ? Theme.accent : "transparent"
                    border.width: checkRow.activeFocus ? 2 : 1.5
                    border.color: root.checked || checkRow.activeFocus
                                  ? Theme.accent : root.shade(Theme.foreground, 0.4)
                    Text {
                        anchors.centerIn: parent
                        visible: root.checked
                        text: "✓"
                        font.pixelSize: 11
                        font.weight: Font.Bold
                        color: Theme.background
                    }
                }
                Text {
                    anchors.left: box.right
                    anchors.leftMargin: 8
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.checkLabel
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    color: Theme.foreground
                }
            }

            Item { width: 1; height: 8 }

            Row {
                anchors.right: parent.right
                spacing: 8

                PillButton {
                    objectName: "confirmationCancel"
                    label: "Cancel"
                    onClicked: root.close()
                }

                PillButton {
                    objectName: "confirmationAccept"
                    label: root.confirmLabel
                    active: true
                    onClicked: {
                        root.close()
                        root.accepted()
                    }
                }
            }
        }
    }

    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Escape) {
            root.close()
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root.close()
            root.accepted()
            event.accepted = true
        }
    }
}
