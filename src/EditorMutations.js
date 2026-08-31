.pragma library

function normalizePlainText(text) {
    return text.replace(/\r\n/g, "\n").replace(/\r/g, "\n");
}

function replaceRange(editor, rangeStart, rangeEnd, replacement,
                      selectionStartOffset, selectionEndOffset) {
    var start = Math.max(0, Math.min(editor.text.length, rangeStart));
    var end = Math.max(start, Math.min(editor.text.length, rangeEnd));
    var insertedText = normalizePlainText(replacement);

    if (start !== end)
        editor.remove(start, end);

    editor.cursorPosition = start;
    editor.insert(start, insertedText);

    // TextEdit.insert() already leaves the caret after the inserted text. Only
    // move it again when the caller deliberately requests a selection/caret
    // within the replacement.
    if (selectionStartOffset !== undefined && selectionEndOffset !== undefined) {
        var insertedEnd = editor.cursorPosition;
        var selectionStart = Math.max(start,
                                      Math.min(insertedEnd, start + selectionStartOffset));
        var selectionEnd = Math.max(start,
                                    Math.min(insertedEnd, start + selectionEndOffset));
        if (selectionStart === selectionEnd)
            editor.cursorPosition = selectionStart;
        else
            editor.select(selectionStart, selectionEnd);
    }

    return insertedText;
}

function catalog() {
    return [
        { kind: "heading1", section: "Structure", title: "Heading 1",
          aliases: "h1 heading title", shortcut: "", icon: "heading" },
        { kind: "heading2", section: "Structure", title: "Heading 2",
          aliases: "h2 heading", shortcut: "", icon: "heading" },
        { kind: "heading3", section: "Structure", title: "Heading 3",
          aliases: "h3 heading", shortcut: "", icon: "heading" },
        { kind: "quote", section: "Structure", title: "Quote",
          aliases: "blockquote cite", shortcut: "", icon: "quote" },
        { kind: "divider", section: "Structure", title: "Divider",
          aliases: "hr rule horizontal", shortcut: "", icon: "divider" },
        { kind: "bullet", section: "Lists", title: "Bullet list",
          aliases: "ul unordered list", shortcut: "", icon: "bullet" },
        { kind: "numbered", section: "Lists", title: "Numbered list",
          aliases: "ol ordered list", shortcut: "", icon: "numbered" },
        { kind: "task", section: "Lists", title: "Task list",
          aliases: "todo checkbox", shortcut: "", icon: "task" },
        { kind: "inlineCode", section: "Code", title: "Inline code",
          aliases: "code backtick", shortcut: "", icon: "inlineCode" },
        { kind: "fence", section: "Code", title: "Code block",
          aliases: "fence pre snippet", shortcut: "", icon: "fence" },
        { kind: "table", section: "Insert", title: "Table",
          aliases: "grid", shortcut: "", icon: "table" },
        { kind: "image", section: "Insert", title: "Image",
          aliases: "img picture photo", shortcut: "", icon: "image" },
        { kind: "link", section: "Insert", title: "Link",
          aliases: "url href", shortcut: "Ctrl+K", icon: "link" },
        { kind: "date", section: "Insert", title: "Today",
          aliases: "date day iso", shortcut: "", icon: "date" },
        { kind: "bold", section: "Marks", title: "Bold",
          aliases: "strong", shortcut: "Ctrl+B", icon: "bold" },
        { kind: "italic", section: "Marks", title: "Italic",
          aliases: "emphasis em", shortcut: "Ctrl+I", icon: "italic" }
    ];
}

function headingPrefix(level) {
    var hashes = "";
    var count = Math.max(1, Math.min(6, level));
    for (var i = 0; i < count; i++)
        hashes += "#";
    return hashes + " ";
}

function listPrefix(kind, index) {
    if (kind === "numbered")
        return (index + 1) + ". ";
    if (kind === "task")
        return "- [ ] ";
    return "- ";
}

function quotePrefix() {
    return "> ";
}

function horizontalRule() {
    return "---";
}

function inlineCode(text) {
    return "`" + (text || "") + "`";
}

function fencedBlock(body) {
    var content = body === undefined || body === null ? "" : String(body);
    content = normalizePlainText(content);
    if (content.length > 0 && content.charAt(content.length - 1) !== "\n")
        content += "\n";
    if (content.length === 0)
        content = "\n";
    return "```\n" + content + "```";
}

function taskItem(text) {
    return "- [ ] " + (text || "");
}

function pipeRow(cells) {
    return "| " + cells.join(" | ") + " |";
}

function emptyCells(count) {
    var cells = [];
    for (var i = 0; i < count; i++)
        cells.push("");
    return cells;
}

function pipeTable(columns, rows) {
    var cols = Math.max(1, columns);
    var totalRows = Math.max(1, rows);
    var separator = [];
    for (var i = 0; i < cols; i++)
        separator.push("---");
    var lines = [pipeRow(emptyCells(cols)), pipeRow(separator)];
    for (var r = 1; r < totalRows; r++)
        lines.push(pipeRow(emptyCells(cols)));
    return lines.join("\n");
}

function splitCsvLine(line) {
    return line.split(",").map(function(cell) { return cell.trim(); });
}

function splitTableLine(line) {
    if (line.indexOf("\t") !== -1)
        return line.split("\t").map(function(cell) { return cell.trim(); });
    var trimmed = line.trim();
    if (trimmed.indexOf("|") !== -1) {
        var cells = trimmed.split("|").map(function(cell) { return cell.trim(); });
        if (cells.length && cells[0] === "")
            cells.shift();
        if (cells.length && cells[cells.length - 1] === "")
            cells.pop();
        return cells;
    }
    if (line.indexOf(",") !== -1)
        return splitCsvLine(line);
    return [line.trim()];
}

function lineHasTableDelimiter(line) {
    return line.indexOf("\t") !== -1 || line.indexOf("|") !== -1 || line.indexOf(",") !== -1;
}

function tableFromSelection(selected) {
    var block = normalizePlainText(selected).replace(/\n+$/, "");
    if (block.length === 0)
        return pipeTable(3, 3);

    var lines = block.split("\n");
    var rows = [];
    var columns = 1;
    var delimited = lines.some(lineHasTableDelimiter);
    for (var i = 0; i < lines.length; i++) {
        var cells = delimited ? splitTableLine(lines[i]) : [lines[i].trim()];
        if (cells.length === 0)
            cells = [""];
        columns = Math.max(columns, cells.length);
        rows.push(cells);
    }
    for (var r = 0; r < rows.length; r++) {
        while (rows[r].length < columns)
            rows[r].push("");
    }

    var separator = [];
    for (var c = 0; c < columns; c++)
        separator.push("---");
    var out = [pipeRow(rows[0]), pipeRow(separator)];
    for (var body = 1; body < rows.length; body++)
        out.push(pipeRow(rows[body]));
    return out.join("\n");
}

function escapeMarkdownLinkText(linkText) {
    return String(linkText || "").replace(/\\/g, "\\\\")
                                 .replace(/\[/g, "\\[")
                                 .replace(/\]/g, "\\]");
}

function escapeMarkdownLinkDestination(linkUrl) {
    return String(linkUrl || "").replace(/\\/g, "\\\\")
                                .replace(/\(/g, "\\(")
                                .replace(/\)/g, "\\)");
}

function imageMarkdown(alt, path) {
    var dest = String(path || "");
    if (/[\s<>]/.test(dest))
        dest = "<" + dest.replace(/\\/g, "\\\\").replace(/>/g, "\\>") + ">";
    else
        dest = escapeMarkdownLinkDestination(dest);
    return "![" + escapeMarkdownLinkText(alt) + "](" + dest + ")";
}

function imageAltFromPath(path) {
    var name = String(path || "");
    var slash = Math.max(name.lastIndexOf("/"), name.lastIndexOf("\\"));
    name = slash >= 0 ? name.slice(slash + 1) : name;
    var dot = name.lastIndexOf(".");
    if (dot > 0)
        name = name.slice(0, dot);
    return name;
}

function localFilePath(url) {
    if (!url && url !== "")
        return "";
    var s = String(url);
    if (s.indexOf("file:") === 0) {
        var path = s.replace(/^file:\/\//, "");
        if (path.charAt(0) !== "/" && path.charAt(1) !== ":")
            path = "/" + path;
        try {
            path = decodeURIComponent(path);
        } catch (error) {
        }
        return path;
    }
    return s;
}

function relativePath(fromDir, toFile) {
    if (!fromDir)
        return toFile;
    var fromParts = fromDir.split("/").filter(function(part) { return part.length > 0; });
    var toParts = toFile.split("/").filter(function(part) { return part.length > 0; });
    var i = 0;
    while (i < fromParts.length && i < toParts.length && fromParts[i] === toParts[i])
        i++;
    var ups = [];
    for (var u = 0; u < fromParts.length - i; u++)
        ups.push("..");
    var rel = ups.concat(toParts.slice(i)).join("/");
    if (rel.length === 0) {
        var slash = toFile.lastIndexOf("/");
        return slash >= 0 ? toFile.slice(slash + 1) : toFile;
    }
    return rel;
}

function relativeImagePath(documentUrl, imageUrl) {
    var imagePath = localFilePath(imageUrl);
    if (!imagePath)
        return String(imageUrl || "");
    var docPath = localFilePath(documentUrl);
    if (!docPath)
        return imagePath;
    var slash = docPath.lastIndexOf("/");
    var fromDir = slash >= 0 ? docPath.slice(0, slash) : "";
    return relativePath(fromDir, imagePath);
}

function isoDate(now) {
    var date = now || new Date();
    var year = date.getFullYear();
    var month = date.getMonth() + 1;
    var day = date.getDate();
    return year + "-" + (month < 10 ? "0" : "") + month + "-"
           + (day < 10 ? "0" : "") + day;
}

function previewMarkdown(kind, options) {
    options = options || {};
    switch (kind) {
    case "heading1":
        return "# Heading";
    case "heading2":
        return "## Heading";
    case "heading3":
        return "### Heading";
    case "quote":
        return "> Quote";
    case "divider":
        return "---";
    case "bullet":
        return "- List item";
    case "numbered":
        return "1. List item";
    case "task":
        return "- [ ] Task";
    case "inlineCode":
        return "`code`";
    case "fence":
        return "```\n\n```";
    case "table":
        return pipeTable(options.columns || 3, options.rows || 3).split("\n").slice(0, 2).join("\n");
    case "image":
        return "![alt](path)";
    case "link":
        return "[link text](https://)";
    case "date":
        return isoDate();
    case "bold":
        return "**bold**";
    case "italic":
        return "*italic*";
    default:
        return "";
    }
}

function isInsideFence(text, position) {
    var before = text.slice(0, position);
    var fences = (before.match(/^\s*```/gm) || []).length;
    return (fences % 2) === 1;
}

function lineBounds(text, position) {
    var start = text.lastIndexOf("\n", position - 1) + 1;
    var end = text.indexOf("\n", position);
    if (end < 0)
        end = text.length;
    return { start: start, end: end, line: text.slice(start, end) };
}

function expandToWholeLines(text, start, end) {
    var from = text.lastIndexOf("\n", start - 1) + 1;
    var to;
    if (end > start && text.charAt(end - 1) === "\n")
        to = end - 1;
    else {
        to = text.indexOf("\n", end);
        if (to < 0)
            to = text.length;
    }
    return { start: from, end: to };
}

function isolateBlock(text, start, end, snippet) {
    var prefix = "";
    if (start > 0) {
        if (text.charAt(start - 1) !== "\n")
            prefix = "\n\n";
        else if (start > 1 && text.charAt(start - 2) !== "\n")
            prefix = "\n";
    }
    var suffix = "";
    if (end < text.length) {
        if (text.charAt(end) !== "\n")
            suffix = "\n\n";
        else if (end + 1 < text.length && text.charAt(end + 1) !== "\n")
            suffix = "\n";
    }
    return { text: prefix + snippet + suffix, shift: prefix.length };
}

function stripHeading(line) {
    return line.replace(/^\s{0,3}#{1,6}\s+/, "");
}

function stripListMarker(line) {
    return line.replace(/^\s*(?:[-+*]|\d+[.)])\s+(?:\[[ xX]\]\s+)?/, "");
}

function applyLinePrefix(line, prefix, kind) {
    if (kind.indexOf("heading") === 0)
        return prefix + stripHeading(line);
    if (kind === "quote") {
        if (/^\s*>/.test(line))
            return line;
        return prefix + line.replace(/^\s+/, "");
    }
    if (kind === "bullet" || kind === "numbered" || kind === "task")
        return prefix + stripListMarker(line);
    return prefix + line;
}

function prefixSelectedLines(editor, kind) {
    var text = editor.text;
    var rawStart = Math.min(editor.selectionStart, editor.selectionEnd);
    var rawEnd = Math.max(editor.selectionStart, editor.selectionEnd);
    var range = expandToWholeLines(text, rawStart, rawEnd);
    var block = text.slice(range.start, range.end);
    var lines = block.split("\n");
    var prefixed = [];
    var firstHole = 0;
    for (var i = 0; i < lines.length; i++) {
        var prefix = kind.indexOf("heading") === 0
            ? headingPrefix(kind === "heading1" ? 1 : kind === "heading2" ? 2 : 3)
            : kind === "quote" ? quotePrefix()
            : listPrefix(kind, i);
        var next = applyLinePrefix(lines[i], prefix, kind);
        if (i === 0)
            firstHole = prefix.length;
        prefixed.push(next);
    }
    var replacement = prefixed.join("\n");
    var holeStart = firstHole;
    var holeEnd = prefixed[0].length;
    replaceRange(editor, range.start, range.end, replacement, holeStart, holeEnd);
}

function insertIsolated(editor, snippet, holeStart, holeEnd) {
    var text = editor.text;
    var start = Math.min(editor.selectionStart, editor.selectionEnd);
    var end = Math.max(editor.selectionStart, editor.selectionEnd);
    if (start === end) {
        var bounds = lineBounds(text, editor.cursorPosition);
        if (bounds.line.trim().length === 0) {
            start = bounds.start;
            end = bounds.end;
        } else {
            start = bounds.end;
            end = bounds.end;
        }
    }
    var wrapped = isolateBlock(text, start, end, snippet);
    replaceRange(editor, start, end, wrapped.text,
                 wrapped.shift + holeStart, wrapped.shift + holeEnd);
}

function wrapInline(editor, before, after, placeholder) {
    var start = Math.min(editor.selectionStart, editor.selectionEnd);
    var end = Math.max(editor.selectionStart, editor.selectionEnd);
    var selected = editor.text.slice(start, end);
    var inner = selected.length > 0 ? selected : (placeholder || "");
    replaceRange(editor, start, end, before + inner + after,
                 before.length, before.length + inner.length);
}

function applyInsert(editor, kind, options) {
    options = options || {};
    var text = editor.text;
    var start = Math.min(editor.selectionStart, editor.selectionEnd);
    var end = Math.max(editor.selectionStart, editor.selectionEnd);
    var selected = text.slice(start, end);
    var hasSelection = start !== end;

    if (kind === "bold") {
        wrapInline(editor, "**", "**", "");
        return;
    }
    if (kind === "italic") {
        wrapInline(editor, "*", "*", "");
        return;
    }
    if (kind === "inlineCode") {
        wrapInline(editor, "`", "`", selected.length ? "" : "code");
        return;
    }
    if (kind === "date") {
        var stamp = options.date || isoDate();
        replaceRange(editor, start, end, stamp);
        return;
    }
    if (kind === "image") {
        var alt = options.alt || "alt";
        var markdown = imageMarkdown(alt, options.path || "");
        insertIsolated(editor, markdown, 2, 2 + escapeMarkdownLinkText(alt).length);
        return;
    }
    if (kind === "link")
        return;

    if (kind === "fence") {
        if (isInsideFence(text, start))
            return;
        var fence = fencedBlock(hasSelection ? selected : "");
        if (hasSelection) {
            var wrappedFence = isolateBlock(text, start, end, fence);
            replaceRange(editor, start, end, wrappedFence.text,
                         wrappedFence.shift + 3, wrappedFence.shift + 3);
        } else {
            insertIsolated(editor, fence, 3, 3);
        }
        return;
    }

    if (kind === "divider") {
        insertIsolated(editor, horizontalRule() + "\n", 4, 4);
        return;
    }

    if (kind === "table") {
        var table = hasSelection
            ? tableFromSelection(selected)
            : pipeTable(options.columns || 3, options.rows || 3);
        if (hasSelection) {
            var wrappedTable = isolateBlock(text, start, end, table);
            replaceRange(editor, start, end, wrappedTable.text,
                         wrappedTable.shift + 2, wrappedTable.shift + 2);
        } else {
            insertIsolated(editor, table, 2, 2);
        }
        return;
    }

    if (hasSelection) {
        prefixSelectedLines(editor, kind);
        return;
    }

    var prefix = kind.indexOf("heading") === 0
        ? headingPrefix(kind === "heading1" ? 1 : kind === "heading2" ? 2 : 3)
        : kind === "quote" ? quotePrefix()
        : listPrefix(kind, 0);
    insertIsolated(editor, prefix, prefix.length, prefix.length);
}

function isTableRow(line) {
    var trimmed = line.trim();
    if (trimmed.length === 0 || trimmed.charAt(0) !== "|")
        return false;
    return trimmed.indexOf("|", 1) !== -1;
}

function isSeparatorRow(line) {
    var trimmed = line.trim();
    if (!isTableRow(trimmed))
        return false;
    return /^\s*\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)*\|?\s*$/.test(line);
}

function parseTableRow(line) {
    if (!isTableRow(line))
        return null;
    var cells = [];
    var i = 0;
    while (i < line.length && (line.charAt(i) === " " || line.charAt(i) === "\t"))
        i++;
    if (i < line.length && line.charAt(i) === "|")
        i++;
    var cellStart = i;
    while (i <= line.length) {
        if (i === line.length || line.charAt(i) === "|") {
            cells.push({ start: cellStart, end: i });
            if (i === line.length)
                break;
            i++;
            cellStart = i;
        } else {
            i++;
        }
    }
    if (cells.length && cells[cells.length - 1].start === cells[cells.length - 1].end
            && line.trim().charAt(line.trim().length - 1) === "|")
        cells.pop();
    return { cells: cells };
}

function cellContent(line, cell) {
    var start = cell.start;
    var end = cell.end;
    while (start < end && (line.charAt(start) === " " || line.charAt(start) === "\t"))
        start++;
    while (end > start && (line.charAt(end - 1) === " " || line.charAt(end - 1) === "\t"))
        end--;
    return { start: start, end: end };
}

function tableCellAt(text, position) {
    var bounds = lineBounds(text, position);
    if (!isTableRow(bounds.line))
        return null;
    var parsed = parseTableRow(bounds.line);
    if (!parsed || parsed.cells.length === 0)
        return null;
    var column = position - bounds.start;
    var index = parsed.cells.length - 1;
    for (var i = 0; i < parsed.cells.length; i++) {
        if (column <= parsed.cells[i].end) {
            index = i;
            break;
        }
    }
    var content = cellContent(bounds.line, parsed.cells[index]);
    return {
        lineStart: bounds.start,
        lineEnd: bounds.end,
        line: bounds.line,
        cells: parsed.cells,
        cellIndex: index,
        contentStart: content.start,
        contentEnd: content.end
    };
}

function tableCellRef(lineStart, line, cell, cellIndex) {
    var content = cellContent(line, cell);
    return {
        lineStart: lineStart,
        line: line,
        cellIndex: cellIndex,
        contentStart: content.start,
        contentEnd: content.end
    };
}

function walkTableRow(text, fromPosition, direction) {
    var position = fromPosition;
    while (position >= 0 && position <= text.length) {
        var bounds = lineBounds(text, Math.min(position, text.length));
        if (!isTableRow(bounds.line) && !isSeparatorRow(bounds.line))
            return null;
        if (isTableRow(bounds.line) && !isSeparatorRow(bounds.line))
            return bounds;
        if (direction > 0) {
            if (bounds.end >= text.length)
                return null;
            position = bounds.end + 1;
        } else {
            if (bounds.start === 0)
                return null;
            position = bounds.start - 1;
        }
    }
    return null;
}

function moveTableCell(editor, direction) {
    var text = editor.text;
    var position = editor.cursorPosition;
    var info = tableCellAt(text, position);
    if (!info)
        return false;

    var next = null;
    if (direction > 0) {
        if (info.cellIndex + 1 < info.cells.length) {
            next = tableCellRef(info.lineStart, info.line,
                                info.cells[info.cellIndex + 1], info.cellIndex + 1);
        } else {
            var after = info.lineEnd < text.length ? info.lineEnd + 1 : -1;
            var nextLine = after >= 0 ? walkTableRow(text, after, 1) : null;
            if (nextLine) {
                var parsedNext = parseTableRow(nextLine.line);
                if (parsedNext && parsedNext.cells.length)
                    next = tableCellRef(nextLine.start, nextLine.line, parsedNext.cells[0], 0);
            }
        }
    } else if (info.cellIndex > 0) {
        next = tableCellRef(info.lineStart, info.line,
                            info.cells[info.cellIndex - 1], info.cellIndex - 1);
    } else {
        var previous = info.lineStart > 0
            ? walkTableRow(text, info.lineStart - 1, -1) : null;
        if (previous) {
            var parsedPrev = parseTableRow(previous.line);
            if (parsedPrev && parsedPrev.cells.length)
                next = tableCellRef(previous.start, previous.line,
                                    parsedPrev.cells[parsedPrev.cells.length - 1],
                                    parsedPrev.cells.length - 1);
        }
    }

    if (!next)
        return true;

    var caretStart = next.lineStart + next.contentStart;
    var caretEnd = next.lineStart + next.contentEnd;
    if (caretStart === caretEnd)
        editor.cursorPosition = caretStart;
    else
        editor.select(caretStart, caretEnd);
    return true;
}
