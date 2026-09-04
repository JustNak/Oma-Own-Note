#include "tablegeometry.h"

#include "markdownhighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QHash>
#include <QObject>
#include <QSizeF>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>

#include <limits>

namespace {

qreal effectiveWrapWidth(QTextDocument *document, qreal wrapWidth) {
    if (wrapWidth > 1)
        return wrapWidth;
    if (!document)
        return 10000;
    const qreal textWidth = document->textWidth();
    if (textWidth > 1 && textWidth < 1e6)
        return textWidth;
    return 10000;
}

void typingSpan(const QString &line, const MarkdownHighlighter::Span &cell,
                int *typingStart, int *typingEnd) {
    int left = cell.start;
    const int right = cell.start + cell.length;
    if (left < right && (line.at(left) == QLatin1Char(' ')
                         || line.at(left) == QLatin1Char('\t')))
        ++left;
    *typingStart = left;
    *typingEnd = right;
}

void contentSpan(const QString &line, const MarkdownHighlighter::Span &cell,
                 int *contentStart, int *contentEnd, QString *text) {
    int start = cell.start;
    int end = cell.start + cell.length;
    while (start < end && (line.at(start) == QLatin1Char(' ')
                           || line.at(start) == QLatin1Char('\t')))
        ++start;
    while (end > start && (line.at(end - 1) == QLatin1Char(' ')
                           || line.at(end - 1) == QLatin1Char('\t')))
        --end;
    if (start >= end) {
        int left = cell.start;
        if (left < cell.start + cell.length
                && (line.at(left) == QLatin1Char(' ')
                    || line.at(left) == QLatin1Char('\t')))
            ++left;
        *contentStart = left;
        *contentEnd = left;
        *text = QString();
        return;
    }
    *contentStart = start;
    *contentEnd = end;
    *text = line.mid(start, end - start);
}

QString visibleCellText(const QString &line, int contentStart, int contentEnd,
                        int typingStart, int typingEnd,
                        int blockPosition, int cursorPosition) {
    const int visibleEnd = TableGeometry::visibleEndForCursor(
        line, contentEnd, typingStart, typingEnd, blockPosition, cursorPosition);
    if (visibleEnd <= contentStart)
        return {};
    return line.mid(contentStart, visibleEnd - contentStart);
}

struct CellSpan {
    int contentStart = 0;
    int contentEnd = 0;
    int typingStart = 0;
    int typingEnd = 0;
    QString text;
};

CellSpan cellSpanFor(const QString &line, const MarkdownHighlighter::Span &cell,
                     int blockPosition, int cursorPosition) {
    CellSpan span;
    typingSpan(line, cell, &span.typingStart, &span.typingEnd);
    contentSpan(line, cell, &span.contentStart, &span.contentEnd, &span.text);
    span.text = visibleCellText(line, span.contentStart, span.contentEnd,
                                span.typingStart, span.typingEnd,
                                blockPosition, cursorPosition);
    return span;
}

QVector<qreal> shrinkInners(QVector<qreal> inners, qreal minInner, qreal extra) {
    // Overflow shrinks every currently-widest column together down to the next
    // tier (or minInner). Dumping the whole extra onto one column crushed it to
    // a few characters while a slightly shorter sibling kept the row.
    while (extra > 0.5) {
        qreal widestWidth = minInner;
        for (qreal inner : inners)
            widestWidth = qMax(widestWidth, inner);
        if (widestWidth <= minInner + 0.5)
            break;

        QVector<int> widest;
        qreal nextWidth = minInner;
        for (int i = 0; i < inners.size(); ++i) {
            if (inners.at(i) > widestWidth - 0.5) {
                widest.append(i);
                continue;
            }
            nextWidth = qMax(nextWidth, inners.at(i));
        }
        if (widest.isEmpty())
            break;

        const qreal roomEach = widestWidth - nextWidth;
        if (roomEach <= 0.5)
            break;
        const qreal takeEach = qMin(extra / widest.size(), roomEach);
        for (int index : widest)
            inners[index] -= takeEach;
        extra -= takeEach * widest.size();
    }
    return inners;
}

struct ParsedTable {
    QVector<QTextBlock> dataRows;
    QTextBlock header;
    int columns = 0;
    QVector<QVector<MarkdownHighlighter::Span>> rowCells;
    QVector<QString> rowTexts;
    QVector<QVector<CellSpan>> rowSpans;
    QVector<qreal> inners;
};

struct MeasureContext {
    QFont font;
    QFont headerFont;
    qreal minRow = 0;
    qreal pipeAdvance = 1;
    qreal spaceAdvance = 1;
    qreal minInner = 1;
    qreal cap = 10000;
    qreal margin = 0;
};

MeasureContext measureContext(QTextDocument *document, qreal wrapWidth) {
    MeasureContext ctx;
    ctx.font = document->defaultFont();
    ctx.headerFont = ctx.font;
    ctx.headerFont.setBold(true);
    const QFontMetricsF metrics(ctx.font);
    ctx.minRow = MarkdownHighlighter::tableDataRowLineHeight(ctx.font);
    ctx.pipeAdvance = qMax(qreal(1), metrics.horizontalAdvance(QLatin1Char('|')));
    ctx.spaceAdvance = qMax(qreal(1), metrics.horizontalAdvance(QLatin1Char(' ')));
    ctx.minInner = qMax(ctx.spaceAdvance * 3, metrics.averageCharWidth() * 3);
    ctx.cap = effectiveWrapWidth(document, wrapWidth);
    ctx.margin = document->documentMargin();
    return ctx;
}

const QFont &rowFontFor(const ParsedTable &parsed, int row, const MeasureContext &ctx) {
    if (parsed.header.isValid()
            && parsed.dataRows.at(row).position() == parsed.header.position())
        return ctx.headerFont;
    return ctx.font;
}

void finishParsedInners(ParsedTable *parsed, const MeasureContext &ctx) {
    const qreal gutter = 2 * ctx.spaceAdvance + ctx.pipeAdvance;
    qreal total = ctx.pipeAdvance;
    for (qreal inner : parsed->inners)
        total += inner + gutter;
    const qreal available = qMax(ctx.pipeAdvance
                                     + ctx.minInner * parsed->columns
                                     + gutter * parsed->columns,
                                 ctx.cap - ctx.margin);
    if (total > available)
        parsed->inners = shrinkInners(parsed->inners, ctx.minInner, total - available);
}

qreal storedInner(const TableGeometry::Table &geom, int column) {
    for (const TableGeometry::Cell &cell : geom.cells) {
        if (cell.column == column)
            return cell.textRect.width();
    }
    return 0;
}

QString storedCellText(const TableGeometry::Table &geom, int row, int column) {
    for (const TableGeometry::Cell &cell : geom.cells) {
        if (cell.row == row && cell.column == column)
            return cell.text;
    }
    return {};
}

int dirtyRowCount(const ParsedTable &parsed, const TableGeometry::Table &old) {
    if (parsed.columns != old.box.columns.size() - 1)
        return std::numeric_limits<int>::max();
    const int rows = qMin(parsed.dataRows.size(), old.rowBlockPositions.size());
    int dirtyRows = 0;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < parsed.columns; ++c) {
            const QString text = c < parsed.rowSpans.at(r).size()
                ? parsed.rowSpans.at(r).at(c).text : QString();
            if (text != storedCellText(old, r, c)) {
                ++dirtyRows;
                break;
            }
        }
        if (dirtyRows > 1)
            return dirtyRows;
    }
    if (parsed.dataRows.size() != old.rowBlockPositions.size() && dirtyRows > 0)
        return std::numeric_limits<int>::max();
    return dirtyRows;
}

bool stickAllocatedInners(ParsedTable *parsed, const TableGeometry::Table &old,
                          const MeasureContext &ctx) {
    if (dirtyRowCount(*parsed, old) > 1)
        return false;
    bool grew = false;
    for (int c = 0; c < parsed->columns; ++c) {
        const qreal stored = storedInner(old, c);
        const qreal measured = parsed->inners.at(c);
        if (measured <= stored + 0.5) {
            parsed->inners[c] = stored;
            continue;
        }
        parsed->inners[c] = measured + ctx.minInner;
        grew = true;
    }
    finishParsedInners(parsed, ctx);
    return grew;
}

QVector<qreal> columnXsFor(const ParsedTable &parsed, const MeasureContext &ctx) {
    QVector<qreal> columnXs;
    qreal x = ctx.margin + ctx.pipeAdvance * 0.5;
    columnXs.append(x);
    const qreal gutter = 2 * ctx.spaceAdvance + ctx.pipeAdvance;
    for (int c = 0; c < parsed.columns; ++c) {
        x += parsed.inners.at(c) + gutter;
        columnXs.append(x);
    }
    return columnXs;
}

qreal rowLayoutHeight(const ParsedTable &parsed, int row, const MeasureContext &ctx) {
    const QFont &rowFont = rowFontFor(parsed, row, ctx);
    qreal rowHeight = ctx.minRow;
    const QVector<CellSpan> &spans = parsed.rowSpans.at(row);
    for (int c = 0; c < parsed.columns; ++c) {
        const QString text = c < spans.size() ? spans.at(c).text : QString();
        if (text.isEmpty())
            continue;
        QTextLayout cellLayout;
        TableGeometry::prepareCellLayout(&cellLayout, text, rowFont,
                                         qMax(qreal(1), parsed.inners.at(c)));
        rowHeight = qMax(rowHeight, cellLayout.boundingRect().height());
    }
    return rowHeight;
}

TableGeometry::Table fillTable(const ParsedTable &parsed, const QVector<qreal> &columnXs,
                               const QVector<qreal> &rowEdges, const QVector<qreal> &rowHeights,
                               const MeasureContext &ctx) {
    TableGeometry::Table geom;
    geom.rowHeights = rowHeights;
    geom.box.columns = columnXs;
    geom.box.rowEdges = rowEdges;
    geom.box.bounds = QRectF(QPointF(columnXs.first(), rowEdges.first()),
                             QPointF(columnXs.last(), rowEdges.last()));
    for (int r = 0; r < parsed.dataRows.size(); ++r) {
        geom.rowBlockPositions.append(parsed.dataRows.at(r).position());
        const bool isHeader = parsed.header.isValid()
            && parsed.dataRows.at(r).position() == parsed.header.position();
        if (isHeader) {
            geom.box.header = QRectF(QPointF(columnXs.first(), rowEdges.at(r)),
                                     QPointF(columnXs.last(), rowEdges.at(r + 1)));
        }
        const QVector<MarkdownHighlighter::Span> &cells = parsed.rowCells.at(r);
        for (int c = 0; c < parsed.columns; ++c) {
            TableGeometry::Cell cell;
            cell.row = r;
            cell.column = c;
            cell.blockPosition = parsed.dataRows.at(r).position();
            cell.header = isHeader;
            cell.rect = QRectF(QPointF(columnXs.at(c), rowEdges.at(r)),
                               QPointF(columnXs.at(c + 1), rowEdges.at(r + 1)));
            const qreal textLeft = columnXs.at(c) + ctx.pipeAdvance * 0.5 + ctx.spaceAdvance;
            const qreal textRight = columnXs.at(c + 1) - ctx.pipeAdvance * 0.5 - ctx.spaceAdvance;
            cell.textRect = QRectF(QPointF(textLeft, rowEdges.at(r)),
                                   QPointF(qMax(textLeft + 1, textRight),
                                           rowEdges.at(r + 1)));
            if (c < cells.size()) {
                const CellSpan &span = parsed.rowSpans.at(r).at(c);
                cell.contentStart = span.contentStart;
                cell.contentEnd = span.contentEnd;
                cell.typingStart = span.typingStart;
                cell.typingEnd = span.typingEnd;
                cell.text = span.text;
            } else {
                cell.contentStart = parsed.dataRows.at(r).text().size();
                cell.contentEnd = cell.contentStart;
                cell.typingStart = cell.contentStart;
                cell.typingEnd = cell.contentStart;
            }
            geom.cells.append(cell);
        }
    }
    return geom;
}

QVector<ParsedTable> parseTables(QTextDocument *document, int layoutCursor,
                                 const MeasureContext &ctx) {
    QVector<ParsedTable> tables;
    QVector<QTextBlock> run;
    const auto flush = [&]() {
        if (run.isEmpty())
            return;

        ParsedTable parsed;
        for (const QTextBlock &block : run) {
            if (MarkdownHighlighter::isTableSeparator(block.text()))
                continue;
            if (!parsed.header.isValid() && block.next().isValid()
                    && MarkdownHighlighter::isTableSeparator(block.next().text()))
                parsed.header = block;
            parsed.dataRows.append(block);
        }
        if (parsed.dataRows.isEmpty()) {
            run.clear();
            return;
        }

        parsed.rowCells.reserve(parsed.dataRows.size());
        for (const QTextBlock &block : parsed.dataRows) {
            const MarkdownHighlighter::TableLine line =
                MarkdownHighlighter::parseTableLine(block.text());
            QVector<MarkdownHighlighter::Span> cells;
            cells.reserve(line.cells.size());
            for (const MarkdownHighlighter::Span &cell : line.cells)
                cells.append(cell);
            parsed.rowCells.append(cells);
            parsed.rowTexts.append(block.text());
            parsed.columns = qMax(parsed.columns, cells.size());
        }
        if (parsed.columns < 1) {
            run.clear();
            return;
        }

        parsed.rowSpans.resize(parsed.dataRows.size());
        parsed.inners.fill(ctx.minInner, parsed.columns);
        for (int r = 0; r < parsed.dataRows.size(); ++r) {
            const QFontMetricsF rowMetrics(rowFontFor(parsed, r, ctx));
            const QVector<MarkdownHighlighter::Span> &cells = parsed.rowCells.at(r);
            parsed.rowSpans[r].resize(cells.size());
            for (int c = 0; c < parsed.columns; ++c) {
                QString text;
                if (c < cells.size()) {
                    parsed.rowSpans[r][c] = cellSpanFor(parsed.rowTexts.at(r), cells.at(c),
                                                        parsed.dataRows.at(r).position(),
                                                        layoutCursor);
                    text = parsed.rowSpans[r].at(c).text;
                }
                parsed.inners[c] = qMax(parsed.inners.at(c), rowMetrics.horizontalAdvance(text));
            }
        }
        finishParsedInners(&parsed, ctx);
        tables.append(parsed);
        run.clear();
    };

    bool inFence = false;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        if (MarkdownHighlighter::isFenceLine(block.text())) {
            flush();
            inFence = !inFence;
            continue;
        }
        if (inFence || !MarkdownHighlighter::isTableRow(block.text()))
            flush();
        else
            run.append(block);
    }
    flush();
    return tables;
}

QVector<TableGeometry::Table> buildGeometries(QTextDocument *document, qreal wrapWidth,
                                              int layoutCursor,
                                              const QVector<TableGeometry::Table> *previous) {
    QVector<TableGeometry::Table> tables;
    if (!document)
        return tables;
    QAbstractTextDocumentLayout *layout = document->documentLayout();
    if (!layout)
        return tables;

    const MeasureContext ctx = measureContext(document, wrapWidth);
    const QVector<ParsedTable> parsedTables = parseTables(document, layoutCursor, ctx);
    for (int t = 0; t < parsedTables.size(); ++t) {
        ParsedTable parsed = parsedTables.at(t);
        if (previous && t < previous->size())
            stickAllocatedInners(&parsed, previous->at(t), ctx);
        const QVector<qreal> columnXs = columnXsFor(parsed, ctx);
        const QRectF firstRow = layout->blockBoundingRect(parsed.dataRows.first());
        qreal y = firstRow.top();
        QVector<qreal> rowEdges;
        rowEdges.append(y);
        QVector<qreal> rowHeights;
        rowHeights.reserve(parsed.dataRows.size());
        for (int r = 0; r < parsed.dataRows.size(); ++r) {
            const qreal rowHeight = rowLayoutHeight(parsed, r, ctx);
            rowHeights.append(rowHeight);
            y += rowHeight;
            // The collapsed separator block still occupies its fixed line
            // height between the header and the first body block. Fold it
            // into the header row's edge so body hairlines land on the body
            // blocks instead of drifting above them.
            if (parsed.header.isValid()
                    && parsed.dataRows.at(r).position() == parsed.header.position())
                y += MarkdownHighlighter::tableSeparatorLineHeight;
            rowEdges.append(y);
        }
        tables.append(fillTable(parsed, columnXs, rowEdges, rowHeights, ctx));
    }
    return tables;
}

uint layoutFingerprint(const QVector<TableGeometry::Table> &tables) {
    uint hash = 0;
    for (const TableGeometry::Table &table : tables) {
        for (qreal x : table.box.columns)
            hash = hash * 31u + uint(qRound(x * 100));
        for (qreal y : table.box.rowEdges)
            hash = hash * 31u + uint(qRound(y * 100));
    }
    return hash;
}

class GeomStore : public QObject {
public:
    explicit GeomStore(QTextDocument *parent)
        : QObject(parent)
    {
    }

    int revision = -1;
    qreal wrapWidth = -1;
    int layoutCursor = std::numeric_limits<int>::min();
    qreal layoutWidth = -1;
    qreal layoutHeight = -1;
    int structureRevision = 0;
    uint fingerprint = 0;
    QVector<TableGeometry::Table> tables;
};

GeomStore *storeFor(QTextDocument *document) {
    if (!document)
        return nullptr;
    const QString name = QStringLiteral("oma.TableGeomStore");
    if (QObject *existing = document->findChild<QObject *>(name, Qt::FindDirectChildrenOnly))
        return static_cast<GeomStore *>(existing);
    auto *store = new GeomStore(document);
    store->setObjectName(name);
    return store;
}

void rememberLayoutSize(GeomStore *store, QTextDocument *document) {
    store->layoutWidth = 0;
    store->layoutHeight = 0;
    if (!document)
        return;
    if (QAbstractTextDocumentLayout *layout = document->documentLayout()) {
        const QSizeF size = layout->documentSize();
        store->layoutWidth = size.width();
        store->layoutHeight = size.height();
    }
}

bool tryReuseLayout(GeomStore *store, QTextDocument *document, qreal wrapWidth,
                    int layoutCursor) {
    if (!store || !document || store->tables.isEmpty())
        return false;
    QAbstractTextDocumentLayout *layout = document->documentLayout();
    if (!layout)
        return false;

    const MeasureContext ctx = measureContext(document, wrapWidth);
    if (qAbs(ctx.cap - store->wrapWidth) > 0.5)
        return false;

    const QVector<ParsedTable> parsedTables = parseTables(document, layoutCursor, ctx);
    if (parsedTables.size() != store->tables.size())
        return false;

    QVector<TableGeometry::Table> next;
    next.reserve(parsedTables.size());
    bool moved = false;
    for (int t = 0; t < parsedTables.size(); ++t) {
        const ParsedTable &parsed = parsedTables.at(t);
        const TableGeometry::Table &old = store->tables.at(t);
        if (parsed.dataRows.size() != old.rowHeights.size()
                || dirtyRowCount(parsed, old) > 1)
            return false;

        ParsedTable laidOut = parsed;
        const bool grew = stickAllocatedInners(&laidOut, old, ctx);
        if (grew)
            return false;

        int dirtyRow = -1;
        for (int r = 0; r < parsed.dataRows.size(); ++r) {
            for (int c = 0; c < parsed.columns; ++c) {
                const QString text = c < parsed.rowSpans.at(r).size()
                    ? parsed.rowSpans.at(r).at(c).text : QString();
                if (text != storedCellText(old, r, c)) {
                    dirtyRow = r;
                    break;
                }
            }
            if (dirtyRow >= 0)
                break;
        }
        if (dirtyRow >= 0
                && qAbs(rowLayoutHeight(laidOut, dirtyRow, ctx) - old.rowHeights.at(dirtyRow)) > 0.5)
            return false;

        const QVector<qreal> columnXs = old.box.columns;
        QVector<qreal> rowEdges = old.box.rowEdges;
        const QRectF firstRow = layout->blockBoundingRect(parsed.dataRows.first());
        const qreal dy = firstRow.top() - rowEdges.first();
        if (qAbs(dy) > 0.5) {
            moved = true;
            for (qreal &edge : rowEdges)
                edge += dy;
        }
        next.append(fillTable(laidOut, columnXs, rowEdges, old.rowHeights, ctx));
    }

    store->tables = next;
    store->revision = document->revision();
    store->layoutCursor = layoutCursor;
    store->wrapWidth = ctx.cap;
    rememberLayoutSize(store, document);
    if (moved) {
        store->fingerprint = layoutFingerprint(store->tables);
        ++store->structureRevision;
    }
    return true;
}

void storeBuilt(GeomStore *store, QTextDocument *document, qreal wrapWidth,
                int layoutCursor, QVector<TableGeometry::Table> tables) {
    const uint fingerprint = layoutFingerprint(tables);
    if (fingerprint != store->fingerprint)
        ++store->structureRevision;
    store->revision = document ? document->revision() : -1;
    store->wrapWidth = effectiveWrapWidth(document, wrapWidth);
    store->layoutCursor = layoutCursor;
    store->fingerprint = fingerprint;
    store->tables = std::move(tables);
    rememberLayoutSize(store, document);
}

} // namespace

int TableGeometry::visibleEndForCursor(const QString &line, int contentEnd, int typingStart,
                                       int typingEnd, int blockPosition, int cursorPosition) {
    // Pretty-print padding stays hidden, but spaces at or before the caret are
    // real cell text so a typed trailing space stays visible and the overlay
    // caret can sit in it. A caret on a closing `|` must not unhide unused
    // padding. A last cell with no closing pipe uses typingEnd at the end of
    // the line; a typed space there is real content.
    if (cursorPosition < 0)
        return contentEnd;
    const int absStart = blockPosition + typingStart;
    const int absEnd = blockPosition + typingEnd;
    if (cursorPosition < absStart || cursorPosition > absEnd)
        return contentEnd;
    const bool onClosingPipe = cursorPosition == absEnd
            && typingEnd < line.size()
            && line.at(typingEnd) == QLatin1Char('|');
    if (onClosingPipe)
        return contentEnd;
    const int cursorInLine = cursorPosition - blockPosition;
    return qBound(typingStart, qMax(contentEnd, cursorInLine), typingEnd);
}

void TableGeometry::prepareCellLayout(QTextLayout *layout, const QString &text,
                                      const QFont &font, qreal width) {
    layout->setFont(font);
    layout->setText(text);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->setTextOption(option);
    layout->beginLayout();
    qreal y = 0;
    if (text.isEmpty()) {
        layout->endLayout();
        return;
    }
    while (true) {
        QTextLine line = layout->createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(qMax(qreal(1), width));
        line.setPosition(QPointF(0, y));
        y += line.height();
    }
    layout->endLayout();
}

qreal TableGeometry::cellLineHeight(const QTextLayout &layout, const QFont &font) {
    // An empty cell lays out no lines, so its bounding rect is zero-height.
    // Treat it as a single line of the cell font so the caret and text origin
    // are centred like a one-line cell instead of collapsing to the row's midpoint.
    if (layout.lineCount() > 0)
        return layout.boundingRect().height();
    return QFontMetricsF(font).height();
}

qreal TableGeometry::cellTextOffset(const QTextLayout &layout, const QFont &font,
                                    const QRectF &textRect) {
    return qMax(qreal(0), (textRect.height() - cellLineHeight(layout, font)) * 0.5);
}

int TableGeometry::layoutRelevantCursor(QTextDocument *document, int cursorPosition) {
    if (!document || cursorPosition < 0)
        return -1;
    const QTextBlock block = document->findBlock(cursorPosition);
    if (!block.isValid()
            || !MarkdownHighlighter::isTableRow(block.text())
            || MarkdownHighlighter::isTableSeparator(block.text())
            || block.userState() == 1)
        return -1;
    const QString line = block.text();
    const MarkdownHighlighter::TableLine parsed = MarkdownHighlighter::parseTableLine(line);
    const int blockPosition = block.position();
    for (const MarkdownHighlighter::Span &cell : parsed.cells) {
        int typingStart = 0;
        int typingEnd = 0;
        int contentStart = 0;
        int contentEnd = 0;
        QString text;
        typingSpan(line, cell, &typingStart, &typingEnd);
        contentSpan(line, cell, &contentStart, &contentEnd, &text);
        const int visibleEnd = visibleEndForCursor(line, contentEnd, typingStart, typingEnd,
                                                   blockPosition, cursorPosition);
        if (visibleEnd > contentEnd)
            return cursorPosition;
    }
    return -1;
}

int TableGeometry::structureRevision(QTextDocument *document) {
    GeomStore *store = storeFor(document);
    return store ? store->structureRevision : 0;
}

QVector<TableGeometry::Table> TableGeometry::geometriesFor(QTextDocument *document,
                                                           qreal wrapWidth,
                                                           int cursorPosition) {
    const int layoutCursor = layoutRelevantCursor(document, cursorPosition);
    GeomStore *store = storeFor(document);
    const int revision = document ? document->revision() : -1;
    const qreal cap = effectiveWrapWidth(document, wrapWidth);
    qreal layoutWidth = 0;
    qreal layoutHeight = 0;
    if (document) {
        if (QAbstractTextDocumentLayout *layout = document->documentLayout()) {
            const QSizeF size = layout->documentSize();
            layoutWidth = size.width();
            layoutHeight = size.height();
        }
    }

    if (store
            && store->revision == revision
            && store->layoutCursor == layoutCursor
            && qAbs(store->wrapWidth - cap) < 0.5
            && qAbs(store->layoutWidth - layoutWidth) < 0.5
            && qAbs(store->layoutHeight - layoutHeight) < 0.5)
        return store->tables;

    if (store
            && qAbs(store->wrapWidth - cap) < 0.5
            && qAbs(store->layoutWidth - layoutWidth) < 0.5
            && qAbs(store->layoutHeight - layoutHeight) < 0.5
            && tryReuseLayout(store, document, wrapWidth, layoutCursor))
        return store->tables;

    QVector<Table> tables = buildGeometries(document, wrapWidth, layoutCursor,
        (store && !store->tables.isEmpty()) ? &store->tables : nullptr);
    if (!store)
        return tables;
    storeBuilt(store, document, wrapWidth, layoutCursor, tables);
    return store->tables;
}

QVector<TableGeometry::Box> TableGeometry::collectTables(QTextDocument *document,
                                                         qreal wrapWidth) {
    QVector<Box> tables;
    const QVector<Table> geoms = geometriesFor(document, wrapWidth, -1);
    tables.reserve(geoms.size());
    for (const Table &geom : geoms)
        tables.append(geom.box);
    return tables;
}

QHash<int, qreal> TableGeometry::dataRowHeights(QTextDocument *document, qreal wrapWidth,
                                                int cursorPosition) {
    QHash<int, qreal> heights;
    const QVector<Table> geoms = geometriesFor(document, wrapWidth, cursorPosition);
    for (const Table &geom : geoms) {
        for (int i = 0; i < geom.rowBlockPositions.size(); ++i)
            heights.insert(geom.rowBlockPositions.at(i), geom.rowHeights.at(i));
    }
    return heights;
}

qreal TableGeometry::naturalWidthOf(QTextDocument *document) {
    if (!document)
        return 0;

    const QFontMetricsF metrics(document->defaultFont());
    const qreal left = document->documentMargin();
    qreal maxAdvance = 0;
    bool inFence = false;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        if (MarkdownHighlighter::isFenceLine(block.text())) {
            inFence = !inFence;
            continue;
        }
        if (inFence || !MarkdownHighlighter::isTableRow(block.text()))
            continue;
        maxAdvance = qMax(maxAdvance, metrics.horizontalAdvance(block.text()));
    }
    if (maxAdvance <= 0)
        return 0;
    return left + maxAdvance + 2;
}
