import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "EditorMutations.js" as EditorMutations

Popup {
    id: root

    objectName: "insertPalette"

    property bool darkMode: true
    property real textScale: 1
    property color textColor: darkMode ? "#d0d0d0" : "#42464c"
    property color strongTextColor: darkMode ? "#eeeeee" : "#222324"
    property color mutedColor: darkMode ? "#909191" : "#aeb1b5"
    property color selectionFill: "#3d5a73"
    property color accentColor: "#5584aa"
    property int containerWidth: 720
    property int containerHeight: 520

    readonly property color paperColor: darkMode ? "#22221f" : "#fffef2"
    readonly property color hairlineColor: darkMode ? "#4f525a" : "#d5d56e"
    readonly property color shadowColor: darkMode ? "#00000066" : "#0000001a"

    property string mode: "list"
    property int selectedIndex: 0
    property int tableColumns: 3
    property int tableRows: 3
    property var visibleItems: []
    property var rows: []
    property bool accepted: false

    readonly property int paletteWidth: Math.max(1, Math.round(320 * textScale))
    readonly property int rowHeight: Math.max(1, Math.round(36 * textScale))
    readonly property int sectionHeight: Math.max(1, Math.round(22 * textScale))
    readonly property int filterHeight: Math.max(1, Math.round(44 * textScale))
    readonly property int previewHeight: Math.max(1, Math.round(48 * textScale))
    readonly property int cellSize: Math.max(1, Math.round(18 * textScale))
    readonly property int cellGap: Math.max(1, Math.round(3 * textScale))
    readonly property int maxListHeight: Math.max(rowHeight * 4,
        Math.round(containerHeight * 0.56) - filterHeight - previewHeight - 16)

    readonly property string previewText: {
        if (mode === "table")
            return EditorMutations.previewMarkdown("table", {
                columns: tableColumns,
                rows: tableRows
            });
        if (selectedIndex < 0 || selectedIndex >= visibleItems.length)
            return "";
        return EditorMutations.previewMarkdown(visibleItems[selectedIndex].kind);
    }

    signal triggered(string kind, var options)
    signal cancelled()

    modal: false
    focus: true
    padding: 0
    width: paletteWidth
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    opacity: 1

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: 100
            easing.type: Easing.OutCubic
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: 80
            easing.type: Easing.InCubic
        }
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.margins: -6
            anchors.topMargin: -2
            color: root.shadowColor
            radius: 12
        }
        Rectangle {
            anchors.fill: parent
            color: root.paperColor
            radius: 9
            border.width: 1
            border.color: root.hairlineColor
        }
    }

    contentItem: Column {
        id: paper
        width: root.paletteWidth
        spacing: 0

        Item {
            width: parent.width
            height: root.filterHeight

            TextInput {
                id: filterField
                objectName: "insertPaletteFilter"
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                verticalAlignment: TextInput.AlignVCenter
                visible: root.mode === "list"
                selectByMouse: true
                color: root.strongTextColor
                font.family: "iA Writer Mono S"
                font.pixelSize: Math.round(15 * root.textScale)
                clip: true
                onTextChanged: root.refreshFilter()

                Keys.onPressed: function(event) {
                    root.handleListKeys(event);
                }
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 16
                text: "Insert"
                visible: root.mode === "list" && filterField.text.length === 0
                color: root.mutedColor
                font.family: "iA Writer Mono S"
                font.pixelSize: Math.round(15 * root.textScale)
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 16
                text: "Table size"
                visible: root.mode === "table"
                color: root.strongTextColor
                font.family: "iA Writer Mono S"
                font.pixelSize: Math.round(15 * root.textScale)
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: root.hairlineColor
            opacity: 0.85
        }

        Item {
            width: parent.width
            height: root.mode === "table" ? tablePane.implicitHeight : listPane.height

            ListView {
                id: listPane
                objectName: "insertPaletteList"
                anchors.left: parent.left
                anchors.right: parent.right
                height: Math.min(contentHeight, root.maxListHeight)
                visible: root.mode === "list"
                opacity: root.mode === "list" ? 1 : 0
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.rows
                currentIndex: root.rowIndexForItem(root.selectedIndex)

                Behavior on opacity {
                    NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
                }

                delegate: Item {
                    required property var modelData
                    width: ListView.view.width
                    height: modelData.rowType === "section" ? root.sectionHeight : root.rowHeight

                    Label {
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 2
                        visible: modelData.rowType === "section"
                        text: modelData.title
                        color: root.mutedColor
                        font.family: "iA Writer Mono S"
                        font.pixelSize: Math.round(11 * root.textScale)
                        font.letterSpacing: 0.8
                    }

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        anchors.topMargin: 2
                        anchors.bottomMargin: 2
                        visible: modelData.rowType === "item"
                        radius: 2
                        color: modelData.itemIndex === root.selectedIndex
                               ? root.selectionFill : "transparent"

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 10
                            spacing: 10

                            InsertIcon {
                                anchors.verticalCenter: parent.verticalCenter
                                iconName: modelData.icon || ""
                                iconColor: modelData.itemIndex === root.selectedIndex
                                           ? root.strongTextColor : root.mutedColor
                            }

                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 26 - shortcutLabel.width - 20
                                text: modelData.title || ""
                                color: modelData.itemIndex === root.selectedIndex
                                       ? root.strongTextColor : root.textColor
                                elide: Text.ElideRight
                                font.family: "iA Writer Mono S"
                                font.pixelSize: Math.round(13 * root.textScale)
                            }

                            Label {
                                id: shortcutLabel
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.shortcut || ""
                                color: root.mutedColor
                                font.family: "iA Writer Mono S"
                                font.pixelSize: Math.round(11 * root.textScale)
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: root.selectedIndex = modelData.itemIndex
                            onClicked: root.activateIndex(modelData.itemIndex)
                        }
                    }
                }
            }

            Label {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.top: parent.top
                anchors.topMargin: 12
                visible: root.mode === "list" && root.visibleItems.length === 0
                text: "Nothing matches"
                color: root.mutedColor
                font.family: "iA Writer Mono S"
                font.pixelSize: Math.round(13 * root.textScale)
            }

            Column {
                id: tablePane
                anchors.left: parent.left
                anchors.right: parent.right
                visible: root.mode === "table"
                opacity: root.mode === "table" ? 1 : 0
                topPadding: 14
                bottomPadding: 12
                leftPadding: 16
                rightPadding: 16
                spacing: 10

                Behavior on opacity {
                    NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
                }

                Grid {
                    id: tableGrid
                    columns: 8
                    rows: 6
                    spacing: root.cellGap
                    focus: root.mode === "table"

                    Repeater {
                        model: 48
                        Rectangle {
                            required property int index
                            readonly property int cellColumn: (index % 8) + 1
                            readonly property int cellRow: Math.floor(index / 8) + 1
                            readonly property bool chosen: cellColumn <= root.tableColumns
                                                           && cellRow <= root.tableRows
                            width: root.cellSize
                            height: root.cellSize
                            radius: 1
                            color: chosen ? Qt.rgba(root.accentColor.r, root.accentColor.g,
                                                    root.accentColor.b, 0.28)
                                          : "transparent"
                            border.width: chosen
                                          && (cellColumn === root.tableColumns
                                              || cellRow === root.tableRows
                                              || cellColumn === 1 || cellRow === 1)
                                          ? 1 : 1
                            border.color: chosen ? root.accentColor : root.hairlineColor

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                onEntered: {
                                    root.tableColumns = parent.cellColumn;
                                    root.tableRows = parent.cellRow;
                                }
                                onClicked: root.commitTable()
                            }
                        }
                    }

                    Keys.onPressed: function(event) {
                        root.handleTableKeys(event);
                    }
                }

                Label {
                    text: root.tableColumns + " columns × " + root.tableRows + " rows"
                    color: root.strongTextColor
                    font.family: "iA Writer Mono S"
                    font.pixelSize: Math.round(12 * root.textScale)
                }

                Label {
                    text: "includes header"
                    color: root.mutedColor
                    font.family: "iA Writer Mono S"
                    font.pixelSize: Math.round(11 * root.textScale)
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: root.hairlineColor
            opacity: 0.85
        }

        Item {
            width: parent.width
            height: root.previewHeight

            Label {
                id: previewLabel
                objectName: "insertPalettePreview"
                anchors.fill: parent
                anchors.margins: 12
                text: root.previewText
                color: root.mutedColor
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
                maximumLineCount: 2
                font.family: "iA Writer Mono S"
                font.pixelSize: Math.round(11 * root.textScale)
                lineHeight: 1.35
            }
        }
    }

    onOpened: {
        accepted = false;
        if (mode === "list")
            filterField.forceActiveFocus();
        else
            tableGrid.forceActiveFocus();
        refreshFilter();
    }

    onClosed: {
        mode = "list";
        if (!accepted)
            cancelled();
        accepted = false;
    }

    function refreshFilter() {
        var query = filterField.text.toLocaleLowerCase();
        var catalog = EditorMutations.catalog();
        var items = [];
        for (var i = 0; i < catalog.length; i++) {
            var item = catalog[i];
            if (query.length > 0) {
                var hay = (item.title + " " + item.aliases + " " + item.kind).toLocaleLowerCase();
                if (hay.indexOf(query) < 0)
                    continue;
            }
            items.push(item);
        }
        visibleItems = items;
        rows = buildRows(items);
        if (selectedIndex >= items.length)
            selectedIndex = Math.max(0, items.length - 1);
        if (items.length > 0 && selectedIndex < 0)
            selectedIndex = 0;
        positionSelected();
    }

    function buildRows(items) {
        var next = [];
        var lastSection = "";
        for (var i = 0; i < items.length; i++) {
            if (items[i].section !== lastSection) {
                lastSection = items[i].section;
                next.push({
                    rowType: "section",
                    title: lastSection,
                    itemIndex: -1,
                    icon: "",
                    shortcut: ""
                });
            }
            next.push({
                rowType: "item",
                title: items[i].title,
                kind: items[i].kind,
                shortcut: items[i].shortcut,
                icon: items[i].icon,
                itemIndex: i
            });
        }
        if (items.length === 0)
            next.push({ rowType: "empty", title: "", itemIndex: -1, icon: "", shortcut: "" });
        return next;
    }

    function rowIndexForItem(itemIndex) {
        for (var i = 0; i < rows.length; i++) {
            if (rows[i].itemIndex === itemIndex)
                return i;
        }
        return 0;
    }

    function positionSelected() {
        var row = rowIndexForItem(selectedIndex);
        if (row >= 0)
            listPane.positionViewAtIndex(row, ListView.Contain);
    }

    function moveSelection(delta) {
        if (visibleItems.length === 0)
            return;
        selectedIndex = (selectedIndex + delta + visibleItems.length) % visibleItems.length;
        positionSelected();
    }

    function enterTableMode() {
        mode = "table";
        tableColumns = 3;
        tableRows = 3;
        tableGrid.forceActiveFocus();
    }

    function leaveTableMode() {
        mode = "list";
        filterField.forceActiveFocus();
    }

    function commit(kind, options) {
        accepted = true;
        close();
        triggered(kind, options || {});
    }

    function commitTable() {
        commit("table", { columns: tableColumns, rows: tableRows });
    }

    function activateIndex(index) {
        if (index < 0 || index >= visibleItems.length)
            return;
        var item = visibleItems[index];
        if (item.kind === "table") {
            enterTableMode();
            return;
        }
        commit(item.kind, {});
    }

    function activateKind(kind, options) {
        options = options || {};
        if (kind === "table" && options.columns === undefined && options.rows === undefined) {
            if (!opened)
                open();
            enterTableMode();
            return;
        }
        commit(kind, options);
    }

    function activateSelected() {
        if (mode === "table") {
            commitTable();
            return;
        }
        activateIndex(selectedIndex);
    }

    function handleListKeys(event) {
        var controlHeld = event.modifiers & Qt.ControlModifier;
        if (event.key === Qt.Key_Down || (controlHeld && event.key === Qt.Key_N)) {
            moveSelection(1);
            event.accepted = true;
        } else if (event.key === Qt.Key_Up || (controlHeld && event.key === Qt.Key_P)) {
            moveSelection(-1);
            event.accepted = true;
        } else if (event.key === Qt.Key_Right
                   && selectedIndex >= 0 && selectedIndex < visibleItems.length
                   && visibleItems[selectedIndex].kind === "table") {
            enterTableMode();
            event.accepted = true;
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            activateSelected();
            event.accepted = true;
        } else if (event.key === Qt.Key_Escape) {
            accepted = false;
            close();
            event.accepted = true;
        }
    }

    function handleTableKeys(event) {
        if (event.key === Qt.Key_Right) {
            tableColumns = Math.min(8, tableColumns + 1);
            event.accepted = true;
        } else if (event.key === Qt.Key_Left) {
            tableColumns = Math.max(1, tableColumns - 1);
            event.accepted = true;
        } else if (event.key === Qt.Key_Down) {
            tableRows = Math.min(6, tableRows + 1);
            event.accepted = true;
        } else if (event.key === Qt.Key_Up) {
            tableRows = Math.max(1, tableRows - 1);
            event.accepted = true;
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            commitTable();
            event.accepted = true;
        } else if (event.key === Qt.Key_Escape) {
            leaveTableMode();
            event.accepted = true;
        }
    }

    function openAt(caretX, caretTop, caretBottom) {
        mode = "list";
        filterField.text = "";
        selectedIndex = 0;
        tableColumns = 3;
        tableRows = 3;
        refreshFilter();

        var paletteHeight = filterHeight + previewHeight + 2
            + Math.min(listContentHeight(), maxListHeight);
        var x = caretX;
        var y = caretBottom + 6;
        if (x + paletteWidth > containerWidth - 12)
            x = Math.max(12, containerWidth - paletteWidth - 12);
        if (x < 12)
            x = 12;
        if (y + paletteHeight > containerHeight - 12)
            y = caretTop - paletteHeight - 6;
        if (y < 12)
            y = 12;

        root.x = x;
        root.y = y;
        open();
    }

    function listContentHeight() {
        var height = 0;
        for (var i = 0; i < rows.length; i++)
            height += rows[i].rowType === "section" ? sectionHeight : rowHeight;
        if (visibleItems.length === 0)
            height = rowHeight;
        return height;
    }
}
