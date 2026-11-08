import QtQuick
import QtQuick.Controls.Basic

// The gallery layout of the library: masonry columns, like Pinterest. Every
// picture is drawn whole at its own height, never cropped, and a header
// starts each day.
//
// Positions come from Captures.galleryLayout(), computed in C++. Only the
// tiles near the screen exist: tops never decrease in model order, so the
// visible range is two binary searches, and a fixed pool of tiles is reused
// as it slides. Selection, the current tile and every action stay with the
// owning CaptureGrid, which is what Main talks to.
Flickable {
    id: view

    // The CaptureGrid that owns selection and the current index.
    required property Item owner
    property var layout: ({ columns: 0, columnWidth: 0, height: 0, tallest: 0,
                            xs: [], ys: [], hs: [], headerYs: [], headerLabels: [] })

    readonly property int spacingPx: 8
    readonly property int headerHeight: 40
    // Ctrl+wheel and Ctrl+plus/minus step the tile width, which the gallery
    // reads as its column width, so both views follow the same setting.
    readonly property real targetColumnWidth: Settings.tileWidth
    readonly property bool grouped: Captures.duplicatesOnly || Captures.similarOnly
                                    || Captures.sortMode === 0 || Captures.sortMode === 1
    readonly property real overscan: Math.max(300, height / 2)

    // The window of items that may be on screen, and the tile pool.
    property int first: 0
    property int last: -1
    property int pool: 0
    property int headerFirst: 0
    property int headerLast: -1

    clip: true
    boundsBehavior: Flickable.StopAtBounds
    contentWidth: width
    contentHeight: layout.height + 12

    // First index whose value is at least target, in a sorted array.
    function lowerBound(values, target) {
        let lo = 0
        let hi = values.length
        while (lo < hi) {
            const mid = (lo + hi) >> 1
            if (values[mid] < target) lo = mid + 1
            else hi = mid
        }
        return lo
    }

    function updateWindow() {
        const ys = layout.ys
        if (!ys || ys.length === 0) {
            first = 0
            last = -1
            headerFirst = 0
            headerLast = -1
            return
        }
        const top = contentY - overscan
        const bottom = contentY + height + overscan
        first = Math.min(ys.length - 1, lowerBound(ys, top - layout.tallest))
        last = Math.max(first - 1, lowerBound(ys, bottom) - 1)
        // The pool only grows, so sliding the window rebinds a few tiles
        // instead of recreating them all.
        const needed = last - first + 1
        if (needed > pool) pool = needed
        const hy = layout.headerYs
        headerFirst = lowerBound(hy, top - headerHeight)
        headerLast = lowerBound(hy, bottom) - 1
    }

    // The item that the pool slot shows: the one in the window congruent to
    // the slot, or -1.
    function itemForSlot(slot, windowFirst, windowLast, size) {
        if (size <= 0) return -1
        let item = windowFirst - (windowFirst % size) + slot
        if (item < windowFirst) item += size
        return item <= windowLast ? item : -1
    }

    // The item drawn at the top edge of the view.
    function itemAtTop() {
        const ys = layout.ys
        const hs = layout.hs
        if (!ys || ys.length === 0) return -1
        let i = lowerBound(ys, contentY - layout.tallest)
        for (; i < ys.length; ++i) {
            if (ys[i] + hs[i] > contentY) return i
        }
        return ys.length - 1
    }

    function sameArray(a, b) {
        if (!a || !b || a.length !== b.length) return false
        for (let i = 0; i < a.length; ++i) {
            if (a[i] !== b[i]) return false
        }
        return true
    }

    function sameLayout(a, b) {
        return a.columns === b.columns && a.columnWidth === b.columnWidth
            && a.height === b.height && sameArray(a.ys, b.ys) && sameArray(a.xs, b.xs)
            && sameArray(a.hs, b.hs) && sameArray(a.headerYs, b.headerYs)
            && sameArray(a.headerLabels, b.headerLabels)
    }

    function rebuild() {
        if (!visible || width <= 0) {
            return
        }
        const anchor = itemAtTop()
        const anchorPath = anchor >= 0 ? Captures.pathAt(anchor) : ""
        const offset = anchor >= 0 ? contentY - layout.ys[anchor] : 0
        const next = Captures.galleryLayout(width, targetColumnWidth, spacingPx, headerHeight, grouped)
        // Tiles keep their slots, so a row change rereads only the tiles on
        // screen; their pictures stay up when nothing about them changed.
        for (let slot = 0; slot < tiles.count; ++slot) {
            const tile = tiles.itemAt(slot)
            if (tile) tile.revision++
        }
        // Background work that leaves every position alone, such as a scan of
        // another folder, must not move or redraw this one.
        if (sameLayout(layout, next)) return
        layout = next
        const item = anchorPath !== "" ? Captures.rowOf(anchorPath) : -1
        if (item >= 0 && item < layout.ys.length) {
            contentY = Math.max(0, Math.min(layout.ys[item] + offset,
                                            Math.max(0, contentHeight - height)))
        } else {
            contentY = Math.max(0, Math.min(contentY, Math.max(0, contentHeight - height)))
        }
        updateWindow()
        owner.updateDay()
    }

    Timer {
        id: relayout
        interval: 60
        onTriggered: view.rebuild()
    }
    function scheduleRebuild() { relayout.restart() }

    onWidthChanged: scheduleRebuild()
    onHeightChanged: updateWindow()
    onTargetColumnWidthChanged: scheduleRebuild()
    onGroupedChanged: scheduleRebuild()
    onVisibleChanged: if (visible) rebuild()
    onContentYChanged: {
        updateWindow()
        owner.updateDay()
    }
    Component.onCompleted: scheduleRebuild()

    Connections {
        target: Captures
        function onModelReset() { view.scheduleRebuild() }
        function onLayoutChanged() { view.scheduleRebuild() }
        function onRowsInserted() { view.scheduleRebuild() }
        function onRowsRemoved() { view.scheduleRebuild() }
        // Only tiles showing a changed row read it again, so a background
        // index writing metadata does not refresh the whole screen.
        function onDataChanged(topLeft, bottomRight) {
            const from = topLeft.row
            const to = bottomRight.row
            for (let slot = 0; slot < tiles.count; ++slot) {
                const tile = tiles.itemAt(slot)
                if (tile && tile.item >= from && tile.item <= to) tile.revision++
            }
        }
    }
    Connections {
        target: Aspects
        function onRatiosChanged() { view.scheduleRebuild() }
    }
    Connections {
        target: Library
        function onDayLabelsChanged() { view.scheduleRebuild() }
    }

    function revealItem(item) {
        const ys = layout.ys
        if (item < 0 || !ys || item >= ys.length) return
        const top = ys[item]
        const bottom = top + layout.hs[item]
        if (top < contentY) {
            contentY = Math.max(0, top - spacingPx)
        } else if (bottom > contentY + height) {
            contentY = Math.min(Math.max(0, contentHeight - height),
                                bottom - height + spacingPx)
        }
    }

    function stepItem(direction) {
        const count = owner.count
        if (count === 0) return
        owner.currentIndex = (owner.currentIndex + direction + count) % count
        revealItem(owner.currentIndex)
    }

    // Up and Down move within the column, to the picture directly above or
    // below; at a group's edge they cross to the nearest picture there.
    function stepRow(direction) {
        const xs = layout.xs
        const ys = layout.ys
        const count = xs ? xs.length : 0
        const current = owner.currentIndex
        if (count === 0) return
        if (current < 0 || current >= count) {
            owner.currentIndex = 0
            return
        }
        const x = xs[current]
        const y = ys[current]
        let best = -1
        let bestDistance = Infinity
        const reach = Math.max(64, layout.columns * 8)
        for (let step = 1; step <= reach; ++step) {
            const j = current + direction * step
            if (j < 0 || j >= count) break
            const dy = direction > 0 ? ys[j] - y : y - ys[j]
            if (dy <= 0) continue
            const distance = dy + Math.abs(xs[j] - x) * 4
            if (distance < bestDistance) {
                bestDistance = distance
                best = j
            }
            if (Math.abs(xs[j] - x) < 1) break
        }
        if (best < 0) return
        owner.currentIndex = best
        revealItem(best)
    }

    // Every on-screen tile has its thumbnail up. Used by --render.
    function viewportReady() {
        if (!visible || !layout.ys || layout.ys.length === 0 || width <= 0 || height <= 0)
            return false
        for (let slot = 0; slot < tiles.count; ++slot) {
            const tile = tiles.itemAt(slot)
            if (!tile || tile.item < 0) continue
            if (tile.y + tile.height <= contentY || tile.y >= contentY + height) continue
            if (!tile.thumbnailPresented) return false
        }
        return true
    }

    WheelHandler {
        target: null
        acceptedModifiers: Qt.ControlModifier
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function (event) {
            const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.pixelDelta.y
            Settings.tileWidth += delta > 0 ? view.owner.tileStep : -view.owner.tileStep
            event.accepted = true
        }
    }

    Repeater {
        model: Math.max(0, view.headerLast - view.headerFirst + 1)

        Text {
            objectName: "galleryDayHeader"
            required property int index
            readonly property int header: view.headerFirst + index
            x: 2
            y: (view.layout.headerYs[header] || 0) + view.headerHeight - height - 10
            width: view.width - 4
            text: view.layout.headerLabels[header] || ""
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: 14
            font.weight: Font.DemiBold
            color: Theme.brightForeground
        }
    }

    Repeater {
        id: tiles
        model: view.pool

        CaptureCard {
            id: tile
            required property int index
            readonly property int item: view.itemForSlot(index, view.first, view.last, view.pool)
            property int revision: 0
            readonly property var info: item >= 0 ? (revision, Captures.rowData(item)) : ({})

            gallery: true
            visible: item >= 0
            x: item >= 0 ? view.layout.xs[item] : 0
            y: item >= 0 ? view.layout.ys[item] : 0
            width: Math.max(1, Math.floor(view.layout.columnWidth))
            height: item >= 0 ? Math.max(1, view.layout.hs[item]) : 1

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
            selected: view.owner.currentIndex === item
            keyboardCurrent: selected && view.activeFocus
            checked: view.owner.isChecked(path)
            selectionMode: view.owner.checkedCount > 0
            dragPaths: view.owner.isChecked(path) ? view.owner.checkedPaths() : [path]

            onActivated: view.owner.activatePath(path)
            onChosen: view.owner.choosePath(path)
            onContextRequested: function (x, y) { view.owner.contextPath(path, x, y) }
            onToggleChecked: view.owner.togglePath(path)
            // A shape the headers could not give (video, PDF, raw) comes from
            // the thumbnail itself, and the columns settle to it.
            onHoveredChanged: {
                if (hovered) view.owner.hoveredPath = path
                else if (view.owner.hoveredPath === path) view.owner.hoveredPath = ""
            }
            onPathChanged: if (hovered) view.owner.hoveredPath = path
            onShapeSeen: function (ratio) { Aspects.learn(path, stamp, ratio) }
        }
    }
}
