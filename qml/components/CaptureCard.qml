import QtQuick
import QtQuick.Controls.Basic

// One capture. The thumbnail is drawn at full opacity on top of the translucent
// chrome: dimming the pixels someone is trying to look at is a bug, not a style,
// which is why every media app in Omarchy opts out of window transparency.
Item {
    id: root

    property string path: ""
    property string fileName: ""
    property string kindLabel: ""
    property string timeLabel: ""
    property string sizeLabel: ""
    property bool isVideo: false
    property bool isDocument: false
    // mtime, carried in the thumbnail URL so a rewritten file busts Qt's
    // in-memory pixmap cache as well as the disk one.
    property double stamp: 0
    property string thumbnailVersion: ""
    property real thumbnailDpr: Screen.devicePixelRatio
    readonly property string thumbnailIdentity: thumbnailVersion || String(stamp)
    property bool favorite: false
    property int rating: 0
    property bool hiddenMark: false
    property bool selected: false
    property bool keyboardCurrent: false
    readonly property bool thumbnailFailed: thumbnail.status === Image.Error
    property bool checked: false
    property bool selectionMode: false
    // Gallery tiles drop the card frame and keep the picture edge to edge;
    // time and size appear on hover, like a photos app.
    property bool gallery: false
    readonly property bool hovered: hover.hovered
    readonly property int galleryWidthStep: {
        const steps = [160, 200, 240, 280, 320, 360, 400, 480, 560, 640, 800, 960, 1200, 1600]
        for (const step of steps) {
            if (step >= width) return step
        }
        return Math.ceil(width / 400) * 400
    }
    readonly property bool stripShown: !gallery || hover.hovered || keyboardCurrent
                                       || Settings.showGridFilenames
                                       || ocrSnippet !== ""
    property string ocrSnippet: ""
    property string caption: ""
    // The camera format of a RAW file, so it reads apart from the JPEG the
    // camera wrote beside it.
    property string rawFormat: ""
    property bool thumbnailReady: false
    property bool thumbnailLayoutReady: false
    property string readyPath: ""
    property string readyIdentity: ""
    readonly property bool thumbnailPresented: thumbnail.status === Image.Ready
                                               && thumbnail.visible
                                               && thumbnail.opacity >= 0.999
    // What leaves when this tile is dragged out. The grid sets it to the whole
    // selection when this tile is part of one.
    property var dragPaths: [path]
    // Raised before the drag image is grabbed, so the badge below is in it.
    property bool dragging: false

    Accessible.role: Accessible.ListItem
    Accessible.name: [fileName, kindLabel, timeLabel].filter(function(value) { return value !== "" }).join(", ")
    Accessible.description: thumbnailFailed ? "Preview unavailable" : ""
    Accessible.selected: selected
    Accessible.selectable: true
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.onPressAction: root.chosen()
    Accessible.onToggleAction: root.toggleChecked()

    ToolTip {
        objectName: "captureFilenameTip"
        visible: root.fileName !== "" && (hover.hovered || root.keyboardCurrent)
        text: root.fileName
        delay: 600
    }

    signal activated()
    signal chosen()
    signal contextRequested(real x, real y)
    signal toggleChecked()
    // Gallery only: the width over height of the thumbnail that arrived.
    signal shapeSeen(real ratio)

    function shade(base, amount) {
        return Qt.rgba(base.r, base.g, base.b, amount)
    }

    function updateThumbnailLayout() {
        // Read the size outside the source binding to avoid a binding loop.
        thumbnailLayoutReady = thumbnail.sourceSize.width > 0
                               && (root.gallery || thumbnail.sourceSize.height > 0)
    }
    Component.onCompleted: root.updateThumbnailLayout()

    // Hovering a recording walks a few frames through the clip, so you can tell
    // two similar recordings apart without opening either. Frames are generated
    // lazily on first hover and cached, never during a scan.
    readonly property var scrubStops: [20, 40, 60, 80]
    property int scrubIndex: 0
    readonly property int scrubPercent: scrubStops[scrubIndex]

    Timer {
        id: scrubTimer
        interval: 700
        repeat: true
        running: root.isVideo && hover.hovered && root.path !== ""
        // Back to the resting frame when the hover ends, or a browsed-over
        // tile parks mid-scrub and no longer matches its neighbours.
        onRunningChanged: if (!running) root.scrubIndex = 0
        onTriggered: root.scrubIndex = (root.scrubIndex + 1) % root.scrubStops.length
    }

    onPathChanged: {
        root.scrubIndex = 0
        root.thumbnailReady = false
    }
    onStampChanged: root.thumbnailReady = false
    onThumbnailIdentityChanged: root.thumbnailReady = false

    // Drag out and it drops as the real file into anything on the desktop that
    // takes one: a Discord message, a Nautilus window, a browser upload. Copy
    // only. A drop that moved the file would break the promise that omaroll
    // never moves a capture.
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.proposedAction: Qt.CopyAction
    Drag.mimeData: ({
        "text/uri-list": Library.uriList(root.dragPaths),
        "text/plain": root.dragPaths.join("\n")
    })
    Drag.onDragFinished: {
        root.dragging = false
        // The release that ended the drag went to the drop target, not to us,
        // so the handler would otherwise stay active and miss the next drag.
        dragOut.enabled = false
        dragOut.enabled = true
    }

    DragHandler {
        id: dragOut
        target: null
        acceptedButtons: Qt.LeftButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        // The GridView is a Flickable that would otherwise steal the drag to
        // scroll. Wheel and scrollbar cover scrolling; a mouse drag on a tile
        // means "take this file".
        grabPermissions: PointerHandler.CanTakeOverFromItems
                         | PointerHandler.CanTakeOverFromHandlersOfDifferentType
                         | PointerHandler.ApprovesTakeOverByHandlersOfSameType
        onActiveChanged: {
            if (!active || root.path === "") {
                return
            }
            // The tile itself is the drag image, grabbed at the pointer's
            // offset so it does not jump under the cursor.
            root.Drag.hotSpot = Qt.point(dragOut.centroid.pressPosition.x,
                                         dragOut.centroid.pressPosition.y)
            root.dragging = true
            root.grabToImage(function (result) {
                // The grab is asynchronous; a press-move-release quicker than
                // it would otherwise start a drag with no button held.
                if (!dragOut.active) {
                    root.dragging = false
                    return
                }
                root.Drag.imageSource = result.url
                root.Drag.active = true
            })
        }
    }

    Rectangle {
        id: frame
        anchors.fill: parent
        color: root.gallery ? root.shade(Theme.foreground, 0.06)
                            : root.shade(Theme.background, root.selected ? 0.62 : 0.34)
        radius: root.gallery ? Math.max(Theme.cornerRadius, 6) : Theme.cornerRadius
        border.width: root.gallery ? 0 : 1
        border.color: root.shade(Theme.foreground, hover.hovered ? 0.22 : 0.10)
        clip: true

        Behavior on color { ColorAnimation { duration: 180; easing.type: Easing.OutQuad } }
        Behavior on border.color { ColorAnimation { duration: 140; easing.type: Easing.OutQuad } }

        Image {
            id: thumbnail
            objectName: "captureThumbnail"
            anchors.fill: parent
            anchors.margins: root.gallery ? 0 : 1
            // The gallery shows the whole picture, always: if a tile's shape
            // is still a guess, the picture is letterboxed, never cut.
            fillMode: root.gallery ? Image.PreserveAspectFit : Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            retainWhileLoading: true
            // Keep a scrub frame while the next frame loads, but never display
            // a retained image from the previous file on a recycled delegate.
            visible: root.readyPath === root.path
                     && root.readyIdentity === root.thumbnailIdentity
            // Decode at the size actually drawn, on the ratio of the screen this
            // window is on. Without the ratio, a 1.5x monitor shows an upscaled
            // tile and the whole grid reads as soft.
            //
            // A gallery tile asks for the whole picture at a width step and
            // no height. The cached tile then does not change as the picture's
            // shape is learned or the window is resized a little, which on a
            // network share is the difference between a cache hit and reading
            // the whole photo again.
            sourceSize: root.gallery ? Qt.size(root.galleryWidthStep, 0)
                                     : Qt.size(Math.round(root.width), Math.round(root.height))
            // Let Image's size setter finish before enabling a new source.
            onSourceSizeChanged: Qt.callLater(root.updateThumbnailLayout)
            // "image://thumbs/<ratio>[@<seek%>][~<stamp>]<encoded path>". The
            // path is percent-encoded whole, so a '#', a '?' or a literal '%'
            // in a filename survives URL parsing; the provider decodes once.
            // The card can have dimensions before Image's sourceSize binding
            // catches up. Admit the first request only after that binding.
            source: !root.thumbnailLayoutReady || root.path === ""
                    || root.width <= 0 || root.height <= 0
                    ? ""
                    : "image://thumbs/" + root.thumbnailDpr
                      + (root.isVideo ? "@" + root.scrubPercent : "")
                      + "~" + encodeURIComponent(root.thumbnailIdentity)
                      + encodeURIComponent(root.path)
            smooth: true
            mipmap: true
            opacity: root.thumbnailReady ? 1 : 0

            onStatusChanged: {
                if (status === Image.Ready) {
                    root.readyPath = root.path
                    root.readyIdentity = root.thumbnailIdentity
                    root.thumbnailReady = true
                    if (root.gallery && implicitWidth > 0 && implicitHeight > 0) {
                        root.shapeSeen(implicitWidth / implicitHeight)
                    }
                }
            }

            Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
        }

        // Failed decodes stay distinct from a thumbnail still being made.
        Text {
            anchors.centerIn: parent
            visible: !root.thumbnailReady
            objectName: "thumbnailPlaceholder"
            text: root.thumbnailFailed ? "Preview unavailable"
                  : root.isVideo ? "▶" : (root.isDocument ? "PDF" : "▦")
            font.family: Theme.fontFamily
            width: parent.width - 24
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: root.thumbnailFailed ? 12 : 22
            color: Theme.mutedText
        }

        // Hidden entries stay legible but visibly set aside, so "show hidden"
        // does not just look like the filter failed.
        Rectangle {
            anchors.fill: parent
            visible: root.hiddenMark
            color: root.shade(Theme.background, 0.55)
        }

        // A fixed dark strip keeps metadata readable over every image and theme.
        // It is deliberately compact so the picture still owns almost the whole
        // card.
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: (root.ocrSnippet !== "" || root.caption !== "" ? 52 : 30)
                    + (Settings.showGridFilenames ? 20 : 0)
            visible: root.thumbnailReady && root.stripShown && !root.gallery
            color: Qt.rgba(0, 0, 0, 0.72)
        }

        // The gallery fades its scrim in from the picture instead of a hard
        // strip, so a hovered tile still reads as one photograph.
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Math.min(parent.height, 64 + (Settings.showGridFilenames ? 20 : 0))
            visible: root.thumbnailReady && root.stripShown && root.gallery
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.72) }
            }
        }

        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: metadataRow.top
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            anchors.bottomMargin: 2
            // A search match outranks the caption on the one line there is.
            visible: (root.ocrSnippet !== "" || root.caption !== "") && root.thumbnailReady
                     && root.stripShown
            text: root.ocrSnippet !== "" ? root.ocrSnippet : root.caption
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: 10
            color: "#ffffff"
            opacity: 0.90
        }

        Text {
            objectName: "captureFilenameLabel"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: metadataRow.top
            anchors.bottomMargin: root.ocrSnippet !== "" || root.caption !== "" ? 24 : 2
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            visible: Settings.showGridFilenames
            text: root.fileName
            elide: Text.ElideMiddle
            font.family: Theme.fontFamily
            font.pixelSize: 11
            color: root.thumbnailReady ? "#ffffff" : Theme.foreground
        }

        // Stars sit at the right end of the strip, so they read with the
        // time and size rather than competing with the picture.
        Text {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 8
            visible: root.rating > 0
            text: "★".repeat(root.rating)
            font.pixelSize: 11
            color: root.thumbnailReady ? Theme.yellow : Theme.foreground
        }

        Row {
            id: metadataRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 8
            spacing: 8
            visible: root.stripShown

            // White over the scrim once the picture is up; theme text over the
            // bare frame before that, or on a light theme it would vanish.
            Text {
                text: root.timeLabel
                font.family: Theme.fontFamily
                font.pixelSize: 11
                font.weight: Font.DemiBold
                color: root.thumbnailReady ? "#ffffff" : Theme.foreground
                opacity: 1
            }

            Text {
                text: root.sizeLabel
                font.family: Theme.fontFamily
                font.pixelSize: 11
                color: root.thumbnailReady ? "#ffffff" : Theme.foreground
                opacity: 0.82
            }
        }

        // Kind marker. A recording needs to be tellable from a screenshot at a
        // glance, before any text is read, and a RAW file from its JPEG.
        Rectangle {
            objectName: "rawBadge"
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 8
            visible: root.rawFormat !== ""
            width: rawLabel.implicitWidth + 12
            height: rawLabel.implicitHeight + 6
            radius: 3
            color: Qt.rgba(0, 0, 0, 0.55)

            Text {
                id: rawLabel
                anchors.centerIn: parent
                text: root.rawFormat
                font.family: Theme.fontFamily
                font.pixelSize: 10
                font.weight: Font.DemiBold
                color: "#ffffff"
            }
        }

        Rectangle {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 8
            visible: root.isVideo
            width: badge.implicitWidth + 12
            height: badge.implicitHeight + 6
            radius: 3
            color: Qt.rgba(0, 0, 0, 0.55)

            Text {
                id: badge
                anchors.centerIn: parent
                text: (scrubTimer.running ? "◉ " : "▶ ") + root.kindLabel
                font.family: Theme.fontFamily
                font.pixelSize: 10
                color: "#ffffff"
            }
        }

        Text {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 8
            visible: root.favorite
            text: "★"
            font.pixelSize: 15
            color: Theme.yellow
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.6)
        }

        // Multi-select checkbox. Only present once selection is under way or the
        // card is hovered, so the resting grid stays pictures rather than chrome.
        Rectangle {
            id: checkBox
            Accessible.ignored: true
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 7
            visible: root.selectionMode || hover.hovered
            width: root.gallery ? 20 : 18
            height: width
            radius: root.gallery ? width / 2 : 3
            color: root.checked ? Theme.accent : Qt.rgba(0, 0, 0, 0.5)
            border.width: 1
            border.color: root.checked ? Theme.accent : Qt.rgba(1, 1, 1, 0.5)

            Text {
                anchors.centerIn: parent
                visible: root.checked
                text: "✓"
                font.pixelSize: 11
                font.weight: Font.Bold
                color: Theme.background
            }
        }

        // Count badge, drawn only while a multi-file drag is being captured, so
        // the drag image says how many files are leaving.
        Rectangle {
            anchors.centerIn: parent
            visible: root.dragging && root.dragPaths.length > 1
            width: dragCount.implicitWidth + 24
            height: dragCount.implicitHeight + 14
            radius: height / 2
            color: Theme.accent

            Text {
                id: dragCount
                anchors.centerIn: parent
                text: root.dragPaths.length + " files"
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.weight: Font.Bold
                color: Theme.background
            }
        }

        // Keep the current/checked outline above the thumbnail and metadata
        // strip. A Rectangle's own border is painted below its children, which
        // let the bottom strip cover the lower edge of the selection.
        // A gallery tile has no resting border, so hover lifts it with a soft
        // wash and a faint accent edge instead.
        Rectangle {
            anchors.fill: parent
            z: 19
            visible: root.gallery
            radius: frame.radius
            color: root.shade(Theme.foreground, hover.hovered ? 0.07 : 0)
            border.width: hover.hovered && !root.selected && !root.checked ? 1 : 0
            border.color: root.shade(Theme.accent, 0.55)
            Behavior on color { ColorAnimation { duration: 120; easing.type: Easing.OutQuad } }
        }

        Rectangle {
            anchors.fill: parent
            z: 20
            color: "transparent"
            radius: frame.radius
            border.width: root.selected || root.checked ? 2 : 0
            border.color: root.checked ? Theme.accent : root.shade(Theme.accent, 0.75)
        }

        HoverHandler { id: hover }

        TapHandler {
            acceptedButtons: Qt.LeftButton
            onSingleTapped: function (eventPoint) {
                const checkboxPoint = checkBox.mapFromItem(frame, eventPoint.position)
                if (root.selectionMode || checkBox.contains(checkboxPoint)) {
                    root.toggleChecked()
                } else {
                    root.activated()
                }
            }
            onDoubleTapped: root.chosen()
            // Touch has no hover, so no checkbox; a long press selects instead.
            onLongPressed: root.toggleChecked()
        }

        // Right click goes straight to the actions, which is what a right
        // click on a file means everywhere else on the desktop.
        TapHandler {
            acceptedButtons: Qt.RightButton
            onSingleTapped: function(point) {
                const at = frame.mapToItem(null, point.position.x, point.position.y)
                root.contextRequested(at.x, at.y)
            }
        }
    }
}
