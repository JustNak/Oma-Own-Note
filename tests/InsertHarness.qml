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
    property string dividerMidText
    property int dividerMidCursor
    property string emptyCellTypedLine
    property bool confinedOutsideResult
    property string confinedOutsideLine
    property bool tableReturnKeptRows
    property bool arrowKeptRagged
    property int skipPipePosition

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

        // Regression: Tab into an empty cell must leave the caret at the cell's
        // left column so typed text is left-aligned, not jammed against the
        // closing pipe (which only self-corrected on the next Tab).
        text = EditorMutations.pipeTable(2, 2);
        cursorPosition = 2;
        EditorMutations.moveTableCell(this, 1);
        insert(cursorPosition, "x");
        emptyCellTypedLine = text.split("\n")[0];

        text = "";
        cursorPosition = 0;
        EditorMutations.applyInsert(this, "divider");
        dividerText = text;
        dividerCursor = cursorPosition;

        text = "hello\nworld";
        cursorPosition = 4;
        EditorMutations.applyInsert(this, "divider");
        dividerMidText = text;
        dividerMidCursor = cursorPosition;

        escapedImage = EditorMutations.imageMarkdown("Cat [1]", "photo (2).png");

        text = EditorMutations.pipeTable(1, 2);
        cursorPosition = 2;
        oneColumnMoved = EditorMutations.moveTableCell(this, 1);
        oneColumnCursor = cursorPosition;
        if (selectionStart !== selectionEnd)
            oneColumnCursor = selectionStart;

        text = "| a | b |\n| --- | --- |\n|     |     |";
        cursorPosition = text.indexOf("\n") - 1;
        confinedOutsideResult = EditorMutations.confineTableCaret(this);
        confinedOutsideLine = text.split("\n")[0];
        insert(cursorPosition, "x");
        confinedOutsideLine = text.split("\n")[0];

        text = EditorMutations.pipeTable(2, 2);
        cursorPosition = text.length - 2;
        EditorMutations.confineTableCaret(this);
        var beforeReturn = text.split("\n").length;
        tableReturnKeptRows = EditorMutations.tableReturn(this)
            && text.split("\n").every(function(line) {
                   return line.indexOf("|") === 0 && line.lastIndexOf("|") > 0;
               })
            && text.split("\n").length >= beforeReturn;

        text = "| a | b |\n| - | --- |\n| longcell | x |";
        var ragged = text;
        cursorPosition = 2;
        EditorMutations.moveInsideTable(this, 1);
        arrowKeptRagged = text === ragged;

        text = "| a | b |\n| --- | --- |\n|     |     |";
        cursorPosition = 2;
        skipPipePosition = EditorMutations.nextInsideTablePosition(text, 4, 1);
    }
}
