import QtQuick
import "../src/EditorMutations.js" as EditorMutations

TextEdit {
    property string headingText
    property int headingCursor
    property string selectedHeading
    property string midLineHeading
    property string tableText
    property int tableCursor
    property string convertedText
    property string fenceText
    property int fenceCursor
    property string imageText
    property int imageSelStart
    property int imageSelEnd
    property string dateText
    property string relativeImage
    property bool movedInTable
    property int nextCellStart
    property int nextCellEnd
    property string pipeTableSample
    property string dividerText
    property int dividerCursor
    property string escapedImage
    property bool oneColumnMoved
    property int oneColumnCursor

    Component.onCompleted: {
        text = "";
        cursorPosition = 0;
        EditorMutations.applyInsert(this, "heading1");
        headingText = text;
        headingCursor = cursorPosition;

        text = "hello";
        select(0, 5);
        EditorMutations.applyInsert(this, "heading1");
        selectedHeading = text;

        text = "hello";
        cursorPosition = 5;
        EditorMutations.applyInsert(this, "heading1");
        midLineHeading = text;

        text = "";
        cursorPosition = 0;
        EditorMutations.applyInsert(this, "table", { columns: 3, rows: 2 });
        tableText = text;
        tableCursor = cursorPosition;

        text = "a\tb\n1\t2";
        select(0, text.length);
        EditorMutations.applyInsert(this, "table");
        convertedText = text;

        text = "";
        cursorPosition = 0;
        EditorMutations.applyInsert(this, "fence");
        fenceText = text;
        fenceCursor = cursorPosition;

        text = "";
        cursorPosition = 0;
        EditorMutations.applyInsert(this, "image", { alt: "Cat", path: "cat.png" });
        imageText = text;
        imageSelStart = selectionStart;
        imageSelEnd = selectionEnd;

        text = "";
        cursorPosition = 0;
        EditorMutations.applyInsert(this, "date", { date: "2026-08-31" });
        dateText = text;

        relativeImage = EditorMutations.relativeImagePath(
            "file:///home/writer/note.md",
            "file:///home/writer/img/cat.png");
        pipeTableSample = EditorMutations.pipeTable(3, 2);

        text = EditorMutations.pipeTable(2, 2);
        cursorPosition = 2;
        movedInTable = EditorMutations.moveTableCell(this, 1);
        nextCellStart = selectionStart;
        nextCellEnd = selectionEnd;
        if (selectionStart === selectionEnd)
            nextCellStart = nextCellEnd = cursorPosition;

        text = "";
        cursorPosition = 0;
        EditorMutations.applyInsert(this, "divider");
        dividerText = text;
        dividerCursor = cursorPosition;

        escapedImage = EditorMutations.imageMarkdown("Cat [1]", "photo (2).png");

        text = EditorMutations.pipeTable(1, 2);
        cursorPosition = 2;
        oneColumnMoved = EditorMutations.moveTableCell(this, 1);
        oneColumnCursor = cursorPosition;
        if (selectionStart !== selectionEnd)
            oneColumnCursor = selectionStart;
    }
}
