import QtQuick

Item {
    id: preview

    property var blocks: []
    property color textColor: "#eeeeee"
    property color mutedColor: "#909191"
    property color accentColor: "#5584aa"
    property color pageColor: "#101010"
    property int baseSize: 20
    property string fontFamily: "iA Writer Mono S"

    readonly property bool darkPage: (pageColor.r + pageColor.g + pageColor.b) < 1.5
    readonly property color codeBackground: mix(pageColor, textColor, darkPage ? 0.08 : 0.05)
    readonly property string summary: {
        var names = []
        var list = blocks || []
        for (var i = 0; i < list.length; ++i)
            names.push(list[i].type)
        return names.join(",")
    }

    implicitHeight: blocksColumn.implicitHeight
    height: implicitHeight

    signal anchorRequested(Item item)

    function mix(base, ink, amount) {
        return Qt.rgba(base.r + (ink.r - base.r) * amount,
                        base.g + (ink.g - base.g) * amount,
                        base.b + (ink.b - base.b) * amount,
                        1)
    }

    MarkdownBlocks {
        id: blocksColumn
        width: preview.width
        modelBlocks: preview.blocks
        textColor: preview.textColor
        mutedColor: preview.mutedColor
        accentColor: preview.accentColor
        pageColor: preview.pageColor
        baseSize: preview.baseSize
        fontFamily: preview.fontFamily
        codeBackground: preview.codeBackground
        onAnchorRequested: function(item) { preview.anchorRequested(item) }
    }
}
