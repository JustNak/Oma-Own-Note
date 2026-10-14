import QtQuick

// In-editor chrome for the live markdown surface. It does not own the text:
// a task click calls Backend.toggleTask, and Image.source is set only for a
// local file the backend has canonicalized, or a remote URL after an opt-in.
Item {
    id: chrome
    objectName: underlay ? "" : "editorChrome"

    property var editorItem
    property var decorations: []
    property color pageColor: "#101010"
    property color textColor: "#eeeeee"
    property color mutedColor: "#909191"
    property color accentColor: "#5584aa"
    property color ruleColor: "#4f525a"
    property color codeFill: "#1c1a1a"
    property int layoutEpoch: 0
    property bool underlay: false
    property var consentedRemote: ({})

    readonly property color headerFill: Qt.rgba(
        pageColor.r + (textColor.r - pageColor.r) * 0.16,
        pageColor.g + (textColor.g - pageColor.g) * 0.16,
        pageColor.b + (textColor.b - pageColor.b) * 0.16,
        1)
    readonly property color bodyFill: Qt.rgba(
        pageColor.r + (textColor.r - pageColor.r) * 0.07,
        pageColor.g + (textColor.g - pageColor.g) * 0.07,
        pageColor.b + (textColor.b - pageColor.b) * 0.07,
        1)

    function rectAt(position) {
        var unused = chrome.layoutEpoch
        if (!editorItem)
            return Qt.rect(0, 0, 0, 0)
        return editorItem.positionToRectangle(position)
    }

    function consentRemote(url) {
        if (!url)
            return
        var next = {}
        var key
        for (key in chrome.consentedRemote)
            next[key] = true
        next[url] = true
        chrome.consentedRemote = next
    }

    Repeater {
        id: decorationRepeater
        objectName: "decorationRepeater"
        model: chrome.decorations

        delegate: Item {
            id: overlay
            required property var modelData

            Rectangle {
                id: box
                objectName: !chrome.underlay && overlay.modelData.kind === "task" ? "taskCheckbox" : ""
                visible: !chrome.underlay && overlay.modelData.kind === "task"
                readonly property rect startRect: chrome.rectAt(overlay.modelData.start)
                readonly property rect endRect: chrome.rectAt(overlay.modelData.end)
                x: startRect.x
                y: startRect.y + Math.max(0, (startRect.height - height) / 2)
                width: Math.max(12, endRect.x - startRect.x)
                height: Math.max(12, Math.min(Math.max(12, startRect.height - 2), width))
                radius: 2
                color: chrome.pageColor
                border.width: activeFocus ? 2 : 1
                border.color: activeFocus ? chrome.accentColor : chrome.ruleColor
                activeFocusOnTab: visible
                Accessible.role: Accessible.CheckBox
                Accessible.name: overlay.modelData.checked ? "Checked task" : "Unchecked task"

                Rectangle {
                    visible: overlay.modelData.checked === true
                    anchors.centerIn: parent
                    width: parent.width * 0.45
                    height: parent.height * 0.45
                    radius: 1
                    color: chrome.accentColor
                }

                function toggle() {
                    backend.toggleTask(overlay.modelData.start)
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: box.visible
                    cursorShape: Qt.PointingHandCursor
                    onClicked: box.toggle()
                }

                Keys.onPressed: function(event) {
                    if (!box.visible)
                        return
                    if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                            || event.key === Qt.Key_Enter) {
                        box.toggle()
                        event.accepted = true
                    }
                }
            }

            Item {
                id: imageItem
                objectName: overlay.modelData.kind === "image" ? "imageDecoration" : ""
                visible: !chrome.underlay && overlay.modelData.kind === "image"
                readonly property int imageCap: 160
                readonly property rect startRect: chrome.rectAt(overlay.modelData.start)
                readonly property bool remoteAllowed: overlay.modelData.mode === "remote"
                        && chrome.consentedRemote[overlay.modelData.remoteUrl] === true
                readonly property int labelSize: chrome.editorItem
                        ? Math.max(11, Math.round(chrome.editorItem.font.pixelSize * 0.65))
                        : 13
                x: startRect.x
                y: startRect.y
                width: Math.max(160, image.paintedWidth, placeholder.implicitWidth + 16)
                height: imageCap
                clip: true

                Image {
                    id: image
                    objectName: imageItem.visible ? "renderedImage" : ""
                    anchors.left: parent.left
                    anchors.top: parent.top
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    source: {
                        if (!imageItem.visible)
                            return ""
                        if (overlay.modelData.mode === "local")
                            return overlay.modelData.source || ""
                        if (imageItem.remoteAllowed)
                            return overlay.modelData.remoteUrl || ""
                        return ""
                    }
                    sourceSize.height: imageItem.imageCap
                    width: source === "" ? 0 : Math.min(320, sourceSize.width > 0 ? sourceSize.width : 160)
                    height: source === "" ? 0 : Math.min(imageItem.imageCap,
                            sourceSize.height > 0 ? sourceSize.height : imageItem.imageCap)
                }

                Rectangle {
                    id: placeholder
                    objectName: !imageItem.visible ? ""
                            : overlay.modelData.mode === "remote" ? "remoteImageButton"
                            : "imagePlaceholder"
                    visible: imageItem.visible && overlay.modelData.mode !== "local"
                            && !imageItem.remoteAllowed
                    width: Math.max(168, label.implicitWidth + 24)
                    height: Math.max(label.implicitHeight + 12, 28)
                    radius: 2
                    color: chrome.headerFill
                    border.width: activeFocus ? 2 : 1
                    border.color: activeFocus ? chrome.accentColor : chrome.ruleColor
                    activeFocusOnTab: overlay.modelData.mode === "remote"
                    Accessible.role: Accessible.Button
                    Accessible.name: "Load image"

                    Text {
                        id: label
                        objectName: placeholder.visible ? "imageActionLabel" : ""
                        anchors.centerIn: parent
                        color: chrome.textColor
                        font.family: "iA Writer Mono S"
                        font.pixelSize: imageItem.labelSize
                        text: overlay.modelData.mode === "remote" ? "Load image"
                                : overlay.modelData.mode === "missing" ? "Missing image"
                                : "Image blocked"
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: overlay.modelData.mode === "remote"
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: {
                            placeholder.forceActiveFocus()
                            chrome.consentRemote(overlay.modelData.remoteUrl)
                        }
                    }

                    Keys.onPressed: function(event) {
                        if (overlay.modelData.mode !== "remote")
                            return
                        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                                || event.key === Qt.Key_Enter) {
                            chrome.consentRemote(overlay.modelData.remoteUrl)
                            event.accepted = true
                        }
                    }
                }
            }

            Item {
                id: tableItem
                objectName: !chrome.underlay && overlay.modelData.kind === "table" ? "tableFrame" : ""
                visible: overlay.modelData.kind === "table"
                enabled: false
                readonly property var pipeList: overlay.modelData.pipes || []
                readonly property rect startRect: chrome.rectAt(overlay.modelData.start)
                readonly property rect endRect: chrome.rectAt(Math.max(overlay.modelData.start,
                                                                       overlay.modelData.end - 1))
                readonly property rect firstPipe: pipeList.length > 0 ? chrome.rectAt(pipeList[0]) : startRect
                readonly property rect lastPipe: pipeList.length > 0
                        ? chrome.rectAt(pipeList[pipeList.length - 1]) : endRect
                x: firstPipe.x
                y: startRect.y
                width: Math.max(8, lastPipe.x + Math.max(1, lastPipe.width) - firstPipe.x)
                height: Math.max(8, endRect.y + endRect.height - startRect.y)

                Rectangle {
                    visible: chrome.underlay && overlay.modelData.headerEnd >= 0
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: {
                        var header = chrome.rectAt(overlay.modelData.headerEnd)
                        return Math.max(1, header.y + header.height - tableItem.y)
                    }
                    color: chrome.headerFill
                }

                Rectangle {
                    visible: chrome.underlay
                    anchors.left: parent.left
                    anchors.right: parent.right
                    y: {
                        var header = chrome.rectAt(overlay.modelData.headerEnd)
                        return Math.max(0, header.y + header.height - tableItem.y)
                    }
                    height: Math.max(0, parent.height - y)
                    color: chrome.bodyFill
                }

                Rectangle {
                    visible: !chrome.underlay
                    anchors.fill: parent
                    color: "transparent"
                    border.width: 1
                    border.color: chrome.ruleColor
                }

                Repeater {
                    model: chrome.underlay ? [] : (overlay.modelData.rows || [])
                    delegate: Rectangle {
                        objectName: !chrome.underlay && index > 0 ? "tableRowRule" : ""
                        required property int index
                        required property var modelData
                        readonly property rect rowRect: chrome.rectAt(modelData)
                        visible: index > 0
                        x: 0
                        y: Math.max(0, rowRect.y - tableItem.y)
                        width: parent.width
                        height: 1
                        color: chrome.ruleColor
                    }
                }

                Repeater {
                    model: chrome.underlay ? [] : (overlay.modelData.pipes || [])
                    delegate: Rectangle {
                        required property var modelData
                        readonly property rect pipeRect: chrome.rectAt(modelData)
                        x: pipeRect.x - parent.x
                        y: 0
                        width: 1
                        height: parent.height
                        color: chrome.ruleColor
                    }
                }
            }

            Repeater {
                model: !chrome.underlay && overlay.modelData.kind === "quote"
                        ? (overlay.modelData.bars || []) : []
                delegate: Rectangle {
                    objectName: "quoteBar"
                    required property var modelData
                    readonly property rect barRect: chrome.rectAt(modelData)
                    x: barRect.x
                    y: barRect.y
                    width: 2
                    height: Math.max(1, barRect.height)
                    color: chrome.mutedColor
                }
            }

            Text {
                objectName: !chrome.underlay && overlay.modelData.kind === "code"
                        && overlay.modelData.showLabel === true
                        && overlay.modelData.language ? "codeLanguage" : ""
                visible: objectName === "codeLanguage"
                readonly property rect startRect: chrome.rectAt(overlay.modelData.start)
                x: 8
                y: startRect.y - Math.max(12, font.pixelSize + 2)
                color: chrome.mutedColor
                font.family: "iA Writer Mono S"
                font.pixelSize: chrome.editorItem
                        ? Math.max(11, Math.round(chrome.editorItem.font.pixelSize * 0.65))
                        : 13
                text: overlay.modelData.language || ""
            }

            Item {
                visible: chrome.underlay && overlay.modelData.kind === "code"
                readonly property rect startRect: chrome.rectAt(overlay.modelData.start)
                readonly property rect endRect: chrome.rectAt(Math.max(overlay.modelData.start,
                                                                       overlay.modelData.end - 1))
                x: 0
                y: startRect.y
                width: chrome.editorItem ? chrome.editorItem.width : 0
                height: Math.max(1, endRect.y + endRect.height - startRect.y)

                Rectangle {
                    anchors.fill: parent
                    color: chrome.codeFill
                }
            }

            Rectangle {
                objectName: !chrome.underlay && overlay.modelData.kind === "rule" ? "horizontalRule" : ""
                visible: !chrome.underlay && overlay.modelData.kind === "rule"
                enabled: false
                readonly property rect startRect: chrome.rectAt(overlay.modelData.start)
                x: 0
                y: startRect.y + startRect.height / 2
                width: chrome.editorItem ? chrome.editorItem.width : 0
                height: 1
                color: chrome.ruleColor
            }
        }
    }
}
