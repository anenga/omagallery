import QtQuick
import QtQuick.Controls.Basic

// The gallery layout of the library: justified rows that keep every picture's
// own shape, with a header over each day, in the manner of a photos app.
//
// The rows come from Captures.galleryRows(), packed in C++ from each file's
// shape. The ListView recycles whole rows, so scrolling cost stays with what
// is on screen however large the library is. Selection, the current tile and
// every action stay with the owning CaptureGrid, which is what Main talks to.
ListView {
    id: list

    // The CaptureGrid that owns selection and the current index.
    required property Item owner
    property var rows: []
    // Bumped when any row's data changes, so tiles that read rows by index
    // (not as delegates of the capture model) pick the change up.
    property int dataRevision: 0

    readonly property int spacingPx: 4
    readonly property int headerHeight: 34
    // Ctrl+wheel and Ctrl+plus/minus already step the tile width; the gallery
    // reads the same setting as its row height so both views follow it.
    readonly property real targetHeight: Math.round(Settings.tileWidth * 0.72)
    readonly property bool grouped: Captures.duplicatesOnly || Captures.similarOnly
                                    || Captures.sortMode === 0 || Captures.sortMode === 1

    clip: true
    reuseItems: true
    boundsBehavior: Flickable.StopAtBounds
    cacheBuffer: Math.max(400, height)
    model: rows.length
    // Rows are rebuilt as a whole on relayout; keep the view still meanwhile.
    highlightFollowsCurrentItem: false
    currentIndex: -1

    function shade(base, amount) {
        return Qt.rgba(base.r, base.g, base.b, amount)
    }

    function rebuild() {
        if (!visible || width <= 0) {
            return
        }
        const anchorRow = indexAt(4, contentY + 4)
        const anchorItem = anchorRow >= 0 && anchorRow < rows.length ? rows[anchorRow].start : -1
        const anchorPath = anchorItem >= 0 ? Captures.pathAt(anchorItem) : ""
        rows = Captures.galleryRows(width, targetHeight, spacingPx, grouped)
        dataRevision++
        if (anchorPath !== "") {
            const item = Captures.rowOf(anchorPath)
            if (item >= 0) {
                positionViewAtIndex(rowOfItem(item), ListView.Beginning)
            }
        }
        owner.updateDay()
    }

    Timer {
        id: relayout
        interval: 60
        onTriggered: list.rebuild()
    }
    function scheduleRebuild() { relayout.restart() }

    onWidthChanged: scheduleRebuild()
    onTargetHeightChanged: scheduleRebuild()
    onGroupedChanged: scheduleRebuild()
    onVisibleChanged: if (visible) rebuild()
    Component.onCompleted: scheduleRebuild()

    Connections {
        target: Captures
        function onModelReset() { list.scheduleRebuild() }
        function onLayoutChanged() { list.scheduleRebuild() }
        function onRowsInserted() { list.scheduleRebuild() }
        function onRowsRemoved() { list.scheduleRebuild() }
        function onDataChanged() { list.dataRevision++ }
    }
    Connections {
        target: Aspects
        function onRatiosChanged() { list.scheduleRebuild() }
    }
    Connections {
        target: Library
        function onDayLabelsChanged() { list.scheduleRebuild() }
    }

    // The row holding a model row, by binary search over row starts.
    function rowOfItem(item) {
        let lo = 0
        let hi = rows.length - 1
        while (lo <= hi) {
            const mid = (lo + hi) >> 1
            const row = rows[mid]
            if (item < row.start) hi = mid - 1
            else if (item >= row.start + row.count) lo = mid + 1
            else return mid
        }
        return -1
    }

    function tileLeft(row, slot) {
        const r = rows[row]
        let x = 0
        for (let i = 0; i < slot; ++i) x += Math.floor(r.ratios[i] * r.height) + spacingPx
        return x
    }

    function revealItem(item) {
        const row = rowOfItem(item)
        if (row >= 0) positionViewAtIndex(row, ListView.Contain)
    }

    // Up and Down land on the tile nearest the current one's centre in the
    // row above or below, as in any justified gallery.
    function stepRow(direction) {
        const current = owner.currentIndex
        const row = rowOfItem(current)
        if (row < 0) {
            if (owner.count > 0) owner.currentIndex = 0
            return
        }
        const target = row + direction
        if (target < 0 || target >= rows.length) return
        const from = rows[row]
        const slot = current - from.start
        const centre = tileLeft(row, slot) + Math.floor(from.ratios[slot] * from.height) / 2
        const to = rows[target]
        let best = to.start
        let x = 0
        for (let i = 0; i < to.count; ++i) {
            const w = Math.floor(to.ratios[i] * to.height)
            if (centre >= x - spacingPx / 2) best = to.start + i
            x += w + spacingPx
        }
        owner.currentIndex = best
        revealItem(best)
    }

    function stepItem(direction) {
        if (owner.count === 0) return
        owner.currentIndex = (owner.currentIndex + direction + owner.count) % owner.count
        revealItem(owner.currentIndex)
    }

    // Every on-screen tile has its thumbnail up. Used by --render.
    function viewportReady() {
        if (!visible || rows.length === 0 || width <= 0 || height <= 0) return false
        const first = indexAt(4, contentY + 1)
        const last = indexAt(4, contentY + height - 1)
        if (first < 0) return false
        const end = last < 0 ? rows.length - 1 : last
        for (let r = first; r <= end; ++r) {
            const item = itemAtIndex(r)
            if (!item || !item.presented()) return false
        }
        return true
    }

    onContentYChanged: owner.updateDay()

    WheelHandler {
        target: null
        acceptedModifiers: Qt.ControlModifier
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function (event) {
            const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.pixelDelta.y
            Settings.tileWidth += delta > 0 ? list.owner.tileStep : -list.owner.tileStep
            event.accepted = true
        }
    }

    delegate: Item {
        id: rowItem
        required property int index
        readonly property var row: list.rows[index] || ({ start: 0, count: 0, height: 0, label: "", justified: false, ratios: [] })
        readonly property bool hasHeader: row.label !== ""

        width: list.width
        height: (hasHeader ? list.headerHeight + (index > 0 ? 14 : 0) : 0)
                + Math.floor(row.height) + list.spacingPx

        function presented() {
            for (let i = 0; i < tiles.count; ++i) {
                const tile = tiles.itemAt(i)
                if (!tile || !tile.thumbnailPresented) return false
            }
            return true
        }

        Text {
            objectName: "galleryDayHeader"
            visible: rowItem.hasHeader
            anchors.left: parent.left
            anchors.bottom: tileRow.top
            anchors.bottomMargin: 10
            anchors.leftMargin: 2
            text: rowItem.row.label
            font.family: Theme.fontFamily
            font.pixelSize: 14
            font.weight: Font.DemiBold
            color: Theme.brightForeground
        }

        Row {
            id: tileRow
            anchors.bottom: parent.bottom
            anchors.bottomMargin: list.spacingPx
            spacing: list.spacingPx

            Repeater {
                id: tiles
                model: rowItem.row.count

                CaptureCard {
                    id: tile
                    required property int index
                    readonly property int item: rowItem.row.start + index
                    readonly property var info: (list.dataRevision, Captures.rowData(item))
                    readonly property bool lastInRow: index === rowItem.row.count - 1

                    gallery: true
                    height: Math.floor(rowItem.row.height)
                    // The last tile takes up the rounding, so justified rows
                    // meet the right edge exactly.
                    width: {
                        const r = rowItem.row
                        const natural = Math.floor(r.ratios[index] * r.height)
                        if (!lastInRow || !r.justified) return natural
                        return Math.max(1, list.width - list.tileLeft(rowItem.index, index))
                    }

                    path: info.path || ""
                    fileName: info.fileName || ""
                    kindLabel: info.kindLabel || ""
                    timeLabel: info.timeLabel || ""
                    sizeLabel: info.sizeLabel || ""
                    isVideo: info.isVideo === true
                    isDocument: info.isDocument === true
                    stamp: info.stamp || 0
                    thumbnailVersion: info.thumbnailVersion || ""
                    favorite: info.favorite === true
                    rating: info.rating || 0
                    hiddenMark: info.hidden === true
                    ocrSnippet: info.ocrSnippet || ""
                    caption: info.caption || ""
                    rawFormat: info.rawFormat || ""
                    selected: list.owner.currentIndex === item
                    keyboardCurrent: selected && list.activeFocus
                    checked: list.owner.isChecked(path)
                    selectionMode: list.owner.checkedCount > 0
                    dragPaths: list.owner.isChecked(path) ? list.owner.checkedPaths() : [path]

                    onActivated: list.owner.activatePath(path)
                    onChosen: list.owner.choosePath(path)
                    onContextRequested: function (x, y) { list.owner.contextPath(path, x, y) }
                    onToggleChecked: list.owner.togglePath(path)
                }
            }
        }
    }
}
