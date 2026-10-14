import QtQuick
import QtQuick.Layouts

Column {
    id: column

    property var modelBlocks: []
    property bool quoted: false
    property color textColor: "#eeeeee"
    property color mutedColor: "#909191"
    property color accentColor: "#5584aa"
    property color pageColor: "#101010"
    property int baseSize: 20
    property string fontFamily: "iA Writer Mono S"
    property color codeBackground: "#1c1a1a"

    signal linkActivated(string href)
    signal anchorRequested(Item item)

    spacing: 0
    width: parent ? parent.width : 0

    function mix(base, ink, amount) {
        return Qt.rgba(base.r + (ink.r - base.r) * amount,
                        base.g + (ink.g - base.g) * amount,
                        base.b + (ink.b - base.b) * amount,
                        1)
    }

    function openLink(href) {
        if (!href)
            return
        if (href.indexOf("#fn-") === 0) {
            var target = findNamed(column, href.substring(1))
            if (!target) {
                var root = column
                while (root.parent)
                    root = root.parent
                target = findNamed(root, href.substring(1))
            }
            if (target)
                anchorRequested(target)
            return
        }
        linkActivated(href)
        backend.openExternalUrl(href)
    }

    function findNamed(item, name) {
        if (!item)
            return null
        if (item.objectName === name)
            return item
        var kids = item.children || []
        for (var i = 0; i < kids.length; ++i) {
            var found = findNamed(kids[i], name)
            if (found)
                return found
        }
        return null
    }

    function gapBefore(kind) {
        if (kind === "heading")
            return Math.round(baseSize * 0.95)
        if (kind === "rule" || kind === "footnotes")
            return Math.round(baseSize * 0.8)
        if (kind === "code" || kind === "table" || kind === "quote")
            return Math.round(baseSize * 0.65)
        return Math.round(baseSize * 0.4)
    }

    function headingSize(level) {
        var scale = [1.55, 1.32, 1.14, 1.0, 0.9, 0.82]
        var index = Math.max(0, Math.min(5, level - 1))
        return Math.max(12, Math.round(baseSize * scale[index]))
    }

    function cellAlign(block, cellIndex) {
        var align = block && block.align ? block.align : []
        var name = align[cellIndex] || "left"
        if (name === "center")
            return Text.AlignHCenter
        if (name === "right")
            return Text.AlignRight
        return Text.AlignLeft
    }

    function rowFill(header, rowIndex) {
        if (header)
            return mix(pageColor, textColor, 0.14)
        if (rowIndex % 2 === 1)
            return mix(pageColor, textColor, 0.07)
        return "transparent"
    }

    function applyTheme(item, blocksBinding, quotedBinding) {
        item.modelBlocks = blocksBinding
        item.quoted = quotedBinding
        item.textColor = Qt.binding(function() { return column.textColor })
        item.mutedColor = Qt.binding(function() { return column.mutedColor })
        item.accentColor = Qt.binding(function() { return column.accentColor })
        item.pageColor = Qt.binding(function() { return column.pageColor })
        item.baseSize = Qt.binding(function() { return column.baseSize })
        item.fontFamily = Qt.binding(function() { return column.fontFamily })
        item.codeBackground = Qt.binding(function() { return column.codeBackground })
    }

    component Prose: Text {
        property bool quoted: false
        width: parent ? parent.width : 0
        textFormat: Text.RichText
        wrapMode: Text.Wrap
        color: quoted ? column.mutedColor : column.textColor
        font.family: column.fontFamily
        font.pixelSize: column.baseSize
        font.italic: quoted
        lineHeight: 1.35
        lineHeightMode: Text.ProportionalHeight
        onLinkActivated: function(href) { column.openLink(href) }
    }

    component Cell: Rectangle {
        id: cell
        property string html: ""
        property bool headerCell: false
        property int cellIndex: 0
        property var alignBlock
        border.width: 1
        border.color: column.mutedColor
        color: column.rowFill(headerCell, 0)
        implicitHeight: cellText.implicitHeight + Math.round(column.baseSize * 0.75)
        height: implicitHeight

        Text {
            id: cellText
            x: 8
            y: Math.round(column.baseSize * 0.32)
            width: Math.max(1, parent.width - 16)
            text: cell.html
            textFormat: Text.RichText
            wrapMode: Text.Wrap
            color: column.textColor
            font.family: column.fontFamily
            font.pixelSize: column.baseSize
            font.bold: cell.headerCell
            horizontalAlignment: column.cellAlign(cell.alignBlock, cell.cellIndex)
            lineHeight: 1.3
            lineHeightMode: Text.ProportionalHeight
            onLinkActivated: function(href) { column.openLink(href) }
        }
    }

    Repeater {
        model: column.modelBlocks
        delegate: Column {
            id: blockItem
            width: column.width
            spacing: 0
            property var block: modelData
            property string kind: block && block.type ? block.type : ""
            property int columns: {
                if (kind !== "table")
                    return 0
                var header = block.header || []
                if (header.length > 0)
                    return header.length
                var body = block.rows || []
                return body.length > 0 ? body[0].length : 0
            }
            property real cellWidth: columns > 0 ? (width + columns - 1) / columns : width

            Item {
                width: 1
                height: blockItem.index === 0 ? 0 : column.gapBefore(blockItem.kind)
            }

            Prose {
                visible: blockItem.kind === "heading"
                width: blockItem.width
                text: blockItem.block.html || ""
                font.pixelSize: column.headingSize(blockItem.block.level || 1)
                font.bold: true
                font.italic: false
                color: (blockItem.block.level || 1) >= 6 ? column.mutedColor : column.textColor
                lineHeight: 1.15
            }

            Prose {
                visible: blockItem.kind === "paragraph" || blockItem.kind === "html"
                width: blockItem.width
                text: blockItem.block.html || ""
                quoted: column.quoted
            }

            Column {
                visible: blockItem.kind === "list"
                width: blockItem.width
                spacing: Math.max(4, Math.round(column.baseSize * 0.28))

                Repeater {
                    model: blockItem.block.items || []
                    delegate: Row {
                        id: listRow
                        width: blockItem.width
                        spacing: Math.round(column.baseSize * 0.45)
                        property var item: modelData
                        property int markerWidth: Math.round(column.baseSize * 1.45)

                        Item {
                            width: listRow.markerWidth
                            height: markerText.implicitHeight
                            Text {
                                id: markerText
                                visible: !(listRow.item && listRow.item.task)
                                text: blockItem.block.ordered
                                      ? ((blockItem.block.start || 1) + index) + "."
                                      : "•"
                                color: column.mutedColor
                                font.family: column.fontFamily
                                font.pixelSize: column.baseSize
                            }
                            Rectangle {
                                visible: !!(listRow.item && listRow.item.task)
                                width: Math.round(column.baseSize * 0.62)
                                height: width
                                anchors.verticalCenter: parent.verticalCenter
                                color: "transparent"
                                border.width: 1
                                border.color: column.mutedColor
                                Rectangle {
                                    visible: !!listRow.item.checked
                                    anchors.centerIn: parent
                                    width: parent.width * 0.55
                                    height: width
                                    color: column.textColor
                                }
                            }
                        }

                        Loader {
                            id: nestedList
                            width: Math.max(1, listRow.width - listRow.markerWidth - listRow.spacing)
                            source: "MarkdownBlocks.qml"
                            onLoaded: column.applyTheme(item,
                                Qt.binding(function() { return listRow.item.blocks || [] }),
                                Qt.binding(function() { return column.quoted }))
                            Connections {
                                target: nestedList.item
                                function onLinkActivated(href) { column.linkActivated(href) }
                                function onAnchorRequested(target) { column.anchorRequested(target) }
                            }
                        }
                    }
                }
            }

            Row {
                id: quoteRow
                visible: blockItem.kind === "quote"
                width: blockItem.width
                spacing: Math.round(column.baseSize * 0.55)

                Rectangle {
                    width: 2
                    height: quoteBody.item ? quoteBody.item.implicitHeight : 0
                    color: column.mutedColor
                }

                Loader {
                    id: quoteBody
                    active: blockItem.kind === "quote"
                    width: Math.max(1, quoteRow.width - quoteRow.spacing - 2)
                    source: "MarkdownBlocks.qml"
                    onLoaded: column.applyTheme(item,
                        Qt.binding(function() { return blockItem.block.blocks || [] }),
                        true)
                    Connections {
                        target: quoteBody.item
                        function onLinkActivated(href) { column.linkActivated(href) }
                        function onAnchorRequested(target) { column.anchorRequested(target) }
                    }
                }
            }

            Rectangle {
                visible: blockItem.kind === "rule"
                width: blockItem.width
                height: 1
                color: column.mutedColor
            }

            Image {
                id: figure
                visible: blockItem.kind === "image"
                source: blockItem.block.src || ""
                fillMode: Image.PreserveAspectFit
                width: Math.min(implicitWidth > 0 ? implicitWidth : blockItem.width, blockItem.width)
                asynchronous: false
            }

            Text {
                visible: blockItem.kind === "image" && figure.status === Image.Error
                text: blockItem.block.alt || ""
                width: blockItem.width
                wrapMode: Text.Wrap
                color: column.mutedColor
                font.family: column.fontFamily
                font.pixelSize: column.baseSize
            }

            Rectangle {
                id: codeFrame
                visible: blockItem.kind === "code"
                width: blockItem.width
                radius: 4
                color: column.codeBackground
                implicitHeight: codeColumn.implicitHeight + Math.round(column.baseSize * 0.9)

                Column {
                    id: codeColumn
                    x: Math.round(column.baseSize * 0.6)
                    y: Math.round(column.baseSize * 0.45)
                    width: Math.max(1, codeFrame.width - x * 2)
                    spacing: Math.round(column.baseSize * 0.3)

                    RowLayout {
                        width: parent.width
                        Text {
                            text: blockItem.block.language || ""
                            visible: text.length > 0
                            color: column.mutedColor
                            font.family: column.fontFamily
                            font.pixelSize: Math.max(11, Math.round(column.baseSize * 0.62))
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            objectName: "codeCopyButton"
                            text: copyFlash ? "Copied" : "Copy"
                            color: column.mutedColor
                            font.family: column.fontFamily
                            font.pixelSize: Math.max(11, Math.round(column.baseSize * 0.62))
                            property bool copyFlash: false

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -6
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    backend.copyPlainText(blockItem.block.text || "")
                                    parent.copyFlash = true
                                    copyReset.start()
                                }
                            }

                            Timer {
                                id: copyReset
                                interval: 1200
                                onTriggered: parent.copyFlash = false
                            }
                        }
                    }

                    Flickable {
                        width: parent.width
                        height: codeBody.implicitHeight
                        contentWidth: Math.max(width, codeBody.implicitWidth)
                        contentHeight: height
                        clip: true
                        flickableDirection: Flickable.HorizontalFlick
                        boundsBehavior: Flickable.StopAtBounds

                        Text {
                            id: codeBody
                            text: blockItem.block.html || ""
                            textFormat: Text.RichText
                            color: column.textColor
                            font.family: column.fontFamily
                            font.pixelSize: Math.max(13, Math.round(column.baseSize * 0.85))
                            wrapMode: Text.NoWrap
                        }
                    }
                }
            }

            Column {
                visible: blockItem.kind === "table"
                width: blockItem.width
                spacing: -1

                Row {
                    width: blockItem.width
                    spacing: -1
                    visible: (blockItem.block.header || []).length > 0
                    Repeater {
                        model: blockItem.block.header || []
                        delegate: Cell {
                            width: blockItem.cellWidth
                            html: modelData
                            headerCell: true
                            cellIndex: index
                            alignBlock: blockItem.block
                            color: column.rowFill(true, 0)
                        }
                    }
                }

                Repeater {
                    model: blockItem.block.rows || []
                    delegate: Row {
                        id: bodyRow
                        width: blockItem.width
                        spacing: -1
                        property var cells: modelData
                        property int bodyIndex: index
                        Repeater {
                            model: bodyRow.cells
                            delegate: Cell {
                                width: blockItem.cellWidth
                                html: modelData
                                headerCell: false
                                cellIndex: index
                                alignBlock: blockItem.block
                                color: column.rowFill(false, bodyRow.bodyIndex)
                            }
                        }
                    }
                }
            }

            Column {
                visible: blockItem.kind === "footnotes"
                width: blockItem.width
                spacing: Math.round(column.baseSize * 0.5)

                Rectangle {
                    width: parent.width
                    height: 1
                    color: column.mutedColor
                }

                Repeater {
                    model: blockItem.block.items || []
                    delegate: Row {
                        id: noteRow
                        objectName: "fn-" + (modelData.id || "")
                        width: blockItem.width
                        spacing: Math.round(column.baseSize * 0.45)

                        Text {
                            id: noteIndex
                            text: (modelData.id || "") + "."
                            color: column.mutedColor
                            font.family: column.fontFamily
                            font.pixelSize: column.baseSize
                        }

                        Loader {
                            id: nestedNote
                            width: Math.max(1, noteRow.width - noteIndex.width - noteRow.spacing)
                            source: "MarkdownBlocks.qml"
                            onLoaded: column.applyTheme(item,
                                Qt.binding(function() { return modelData.blocks || [] }),
                                false)
                            Connections {
                                target: nestedNote.item
                                function onLinkActivated(href) { column.linkActivated(href) }
                                function onAnchorRequested(target) { column.anchorRequested(target) }
                            }
                        }
                    }
                }
            }
        }
    }
}
